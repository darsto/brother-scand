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

struct event_thread *event_thread_create(const char *name,
        void (*update_cb)(void *),
        void (*stop_cb)(void *), void *arg);
int event_thread_enqueue_event(struct event_thread *thread,
                               void (*callback)(void *, void *),
                               void *arg1, void *arg2);
int event_thread_pause(struct event_thread *thread);
int event_thread_kick(struct event_thread *thread);
int event_thread_stop(struct event_thread *thread);
struct event_thread *event_thread_self(void);

#endif //BROTHER_EVENT_THREAD_H
