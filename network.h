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

int network_init_conn(enum network_type type, in_port_t local_port, in_addr_t dest_addr, in_port_t dest_port);
int network_reconnect(int conn_id);
int network_send(int conn_id, const void *buf, size_t len);
int network_receive(int conn_id, void *buf, size_t len);
int network_get_client_ip(int conn_id, char ip[16]);
int network_disconnect(int conn_id);

#endif //BROTHER_NETWORK_H
