#include <atomic>
#include <memory>

template<typename T>
class lock_free_stack
{
private:
    struct node;
    struct counted_node_ptr
    {
        int external_count;
        node* ptr;
    };
    struct node
    {
        std::shared_ptr<T> data;
        std::atomic<int> internal_count;
        counted_node_ptr next; // note that the next value is a plain non-atomic object, so in order to read this safely, there must be a happens-before relationship between the store (by the pushing thread) and the load (by the popping thread)
        node(T const& data_):
            data(std::make_shared<T>(data_)),
            internal_count(0)
        {}
    };
    std::atomic<counted_node_ptr> head;


    void increase_head_count(counted_node_ptr& old_counter)
    {
        counted_node_ptr new_counter;
        do
        {
            new_counter=old_counter;
            ++new_counter.external_count;
        }
        while(!head.compare_exchange_strong(old_counter,new_counter,
                                            std::memory_order_acquire, // 与L53的release配对，make sure the store to the ptr field in the push() happens before the ptr->next access in pop()
                                            std::memory_order_relaxed));
        old_counter.external_count=new_counter.external_count;
    }

public:
    ~lock_free_stack()
    {
        while(pop());
    }
    void push(T const& data)
    {
        counted_node_ptr new_node;
        new_node.ptr=new node(data);
        new_node.external_count=1; // header指针本身是一个外部引用；当它不再被header指涉，它也被前驱节点的next指涉，仍然使得外部引用计数为1
        new_node.ptr->next=head.load(std::memory_order_relaxed);
        while(!head.compare_exchange_weak(new_node.ptr->next,new_node,
                                            std::memory_order_release,
                                            std::memory_order_relaxed));
    }

    std::shared_ptr<T> pop()
    {
        counted_node_ptr old_head=head.load(std::memory_order_relaxed);
        for(;;)
        {
            increase_head_count(old_head); // 令头节点的外部引用计数自增，以表明它正被指涉，从而确保根据head指针读取目标节点是安全行为
            node* const ptr=old_head.ptr;
            if(!ptr)
            {
                return std::shared_ptr<T>();
            }
            if(head.compare_exchange_strong(old_head,ptr->next, std::memory_order_relaxed)) // 修改head成功。此时当前线程独占 old_head.ptr，可以换出其data以返回
            {
                std::shared_ptr<T> res;
                res.swap(ptr->data);
                int const count_increase=old_head.external_count-2; // 负责把外部引用计数转移到内部引用计数，减去2是因为对应两处：L50，L62
                if(ptr->internal_count.fetch_add(count_increase, std::memory_order_release)== 
                   -count_increase) // 若内部引用计数变为0，可以删除；否则，即还有线程处于L63~L79之间，则等待L80不断减1，最终在L83被删除
                {
                    delete ptr;
                }
                return res;
            }
            else if(ptr->internal_count.fetch_add(-1, std::memory_order_relaxed)==1)  // 修改head失败，说明有其他线程抢占，执行了push/pop且先于本线程完成。此时需要减少内部引用计数，以抵消L62。若这其中有其他线程pop成功，这里的 ==1 才可能成立，此时 ==1 代表当前线程是最后一个持有指针的线程。
            {
                ptr->internal_count.load(std::memory_order_acquire); // 与L73的release配对，保证L71的修改对L83可见
                delete ptr;
            }
        }
    }
};
