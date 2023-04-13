// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#ifndef MUDUO_BASE_LOGFILE_H
#define MUDUO_BASE_LOGFILE_H

#include <muduo/base/Mutex.h>
#include <muduo/base/Types.h>

#include <memory>

namespace muduo
{

namespace FileUtil
{
class AppendFile;
}

class LogFile : noncopyable
{
 public:
  LogFile(const string& basename,
          off_t rollSize,
          bool threadSafe = true, // 线程安全控制项, 默认为true. 当只有一个后端AsnycLogging和后端线程时, 该项可置为false. 
          int flushInterval = 3,
          int checkEveryN = 1024);
  ~LogFile();

  void append(const char* logline, int len);
  void flush();
  bool rollFile(); // 当日志文件接近指定的滚动限值（rollSize）时，需要换一个新文件写数据，便于后续归档、查看

 private:
  void append_unlocked(const char* logline, int len);

  static string getLogFileName(const string& basename, time_t* now); // 得到一个全新的、唯一的log文件名

  const string basename_; // 基础文件名, 用于新log文件命名
  const off_t rollSize_;  // 滚动文件大小
  const int flushInterval_; // 冲刷时间限值, 默认3 (秒)
  const int checkEveryN_; // 写数据次数限值, 默认1024

  int count_; // 写数据次数计数, 超过限值checkEveryN_时清除, 然后重新计数

  std::unique_ptr<MutexLock> mutex_; // 互斥锁指针, 根据是否需要线程安全来初始化
                                     // AsyncLogging 和许多日志前端打交道，是一对多的问题，因此必须考虑线程安全性。而 LogFile 只和 AsyncLogging 打交道，是一对一的情况，主要以无锁版本为主。
  time_t startOfPeriod_; // 本次写log周期的起始时间(秒)
  time_t lastRoll_; // 上次roll日志文件时间(秒)
  time_t lastFlush_; // 上次flush日志文件时间(秒)
  std::unique_ptr<FileUtil::AppendFile> file_;

  const static int kRollPerSeconds_ = 60*60*24;
};

}  // namespace muduo
#endif  // MUDUO_BASE_LOGFILE_H
