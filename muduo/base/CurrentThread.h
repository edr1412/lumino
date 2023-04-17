// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_CURRENTTHREAD_H
#define MUDUO_BASE_CURRENTTHREAD_H

#include <muduo/base/Types.h>

namespace muduo
{
namespace CurrentThread
{
  // internal
  extern thread_local int t_cachedTid; // 缓存 gettid 的执行结果，避免反复调用系统调用带来的开销
  extern thread_local char t_tidString[32];
  extern thread_local int t_tidStringLength;
  extern thread_local const char* t_threadName;
  void cacheTid();

  inline int tid()
  {
    // __builtin_expect 是 GCC 的一个内置函数，
    // 其作用是供程序员将分支信息提供给编译器，以方便编译器调整取指令的顺序进行优化，
    // 这样可以减少 cache 产生控制冒险。
    if (__builtin_expect(t_cachedTid == 0, 0))
    {
      cacheTid();
    }
    return t_cachedTid;
  }

  inline const char* tidString() // for logging
  {
    return t_tidString;
  }

  inline int tidStringLength() // for logging
  {
    return t_tidStringLength;
  }

  inline const char* name()
  {
    return t_threadName;
  }

  bool isMainThread();

  void sleepUsec(int64_t usec);  // for testing

  string stackTrace(bool demangle);
}  // namespace CurrentThread
}  // namespace muduo

#endif  // MUDUO_BASE_CURRENTTHREAD_H
