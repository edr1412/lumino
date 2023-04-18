#ifndef MUDUO_BASE_BLOCKINGQUEUEFORTHREADPOOL_H
#define MUDUO_BASE_BLOCKINGQUEUEFORTHREADPOOL_H

#include <muduo/base/Condition.h>
#include <muduo/base/Mutex.h>
#include <deque>

namespace muduo
{
template <typename T>
class BlockingQueueForThreadPool : noncopyable
{
public:
  BlockingQueueForThreadPool(int maxsize = 0)
      : mutex_(), notFull_(), notEmpty_(), maxSize_(maxsize), running_(true) {}
  ~BlockingQueueForThreadPool() = default;

  void put(T &&x)
  {
    MutexLockGuard lock(mutex_);
    if (maxSize_ != 0)
    {
      while (queue_.size() >= maxSize_ && running_)
      {
        notFull_.wait(lock);
      }
    }

    if (!running_)
      return;
    queue_.push_back(std::forward<T>(x));
    notEmpty_.notify();
  }

T take()
{
  MutexLockGuard lock(mutex_);
  while (queue_.empty() && running_)
  {
    notEmpty_.wait(lock);
  }
  if (!running_)
    return T(); // Return a default-constructed T object if the queue is not running
  assert(!queue_.empty());
  T front(std::move(queue_.front()));
  queue_.pop_front();
  notFull_.notify();
  return front;
}

  size_t size() const
  {
    MutexLockGuard lock(mutex_);
    return queue_.size();
  }

  void setMaxSize(size_t maxSize)
  {
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
  std::deque<T> queue_;
  mutable MutexLock mutex_;
  Condition notFull_ GUARDED_BY(mutex_);
  Condition notEmpty_ GUARDED_BY(mutex_);
  size_t maxSize_; // 0 means no limit
  bool running_;
};

} // namespace muduo

#endif  // MUDUO_BASE_BLOCKINGQUEUEFORTHREADPOOL_H