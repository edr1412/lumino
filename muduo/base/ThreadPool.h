// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include <muduo/base/Thread.h>
#include <muduo/base/Types.h>
#include <muduo/base/BlockingQueueForThreadPool.h>
#include <muduo/base/LockFreeQueue.h>

#include <atomic>
#include <vector>

#define use_lock_free 1 // 0: use Blocking Queue, 1: use Lock-Free Queue

namespace muduo
{

// 同步队列，供线程池使用，负责任务队列的同步操作
template <typename T>
class SyncQueue
{
public:
  SyncQueue(int maxsize = 0)
  {
#if use_lock_free
    LockFreeQueue<T> lock_free_queue;
#else
    BlockingQueueForThreadPool<T> blocking_queue(maxsize);
#endif
  }

  ~SyncQueue() = default;

  template<typename U>
  void put(U&& x)
  {
#if use_lock_free
    lock_free_queue_.push(std::forward<U>(x));
#else
    blocking_queue_.put(std::forward<U>(x));
#endif
  }

  T take()
  {
#if use_lock_free
    std::unique_ptr<T> ptr = lock_free_queue_.pop();
    return ptr ? *ptr : T();
#else
    return blocking_queue_.take();
#endif
  }

  size_t size() const
  {
#if use_lock_free
    // LockFreeQueue does not support size() function
    return 0;
#else
    return blocking_queue_.size();
#endif
  }

  void setMaxSize(size_t maxSize)
  {
#if use_lock_free
    // LockFreeQueue does not support setMaxSize() function
#else
    blocking_queue_.setMaxSize(maxSize);
#endif
  }

  void stop()
  {
#if use_lock_free
    // LockFreeQueue does not support stop() function
#else
    blocking_queue_.stop();
#endif
  }

private:
#if use_lock_free
  LockFreeQueue<T> lock_free_queue_;
#else
  BlockingQueueForThreadPool<T> blocking_queue_;
#endif
};

  // 线程池
  // 上层通过调用 run 函数将任务添加到同步层中，
  // 异步层中的线程的 runInThread 则会在空闲的时候将任务取出并执行。
  class ThreadPool : noncopyable
  {
  public:
    using Task = std::function<void()>;

    explicit ThreadPool(const string &nameArg = string("ThreadPool"));
    ~ThreadPool();

    // Must be called before start().
    void setMaxQueueSize(int maxSize) { queue_.setMaxSize(maxSize); }
    void setThreadInitCallback(const Task &cb)
    {
      threadInitCallback_ = cb;
    }

    void start(int numThreads);
    void stop();

    const string &name() const
    {
      return name_;
    }

    size_t queueSize() const; //不能是内联的

    // Could block if maxQueueSize > 0
    void run(Task&& f);

  private:
    void runInThread();
    std::string name_;
    Task threadInitCallback_;
    std::vector<std::unique_ptr<Thread>> threads_;
    SyncQueue<Task> queue_;
    std::atomic<bool> running_;
  };

} // namespace muduo

#endif // MUDUO_BASE_THREADPOOL_H
