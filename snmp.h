/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_SNMP_H
#define BROTHER_SNMP_H

#include <netinet/in.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>

struct brother_conn;

int snmp_get_printer_status(struct brother_conn *conn,
                            uint8_t *buf, size_t buf_len, in_addr_t dest_addr);
int snmp_register_scanner_driver(struct brother_conn *conn, bool enabled,
                                 const char **functions,
                                 size_t functions_length, in_addr_t dest_addr);

#endif //BROTHER_SNMP_H
