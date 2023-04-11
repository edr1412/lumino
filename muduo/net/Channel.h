// Copyright 2010, Shuo Chen.  All rights reserved.
// http://code.google.com/p/muduo/
//
// Use of this source code is governed by a BSD-style license
// that can be found in the License file.

// Author: Shuo Chen (chenshuo at chenshuo dot com)
//
// This is an internal header file, you should not include this.

#ifndef MUDUO_NET_CHANNEL_H
#define MUDUO_NET_CHANNEL_H

#include <muduo/base/noncopyable.h>
#include <muduo/base/Timestamp.h>

#include <functional>
#include <memory>

namespace muduo
{
namespace net
{

class EventLoop; // 前向声明，简化了头文件之间的依赖关系，避免包含EventLoop.h

///
/// A selectable I/O channel.
///
/// 每个Channel对象自始至终只负责一个文件描述符（fd）的IO事件分发，
/// 但它并不拥有这个fd，也不会在析构的时候关闭这个fd。
/// The file descriptor could be a socket,
/// an eventfd, a timerfd, or a signalfd
/// Channel会把不同的IO事件分发为不同的回调，例如ReadCallback、WriteCallback等
/// Channel的生命期由其owner class负责管理，它一般是其他class的直接或间接成员。
class Channel : noncopyable
{
 public:
  typedef std::function<void()> EventCallback;
  typedef std::function<void(Timestamp)> ReadEventCallback;

  Channel(EventLoop* loop, int fd);
  ~Channel();

  void handleEvent(Timestamp receiveTime); // Channel的核心，由EventLoop::loop调用，作用是根据revents_的值调用不同的用户回调
  void setReadCallback(ReadEventCallback cb)
  { readCallback_ = std::move(cb); }
  void setWriteCallback(EventCallback cb)
  { writeCallback_ = std::move(cb); }
  void setCloseCallback(EventCallback cb)
  { closeCallback_ = std::move(cb); }
  void setErrorCallback(EventCallback cb)
  { errorCallback_ = std::move(cb); }

  /// Tie this channel to the owner object managed by shared_ptr,
  /// prevent the owner object being destroyed in handleEvent.
  void tie(const std::shared_ptr<void>&);

  int fd() const { return fd_; }
  int events() const { return events_; }
  void set_revents(int revt) { revents_ = revt; } // used by pollers
  // int revents() const { return revents_; }
  bool isNoneEvent() const { return events_ == kNoneEvent; }

  void enableReading() { events_ |= kReadEvent; update(); }
  void disableReading() { events_ &= ~kReadEvent; update(); }
  void enableWriting() { events_ |= kWriteEvent; update(); }
  void disableWriting() { events_ &= ~kWriteEvent; update(); }
  void disableAll() { events_ = kNoneEvent; update(); }
  bool isWriting() const { return events_ & kWriteEvent; }
  bool isReading() const { return events_ & kReadEvent; }

  // for Poller
  int index() { return index_; }
  void set_index(int idx) { index_ = idx; }

  // for debug
  string reventsToString() const;
  string eventsToString() const;

  void doNotLogHup() { logHup_ = false; }

  EventLoop* ownerLoop() { return loop_; }
  void remove();

 private:
  static string eventsToString(int fd, int ev);

  void update(); // 会调用EventLoop::updateChannel，后者会调用Poller::updateChannel。必须定义在Channel.cc中，因为Channel.cc包含了EventLoop.h
  void handleEventWithGuard(Timestamp receiveTime);

  // Channel.h 没有包含任何POSIX头文件，因此这些常量的定义放在了Channel.cc中
  static const int kNoneEvent;
  static const int kReadEvent;
  static const int kWriteEvent;

  EventLoop* loop_;
  const int  fd_;
  int        events_; // 关心的IO事件，由用户设置。bit pattern
  int        revents_; // 目前活动的IO事件，由Poller设置。bit pattern
  int        index_; // used by Poller. 在PollPoller中表示 pollfds_ 数组中的下标，在EPollPoller中被挪用为标记此Channel是否位于epoll的关注列表之中
  bool       logHup_;

  std::weak_ptr<void> tie_;
  bool tied_;
  bool eventHandling_;
  bool addedToLoop_;
  ReadEventCallback readCallback_;
  EventCallback writeCallback_;
  EventCallback closeCallback_;
  EventCallback errorCallback_;
};

}  // namespace net
}  // namespace muduo

#endif  // MUDUO_NET_CHANNEL_H
