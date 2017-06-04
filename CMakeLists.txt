cmake_minimum_required(VERSION 3.7)
project(brother)

set(CMAKE_C_STANDARD 11)
set(CMAKE_CXX_FLAGS_DEBUG "${CMAKE_CXX_FLAGS_DEBUG} -pedantic -Wall -Wextra \
    -Werror -Wno-missing-braces -Wno-missing-field-initializers \
    -Wno-unused-variable -Wno-unused-parameter -Wformat=2 -Wswitch-default \
    -Wswitch-enum -Wcast-align -Wpointer-arith -Wbad-function-cast \
    -Wstrict-overflow=5 -Wstrict-prototypes -Winline -Wundef -Wnested-externs \
    -Wcast-qual -Wshadow -Wunreachable-code -Wlogical-op -Wfloat-equal \
    -Wstrict-aliasing=2 -Wredundant-decls -Wold-style-definition")

set(SOURCE_FILES main.c concurrent_queue.h concurrent_queue.c log.h log.c device_handler.h device_handler.c ber.c ber.h)
add_executable(${PROJECT_NAME} ${SOURCE_FILES})

target_link_libraries(${PROJECT_NAME} pthread)
