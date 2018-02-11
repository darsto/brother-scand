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

int network_open(enum network_type type, unsigned timeout_sec);
int network_bind(int conn_id, in_port_t local_port);
int network_reconnect(int conn_id, in_addr_t dest_addr, in_port_t dest_port);
int network_send(int conn_id, const void *buf, size_t len);
int network_receive(int conn_id, void *buf, size_t len);
int network_get_client_ip(int conn_id, char ip[16]);
int network_close(int conn_id);

#endif //BROTHER_NETWORK_H
