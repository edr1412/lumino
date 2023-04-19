#ifndef MUDUO_BASE_LOCKFREEQUEUE_H
#define MUDUO_BASE_LOCKFREEQUEUE_H

#include <muduo/base/noncopyable.h>
#include <atomic>
#include <memory>

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
        int external_count; // 代表该指针的引用计数
        node* ptr;
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

            counted_node_ptr initial_next;
            initial_next.ptr = nullptr;
            initial_next.external_count = 0;
            next.store(initial_next);
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
            while(!count.compare_exchange_strong(
                      old_counter,new_counter,
                      std::memory_order_acq_rel,std::memory_order_relaxed)); // the whole count structure has to be updated atomically
            if(!new_counter.internal_count &&
               !new_counter.external_counters)
            {
                delete this;
            }
        }
    };

    static void increase_external_count(
        std::atomic<counted_node_ptr>& counter, // 要更新的 external counter
        counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter=old_counter;
            ++new_counter.external_count;
        }
        while(!counter.compare_exchange_strong(
                  old_counter,new_counter,
                  std::memory_order_acquire,std::memory_order_relaxed));
        old_counter.external_count=new_counter.external_count;
    }

    // 把 old_node_ptr 的 external counter 通通转化到指向node的 internal_count 里
    static void free_external_counter(counted_node_ptr &old_node_ptr)
    {
        node* const ptr=old_node_ptr.ptr;
        int const count_increase=old_node_ptr.external_count-2;
        node_counter old_counter=
            ptr->count.load(std::memory_order_relaxed);
        node_counter new_counter;
        do
        {
            new_counter=old_counter;
            --new_counter.external_counters;
            new_counter.internal_count+=count_increase;
        }
        while(!ptr->count.compare_exchange_strong(
                  old_counter,new_counter,
                  std::memory_order_acq_rel,std::memory_order_relaxed)); // updates the two counts using a single compare_exchange_strong() on the whole count structure
        if(!new_counter.internal_count &&
           !new_counter.external_counters)
        {
            delete ptr;
        }
    }

    void set_new_tail(counted_node_ptr &old_tail,
                      counted_node_ptr const &new_tail)
    {
        node* const current_tail_ptr=old_tail.ptr;
        while(!tail.compare_exchange_weak(old_tail,new_tail) &&
              old_tail.ptr==current_tail_ptr); // 若其他线程抢先更新tail为new_tail，则会进入else分支；若仅仅是其他有线程更新了tail->external_count，则更新old_tail后重试；若成功更新tail，进入if分支。
        if(old_tail.ptr==current_tail_ptr)
            free_external_counter(old_tail);
        else
            current_tail_ptr->release_ref();
    }

public:
    LockFreeQueue()
    {
        node* new_node = new node();
        counted_node_ptr new_ptr;
        new_ptr.ptr = new_node;
        new_ptr.external_count = 1;
        head.store(new_ptr);
        tail.store(new_ptr);
    }

    ~LockFreeQueue()
    {
        while (pop()) {}
        node* head_ptr = head.load().ptr;
        delete head_ptr;
    }

    void push(T&& new_value)
    {
        std::unique_ptr<T> new_data(new T(std::forward<T>(new_value)));
        counted_node_ptr new_next;
        new_next.ptr=new node;
        new_next.external_count=1;
        counted_node_ptr old_tail=tail.load();
        for(;;)
        {
            increase_external_count(tail,old_tail);
            T* old_data=nullptr;
            if(old_tail.ptr->data.compare_exchange_strong(
                   old_data,new_data.get()))
            {
                counted_node_ptr old_next={0, nullptr};
                if(!old_tail.ptr->next.compare_exchange_strong(
                       old_next,new_next))
                {   // 失败了代表其他线程已经帮忙更新了next
                    delete new_next.ptr; // 已不需要，可以删掉
                    new_next=old_next; // 使用另一个 thread 更新好的 next 来给 tail
                }
                set_new_tail(old_tail, new_next);
                new_data.release();
                break;
            }
            else
            {
                counted_node_ptr old_next={0, nullptr};
                if(old_tail.ptr->next.compare_exchange_strong(
                       old_next,new_next)) // 帮忙那个成功的线程更新 next，反正不然也只能忙等。最终只会有一个线程成功更新next，而所有参与的线程都会进入 set_new_tail，在那里结算关于old_tail的引用计数。
                {
                    old_next=new_next; // 将这个 thread 的 new node 设为新的 tail node
                    new_next.ptr=new node; // 给 new_next.ptr 重新分配一个新的 node
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
            node* const ptr=old_head.ptr;
            if(ptr==tail.load().ptr)
            {
                return std::unique_ptr<T>();
            }
            counted_node_ptr next=ptr->next.load();
            if(head.compare_exchange_strong(old_head,next)) // maybe std::memory_order_release,std::memory_order_relaxed
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