// Copyright (c) YugaByte, Inc.

#ifndef YB_REDISSERVER_REDIS_SERVER_H_
#define YB_REDISSERVER_REDIS_SERVER_H_

#include <string>

#include "yb/gutil/gscoped_ptr.h"
#include "yb/gutil/macros.h"
#include "yb/redisserver/redis_server_options.h"
#include "yb/server/server_base.h"
#include "yb/tserver/tablet_server.h"
#include "yb/util/status.h"

namespace yb {

namespace redisserver {

class RedisServer : public server::RpcAndWebServerBase {
 public:
  static const uint16_t kDefaultPort = 6379;
  static const uint16_t kDefaultWebPort = 11000;

  explicit RedisServer(const RedisServerOptions& opts, const tserver::TabletServer* tserver);

  CHECKED_STATUS Start();

  using server::RpcAndWebServerBase::Shutdown;

  const tserver::TabletServer* tserver() const { return tserver_; }

 private:
  RedisServerOptions opts_;
  const tserver::TabletServer* const tserver_;

  DISALLOW_COPY_AND_ASSIGN(RedisServer);
};

}  // namespace redisserver
}  // namespace yb
#endif  // YB_REDISSERVER_REDIS_SERVER_H_
