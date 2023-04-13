// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/net/EventLoopThread.h>

#include <muduo/net/EventLoop.h>

using namespace muduo;
using namespace muduo::net;

EventLoopThread::EventLoopThread(const ThreadInitCallback& cb,
                                 const string& name)
  : loop_(nullptr),
    mutex_(),
    cond_(),
    exiting_(false),
    canGetLoop_(true),
    callback_(cb),
    thread_(std::bind(&EventLoopThread::threadFunc, this), name)
{
}

EventLoopThread::~EventLoopThread()
{
  exiting_ = true;
  if (loop_ != nullptr) // not 100% race-free, eg. threadFunc could be running callback_.
  {
    // still a tiny chance to call destructed object, if threadFunc exits just now.
    // but when EventLoopThread destructs, usually programming is exiting anyway.
    loop_->quit();
    thread_.join();
  }
}

EventLoop* EventLoopThread::getLoop()
{
  // 只能调用一次
  assert(canGetLoop_);
  canGetLoop_ = false;

  // 等待threadFunc在stack上定义EventLoop对象，然后将其地址赋值给 loop_ 成员变量后，被唤醒，从 loop_ 拿到EventLoop对象的地址并返回
  EventLoop* loop = nullptr;
  {
    std::unique_lock<std::mutex> lock(mutex_);
    cond_.wait(lock, [this] { return loop_ != nullptr; });
    loop = loop_;
  }

  return loop;
}

void EventLoopThread::threadFunc()
{
  // 这里EventLoop的生命期与线程主函数的作用域相同，
  // 因此在threadFunc()退出之后这个指针就失效了。
  // 好在服务程序一般不要求能安全地退出，这应该不是什么大问题。
  EventLoop loop;

  if (callback_)
  {
    callback_(&loop);
  }

  {
    std::lock_guard<std::mutex> lock(mutex_);
    loop_ = &loop;
    cond_.notify_one();
  }

  loop.loop();
  //assert(exiting_);
  // loop 中有一些函数是可以给其他线程使用的,而有一些是可以给其他线程使用的
  // 因此需要确保在 threadFunc 能够独占这个 loop_ 的情况下,才可以改变 loop_
  std::lock_guard<std::mutex> lock(mutex_);
  loop_ = nullptr;
}

