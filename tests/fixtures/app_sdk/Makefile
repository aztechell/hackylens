include $(HACKYLENS_SOURCE_DIR)/sdk/hackylens-app-sdk.mk

CC ?= cc
BUILD_DIR ?= build-make
TARGET := $(BUILD_DIR)/app_sdk_fixture
SOURCES := minimal_app.c minimal_app_test.c $(HACKYLENS_APP_HOST_FAKE_SOURCES)

.PHONY: all clean

all: $(TARGET)

$(TARGET): $(SOURCES) minimal_private.h
	@cmake -E make_directory $(BUILD_DIR)
	$(CC) $(HACKYLENS_APP_SDK_CFLAGS) $(HACKYLENS_APP_SDK_INCLUDE_FLAGS) \
	  -I. $(SOURCES) -o $(TARGET)

clean:
	@cmake -E rm -rf $(BUILD_DIR)
