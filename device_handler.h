/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_DEVICE_HANDLER_H
#define BROTHER_DEVICE_HANDLER_H

#include "config.h"

void device_handler_init(const char *config_path);
struct device *device_handler_add_device(struct device_config *config);

extern uint16_t button_handler_port;

#endif //BROTHER_DEVICE_HANDLER_H
