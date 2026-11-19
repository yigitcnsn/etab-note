CC      := zig cc
TARGET  := arm-linux-musleabihf
CFLAGS  := -target $(TARGET) -mcpu=cortex_a7 -static -O2 -s -Wall -Wextra -Werror -Iinclude -MMD -MP
LDFLAGS := -lm

BUILD   := build
OBJ_DIR := $(BUILD)/obj

SRCS    := src/main.c \
           src/app.c \
           src/fb.c \
           src/render.c \
           src/storage.c \
           src/prefs.c \
           src/osk.c \
           src/settings.c \
           src/ui.c \
           src/input.c \
           src/mdns.c \
           src/drop.c

OBJS    := $(patsubst src/%.c,$(OBJ_DIR)/%.o,$(SRCS)) $(OBJ_DIR)/stb_impl.o
DEPS    := $(OBJS:.o=.d)

.PHONY: all clean deploy bootimg

all: $(BUILD)/noteos $(BUILD)/noteos-dhcp

$(BUILD)/noteos: $(OBJS)
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(OBJS) -o $@ $(LDFLAGS)

$(OBJ_DIR)/stb_impl.o: src/stb_impl.c third_party/stb_image.h
	@mkdir -p $(OBJ_DIR)
	$(CC) -target $(TARGET) -mcpu=cortex_a7 -static -O2 -Wno-unused-function -Iinclude -MMD -MP -c $< -o $@

$(OBJ_DIR)/%.o: src/%.c
	@mkdir -p $(OBJ_DIR)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/noteos-dhcp: daemons/dhcpc.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $< -o $@

deploy: $(BUILD)/noteos
	./scripts/deploy.sh

bootimg: all
	python3 scripts/build_bootimg.py

clean:
	rm -rf $(BUILD)

-include $(DEPS)
