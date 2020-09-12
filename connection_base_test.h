#ifndef BROTHER_CONNECTION_BASE_TEST_H
#define BROTHER_CONNECTION_BASE_TEST_H

#include <gtest/gtest.h>
#include <thread>

extern "C" {
#include "connection.h"
#include "log.h"
}

#define ASSERT_C_OK(x)       \
  {                          \
    int res = x;             \
    if (res < 0) perror(#x); \
    ASSERT_GE(res, 0);       \
  }

#define BROTHER_CONNECTION_BASE_TEST_PORT 54921

struct ClientConnection {
 public:
  explicit ClientConnection(int server_fd)
      : server_thread([this, server_fd]() {
          struct sockaddr cli_addr;
          int one = 1;
          socklen_t clilen = sizeof(cli_addr);
          client_fd = accept(server_fd, &cli_addr, &clilen);
          ASSERT_C_OK(setsockopt(client_fd, SOL_SOCKET, SO_REUSEADDR, &one,
                                 sizeof(int)));
          LOG_INFO("accepted fd: %d, %x\n", client_fd, this);
        }) {}

  ~ClientConnection() {
    if (client_fd > 0) ::close(client_fd);
  }

  void write(std::string data) {
    ::write(client_fd, data.c_str(), data.size());
  }

  std::thread write_async(std::string data, int delay_us) {
    return std::thread([this, data, delay_us] {
      ASSERT_C_OK(get_client_fd());
      usleep(delay_us);
      write(data);
    });
  }

  std::string read() {
    char buf[1024];
    int len = ::read(client_fd, buf, sizeof(buf));
    return std::string(buf, len);
  }

  int get_client_fd() {
    if (server_thread.joinable()) server_thread.join();
    return client_fd;
  }

 private:
  std::thread server_thread;
  int client_fd;
};

struct ConnectionBaseTest : public ::testing::Test {
 protected:
  ConnectionBaseTest(int port) : port_(port), ::testing::Test() {}
  ConnectionBaseTest()
      : ConnectionBaseTest(BROTHER_CONNECTION_BASE_TEST_PORT) {}

  void SetUp() {
    start_server();
    conn_ = brother_conn_open(BROTHER_CONNECTION_TYPE_TCP, /*timeout_sec=*/1);
    ASSERT_TRUE(conn_);
  }

  void TearDown() {
    LOG_INFO("TearDown\n");
    brother_conn_close(conn_);
    close(server_);
  }

  void start_server() {
    int one = 1;
    server_ = socket(AF_INET, SOCK_STREAM, IPPROTO_IP);
    ASSERT_C_OK(server_);
    ASSERT_C_OK(
        setsockopt(server_, SOL_SOCKET, SO_REUSEADDR, &one, sizeof(int)));
    struct sockaddr_in serv_addr;
    memset(&serv_addr, 0, sizeof(serv_addr));
    serv_addr.sin_family = AF_INET;
    serv_addr.sin_addr.s_addr = INADDR_ANY;
    serv_addr.sin_port = htons(port_);
    ASSERT_C_OK(
        bind(server_, (struct sockaddr *)&serv_addr, sizeof(serv_addr)));
    ASSERT_C_OK(listen(server_, 1));
  }

  std::unique_ptr<ClientConnection> accept_client() {
    return std::make_unique<ClientConnection>(server_);
  }

  int port_;
  int server_ = -1;  // the listening server
  struct brother_conn *conn_;
};

#endif  // BROTHER_CONNECTION_BASE_TEST_H
