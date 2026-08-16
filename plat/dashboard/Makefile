CC ?= cc
CFLAGS ?= -O2 -std=c11 -Wall -Wextra -Werror

ROOT := ../..
BUILD := build
FEED := $(BUILD)/telemetry_feed
SDK_SRCS := $(ROOT)/sdk/src/hyp_cordic_ref.c $(ROOT)/sdk/src/hyp_thejas.c \
            $(ROOT)/sdk/src/hyp_esp32.c $(ROOT)/sdk/src/hyp_router.c

.PHONY: build run clean

build: $(FEED)

$(FEED): telemetry_feed.c $(SDK_SRCS) $(ROOT)/sdk/include/hyprccel.h

	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -I$(ROOT)/sdk/include -I$(ROOT)/sdk/src $< $(SDK_SRCS) -lm -o $@

run: build
	node server.js

clean:
	rm -rf $(BUILD)
