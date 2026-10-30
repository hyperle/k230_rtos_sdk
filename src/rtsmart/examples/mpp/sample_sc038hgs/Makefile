include ../../mkenv.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/toolchain_riscv64_musl.mk
include $(SDK_RTSMART_SRC_DIR)/libs/mk/libmpp.mk

BUILD := $(SDK_RTSMART_BUILD_DIR)/examples/mpp/sample_sc038hgs
BIN := $(RTT_MPP_EXAMPLES_ELF_INSTALL_PATH)/sample_sc038hgs.elf
CFLAGS := -march=rv64imafdcv -mabi=lp64d -mcmodel=medany -Os -std=gnu99
CFLAGS += -Wall -Wextra -Werror -ffunction-sections -fdata-sections $(LIB_CFLAGS)
CFLAGS += -I$(SDK_SRC_ROOT_DIR)/include -include generated/autoconf.h
LDFLAGS := -T $(SDK_RTSMART_SRC_DIR)/libs/mk/link.lds --static -Wl,--gc-sections $(LIB_LDFLAGS)

.PHONY: all clean distclean
all: $(BIN)

$(BIN): $(BUILD)/main.o
	@mkdir -p $(dir $@)
	$(CXX) $^ $(LDFLAGS) -o $@
	$(STRIP) $@

$(BUILD)/main.o: main.c
	@mkdir -p $(dir $@)
	$(CC) $(CFLAGS) -MD -MP -c $< -o $@

clean:
	$(RM) $(BIN)

distclean: clean
	$(RM) $(BUILD)

-include $(BUILD)/main.d
