include makefile_common.mk

# Include build-time make variables that depend on the selected configuration
include $(BUILD_VARIABLES_MK)

.DEFAULT_GOAL := all

unprotected ?= false

# Get the right bootloader binary

BOOTLOADER_SRC :=  $(BUILDPREFIX_BOOTLOADER)bootloader.a

# Bootloader build variables
BL_BUILDPREFIX := $(BUILDPREFIX_BOOTLOADER)

# Linker for the bootloader
ifndef MCU_RAM_VAR
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_bl_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR).ld
else
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_bl_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_$(MCU_RAM_VAR).ld
endif


BOOTLOADER_ELF := $(BL_BUILDPREFIX)bootloader.elf

# Include bootloader makefile
-include bootloader/makefile

# Include board part (for BOARD_HW_xx defines)
-include board/makefile

# Include HAL drivers code (needed to build power.c (DCDC))
-include $(HAL_API_PATH)makefile
INCLUDES += -iquote$(API_PATH) -I$(UTIL_PATH)

OBJS_ = $(SRCS:.c=.o)
OBJS = $(addprefix $(BL_BUILDPREFIX), $(OBJS_))

# Files to be cleaned
CLEAN := $(OBJS) $(BOOTLOADER_ELF) $(BOOTLOADER_HEX)

$(BOOTLOADER_SRC): FORCE
	@echo "$(COLOR_INFO)Select bootloader from the image folder$(COLOR_END)"
	$(DD)$(MKDIR) $(@D)
	$(eval key_type=$(shell $(BOOT_CONF) --in_file $(BOOTLOADER_CONFIG_INI) --get_key_type))
	$(DD)$(FMW_SEL) --firmware_path=$(IMAGE_PATH) \
	                --firmware_type="wp_bootloader" \
	                --version=$(MIN_BOOTLOADER_VERSION) \
	                --output_path=$(@D) \
	                --output_name="bootloader" \
	                --key_type=$(key_type) \
	                --unlocked=$(unprotected) \
	                --mcu=$(MCU) \
	                --mcu_sub=$(MCU_SUB) \
	                --mcu_mem_var=$(MCU_MEM_VAR)

$(BL_BUILDPREFIX)%.o : %.c
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(DD)$(MKDIR) $(@D)
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@


$(BOOTLOADER_ELF): $(OBJS) $(BOOTLOADER_SRC)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Linking with the provided lib:$(COLOR_END) $(BOOTLOADER_SRC)"
	$(D)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(BOOTLOADER_SRC) \
	          -Wl,--defsym,__bl_length__=$(bootloader_area_length) \
	          -Wl,-Map=$(BL_BUILDPREFIX)bootloader.map \
	          -Wl,-T,$(LDSCRIPT) $(LIBS)

$(BOOTLOADER_HEX): $(BOOTLOADER_ELF)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Generating:$(COLOR_END) $@"
	$(D)$(OBJCOPY) $(BOOTLOADER_ELF) -O ihex $@

.PHONY: all
all: $(BOOTLOADER_HEX)

clean:
	$(D)$(RM) -rf $(CLEAN)

# Special ruel to force other rule to run every time
FORCE:
