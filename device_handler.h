/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef DCP_J105_DEVICE_HANDLER_H
#define DCP_J105_DEVICE_HANDLER_H

struct scanner_data_t {
    char my_ip[16]; // just IPv4 for now
    char dest_ip[16];
    char name[16];
    char options_num;
    struct scanner_data_option_t {
        char func[8];
        char appnum;
    } options[8];
};

int device_handler_init();
void device_handler_run(int *exit_status);
void device_handler_add_device(struct scanner_data_t *scanner_data);
void device_handler_stop();

#endif //DCP_J105_DEVICE_HANDLER_H
