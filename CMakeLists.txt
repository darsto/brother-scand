cmake_minimum_required(VERSION 3.10.2)
project(brother_scanner_driver C CXX)

set(CMAKE_C_STANDARD 11)

find_package(Threads)

include_directories(.)
add_library(ber ber/ber.c ber/ber.h ber/snmp.c ber/snmp.h)

add_library(scanner_driver
        con_queue.c
        con_queue.h
        config.c
        config.h
        connection.c
        connection.h
        data_channel.c
        data_channel.h
        event_thread.c
        event_thread.h
        log.c
        log.h)
target_link_libraries(scanner_driver ${CMAKE_THREAD_LIBS_INIT})
add_library(button_driver
        device_handler.c
        device_handler.h
        snmp.c
        snmp.h)
target_link_libraries(button_driver scanner_driver ber)

add_executable(brother_scanner_driver main.c)

add_executable(brother_scanner_cli scanner_cli.c)

target_link_libraries(brother_scanner_driver button_driver)
target_link_libraries(brother_scanner_cli scanner_driver)

add_subdirectory("/usr/src/googletest" ${CMAKE_BINARY_DIR}/gtest)
enable_testing()
include_directories(${GTEST_INCLUDE_DIRS})

add_executable(tests device_handler_test.cpp connection_test.cpp data_channel_test.cpp connection_base_test.h)
target_link_libraries(tests button_driver gtest gtest_main)
add_test(AllTests tests)
