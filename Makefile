CC = gcc
CFLAGS += -std=gnu11 -pedantic -Wall -Wextra \
	-Werror -Wno-missing-braces -Wno-missing-field-initializers \
	-Wno-unused-variable -Wno-unused-parameter -Wformat=2 -Wswitch-default \
	-Wno-unused-label -Wno-unused-function -Wcast-align -Wpointer-arith -Wbad-function-cast \
	-Wstrict-overflow=5 -Wstrict-prototypes -Winline -Wundef -Wnested-externs \
	-Wcast-qual -Wshadow -Wunreachable-code -Wfloat-equal \
	-Wstrict-aliasing=2 -Wredundant-decls -Wold-style-definition \
	-Wno-format-nonliteral -Wno-deprecated-declarations -Wno-gnu-empty-initializer \

LDFLAGS = -pthread
SOURCES = main.c con_queue.c log.c device_handler.c event_thread.c config.c connection.c \
	data_channel.c snmp.c
SOURCES += ber/ber.c ber/snmp.c
OBJECTS = $(patsubst %.c, build/%.o, $(SOURCES))
DEPS := $(OBJECTS:.o=.d)
EXECUTABLE = build/brother-scand

all: $(SOURCES) $(EXECUTABLE)

-include $(DEPS)

$(EXECUTABLE): $(OBJECTS)
	$(CC) $(OBJECTS) -o $@ $(LDFLAGS)

build/%.o: %.c
	@mkdir -p $(@D)
	$(CC) -c -MM -MF $(patsubst %.o,%.d,$@) $<
	$(CC) $(CFLAGS) -c -o $@ $<

.PHONY: clean

clean:
	rm -f $(OBJECTS) $(DEPS) $(EXECUTABLE)
