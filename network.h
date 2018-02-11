/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <netinet/in.h>

#ifndef BROTHER_NETWORK_H
#define BROTHER_NETWORK_H

enum network_type {
    NETWORK_TYPE_UDP,
    NETWORK_TYPE_TCP,
};

struct network_conn;

struct network_conn *network_open(enum network_type type, unsigned timeout_sec);
int network_bind(struct network_conn *conn, in_port_t local_port);
int network_reconnect(struct network_conn *conn, in_addr_t dest_addr, in_port_t dest_port);
int network_send(struct network_conn *conn, const void *buf, size_t len);
int network_receive(struct network_conn *conn, void *buf, size_t len);
int network_get_client_ip(struct network_conn *conn, char ip[16]);
int network_close(struct network_conn *conn);

#endif //BROTHER_NETWORK_H
