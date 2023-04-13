// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Exception.h>
#include <muduo/base/Logging.h>
#include <muduo/base/Thread.h>
#include <muduo/base/CurrentThread.h>

#include <type_traits>

#include <errno.h>
#include <linux/unistd.h>
#include <stdio.h>
#include <sys/prctl.h>
#include <sys/syscall.h>
#include <sys/types.h>
#include <unistd.h>

namespace muduo
{

std::atomic_int32_t Thread::numCreated_(0);

void Thread::runInThread()
{
  tid_ = muduo::CurrentThread::tid();
  if (name_.empty())
  {
    char buf[32];
    snprintf(buf, sizeof buf, "Thread%d", Thread::numCreated());
    muduo::CurrentThread::t_threadName = buf;
  }
  else
  {
    muduo::CurrentThread::t_threadName = name_.c_str();
  }
  //::prctl(PR_SET_NAME, threadName)：表示用 threadName
  // 为当前线程命名，threadName 的长度
  // 不得超过 16 bytes。当名字长度超过 16 个字节时会默认截断
  ::prctl(PR_SET_NAME, muduo::CurrentThread::t_threadName);
  try
  {
    func_();
    muduo::CurrentThread::t_threadName = "finished";
  }
  catch (const Exception &ex)
  {
    muduo::CurrentThread::t_threadName = "crashed";
    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    fprintf(stderr, "stack trace: %s\n", ex.stackTrace());
    abort();
  }
  catch (const std::exception &ex)
  {
    muduo::CurrentThread::t_threadName = "crashed";
    fprintf(stderr, "exception caught in Thread %s\n", name_.c_str());
    fprintf(stderr, "reason: %s\n", ex.what());
    abort();
  }
  catch (...)
  {
    muduo::CurrentThread::t_threadName = "crashed";
    fprintf(stderr, "unknown exception caught in Thread %s\n", name_.c_str());
    throw; // rethrow
  }
}

Thread::Thread(ThreadFunc func, const std::string &name)
    : func_(std::move(func)),
      name_(name),
      tid_(0),
      thread_(std::bind(&Thread::runInThread, this))
{
  ++numCreated_;
}

Thread::Thread(Thread &&rhs) noexcept : thread_(std::move(rhs.thread_)) {}

Thread &Thread::operator=(Thread &&rhs) noexcept
{
  if (this != &rhs)
  {
    thread_ = std::move(rhs.thread_);
  }
  return *this;
}

void Thread::join()
{
  if (thread_.joinable())
  {
    --numCreated_;
    thread_.join();
  }
}

Thread::~Thread()
{
  if (thread_.joinable())
  {
    --numCreated_;
    thread_.detach();
  }
}

} // namespace muduo
