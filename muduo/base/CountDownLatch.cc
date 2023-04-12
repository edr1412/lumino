// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/CountDownLatch.h>

using namespace muduo;

CountDownLatch::CountDownLatch(int count)
  : mutex_(),
    condition_(),
    count_(count)
{
}

void CountDownLatch::wait()
{
  std::unique_lock<std::mutex> lock(mutex_);
  condition_.wait(lock, [this]() { return count_ == 0; }); // 当count_ == 0时，才会返回
}

// 使计数器减一，并在计数器值为零时唤醒所有等待的线程
void CountDownLatch::countDown()
{
  std::lock_guard<std::mutex> lock(mutex_);
  --count_;
  if (count_ == 0)
  {
    condition_.notify_all();
  }
}

int CountDownLatch::getCount()
{
  std::lock_guard<std::mutex> lock(mutex_);
  return count_;
}

