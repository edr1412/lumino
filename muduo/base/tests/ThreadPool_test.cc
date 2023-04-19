#include <muduo/base/ThreadPool.h>
#include <muduo/base/CountDownLatch.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Logging.h>

#include <stdio.h>
#include <unistd.h>  // usleep

void print()
{
  printf("tid=%d\n", muduo::CurrentThread::tid());
}

void printString(const std::string& str)
{
  LOG_INFO << str;
  usleep(100*1000);
}

void longTask(int num)
{
  LOG_INFO << "longTask" << num;
  muduo::CurrentThread::sleepUsec(3000000);
}


void test(int maxSize)
{
  LOG_WARN << "Test ThreadPool with max queue size = " << maxSize;
  muduo::ThreadPool pool("MainThreadPool");
  pool.setMaxQueueSize(maxSize);
  pool.start(5);

  LOG_WARN << "Adding";
  pool.run(print);
  pool.run(print);
  for (int i = 0; i < 100; ++i)
  {
    char buf[32];
    snprintf(buf, sizeof buf, "task %d", i);
    pool.run(std::bind(printString, std::string(buf)));
  }
  LOG_WARN << "Done";
  // show queue size
  LOG_WARN << "now queueSize = " << pool.queueSize();


  muduo::CountDownLatch latch(1);
  pool.run(std::bind(&muduo::CountDownLatch::countDown, &latch));
  latch.wait();
  pool.stop();
}

void test2()
{
  LOG_WARN << "Test ThreadPool by stoping early. tid = "
           << muduo::CurrentThread::tid();
  muduo::ThreadPool pool;
  pool.setMaxQueueSize(5);
  pool.start(3);
  muduo::Thread t1(
      [&pool]()
      {
        LOG_WARN << "The tid of thread t1 is " << muduo::CurrentThread::tid();
        for (int i = 0; i < 20; ++i)
        {
          pool.run(std::bind(longTask, i));
        }
      },
      "thread1");
  muduo::CurrentThread::sleepUsec(5000000);
  LOG_WARN << "stop pool";
  pool.stop();
  t1.join();
  pool.run(print);
  LOG_WARN << "test2 Done";
}

int main()
{
  test(0);
  test(1);
  test(5);
  test(10);
  test(50);
  test2();
}
