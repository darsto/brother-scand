#include <gtest/gtest.h>
#include <netinet/in.h>
#include <unistd.h>
#include <memory>
#include <string>
#include <thread>

#include "connection_base_test.h"
#include "gtest/gtest.h"
extern "C" {
#include "connection.h"
}

struct ConnectionTest : public ConnectionBaseTest {};

TEST_F(ConnectionTest, TestChunkParsing) {
  std::unique_ptr<ClientConnection> client = accept_client();
  ASSERT_C_OK(brother_conn_reconnect((struct brother_conn *)conn_,
                                     htonl(INADDR_LOOPBACK),
                                     htons(BROTHER_CONNECTION_BASE_TEST_PORT)));
  ASSERT_C_OK(client->get_client_fd());
  EXPECT_EQ(brother_conn_data_available(conn_), 0);
  client->write("ABCD");
  EXPECT_EQ(brother_conn_data_available(conn_), 0);
  ASSERT_C_OK(brother_conn_fill_buffer(conn_, /*size=*/1, /*timeout=*/1));
  EXPECT_EQ(brother_conn_data_available(conn_), 4);
  EXPECT_EQ(std::string((char *)brother_conn_peek(conn_, 2), 2), "AB");
  EXPECT_EQ(std::string((char *)brother_conn_read(conn_, 2), 2), "AB");
  client->write("EFGH");
  EXPECT_EQ(std::string((char *)brother_conn_peek(conn_, 4), 4), "CDEF");
  EXPECT_EQ(std::string((char *)brother_conn_read(conn_, 4), 4), "CDEF");
  bool thread_ran = false;
  std::thread receive([this, &thread_ran]() {
    EXPECT_EQ(std::string((char *)brother_conn_read(conn_, 4), 4), "GHIJ");
    thread_ran = true;
  });
  usleep(2000);
  client->write("IJKL");
  receive.join();
  EXPECT_TRUE(thread_ran);
}
