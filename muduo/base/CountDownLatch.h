// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_COUNTDOWNLATCH_H
#define MUDUO_BASE_COUNTDOWNLATCH_H

#include <muduo/base/Mutex.h>
#include <mutex>
#include <condition_variable>
#include <muduo/base/noncopyable.h>

namespace muduo
{

class CountDownLatch : noncopyable
{
 public:

  explicit CountDownLatch(int count); //倒数几次

  void wait();

  void countDown();

  int getCount();

 private:
  std::mutex mutex_;
  std::condition_variable condition_ GUARDED_BY(mutex_);
  int count_ GUARDED_BY(mutex_);
};

}  // namespace muduo
#endif  // MUDUO_BASE_COUNTDOWNLATCH_H
