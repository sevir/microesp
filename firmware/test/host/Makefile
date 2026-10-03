# Host unit tests for the pure-C core of the MicroESP firmware (src/core).
#   make            -> build + run (ASan/UBSan)
#   make clean
# Dependencies are taken from the local TuyaOpen / ESP-IDF checkout (no system packages):
#   Unity (ESP-IDF components/unity), cJSON (TuyaOpen src/libcjson), mbedTLS subset (ESP-IDF).
TOS     ?= /www/MicroESP/tools/TuyaOpen
IDF     ?= $(TOS)/platform/ESP32/esp-idf
ROOT    := $(abspath ../..)
REPO    := $(abspath $(ROOT)/..)
UNITY   := $(IDF)/components/unity/unity/src
CJSON   := $(TOS)/src/libcjson/cJSON
MBED    := $(IDF)/components/mbedtls/mbedtls
BUILD   := build

CC      ?= gcc
CFLAGS  += -std=gnu11 -g -O1 -Wall -Wextra -Wno-unused-parameter -Werror=implicit-function-declaration \
           -fsanitize=address,undefined -fno-omit-frame-pointer \
           -I$(ROOT)/src/core -I. -I$(UNITY) -I$(CJSON) -I$(MBED)/include \
           -DMBEDTLS_CONFIG_FILE='"mbedtls_host_config.h"' \
           -DVECTORS_PATH='"$(REPO)/protocol/testdata/vectors.json"' \
           -DSCHEMA_PATH='"$(ROOT)/schema/dp.json"'
LDFLAGS += -fsanitize=address,undefined -lm

CORE    := $(wildcard $(ROOT)/src/core/*.c)
TESTS   := $(wildcard test_*.c)
DEPS    := $(UNITY)/unity.c $(CJSON)/cJSON.c \
           $(MBED)/library/md.c $(MBED)/library/sha256.c $(MBED)/library/hkdf.c $(MBED)/library/platform_util.c

all: run

$(BUILD)/test_all: $(TESTS) main.c $(CORE) $(DEPS) $(wildcard *.h) $(wildcard $(ROOT)/src/core/*.h)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) -o $@ main.c $(TESTS) $(CORE) $(DEPS) $(LDFLAGS)

run: $(BUILD)/test_all
	./$(BUILD)/test_all

clean:
	rm -rf $(BUILD)

.PHONY: all run clean
