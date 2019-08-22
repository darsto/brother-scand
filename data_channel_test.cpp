#include <gtest/gtest.h>
#include <sys/queue.h>
#include <unistd.h>
#include <initializer_list>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "connection_base_test.h"
#include "gtest/gtest.h"

using namespace std::literals::string_literals;

extern "C" {
#include "config.h"
#include "data_channel.h"
#include "log.h"
}

// Sleep 2ms to avoid packets being grouped into 1.
constexpr int SOCKET_MESSAGE_DELAY_US = 2000;

struct DataChannelTest : public ConnectionBaseTest {
 protected:
  DataChannelTest() : ConnectionBaseTest(DATA_CHANNEL_TARGET_PORT) {}

  void init_data_channel() {
    ASSERT_C_OK(config_init("../testdata/brother.config"));
    data_channel.config = TAILQ_FIRST(&g_config.devices);
    data_channel_set_item(&data_channel,
                          TAILQ_FIRST(&data_channel.config->items));
    ASSERT_C_OK(data_channel_init(&data_channel));
  }

  void init_connection() {
    client = accept_client();
    std::thread server_thread = client->write_async("+200\0OK"s, 0);
    data_channel_init_connection(&data_channel);
    server_thread.join();
  }

  void client_write(std::string data) {
    replay_messages.push_back(data);
    client->write(data);
  }

  std::thread client_write_async(std::string data, int delay_us) {
    replay_messages.push_back(data);
    return client->write_async(data, delay_us);
  }

  void exchange_params() {
    // Replay the initialization for Brother MFC-J430W
    EXPECT_EQ(client->read(), "\x1BK\n\x80"s);
    client_write("\x30\x14\000F=FILE\nD=SIN\nE=SHO\n\x80"s);
    ASSERT_C_OK(data_channel.process_cb(&data_channel));  // exchange_params1
    EXPECT_EQ(client->read(), "\x1bI\nD=SIN\nM=CGRAY\nR=300,300\n\x80"s);
    client_write("\x00\x16\000300,300,1,209,2480,0,0"s);
    ASSERT_C_OK(data_channel.process_cb(&data_channel));  // exchange_params2
    EXPECT_EQ(
        client->read(),
        "\x1BX\nA=0,0,2480,0\nB=50\nC=JPEG\nD=SIN\nG=1\nL=128\nM=CGRAY\nN=50\nR=300,300\n\x80"s);
  }

  void dump(std::string s) { DUMP_DEBUG(s.c_str(), s.size()); }

  struct data_channel data_channel = {};
  std::unique_ptr<ClientConnection> client;
  // Messages sent by the server (i.e. this test).
  std::vector<std::string> replay_messages;
};

TEST_F(DataChannelTest, ChunkedReceiveTest) {
  init_data_channel();
  init_connection();
  exchange_params();

  // Now send some RLE encoded payloads
  // RLE (0x42), Page 1 (0x01), Payload-Len (0x06)
  std::string header = "\x42\x07\x00\x01\x00\x84\x00\x00\x00\x00\x06\x00"s;
  std::string rle_payload = "\x81\x00\x81\x00\xcb\x00"s;  // 461 * "\x00"

  client_write(header + rle_payload);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // receive_initial_data
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_chunk_header
  // the header shoud be successfully processed.
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 6);
  // we wrote a complete chunk, so should be all good
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_page_payload
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);

  client_write(header);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));
  // we wrote the header only, so the data channel will be waiting for the
  // payload
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 6);
  client_write(rle_payload);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);

  client_write(header + rle_payload.substr(0, 1));
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_header
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 6);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_page_payload
  // we wrote the header + 1 payload byte, so the data channel will be waiting
  // for the rest of the payload
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 5);
  client_write(rle_payload.substr(1));
  ASSERT_C_OK(data_channel.process_cb(&data_channel));
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);

  // Make sure that if the header is split at various positions, everything
  // still works.
  auto header_partitions = {0, 1, 2, 6, 10, 11, 12};
  for (int partition : header_partitions) {
    if (partition == 0) continue;
    client_write(header.substr(0, partition));
    // write the remainder of the header with some delay, so the client has to
    // poll for it
    auto thread =
        client_write_async(header.substr(partition), SOCKET_MESSAGE_DELAY_US);
    ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_header
    thread.join();
    ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 6);
    client_write(rle_payload);
    ASSERT_C_OK(data_channel.process_cb(&data_channel));
    ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);
  }

  client_write(header);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));

  // Now do the same, but add the header to the respective payload string.
  for (int partition : header_partitions) {
    client_write(rle_payload + header.substr(0, partition));
    ASSERT_C_OK(
        data_channel.process_cb(&data_channel));  // process_chunk_header
    // the payload should have been completely received
    ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);
    // write the remainder of the header with some delay, so the client has to
    // poll for it
    auto thread =
        client_write_async(header.substr(partition), SOCKET_MESSAGE_DELAY_US);
    ASSERT_C_OK(data_channel.process_cb(&data_channel));
    thread.join();
    ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 6);
  }
  client_write(rle_payload);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));
  ASSERT_EQ(data_channel.page_data.remaining_chunk_bytes, 0);

  std::string page1_complete_packet =
      "\x82\x07\x00\x01\x00\x84\x00\x00\x00\x00"s;
  client_write(page1_complete_packet);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));

  // Empty data on page 2
  std::string page2_data_packet =
      "\x42\x07\x00\x02\x00\x84\x00\x00\x00\x00\x00\x00"s;
  client_write(page2_data_packet);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_header
  ASSERT_C_OK(data_channel.process_cb(&data_channel));  // process_page_payload

  std::string page2_complete_packet =
      "\x82\x07\x00\x02\x00\x84\x00\x00\x00\x00"s;
  client_write(page2_complete_packet);
  ASSERT_C_OK(data_channel.process_cb(&data_channel));

  client_write("\x80");  // scan process end marker
  ASSERT_C_OK(data_channel.process_cb(&data_channel));

  // This closes the existing connection.
  client = nullptr;

  // Now replay the same sequence for the standalone test.
  init_connection();
  std::thread main_loop([this] {
    while (data_channel.process_cb != data_channel_set_paused) {
      data_channel_loop(&data_channel);
    }
  });
  for (auto msg : replay_messages) {
    LOG_DEBUG("write: %d bytes\n", msg.size());
    client->write(msg);
    usleep(SOCKET_MESSAGE_DELAY_US);  // sleep a bit to make sure these are
                                      // separate packets
  }
  main_loop.join();
}
