// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/AsyncLogging.h>
#include <muduo/base/LogFile.h>
#include <muduo/base/Timestamp.h>

#include <stdio.h>

using namespace muduo;

AsyncLogging::AsyncLogging(const string& basename,
                           off_t rollSize,
                           int flushInterval)
  : flushInterval_(flushInterval),
    running_(true),
    basename_(basename),
    rollSize_(rollSize),
    thread_(std::bind(&AsyncLogging::threadFunc, this), "Logging"),
    latch_(1),
    mutex_(),
    cond_(),
    currentBuffer_(new Buffer),
    nextBuffer_(new Buffer),
    buffers_()
{
  // 缓冲全部填充为0，避免程序热身时 page fault 引发性能不稳定
  currentBuffer_->bzero();
  nextBuffer_->bzero();
  buffers_.reserve(16);
}

void AsyncLogging::append(const char* logline, int len)
{
  std::unique_lock<std::mutex> lock(mutex_);
  // 最常见的情况
  // 如果当前缓冲剩余的空间足够大，则直接把日志消息拷贝（追加）到当前缓冲中
  if (currentBuffer_->avail() > len)
  {
    currentBuffer_->append(logline, len);
  }
  else
  {
    buffers_.push_back(std::move(currentBuffer_)); // 当前缓冲满了，把当前缓冲移入buffers_中

    // 试图把预备好的另一块缓冲（nextBuffer_）移用为当前缓冲
    if (nextBuffer_)
    {
      currentBuffer_ = std::move(nextBuffer_);
    }
    else
    {
      currentBuffer_.reset(new Buffer); // 前端写入太快，后端来不及插手，前端一下子把两块缓冲都用完了，只能分配一块新的缓冲。极少发生。
    }
    currentBuffer_->append(logline, len);
    cond_.notify_one(); // 通知后端线程开始写入日志数据
  }
}

void AsyncLogging::threadFunc()
{
  assert(running_ == true);
  latch_.countDown();
  LogFile output(basename_, rollSize_, false);
  // 准备好两块空闲的buffer，以备在临界区内交换
  BufferPtr newBuffer1(new Buffer);
  BufferPtr newBuffer2(new Buffer);
  newBuffer1->bzero();
  newBuffer2->bzero();
  BufferVector buffersToWrite;
  buffersToWrite.reserve(16);
  while (running_)
  {
    assert(newBuffer1 && newBuffer1->length() == 0);
    assert(newBuffer2 && newBuffer2->length() == 0);
    assert(buffersToWrite.empty());

    {
      std::unique_lock<std::mutex> lock(mutex_);
      if (buffers_.empty())
      {
          // 这里是非常规的condition variable用法，它没有使用while循环，而且等待时间有上限
          // 因为在这里等待条件触发，其条件有两个：
          // 1. 前端写满了一个或多个buffer，通知了后端线程
          // 2. 超时
          cond_.wait_for(lock, std::chrono::seconds(flushInterval_));
      }
      // 当“条件”满足时，先将当前缓冲移入buffers_，并立刻将空闲的newBuffer1移为当前缓冲。
      buffers_.push_back(std::move(currentBuffer_)); // 不管写了多少
      currentBuffer_ = std::move(newBuffer1);
      buffersToWrite.swap(buffers_); // buffers_和buffersToWrite交换，后面的代码可以在临界区之外安全地访问buffersToWrite，将其中的日志数据写入文件
      if (!nextBuffer_)
      {
        nextBuffer_ = std::move(newBuffer2); // 让前端始终有一个预备缓冲缓冲可供调配
      }
    }
    // 离开了缓冲区，可以慢悠悠地写入日志到文件了，写完后填上newBuffer1和newBuffer2，等待下一次wait_for返回

    assert(!buffersToWrite.empty());

    // 万一前端陷入死循环，处理日志堆积的方法很简单：直接丢掉多余的日志buffer，以腾出内存
    if (buffersToWrite.size() > 25)
    {
      char buf[256];
      snprintf(buf, sizeof buf, "Dropped log messages at %s, %zd larger buffers\n",
               Timestamp::now().toFormattedString().c_str(),
               buffersToWrite.size()-2);
      fputs(buf, stderr);
      output.append(buf, static_cast<int>(strlen(buf)));
      buffersToWrite.erase(buffersToWrite.begin()+2, buffersToWrite.end());
    }

    // 将buffersToWrite中的日志数据写入文件
    for (const auto& buffer : buffersToWrite)
    {
      // FIXME: use unbuffered stdio FILE ? or use ::writev ?
      output.append(buffer->data(), buffer->length());
    }

    if (buffersToWrite.size() > 2)
    {
      // drop non-bzero-ed buffers, avoid trashing
      buffersToWrite.resize(2);
    }

    // 拿buffersToWrite内的buffer重新填充newBuffer1和newBuffer2
    if (!newBuffer1)
    {
      assert(!buffersToWrite.empty());
      newBuffer1 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer1->reset();
    }

    if (!newBuffer2)
    {
      assert(!buffersToWrite.empty());
      newBuffer2 = std::move(buffersToWrite.back());
      buffersToWrite.pop_back();
      newBuffer2->reset();
    }

    buffersToWrite.clear();
    output.flush();
  }
  output.flush();
}

