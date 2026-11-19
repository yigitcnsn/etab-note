CC      = zig cc
TARGET  = arm-linux-musleabihf
CFLAGS  = -target $(TARGET) -mcpu=cortex_a7 -static -O2 -Wall -Wextra
LDFLAGS = -lm

BUILD   = build
SRCS    = src/main.c src/fb.c src/storage.c src/settings.c

.PHONY: all clean push proto

all: $(BUILD)/noteos

$(BUILD)/noteos: $(SRCS) src/noteos.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

proto: $(BUILD)/proto

$(BUILD)/proto: src/proto.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) src/proto.c -o $@ $(LDFLAGS)

push: $(BUILD)/noteos
	adb push $(BUILD)/noteos /data/local/tmp/noteos
	adb shell "chmod +x /data/local/tmp/noteos"

clean:
	rm -rf $(BUILD)
