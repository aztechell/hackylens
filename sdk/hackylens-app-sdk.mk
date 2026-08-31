ifndef HACKYLENS_SOURCE_DIR
$(error HACKYLENS_SOURCE_DIR must name the HackyLens source tree)
endif

HACKYLENS_APP_SDK_INCLUDE_FLAGS := \
  -I$(HACKYLENS_SOURCE_DIR)/sdk/include \
  -I$(HACKYLENS_SOURCE_DIR)/firmware/include

HACKYLENS_APP_HOST_FAKE_SOURCES := \
  $(HACKYLENS_SOURCE_DIR)/sdk/host/src/host_fake.c

HACKYLENS_APP_SDK_CFLAGS := -std=c11 -Wall -Wextra -Werror
