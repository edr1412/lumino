// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CONDITION_H
#define MUDUO_BASE_CONDITION_H

#include <muduo/base/Mutex.h>
#include <chrono>
#include <condition_variable>

namespace muduo
{

// 对 condition_variable 的简单封装
class Condition : noncopyable
{
 public:
  Condition()
   : m_cond()
  {}

  ~Condition() = default;

  void wait(MutexLockGuard &lck)
  {
    m_cond.wait(lck.getUniqueLock()); 
  }
  template <class Predicate>
  void wait(MutexLockGuard &lck, Predicate pred)
  {
    m_cond.wait(lck.getUniqueLock(), pred);
  }

  // returns true if time out, false otherwise.
  bool waitForSeconds(MutexLockGuard &lck, double seconds)
  {
    std::cv_status status = m_cond.wait_for(lck.getUniqueLock(), 
      std::chrono::duration<double>(seconds));
    return status == std::cv_status::timeout;
  }

  template <class Predicate>
  bool waitForSeconds(MutexLockGuard &lck,
                double seconds,
                Predicate pred)
    {
      return m_cond.wait_for(lck.getUniqueLock(),
        std::chrono::duration<double>(seconds),
        pred);
    }
  void notify()
  {
    m_cond.notify_one();
  }

  void notifyAll()
  {
    m_cond.notify_all();
  }

 private:
  std::condition_variable m_cond;
};

}  // namespace muduo

#endif  // MUDUO_BASE_CONDITION_H
