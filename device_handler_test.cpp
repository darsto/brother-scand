#include <gtest/gtest.h>

#include "gtest/gtest.h"

extern "C" {
#include "device_handler.h"

char *device_handler_extract_hostname(const char *buf);
}

struct DeviceHandlerTest : public ::testing::Test {};

TEST_F(DeviceHandlerTest, FindHostname) {
  EXPECT_EQ(device_handler_extract_hostname("foobar"), nullptr);
  EXPECT_EQ(device_handler_extract_hostname("abc;USER=\"foobar"), nullptr);
  EXPECT_STREQ(device_handler_extract_hostname("abc;USER=\"foobar\";abc"),
               "foobar");
  const char *buf =
      "TYPE=BR;BUTTON=SCAN;USER=\"mymachine\";FUNC=FILE;HOST=192.168.1.2:54925;"
      "APPNUM=1;P1=0;P2=0;P3=0;P4=0;REGID=27164;SEQ=22;";
  EXPECT_STREQ(device_handler_extract_hostname(buf), "mymachine");
}
