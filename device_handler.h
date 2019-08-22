/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_DEVICE_HANDLER_H
#define BROTHER_DEVICE_HANDLER_H

struct device_config;

void device_handler_init(void);
struct device *device_handler_add_device(struct device_config *config);

#endif //BROTHER_DEVICE_HANDLER_H
