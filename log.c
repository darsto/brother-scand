/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <ctype.h>
#include "log.h"

static char
to_printable(int n)
{
    static const char *trans_table = "0123456789abcdef";

    return trans_table[n & 0xf];
};

int
hexdump_line(const char *data, const char *data_start, const char *data_end)
{
    static char buf[256] = {};

    char *buf_ptr = buf;
    int relative_addr = (int) (data - data_start);
    int i, j;

    for (i = 0; i < 2; ++i) {
        buf_ptr[i] = ' ';
    }
    buf_ptr += i;

    for (i = 0; i < sizeof(void *); ++i) {
        buf_ptr[i] = to_printable(
            relative_addr >> (sizeof(void *) * 4 - 4 - i * 4));
    }
    buf_ptr += i;

    buf_ptr[0] = ':';
    buf_ptr[1] = ' ';
    buf_ptr += 2;

    for (j = 0; j < 8; ++j) {
        for (i = 0; i < 2; ++i) {
            if (data < data_end) {
                buf[10 + 5 * 8 + 4 + i + 2*j] = (char) (isprint(*data) ? *data : '.');

                buf_ptr[i * 2] = to_printable(*data >> 4);
                buf_ptr[i * 2 + 1] = to_printable(*data);
                
                ++data;
            } else {
                buf[10 + 5 * 8 + 4 + i + 2*j] = 0;

                buf_ptr[i * 2] = ' ';
                buf_ptr[i * 2 + 1] = ' ';
            }
        }

        buf_ptr[4] = ' ';
        buf_ptr += 5;
    }

    buf[10 + 5 * 8 + 2] = '|';
    buf[10 + 5 * 8 + 3] = ' ';

    printf("%s\n", buf);

    return i * j;
}

void
hexdump(const char *title, const char *data, int len)
{
    const char *data_start = data;
    const char *data_end = data + len;

    printf("%s = {\n", title);
    while (data < data_end) {
        data += hexdump_line(data, data_start, data_end);
    }
    printf("}\n");
}