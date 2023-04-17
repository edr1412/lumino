// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_THREADLOCALSINGLETON_H
#define MUDUO_BASE_THREADLOCALSINGLETON_H

#include <muduo/base/noncopyable.h>

#include <assert.h>
#include <pthread.h>

namespace muduo
{
//一个单例对象需要符合哪些特质才能算得上是一个合格的单例？我想至少有以下几点：
// 1. 该对象必定是不可拷贝的
// 2. 用户只能够通过 instance 方法获得唯一的实例，且在若干次调用 instance() 方法中，仅会在首次调用时进行初始化
// 3. 必须是资源安全的，在程序退出时必须能够正确地析构，不会造成任何的资源泄露
// 4. 若是线程全局单例模式，则还需要考虑线程安全的问题。若是线程局部单例模式，则需要考虑线程局部存储的问题
//
// 对于这些要求，在 C++11 标准下很好实现：
// 1. 通过继承 noncopyable 类即可保证该类不可拷贝
// 2. C++11 标准保证了 thread_local 修饰局部变量只会在程序的控制流首次进入相应的块作用域才进行实例化工作
// 3. C++11 标准保证了 thread_local 修饰局部变量在线程退出时能够自动调用相应的析构函数。 由于 thread_local 修饰的局部变量并不存储在堆内存当中，因此自然也不会造成任何的内存泄露。
// 4. C++11 的 thread_local 本身就代表了线程局部存储，因此不需要再使用 pthread_key_create 和 pthread_setspecific 等函数来实现线程局部存储。
template <typename T>
class ThreadLocalSingleton : noncopyable
{
public:
  ThreadLocalSingleton() = delete;
  ~ThreadLocalSingleton() = delete;
  static T &instance()
  {
    thread_local T instance;
    return instance;
  }
};

}  // namespace muduo
#endif  // MUDUO_BASE_THREADLOCALSINGLETON_H
