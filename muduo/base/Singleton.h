// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_SINGLETON_H
#define MUDUO_BASE_SINGLETON_H

#include <muduo/base/noncopyable.h>

#include <assert.h>
#include <stdint.h>

namespace muduo
{

namespace detail
{
// has_no_destroy 可以检测一个类中是否声明了 no_destroy 函数，参考：
// https://www.hacker-cube.com/2020/11/09/对-muduo-网络库单例模式的思考与实践-下/

template <typename T>
class has_no_destroy
{
  using yes = char;
  using no = int32_t;
  struct Base
  {
    void no_destroy();
  };
  struct Derive : public T, public Base
  {
  };
  template <typename C, C>
  class Helper
  {
  };
  // 如果U或U的基类子类型中声明了 no_destroy，会因产生二义性错误而导致 Helper 实例化失败，
  // 进而导致 static no test 实例化失败。 此时 value 中对 test 的调用将会匹配到 static yes test。
  // 如果U或U的基类子类型中没有声明 no_destroy，则 static no test 将会实例化成功，
  // 根据最佳匹配原则，value 中对 test 的调用将会匹配到 static no test。
  // 最后，根据 test 的返回值类型，value 的类型将会被推导为 yes 或 no。
  template <typename U>
  static no test(
      U *, Helper<decltype(&Base::no_destroy), &U::no_destroy> * = nullptr);
  static yes test(...);

public:
  static const bool value =
      sizeof(yes) == sizeof(test(static_cast<Derive *>(nullptr)));
};
}  // namespace detail

//一个单例对象需要符合哪些特质才能算得上是一个合格的单例？我想至少有以下几点：
// 1. 该对象必定是不可拷贝的
// 2. 用户只能够通过 instance 方法获得唯一的实例，且在若干次调用 instance() 方法中，仅会在首次调用时进行初始化
// 3. 必须是资源安全的，在程序退出时必须能够正确地析构，不会造成任何的资源泄露
// 4. 若是线程全局单例模式，则还需要考虑线程安全的问题。若是线程局部单例模式，则需要考虑线程局部存储的问题
//
// 对于这些要求，在 C++11 标准下很好实现：
// 1. 通过继承 noncopyable 类即可保证该类不可拷贝
// 2. C++11 标准保证了 static 修饰局部变量只会在程序的控制流首次进入相应的块作用域才进行实例化工作
// 3. C++11 标准保证了 static 修饰局部变量在程序退出时能够自动调用相应的析构函数。由于 static 修饰的局部变量并不存储在堆内存当中，因此自然也不会造成任何的内存泄露。
// 4. C++11 标准保证了 static local variable 的初始化是线程安全的。
template <typename T>
class Singleton : noncopyable
{
public:
  Singleton() = delete;
  ~Singleton() = delete;
  static T &instance()
  {
    static T instance;
    return instance;
  }
};

}  // namespace muduo

#endif  // MUDUO_BASE_SINGLETON_H
