#include <muduo/base/ThreadLocal.h>
#include <muduo/base/CurrentThread.h>
#include <muduo/base/Thread.h>

#include <stdio.h>

class Test : muduo::noncopyable
{
 public:
  Test()
  {
    printf("tid=%d, constructing %p\n", muduo::CurrentThread::tid(), this);
  }

  ~Test()
  {
    printf("tid=%d, destructing %p %s\n", muduo::CurrentThread::tid(), this, name_.c_str());
  }

  const muduo::string& name() const { return name_; }
  void setName(const muduo::string& n) { name_ = n; }

 private:
  muduo::string name_;
};

thread_local Test testObj1;
thread_local Test testObj2;

void print()
{
  printf("tid=%d, obj1 %p name=%s\n",
         muduo::CurrentThread::tid(),
         &testObj1,
         testObj1.name().c_str());
  printf("tid=%d, obj2 %p name=%s\n",
         muduo::CurrentThread::tid(),
         &testObj2,
         testObj2.name().c_str());
}

void threadFunc()
{
  print();
  testObj1.setName("changed 1");
  testObj2.setName("changed 42");
  print();
}

int main()
{
  testObj1.setName("main one");
  print();
  muduo::Thread t1(threadFunc);
  t1.join();
  testObj2.setName("main two");
  print();

  pthread_exit(0);
}
