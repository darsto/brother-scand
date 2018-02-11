/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT license.
 * Feel free to reuse, modify and distribute it.
 */

#ifndef BROTHER_CONQUEUE_H
#define BROTHER_CONQUEUE_H

#include <stdatomic.h>
#include <stddef.h>

struct con_queue {
    atomic_size_t head, tail;
    size_t size;
    void *data[];
};

int con_queue_push(struct con_queue *queue, void *element);
int con_queue_pop(struct con_queue *queue, void **element);

#endif //BROTHER_CONQUEUE_H
