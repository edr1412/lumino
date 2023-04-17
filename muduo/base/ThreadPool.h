// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADPOOL_H
#define MUDUO_BASE_THREADPOOL_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <muduo/base/Thread.h>
#include <muduo/base/Types.h>

#include <atomic>
#include <vector>
#include <list>

namespace muduo
{

  // 同步队列，供线程池使用，负责任务队列的同步操作
  template <typename T>
  class SyncQueue : noncopyable
  {
  public:
    SyncQueue(int maxsize = 65536)
        : mutex_(), notFull_(), notEmpty_(), maxSize_(maxsize), running_(true) {}
    ~SyncQueue() = default;

    void put(T &&x) 
    { 
      MutexLockGuard lock(mutex_);
      while (queue_.size() >= maxSize_ && running_)
      {
        notFull_.wait(lock);
      }
      if (!running_)
        return;
      queue_.push_back(std::forward<T>(x));
      notEmpty_.notify();
    }

    void take(T &t)
    {
      MutexLockGuard lock(mutex_);
      while (queue_.empty() && running_)
      {
        notEmpty_.wait(lock);
      }
      if (!running_)
        return;
      t = queue_.front();
      queue_.pop_front();
      notFull_.notify();
    }

    size_t size() const
    {
      MutexLockGuard lock(mutex_);
      return queue_.size();
    }

    void setMaxSize(size_t maxSize)
    {
      assert(maxSize > 0);
      MutexLockGuard lock(mutex_);
      maxSize_ = maxSize;
    }

    void stop() NO_THREAD_SAFETY_ANALYSIS
    {
      {
        MutexLockGuard lock(mutex_);
        running_ = false;
      }
      notFull_.notifyAll();
      notEmpty_.notifyAll();
    }

  private:
    std::list<T> queue_;
    mutable MutexLock mutex_;
    Condition notFull_ GUARDED_BY(mutex_);
    Condition notEmpty_ GUARDED_BY(mutex_);
    size_t maxSize_;
    bool running_;
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
