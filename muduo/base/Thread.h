// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREAD_H
#define MUDUO_BASE_THREAD_H

#include <atomic>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/Types.h>

#include <functional>
#include <memory>
#include <thread>

namespace muduo
{

class Thread : noncopyable {
 public:
  using ThreadFunc = std::function<void()>;
  explicit Thread(ThreadFunc func, const std::string& name = std::string());
  ~Thread();
  Thread(Thread&& rhs) noexcept;
  Thread& operator=(Thread&& rhs) noexcept;
  void join();
  pid_t tid() const { return tid_; }
  static inline int numCreated() { return numCreated_.load(); }

 private:
  
  static std::atomic_int32_t numCreated_;
  ThreadFunc func_;
  string     name_;
  pid_t      tid_;
  //线程实体
  std::thread thread_;
  void runInThread();
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREAD_H
