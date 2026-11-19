CC      = zig cc
TARGET  = arm-linux-musleabihf
CFLAGS  = -target $(TARGET) -mcpu=cortex_a7 -static -O2 -s -Wall -Wextra
LDFLAGS = -lm

BUILD   = build
SRCS    = src/main.c src/fb.c src/storage.c src/settings.c

.PHONY: all clean push proto bootimg

all: $(BUILD)/noteos $(BUILD)/noteos-dhcp

$(BUILD)/noteos: $(SRCS) src/noteos.h
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) $(SRCS) -o $@ $(LDFLAGS)

$(BUILD)/noteos-dhcp: src/dhcpc.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) src/dhcpc.c -o $@

proto: $(BUILD)/proto

$(BUILD)/proto: src/proto.c
	@mkdir -p $(BUILD)
	$(CC) $(CFLAGS) src/proto.c -o $@ $(LDFLAGS)

push: $(BUILD)/noteos $(BUILD)/noteos-dhcp
	adb push $(BUILD)/noteos /data/noteos/noteos.new
	adb shell "su -c 'mkdir -p /data/noteos && mv /data/noteos/noteos.new /data/noteos/noteos && chmod +x /data/noteos/noteos'"

bootimg: all
	python3 scripts/build_bootimg.py

clean:
	rm -rf $(BUILD)
