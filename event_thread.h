/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#ifndef BROTHER_EVENT_THREAD_H
#define BROTHER_EVENT_THREAD_H

void event_thread_lib_init(void);
void event_thread_lib_wait(void);
void event_thread_lib_shutdown(void);

int event_thread_create(const char *name, void (*update_cb)(void *, void *),
                           void *arg1, void *arg2);
int event_thread_enqueue_event(int thread_id, void (*callback)(void *, void *),
                               void *arg1, void *arg2);
int event_thread_stop(int thread_id);

#endif //BROTHER_EVENT_THREAD_H
