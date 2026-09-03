# Private host-compilation sources for production app runtime tests.
# This is not a public SDK target or host runtime framework.

HACKYLENS_APP_RUNTIME_HOST_SOURCES := \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/app_runtime/runtime.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/app_runtime/surface.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/app_runtime/switch.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/capabilities/capability_core.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/capabilities/time.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/capabilities/input.c \
  $(HACKYLENS_SOURCE_DIR)/firmware/src/capabilities/input_state.c \
  $(HACKYLENS_SOURCE_DIR)/tests/time_normative_fake_backend.c \
  $(HACKYLENS_SOURCE_DIR)/tests/input_normative_fake_backend.c \
  $(HACKYLENS_SOURCE_DIR)/tests/capability_fake_display.c \
  $(HACKYLENS_SOURCE_DIR)/tests/app_runtime_host_support.c

HACKYLENS_APP_RUNTIME_HOST_INCLUDE_FLAGS := \
  -I$(HACKYLENS_SOURCE_DIR)/firmware/src \
  -I$(HACKYLENS_SOURCE_DIR)/firmware/src/capabilities \
  -I$(HACKYLENS_SOURCE_DIR)/tests
