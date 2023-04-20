#include <muduo/base/LockFreeQueue.h>
#include <iostream>
#include <thread>
#include <vector>
#include <functional>

using Task = std::function<void()>;

void producer(muduo::LockFreeQueue<Task> &queue, int id) {
  for (int i = 0; i < 10; ++i) {
    queue.push([id, i] { printf("task %d pushed by Producer %d \n", i, id); });
    printf("Producer %d pushed task %d\n", id, i);
  }
}

void consumer(muduo::LockFreeQueue<Task> &queue, int id) {
  for (int i = 0; i < 10; ++i) {
    while (true) {
      std::unique_ptr<Task> task = queue.pop();
      if (task) {
        (*task)();
        printf("Consumer %d executed task\n", id);
        break;
      }
      else {
        printf("Consumer %d failed to pop task\n", id);
      }
    }
  }
}

int main() {
  muduo::LockFreeQueue<Task> queue;

  // printf("pushing task\n");
  // queue.push(1);
  // printf("pushed task\n");
  // printf("pushing task\n");
  // queue.push(2);
  // printf("pushed task\n");

  // std::unique_ptr<Task> task = queue.pop();
  // if (task) {
  //   (*task)();
  //   printf("executed task\n");
  // }
  // else {
  //   printf("failed to pop task\n");
  // }

  for (int i = 0; i < 10; ++i) {
    queue.push([i] { printf("task %d pushed by Producer \n", i); });
    printf("Producer pushed task %d\n", i);
  }

  std::vector<std::thread> producers;
  std::vector<std::thread> consumers;

  for (int i = 0; i < 1; ++i) {
    producers.emplace_back(producer, std::ref(queue), i);
    consumers.emplace_back(consumer, std::ref(queue), i);
  }

  for (auto &producer_thread : producers) {
    producer_thread.join();
  }

  for (auto &consumer_thread : consumers) {
    consumer_thread.join();
  }

  return 0;
}