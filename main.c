/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include "device_handler.h"
#include "event_thread.h"

static void
sig_handler(int signo)
{
    printf("Received signal %d, quitting..\n", signo);
    event_thread_lib_shutdown();
    exit(signo);
}

int
main(int argc, char *argv[])
{
    event_thread_lib_init();
    
    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        fprintf(stderr, "Failed to bind SIGINT handler.\n");
        sig_handler(-1);
    }
    
    device_handler_init();
    
    event_thread_lib_wait();
    return 0;
}
