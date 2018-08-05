/*
 * Copyright (c) 2017 Dariusz Stojaczyk. All Rights Reserved.
 * The following source code is released under an MIT-style license,
 * that can be found in the LICENSE file.
 */

#include <stdio.h>
#include <stdlib.h>
#include <signal.h>
#include <getopt.h>
#include <string.h>

#include "device_handler.h"
#include "event_thread.h"
#include "log.h"

static void
sig_handler(int signo)
{
    printf("Received signal %d, quitting..\n", signo);
    event_thread_lib_shutdown();
}

static void
print_usage(void)
{
    printf("Usage: brother [-c path/to/config/file]\n");
}

static void
print_version(void)
{
    printf("Brother scanner driver. Build " __DATE__ " " __TIME__ "\n"
               "Copyright 2017 Dariusz Stojaczyk\n");
}

int
main(int argc, char *argv[])
{
    int option = 0;
    const char *config_path = "brother.config";

    while ((option = getopt(argc, argv,"c:h")) != -1) {
        switch (option) {
            case 'c':
                config_path = optarg;
                break;
            case 'h':
                print_version();
                exit(EXIT_SUCCESS);
            default: print_usage();
                exit(EXIT_FAILURE);
        }
    }

    event_thread_lib_init();

    if (signal(SIGINT, sig_handler) == SIG_ERR) {
        fprintf(stderr, "Failed to bind SIGINT handler.\n");
        return 1;
    }

    if (config_init(config_path) != 0) {
        fprintf(stderr, "Fatal: could not init config.\n");
        return -1;
    }

    device_handler_init(config_path);
    
    event_thread_lib_wait();
    return 0;
}
