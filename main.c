#include <stdio.h>
#include <string.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdlib.h>
#include <signal.h>
#include "device_handler.h"

int
get_local_ip(char buffer[64])
{
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

int device_handler_status = 0;

static void
sig_handler(int signo)
{
    printf("Received signal %d, quitting..\n", signo);
    device_handler_stop();
}

int
main(int argc, char *argv[])
{
    struct scanner_data_t scanner_data = {
        .dest_ip = "10.0.0.149",
        .name = "open-source-bro",
        .options_num = 3,
        .options = {
            {.func = "IMAGE", .appnum = 1},
            {.func = "EMAIL", .appnum = 2},
            {.func = "FILE", .appnum = 5}
        }};

    unsigned char buf[128] = {};
    size_t ip_len;

    get_local_ip(buf);
    ip_len = strlen((char *) buf);

    if (ip_len < 1 || ip_len > 15) {
        fprintf(stderr, "Couldn't get valid local ip address\n");
        return -1;
    }

    memcpy(scanner_data.my_ip, buf, 16);

    device_handler_init();
    device_handler_add_device(&scanner_data);
    
    if (signal(SIGINT, sig_handler) == SIG_ERR)
        fprintf(stderr, "Failed to bind SIGINT handler.\n");

    device_handler_run(&device_handler_status);
    return device_handler_status == 1 ? 0 : -1;
}
