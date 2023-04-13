// Use of this source code is governed by a BSD-style license
// that can be found in the License file.
//
// Author: Shuo Chen (chenshuo at chenshuo dot com)

#include <muduo/base/Logging.h>

#include <muduo/base/CurrentThread.h>
#include <muduo/base/Timestamp.h>
#include <muduo/base/TimeZone.h>

#include <errno.h>
#include <stdio.h>
#include <string.h>

#include <sstream>

namespace muduo
{

/*
class LoggerImpl
{
 public:
  typedef Logger::LogLevel LogLevel;
  LoggerImpl(LogLevel level, int old_errno, const char* file, int line);
  void finish();

  Timestamp time_;
  LogStream stream_;
  LogLevel level_;
  int line_;
  const char* fullname_;
  const char* basename_;
};
*/

thread_local char t_errnobuf[512]; // 主要供 strerror_tl 函数使用
thread_local char t_time[64]; //  保存了精度到秒的时间
thread_local time_t t_lastSecond;  // 保存了上次格式化时间的秒数。如果时间间隔低于 1 秒，则直接读取 t_time 中的时间，并更新微秒数

// 自定义函数strerror_tl将错误号转换为字符串, 相当于strerror_r(3)
const char* strerror_tl(int savedErrno)
{
  return strerror_r(savedErrno, t_errnobuf, sizeof t_errnobuf); // t_errnobuf 是线程局部变量，保证线程安全
}

Logger::LogLevel initLogLevel()
{
  if (::getenv("MUDUO_LOG_TRACE"))
    return Logger::TRACE;
  else if (::getenv("MUDUO_LOG_DEBUG"))
    return Logger::DEBUG;
  else
    return Logger::INFO;
}

Logger::LogLevel g_logLevel = initLogLevel();

const char* LogLevelName[Logger::NUM_LOG_LEVELS] =
{
  "TRACE ",
  "DEBUG ",
  "INFO  ",
  "WARN  ",
  "ERROR ",
  "FATAL ",
};

// helper class for known string length at compile time
class T
{
 public:
  T(const char* str, unsigned len)
    :str_(str),
     len_(len)
  {
    assert(strlen(str) == len_);
  }

  const char* str_;
  const unsigned len_;
};

inline LogStream& operator<<(LogStream& s, T v)
{
  s.append(v.str_, v.len_);
  return s;
}

inline LogStream& operator<<(LogStream& s, const Logger::SourceFile& v)
{
  s.append(v.data_, v.size_);
  return s;
}

void defaultOutput(const char* msg, int len)
{
  size_t n = fwrite(msg, 1, len, stdout);
  //FIXME check n
  // assert(n == len);
  (void)n;
}

void defaultFlush()
{
  fflush(stdout);
}

// Logger类定义了2个函数指针，用于设置日志的输出位置（g_output），冲刷日志（g_flush）。
// 默认向stdout输出、冲刷。这只能将数据以非线程安全方式输出到stdout，还不能实现异步记录log消息。
Logger::OutputFunc g_output = defaultOutput;
Logger::FlushFunc g_flush = defaultFlush;
TimeZone g_logTimeZone;

}  // namespace muduo

using namespace muduo;

// 在构造函数中，Logger::Impl::Impl()会将日志的时间、线程ID、日志级别、文件名、行号等信息写入到LogStream中，这是日志的前缀信息。
Logger::Impl::Impl(LogLevel level, int savedErrno, const SourceFile& file, int line)
  : time_(Timestamp::now()),
    stream_(),
    level_(level),
    line_(line),
    basename_(file)
{
  formatTime();
  CurrentThread::tid();
  stream_ << T(CurrentThread::tidString(), CurrentThread::tidStringLength());
  stream_ << T(LogLevelName[level], 6);
  if (savedErrno != 0) // 发生系统调用错误
  {
    stream_ << strerror_tl(savedErrno) << " (errno=" << savedErrno << ") ";
  }
}

void Logger::Impl::formatTime()
{
  int64_t microSecondsSinceEpoch = time_.microSecondsSinceEpoch();
  time_t seconds = static_cast<time_t>(microSecondsSinceEpoch / Timestamp::kMicroSecondsPerSecond);
  int microseconds = static_cast<int>(microSecondsSinceEpoch % Timestamp::kMicroSecondsPerSecond);
  if (seconds != t_lastSecond)
  {
    t_lastSecond = seconds;
    struct tm tm_time;
    if (g_logTimeZone.valid())
    {
      tm_time = g_logTimeZone.toLocalTime(seconds);
    }
    else
    {
      ::gmtime_r(&seconds, &tm_time); // FIXME TimeZone::fromUtcTime
    }

    int len = snprintf(t_time, sizeof(t_time), "%4d%02d%02d %02d:%02d:%02d",
        tm_time.tm_year + 1900, tm_time.tm_mon + 1, tm_time.tm_mday,
        tm_time.tm_hour, tm_time.tm_min, tm_time.tm_sec);
    assert(len == 17); (void)len;
  }

  if (g_logTimeZone.valid())
  {
    Fmt us(".%06d ", microseconds);
    assert(us.length() == 8);
    stream_ << T(t_time, 17) << T(us.data(), 8);
  }
  else
  {
    Fmt us(".%06dZ ", microseconds);
    assert(us.length() == 9);
    stream_ << T(t_time, 17) << T(us.data(), 9);
  }
}

void Logger::Impl::finish()
{
  stream_ << " - " << basename_ << ':' << line_ << '\n';
}

Logger::Logger(SourceFile file, int line)
  : impl_(INFO, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, LogLevel level, const char* func)
  : impl_(level, 0, file, line)
{
  impl_.stream_ << func << ' ';
}

Logger::Logger(SourceFile file, int line, LogLevel level)
  : impl_(level, 0, file, line)
{
}

Logger::Logger(SourceFile file, int line, bool toAbort)
  : impl_(toAbort?FATAL:ERROR, errno, file, line)
{
}

// 使用LOG_XXX宏会临时产生一个匿名对象，生命期结束时，会调用Logger的析构函数。在这里，将日志信息放入缓冲区中并发送给后端进行处理。
Logger::~Logger()
{
  impl_.finish(); // 为LogStream对象stream_中的log消息加上后缀（文件名:行号，LF指换行符'\n'）
  const LogStream::Buffer& buf(stream().buffer());
  g_output(buf.data(), buf.length()); // 将stream_缓存的log消息通过g_output回调写入指定文件流
  if (impl_.level_ == FATAL) // 如果发生致命错误, 输出log并终止程序
  {
    g_flush(); // 回调冲刷
    abort();
  }
}

void Logger::setLogLevel(Logger::LogLevel level)
{
  g_logLevel = level;
}

// 提供2个static函数来设置g_output和g_flush
// 用户代码可以这两个函数修改Logger的输出位置（需要同步修改）。
// 一种典型的应用，就是将g_output重定位到后端AsyncLogging::append()，这样就可以异步地将日志写入文件。见AsyncLogging_test.cc
void Logger::setOutput(OutputFunc out)
{
  g_output = out;
}

void Logger::setFlush(FlushFunc flush)
{
  g_flush = flush;
}

void Logger::setTimeZone(const TimeZone& tz)
{
  g_logTimeZone = tz;
}
