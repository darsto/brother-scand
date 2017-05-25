/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include <signal.h>
#include "device_handler.h"
#include "concurrent_queue.h"
#include "log.h"

static int sockfd;
static struct concurrent_queue *events;
static bool running = true;
pthread_mutex_t mutex;
pthread_t tid;

typedef enum {
    EVENT_STOP,
    EVENT_ADD_DEV
} event_type;

struct event {
    event_type type;
};

struct event_add_dev {
    struct event event;
    struct scanner_data_t scanner_data;
};

static int
construct_option_string(unsigned char *buffer, size_t buf_len,
                        struct scanner_data_t *scanner_data, int option_id)
{
    static const unsigned char data_option_layout[] = {
        [0] = 0x30,
        /* 1 byte - option len starting from prev magic (inclusive) */
        [1] = 0x06, 0x0f, 0x2b, 0x06, 0x01, 0x04, 0x01, 0x93,
        [9] = 0x03, 0x02, 0x03, 0x09, 0x02, 0x0b, 0x01, 0x01,
        [17] = 0x00, 0x04,
        /* n+1 bytes - option details. pascal string.
         * first byte is length (of value n), the rest is actual data */
    };

    unsigned char *buf_cur = buffer;
    unsigned char *buf_len_pos;
    unsigned char *buf_str_len_pos;
    unsigned char *buf_end = buffer + buf_len;
    struct scanner_data_option_t *option = &scanner_data->options[option_id];

    buf_cur = memcpy(buf_cur, data_option_layout, 1) + 1;
    buf_len_pos = buf_cur;
    buf_cur += 1;
    buf_cur = memcpy(buf_cur, &data_option_layout[1], 18) + 18;
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

    *buf_str_len_pos = (unsigned char) (buf_cur - buf_str_len_pos - 1);
    *buf_len_pos = (unsigned char) (buf_cur - buf_len_pos - 1);

    return (int) (buf_cur - buffer);
}

ssize_t
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
        buf_cur += construct_option_string(
            buf_cur, buf_end - buf_cur,
            scanner_data, i);
    }

    for (i = 0; i < sizeof(dyn_buf_pos) / sizeof(dyn_buf_pos[0]); ++i) {
        tmp_16 = htons((uint16_t) (buf_cur - dyn_buf_pos[i] - 2));
        /* - 2 as we know that each dyn_buf_pos is
         * preceeded by magic value of length 2 */

        memcpy(dyn_buf_pos[i], &tmp_16, 2);
    }

    return buf_cur - buffer;
}

static void
register_dev(struct event_add_dev *event)
{
    unsigned char buf[2048];
    ssize_t sent_len;
    ssize_t recv_len;
    socklen_t slen = sizeof(struct sockaddr_in);
    struct sockaddr_in server;

    server.sin_addr.s_addr = inet_addr(event->scanner_data.dest_ip);
    server.sin_family = AF_INET;
    server.sin_port = htons(161);

    recv_len = construct_init_message(buf, sizeof(buf), &event->scanner_data);

    if (recv_len <= 0) {
        fprintf(stderr, "Could not construct welcome message.\n");
        return;
    }

    printf("Sending msg to %s:\n", event->scanner_data.dest_ip);
    hexdump("payload", buf, (size_t) recv_len);
    sent_len = sendto(sockfd, buf, recv_len, 0,
                      (struct sockaddr *) &server,
                      slen);
    if (sent_len < 0) {
        perror("sendto");
        return;
    }

    printf("Message sent. (%zd/%zd). Waiting for the reply...\n", recv_len,
           sent_len);

    recv_len = recvfrom(sockfd, buf, 2048, 0,
                        (struct sockaddr *) &server, &slen);
    if (recv_len < 0) {
        perror("recvfrom");
        return;
    }

    printf("Received reply from %s:\n", event->scanner_data.dest_ip);
    hexdump("payload", buf, (size_t) recv_len);

    if (sent_len != recv_len) {
        fprintf(stderr, "Sent/Received length differs. Should be equal.\n");
    }
}

static void
process_event(struct event *event)
{
    switch (event->type) {
        case EVENT_STOP:
            running = false;
            pthread_kill(tid, SIGUSR1);
            break;
        case EVENT_ADD_DEV:
            register_dev((struct event_add_dev *) event);
            break;
    }
}

static void
device_handler_destroy()
{
    struct event *event;

    while (con_queue_pop(events, (void **) &event) == 0) {
        free(event);
    }

    free(events);
    pthread_mutex_destroy(&mutex);
}

static void
enqueue_event(struct event *event)
{
    con_queue_push(events, event);
    pthread_mutex_unlock(&mutex);
}

static void
sig_handler(int signo)
{
}

void *
device_handler_thread_f(void *args)
{
    struct sockaddr_in sin;
    struct event *event;
    int rc = -1;
    int *status = args;

    if (signal(SIGUSR1, sig_handler) == SIG_ERR)
        fprintf(stderr, "Failed to bind SIGINT handler.\n");

    sockfd = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);
    if (sockfd == -1) {
        perror("socket");
        goto out;
    }

    sin.sin_family = AF_INET;
    sin.sin_addr.s_addr = htonl(INADDR_ANY);
    sin.sin_port = 49976;

    if (bind(sockfd, (struct sockaddr *) &sin, sizeof(sin)) == -1) {
        perror("bind");
        fprintf(stderr, "Could not start device handler.\n");
        goto cleanup;
    }

    printf("Device handler started successfully\n");

    while (running) {
        if (con_queue_pop(events, (void **) &event) == 0) {
            process_event(event);
            free(event);
        } else {
            pthread_mutex_lock(&mutex);
        }
    }

    device_handler_destroy();

    rc = 1;

cleanup:
    close(sockfd);
out:
    *status = rc;
    printf("Device handler exiting...\n");
    fflush(stdout);
    pthread_exit(NULL);
}

int
device_handler_init()
{
    pthread_mutex_init(&mutex, NULL);

    events = calloc(1, sizeof(*events) + 32 * sizeof(void *));
    events->size = 32;

    if (!events) {
        fprintf(stderr,
                "Fatal: calloc() failed, cannot start device handler.\n");
        return -1;
    }

    return 0;
}

void
device_handler_run(int *exit_status)
{
    pthread_create(&tid, NULL, device_handler_thread_f, exit_status);
    pthread_join(tid, NULL);
}

void
device_handler_add_device(struct scanner_data_t *scanner_data)
{
    struct event_add_dev *event = malloc(sizeof(*event));

    if (!event) {
        fprintf(stderr, "Fatal: malloc() failed, cannot add device.\n");
        return;
    }

    ((struct event *) event)->type = EVENT_ADD_DEV;
    event->scanner_data = *scanner_data;

    enqueue_event((struct event *) event);
}

void
device_handler_stop()
{
    struct event *event = malloc(sizeof(*event));

    if (!event) {
        fprintf(stderr,
                "Fatal: malloc() failed, cannot stop device handler.\n");
        return;
    }

    event->type = EVENT_STOP;
    enqueue_event(event);
    pthread_kill(tid, SIGUSR1);
}

