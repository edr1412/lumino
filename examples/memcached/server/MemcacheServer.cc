#include "MemcacheServer.h"

#include <atomic>
#include <muduo/base/Logging.h>
#include <muduo/net/EventLoop.h>

using namespace muduo;
using namespace muduo::net;

std::atomic_int64_t g_cas;

MemcacheServer::Options::Options()
{
  memZero(this, sizeof(*this));
}

struct MemcacheServer::Stats
{
};

MemcacheServer::MemcacheServer(muduo::net::EventLoop* loop, const Options& options)
  : loop_(loop),
    options_(options),
    startTime_(::time(NULL)-1),
    server_(loop, InetAddress(options.tcpport), "muduo-memcached"),
    stats_(new Stats)
{
  server_.setConnectionCallback(
      std::bind(&MemcacheServer::onConnection, this, _1));
}

MemcacheServer::~MemcacheServer() = default;

void MemcacheServer::start()
{
  server_.start();
}

void MemcacheServer::stop()
{
  loop_->runAfter(3.0, std::bind(&EventLoop::quit, loop_));
}

bool MemcacheServer::storeItem(const ItemPtr& item, const Item::UpdatePolicy policy, bool* exists)
{
  assert(item->neededBytes() == 0);
  MutexLock& mutex = shards_[item->hash() % kShards].mutex;
  ItemMap& items = shards_[item->hash() % kShards].items;
  MutexLockGuard lock(mutex);
  ItemMap::const_iterator it = items.find(item);
  *exists = it != items.end();
  if (policy == Item::kSet)
  {
    item->setCas(g_cas.fetch_add(1) + 1);
    if (*exists)
    {
      items.erase(it);
    }
    items.insert(item);
  }
  else
  {
    if (policy == Item::kAdd)
    {
      if (*exists)
      {
        return false;
      }
      else
      {
        item->setCas(g_cas.fetch_add(1) + 1);
        items.insert(item);
      }
    }
    else if (policy == Item::kReplace)
    {
      if (*exists)
      {
        item->setCas(g_cas.fetch_add(1) + 1);
        items.erase(it);
        items.insert(item);
      }
      else
      {
        return false;
      }
    }
    else if (policy == Item::kAppend || policy == Item::kPrepend)
    {
      if (*exists)
      {
        const ConstItemPtr& oldItem = *it;
        int newLen = static_cast<int>(item->valueLength() + oldItem->valueLength() - 2);
        ItemPtr newItem(Item::makeItem(item->key(),
                                       oldItem->flags(),
                                       oldItem->rel_exptime(),
                                       newLen,
                                       g_cas.fetch_add(1) + 1));
        if (policy == Item::kAppend)
        {
          newItem->append(oldItem->value(), oldItem->valueLength() - 2);
          newItem->append(item->value(), item->valueLength());
        }
        else
        {
          newItem->append(item->value(), item->valueLength() - 2);
          newItem->append(oldItem->value(), oldItem->valueLength());
        }
        assert(newItem->neededBytes() == 0);
        assert(newItem->endsWithCRLF());
        items.erase(it);
        items.insert(newItem);
      }
      else
      {
        return false;
      }
    }
    else if (policy == Item::kCas)
    {
      if (*exists && (*it)->cas() == item->cas())
      {
        item->setCas(g_cas.fetch_add(1) + 1);
        items.erase(it);
        items.insert(item);
      }
      else
      {
        return false;
      }
    }
    else
    {
      assert(false);
    }
  }
  return true;
}

ConstItemPtr MemcacheServer::getItem(const ConstItemPtr& key) const
{
  MutexLock& mutex = shards_[key->hash() % kShards].mutex;
  const ItemMap& items = shards_[key->hash() % kShards].items;
  MutexLockGuard lock(mutex);
  ItemMap::const_iterator it = items.find(key);
  return it != items.end() ? *it : ConstItemPtr();
}

bool MemcacheServer::deleteItem(const ConstItemPtr& key)
{
  MutexLock& mutex = shards_[key->hash() % kShards].mutex;
  ItemMap& items = shards_[key->hash() % kShards].items;
  MutexLockGuard lock(mutex);
  return items.erase(key) == 1;
}

void MemcacheServer::onConnection(const TcpConnectionPtr& conn)
{
  if (conn->connected())
  {
    SessionPtr session(new Session(this, conn));
    MutexLockGuard lock(mutex_);
    assert(sessions_.find(conn->name()) == sessions_.end());
    sessions_[conn->name()] = session;
    // assert(sessions_.size() == stats_.current_conns);
  }
  else
  {
    MutexLockGuard lock(mutex_);
    assert(sessions_.find(conn->name()) != sessions_.end());
    sessions_.erase(conn->name());
    // assert(sessions_.size() == stats_.current_conns);
  }
}
