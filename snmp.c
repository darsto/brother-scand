/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdint.h>
#include <memory.h>
#include <stdio.h>
#include <stdatomic.h>
#include <stdbool.h>

#include "snmp.h"
#include "ber/snmp.h"
#include "log.h"
#include "config.h"
#include "connection.h"

#define SNMP_PORT 161

static atomic_int g_request_id;
static uint32_t g_brInfoPrinterUStatusOID[] =
{ 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 4, 2, 1, 5, 5, 6, 0, SNMP_MSG_OID_END };
static uint32_t g_brRegisterKeyInfoOID[] =
{ 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 2, 11, 1, 1, 0, SNMP_MSG_OID_END };
static uint32_t g_brUnregisterKeyInfoOID[] =
{ 1, 3, 6, 1, 4, 1, 2435, 2, 3, 9, 2, 11, 1, 2, 0, SNMP_MSG_OID_END };

static void
init_msg_header(struct snmp_msg_header *msg_header, const char *community,
                enum snmp_data_type type)
{
    msg_header->snmp_ver = 0;
    msg_header->community = community;
    msg_header->pdu_type = type;
    msg_header->request_id = atomic_fetch_add(&g_request_id, 1);
}

int
snmp_get_printer_status(struct brother_conn *conn, uint8_t *buf, size_t buf_len,
                        in_addr_t dest_addr)
{
    uint8_t *buf_end = buf + buf_len - 1;
    struct snmp_msg_header msg_header = {0};
    struct snmp_varbind varbind = {0};
    size_t snmp_len;
    uint8_t *out;
    int msg_len, rc = -1;
    uint32_t varbind_num = 1;

    init_msg_header(&msg_header, "public", SNMP_DATA_T_PDU_GET_REQUEST);
    memcpy(varbind.oid, g_brInfoPrinterUStatusOID,
           sizeof(g_brInfoPrinterUStatusOID));
    varbind.value_type = SNMP_DATA_T_NULL;

    out = snmp_encode_msg(buf_end, &msg_header, varbind_num, &varbind);
    snmp_len = buf_end - out + 1;

    msg_len = brother_conn_sendto(conn, out, snmp_len, dest_addr, htons(SNMP_PORT));
    if (msg_len < 0 || (size_t) msg_len != snmp_len) {
        perror("sendto");
        return -1;
    }

    rc = brother_conn_poll(conn, 3);
    if (rc <= 0) {
        LOG_ERR("Failed to receive SNMP status reponse.\n");
        return -1;
    }

    msg_len = brother_conn_receive(conn, buf, buf_len);
    if (msg_len < 6) {
        perror("recvfrom");
        return -1;
    }

    snmp_decode_msg(buf, msg_len, &msg_header, &varbind_num, &varbind);
    if (msg_header.error_index != 0 && msg_header.error_status != 0) {
        LOG_ERR("Received invalid printer status SNMP response\n");
        DUMP_ERR(buf, (size_t) msg_len);
        return -1;
    }

    rc = (int) varbind.value.i;
    return rc;
}

int snmp_register_scanner_driver(struct brother_conn *conn, bool enabled,
                                 uint8_t *buf, size_t buf_len,
                                 const char **functions,
                                 size_t functions_length, in_addr_t dest_addr) {
  uint8_t *buf_end = buf + buf_len - 1;

  struct snmp_msg_header msg_header = {0};
  struct snmp_varbind varbind[functions_length];
  size_t snmp_len;
  uint8_t *out;
  uint32_t i, varbind_num;
  int msg_len, rc = -1;

  init_msg_header(&msg_header, "internal", SNMP_DATA_T_PDU_SET_REQUEST);

  for (i = 0; i < functions_length; ++i) {
    if (functions[i] == NULL || functions[i][0] == 0) {
      break;
    }

    if (enabled) {
      memcpy(varbind[i].oid, g_brRegisterKeyInfoOID,
             sizeof(g_brRegisterKeyInfoOID));
    } else {
      memcpy(varbind[i].oid, g_brUnregisterKeyInfoOID,
             sizeof(g_brUnregisterKeyInfoOID));
    }

    varbind[i].value_type = SNMP_DATA_T_OCTET_STRING;
    varbind[i].value.s = functions[i];
  }

  varbind_num = i;
  out = snmp_encode_msg(buf_end, &msg_header, varbind_num, varbind);
  snmp_len = buf_end - out + 1;

  msg_len =
      brother_conn_sendto(conn, out, snmp_len, dest_addr, htons(SNMP_PORT));
  if (msg_len < 0 || (size_t)msg_len != snmp_len) {
    perror("sendto");
    return -1;
  }

  rc = brother_conn_poll(conn, 3);
  if (rc <= 0) {
    LOG_ERR("Failed to receive SNMP status reponse.\n");
    return -1;
  }

  msg_len = brother_conn_receive(conn, buf, buf_len);
  if (msg_len < 6) {
    perror("recvfrom");
    return -1;
  }

  if (!enabled) {
    /* unregister msg is not implemented for some scanners,
     * ignore all errors */
    return 0;
  }

  snmp_decode_msg(buf, msg_len, &msg_header, &varbind_num, varbind);
  if (msg_header.error_index != 0 && msg_header.error_status != 0) {
    LOG_ERR("Received invalid register SNMP response\n");
    DUMP_ERR(buf, (size_t)msg_len);
    return -1;
  }

  rc = msg_len;
  return rc;
}
