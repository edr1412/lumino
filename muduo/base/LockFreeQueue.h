#ifndef MUDUO_BASE_LOCKFREEQUEUE_H
#define MUDUO_BASE_LOCKFREEQUEUE_H

#include <muduo/base/noncopyable.h>
#include <atomic>
#include <memory>
#include <assert.h>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wconversion"

namespace muduo
{

template<typename T>
class LockFreeQueue : noncopyable
{
private:
    struct node;
    struct counted_node_ptr
    {
        uint64_t ptr_and_external_count; //为什么需要外部计数和内部计数？使用两种引用计数器可以将修改计数的操作分流到两个不同的计数器上，否则所有线程都将试图在同一时刻更新同一个计数器。参考 https://stackoverflow.com/questions/67371033/how-does-the-split-reference-counting-work-in-a-lock-free-stack

        counted_node_ptr() noexcept : ptr_and_external_count(0) {}

        counted_node_ptr(node* ptr, uint64_t count) {
            set_ptr(ptr);
            set_external_count(count);
        }

        node* get_ptr() const {
            return reinterpret_cast<node*>(ptr_and_external_count & (~0ull >> 16));
        }

        uint64_t get_external_count() const {
            return ptr_and_external_count >> 48;
        }

        void set_ptr(node* ptr) {
            uint64_t new_ptr = reinterpret_cast<uint64_t>(ptr);
            ptr_and_external_count = (ptr_and_external_count & (~0ull << 48)) | (new_ptr & (~0ull >> 16));
        }

        void set_external_count(uint64_t count) {
            ptr_and_external_count = (ptr_and_external_count & (~0ull >> 16)) | (count << 48);
        }
    };
    std::atomic<counted_node_ptr> head;
    std::atomic<counted_node_ptr> tail;
    struct node_counter
    {
        unsigned internal_count:30;
        unsigned external_counters:2; // 记录 external counters 数量，最多2个
    };
    struct node
    {
        std::atomic<T*> data;
        std::atomic<node_counter> count;
        std::atomic<counted_node_ptr> next;
        
        node()
        {
            data.store(nullptr);
            node_counter new_count;
            new_count.internal_count=0;
            new_count.external_counters=2;
            count.store(new_count);

            counted_node_ptr initial_next; // (nullptr, 0)
            next.store(initial_next);
        }

        node(int i)
        {
            data.store(nullptr);
            node_counter new_count;
            new_count.internal_count=i;
            new_count.external_counters=2;
            count.store(new_count);

            counted_node_ptr initial_next; // (nullptr, 0)
            next.store(initial_next);
        }

        ~node()
        {
            T* ptr=data.load();
            if(ptr)
            {
                delete ptr;
                //data = nullptr; 
            }
        }

        void release_ref()
        {
            node_counter old_counter=
                count.load(std::memory_order_relaxed);
            node_counter new_counter;
            do
            {
                new_counter=old_counter;
                --new_counter.internal_count; // 只更新 internal_count
            }
            while(!count.compare_exchange_weak(
                      old_counter,new_counter,
                      std::memory_order_release,std::memory_order_relaxed)); // the whole count structure has to be updated atomically
            if(!new_counter.internal_count &&
               !new_counter.external_counters)
            {
                count.load(std::memory_order_acquire); // acquire fence
                delete this;
            }
        }
    };

    static void increase_external_count( // dereference（访问被counted_node_ptr指向的对象） 之前，让外部引用计数加1
        std::atomic<counted_node_ptr>& counter, // 要更新的 external counter
        counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter=old_counter;
            new_counter.set_external_count(new_counter.get_external_count()+1);
        }
        while(!counter.compare_exchange_weak(
                  old_counter,new_counter,
                  std::memory_order_acquire,std::memory_order_relaxed));
        old_counter.set_external_count(new_counter.get_external_count());
    }

    // 把 old_node_ptr 的 external counter 通通转化到指向node的 internal_count 里
    static void free_external_counter(counted_node_ptr &old_node_ptr)
    {
        node* const ptr=old_node_ptr.get_ptr();
        int const count_increase=old_node_ptr.get_external_count()-2;
        node_counter old_counter=
            ptr->count.load(std::memory_order_relaxed);
        node_counter new_counter;
        do
        {
            new_counter=old_counter;
            --new_counter.external_counters;
            new_counter.internal_count+=count_increase;
        }
        while(!ptr->count.compare_exchange_weak(
                  old_counter,new_counter,
                  std::memory_order_release,std::memory_order_relaxed)); // updates the two counts using a single compare_exchange_strong() on the whole count structure
        if(!new_counter.internal_count &&
           !new_counter.external_counters)
        {
            ptr->count.load(std::memory_order_acquire); // acquire fence
            delete ptr;
        }
    }

    void set_new_tail(counted_node_ptr &old_tail,
                      counted_node_ptr const &new_tail)
    {
        node* const current_tail_ptr=old_tail.get_ptr();
        while(!tail.compare_exchange_weak(old_tail,new_tail) &&
              old_tail.get_ptr()==current_tail_ptr); // 只有一个线程能成功。若被其他线程抢先更新tail为new_tail，则代表set_new_tail失败，会进入else分支；若仅仅是其他有线程更新了tail->external_count，则更新old_tail后重试；若成功更新tail，进入if分支。
        if(old_tail.get_ptr()==current_tail_ptr)
            free_external_counter(old_tail);
        else
            current_tail_ptr->release_ref();
    }

    node* first_node;

public:
    LockFreeQueue()
    {
        first_node = new node(114514); // Hack: 这个node有时会删不掉（总是泄露24bytes），找不到原因，只好标记一下最后删，好在不影响性能
        counted_node_ptr new_ptr;
        new_ptr.set_ptr(first_node);
        new_ptr.set_external_count(1);
        head.store(new_ptr);
        tail.store(new_ptr);
        static_assert(std::atomic<counted_node_ptr>::is_always_lock_free, "std::atomic<counted_node_ptr> is not lock free");
        assert (tail.is_lock_free());
    }

    ~LockFreeQueue()
    {
        while (pop()) {}
        node* head_ptr = head.load().get_ptr();
        delete head_ptr;
        delete first_node;
    }

    template<typename U>
    void push(U&& new_value)
    {
        std::unique_ptr<T> new_data(new T(std::forward<U>(new_value)));
        counted_node_ptr new_next;
        new_next.set_ptr(new node);
        new_next.set_external_count(1);
        counted_node_ptr old_tail=tail.load();
        for(;;)
        {
            increase_external_count(tail,old_tail);
            T* old_data=nullptr;
            if(old_tail.get_ptr()->data.compare_exchange_strong(
                   old_data,new_data.get()))
            {
                counted_node_ptr old_next; // 默认 count = 0, ptr = nullptr
                if(!old_tail.get_ptr()->next.compare_exchange_strong(
                       old_next,new_next))
                {   // 失败了代表其他线程已经帮忙更新了next
                    delete new_next.get_ptr(); // 已不需要，可以删掉
                    new_next=old_next; // 使用另一个 thread 更新好的 next 来给 tail
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            }
            else
            {
                counted_node_ptr old_next; // 默认 count = 0, ptr = nullptr
                if(old_tail.get_ptr()->next.compare_exchange_strong(
                       old_next,new_next)) // 帮忙那个成功的线程更新 next，反正不然也只能忙等。最终只会有一个线程成功更新next，而所有参与的线程都会进入 set_new_tail，在那里结算关于old_tail的引用计数。
                {
                    old_next=new_next; // 将这个 thread 的 new node 设为新的 tail node
                    new_next.set_ptr(new node); // 给 new_next.ptr 重新分配一个新的 node
                }
                set_new_tail(old_tail, old_next);
            }
        }
    }

    // 思路与 lock_free_stack::pop() 一致
    std::unique_ptr<T> pop()
    {
        counted_node_ptr old_head=head.load(std::memory_order_relaxed);
        for(;;)
        {
            increase_external_count(head,old_head);
            node* const ptr=old_head.get_ptr();
            if(ptr==tail.load().get_ptr())
            {
                ptr->release_ref();
                return std::unique_ptr<T>();
            }
            counted_node_ptr next=ptr->next.load();
            if(head.compare_exchange_strong(old_head,next,
                                            std::memory_order_release,std::memory_order_relaxed))
            {
                T* const res=ptr->data.exchange(nullptr);
                free_external_counter(old_head);
                return std::unique_ptr<T>(res);
            }
            ptr->release_ref();
        }
    }
};

}

#pragma GCC diagnostic pop

#endif // LOCK_FREE_STACK_H