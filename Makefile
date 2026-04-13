# CONFIDENTIAL - TRADE SECRET - Property of Joseph M. Castillo - All rights reserved
# metal Desktop Edition — Makefile
# Builds: metal-desktop.exe (Windows) / metal-desktop (Linux)

CC      = gcc
CFLAGS  = -Wall -Wextra -O2 -Isdk -Isrc
LDFLAGS =

SRC     = src/main.c src/serve.c src/relay_client.c src/config.c src/nous_userkeys.c
SDK_SRC = $(wildcard sdk/*.c)
ALL_SRC = $(SRC) $(SDK_SRC)

ifeq ($(OS),Windows_NT)
	TARGET  = metal-desktop.exe
	LDFLAGS += -lws2_32 -lwinhttp -lbcrypt
else
	TARGET  = metal-desktop
	LDFLAGS += -lpthread
endif

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(ALL_SRC)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

clean:
ifeq ($(OS),Windows_NT)
	del /Q $(TARGET) 2>NUL
else
	rm -f $(TARGET)
endif
