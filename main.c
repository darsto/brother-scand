#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include "log.h"

struct scanner_data_t {
    char my_ip[16]; // just IPv4 for now
    char dest_ip[16];
    char name[16]; // TODO verify max size
    char options_num;
    struct scanner_data_option_t {
        char func[8];
        char appnum;
    } options[8];
};

static int
construct_option_string(unsigned char *buffer, size_t buf_len,
                        struct scanner_data_t *scanner_data, int option_id)
{
    static const unsigned char data_option_layout[] = {
        0x30, 0x77, 0x06, 0x0f, 0x2b, 0x06, 0x01, 0x04,
        0x01, 0x93, 0x03, 0x02, 0x03, 0x09, 0x02, 0x0b,
        0x01, 0x01, 0x00, 0x04,
        /* n+1 bytes - option details. pascal string.
         * first byte is length (of value n), the rest is actual data */
    };

    unsigned char *buf_cur = buffer;
    unsigned char *buf_str_len_pos;
    unsigned char *buf_end = buffer + buf_len;
    struct scanner_data_option_t *option = &scanner_data->options[option_id];

    buf_cur = memcpy(buf_cur, data_option_layout, 20) + 20;
    buf_str_len_pos = buf_cur;
    buf_cur += 1;

#define bufcat(args...) buf_cur += snprintf(buf_cur, buf_end - buf_cur, args)
    bufcat("TYPE=BR;");
    bufcat("BUTTON=SCAN;");
    bufcat("USER=\"%s\";", scanner_data->name);
    bufcat("FUNC=%s;", option->func);
    bufcat("HOST=%s:54925;", scanner_data->my_ip);
    bufcat("APPNUM=%d;", option->appnum);
    bufcat("DURATION=360;");
    bufcat("CC=1;");
    //BRID field - unimplemented
#undef bufcat

    *buf_str_len_pos = (unsigned char) (buf_cur - buf_str_len_pos + 1);
    return (int) (buf_cur - buffer);
}

unsigned char *
construct_init_message(unsigned char *buffer, size_t buf_len,
                       struct scanner_data_t *scanner_data)
{
    /**
     * the message consists of a single data_header_layout
     * and varying amount of subsequent data_option_layout
     */

    /* lines with nothing but a comment correspond dynamic in-between data */
    static const unsigned char data_header_layout[] = {
        [0] = 0x30, /* magic value, every data (sub-)structure start with it */
        [1] = 0x82, /* unique identifier of this message */
        /* 2 bytes - full packet length */
        [2] = 0x02, 0x01, 0x00, 0x04,
        /* n+1 bytes - message type. pascal string.
         * first byte is length (of value n), the rest is actual data
         * it seems to be either "internal" or "public" */
        [6] = 0xA3, 0x82, /* magic, beginning of some new section */
        /* 2 bytes - remaining packet len starting from prev magic (inclusive)*/
        [8] = 0x02, 0x02,
        /* 2 bytes - !! unknown !! might be related to the number of options */
        [10] = 0x02, 0x01, 0x00, 0x02, 0x01, 0x00,
        [16] = 0x30, 0x82, /* magic, beginning of a new section */
        /* 2 bytes - remaining packet len starting from prev magic (inclusive)*/
    };

    const char *msg_type = "internal";
    size_t msg_type_len = strlen(msg_type);
    unsigned char *buf_cur = buffer;
    unsigned char *buf_end = buffer + buf_len;
    int16_t tmp_16 = htons(0xE5); /* for now unknown variable from offset 10 */
    int i;

    /* we will increment the *buffer* ptr as we write to it,
     * but not all data can be written sequentially,
     * hence these pointers *dynamic_buffer_positions* */
    unsigned char *dyn_buf_pos[3];

    buf_cur = memcpy(buf_cur, data_header_layout, 2) + 2;
    dyn_buf_pos[0] = buf_cur;
    buf_cur += 2;
    buf_cur = memcpy(buf_cur, &data_header_layout[2], 4) + 4;
    *buf_cur = (unsigned char) msg_type_len;
    buf_cur += 1;
    buf_cur = memcpy(buf_cur, msg_type, msg_type_len) + msg_type_len;
    buf_cur = memcpy(buf_cur, &data_header_layout[6], 2) + 2;
    dyn_buf_pos[1] = buf_cur;
    buf_cur += 2;
    buf_cur = memcpy(buf_cur, &data_header_layout[8], 2) + 2;
    buf_cur = memcpy(buf_cur, &tmp_16, 2) + 2;
    buf_cur = memcpy(buf_cur, &data_header_layout[10], 8) + 8;
    dyn_buf_pos[2] = buf_cur;
    buf_cur += 2;

    for (i = 0; i < scanner_data->options_num; ++i) {
        buf_cur += construct_option_string(buf_cur, buf_end - buf_cur,
                                           scanner_data, i);
    }

    for (i = 0; i < sizeof(dyn_buf_pos) / sizeof(dyn_buf_pos[0]); ++i) {
        tmp_16 = htons((uint16_t) (buf_cur - dyn_buf_pos[i] + 2));
        /* +2 as we know that each dyn_buf_pos is
         * preceeded by magic value of length 2 */

        memcpy(dyn_buf_pos[i], &tmp_16, 2);
    }

    return buf_cur;
}

int get_local_ip(char buffer[64]) {
    struct sockaddr_in serv;
    int rc = -1;

    int sock = socket(AF_INET, SOCK_DGRAM, 0);
    if (sock < 0) {
        perror("socket");
        return -1;
    }

    memset(&serv, 0, sizeof(serv));
    serv.sin_family = AF_INET;
    serv.sin_addr.s_addr = inet_addr("8.8.8.8"); // google dns server
    serv.sin_port = htons(53);

    if (connect(sock, (const struct sockaddr *) &serv, sizeof(serv)) != 0) {
        perror("connect");
        goto out;
    }

    struct sockaddr_in name;
    socklen_t namelen = sizeof(name);
    if (getsockname(sock, (struct sockaddr *) &name, &namelen) != 0) {
        perror("getsockname");
        goto out;
    }

    const char *ret = inet_ntop(AF_INET, &name.sin_addr, buffer, 100);

    if (ret) {
        rc = 0;
    } else {
        perror("inet_ntop");
    }
out:
    close(sock);
    return rc;
}

int
main(int argc, char *argv[])
{
    unsigned char buf[2048] = {};
    struct scanner_data_t scanner_data = {
        .dest_ip = "10.0.0.149",
        .name = "open-source-bro",
        .options_num = 3,
        .options = {
            {.func = "IMAGE", .appnum = 1},
            {.func = "EMAIL", .appnum = 2},
            {.func = "FILE", .appnum = 5}
        }};

    unsigned char *ret;
    size_t ip_len;

    get_local_ip(buf);
    ip_len = strlen((char *) buf);

    if (ip_len < 1 || ip_len > 15) {
        fprintf(stderr, "Couldn't get valid local ip address\n");
        return -1;
    }

    memcpy(scanner_data.my_ip, buf, 16);

    ret = construct_init_message(buf, sizeof(buf), &scanner_data);
    
    hexdump("udp data", buf, ret - buf);

    write(1, buf, 2048);
    return ret == NULL;
}
