include makefile_common.mk

.DEFAULT_GOAL := all


# Choose permissible app area IDs
ifeq ($(bl_updater_match_app_area_id),no)
# Match any app area ID
MATCH_APP_AREA_ID :=
else
ifeq ($(bl_updater_match_app_area_id),yes)
# Match only the app area ID of the application being built
MATCH_APP_AREA_ID := $(app_area_id)
else
# Match multiple app area IDs, including the application being built
MATCH_APP_AREA_ID := $(app_area_id),$(bl_updater_match_app_area_id)
endif
endif

# Include HAL driver code, needed to link bootloader updater
-include $(HAL_API_PATH)makefile
INCLUDES += -iquote$(API_PATH) -I$(UTIL_PATH)

stack_mode ?= normal
modemfw_name ?=

# The bootloader library, which includes the Generic Bootloader Updater Tool
BOOTLOADER_LIB := $(BUILDPREFIX_BOOTLOADER)bootloader.a

# Entry point, converted to HEX to determine the overlapping portion with stack
ENTRYPOINT_ASM := $(MCU_PATH)common/bl_updater_entrypoint.s
ENTRYPOINT_ELF := $(BUILDPREFIX_BL_UPDATER)$(ENTRYPOINT_ASM:.s=.elf)
ENTRYPOINT_HEX := $(BUILDPREFIX_BL_UPDATER)$(ENTRYPOINT_ASM:.s=.hex)

# Source files
SRCS += $(notdir $(BL_UPDATER_NEW_BL_DATA) $(BL_UPDATER_CONFIG) $(BL_UPDATER_STACK_BACKUP))
ASM_SRCS := $(ENTRYPOINT_ASM)

# Objects files
OBJS_ := $(SRCS:.c=.o) $(ASM_SRCS:.s=.o)
OBJS := $(addprefix $(BUILDPREFIX_BL_UPDATER), $(OBJS_))

# Linker script for the Generic Bootloader Updater Tool
ifndef MCU_RAM_VAR
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_bl_updater_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR).ld
else
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_bl_updater_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_$(MCU_RAM_VAR).ld
endif

# Compiled and linked bootloader updater
BL_UPDATER_ELF := $(BUILDPREFIX_BL_UPDATER)bl_updater.elf

# Files to be cleaned
CLEAN := $(STACK_WITH_BL_UPDATER_HEX) $(BL_UPDATER_ELF) $(BL_UPDATER_HEX)
CLEAN += $(OBJS) $(BL_UPDATER_BL_DATA) $(BL_UPDATER_CONFIG) $(BL_UPDATER_OLD_CONFIG_INI)
CLEAN += $(BL_UPDATER_STACK_BACKUP) $(ENTRYPOINT_HEX) $(BL_UPDATER_BUILD_VARIABLES_MK)

# Generate a hex file of the stack, with the bootloader updater embedded in it
$(STACK_WITH_BL_UPDATER_HEX): $(BL_UPDATER_STACK_HEX) $(BL_UPDATER_HEX) $(BL_UPDATER_OLD_CONFIG_INI)
	@echo "$(COLOR_INFO)Combining:$(COLOR_END) $@"
	$(D)$(BL_UPD_CONF) \
	    combine \
	    $@ \
	    $(BL_UPDATER_STACK_HEX) \
	    $(BL_UPDATER_HEX) \
	    --oldconfigfile=$(BL_UPDATER_OLD_CONFIG_INI)

$(BL_UPDATER_STACK_HEX): $(BL_UPDATER_BUILD_VARIABLES_MK)
	@echo "$(COLOR_INFO)Select Stack firmware for the Generic Bootloader Updater Tool$(COLOR_END)"
	$(DD)$(MKDIR) $(@D)
	$(D)$(FMW_SEL) \
	    --firmware_path=$(IMAGE_PATH) \
	    --firmware_type="wp_stack" \
	    --output_path=$(@D) \
	    --output_name=$(FIRMWARE_NAME)_for_bl_updater \
	    --mcu=$(MCU) \
	    --mcu_sub=$(MCU_SUB) \
	    --mcu_mem_var=$(MCU_MEM_VAR) \
	    --mac_profile=$(mac_profile) \
	    --mac_profileid=$(mac_profileid) \
	    --mode=$(stack_mode) \
	    --modem_fw=$(modemfw_name) \
	    --radio=$(radio) \
	    --radio_config=$(radio_config) \
	    --version=$(MIN_STACK_VERSION) \
	    --stack_area_addr=$(stack_area_addr)

# Link entrypoint, bootloader updater library, new bootloader and bootloader updater config
$(BL_UPDATER_ELF): $(OBJS) $(BOOTLOADER_LIB)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_LINK)Linking$(COLOR_END) $@"
	$(D)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ \
	          -Wl,-Map=$(BUILDPREFIX_BL_UPDATER)bl_updater.map \
	          -Wl,-T,$(LDSCRIPT) \
	          $(LIBS)

$(BL_UPDATER_HEX): $(BL_UPDATER_ELF)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Generating:$(COLOR_END) $@"
	$(D)$(OBJCOPY) $< -O ihex $@

# Link just the entrypoint, to determine the overlapping portion with stack
# _bl_updater_start is a dummy value here, but it is not needed
$(ENTRYPOINT_ELF): $(ENTRYPOINT_ELF:.elf=.o)
	$(DD)$(MKDIR) $(@D)
	$(D)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ \
	          -Wl,-Map=$(ENTRYPOINT_ELF:.elf=.map) \
	          -Wl,-T,$(LDSCRIPT) \
	          -Wl,--defsym,_bl_updater_start=0x0000

$(ENTRYPOINT_HEX): $(ENTRYPOINT_ELF)
	$(D)$(OBJCOPY) \
	    $< \
	    -O ihex \
	    $@

$(BL_UPDATER_NEW_BL_DATA): $(BOOTLOADER_HEX) $(BOOTLOADER_CONFIG_INI)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Converting:$(COLOR_END) $@"
	$(D)$(BL_UPD_CONF) \
	    bl_to_data \
	    $(BUILDPREFIX_BL_UPDATER) \
	    $< \
	    --configfile=$(BOOTLOADER_CONFIG_INI)

$(BL_UPDATER_CONFIG): $(BOOTLOADER_CONFIG_INI) $(BL_UPDATER_OLD_CONFIG_INI)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Converting:$(COLOR_END) $@"
	$(D)$(BL_UPD_CONF) \
	    config_to_data \
	    $(BUILDPREFIX_BL_UPDATER) \
	    $(BL_UPDATER_CMD_LIST) \
	    --configfile=$(BOOTLOADER_CONFIG_INI) \
	    --oldconfigfile=$(BL_UPDATER_OLD_CONFIG_INI) \
	    --match_app_area_id=$(MATCH_APP_AREA_ID)

$(BL_UPDATER_STACK_BACKUP): $(BL_UPDATER_STACK_HEX) $(ENTRYPOINT_HEX) $(BL_UPDATER_OLD_CONFIG_INI)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_INFO)Converting:$(COLOR_END) $@"
	$(D)$(BL_UPD_CONF) \
	    stack_to_data \
	    $(BUILDPREFIX_BL_UPDATER) \
	    $(BL_UPDATER_STACK_HEX) \
     	    $(ENTRYPOINT_HEX) \
	    --configfile=$(BOOTLOADER_CONFIG_INI) \
	    --oldconfigfile=$(BL_UPDATER_OLD_CONFIG_INI)

$(BUILDPREFIX_BL_UPDATER)%.o: %.c
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDPREFIX_BL_UPDATER)%.o: $(BUILDPREFIX_BL_UPDATER)%.c
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDPREFIX_BL_UPDATER)%.o: %.s
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

# Generate full bootloader configuration and makefile
# fragment that match the old bootloader configuration
$(BL_UPDATER_OLD_CONFIG_INI) $(BL_UPDATER_BUILD_VARIABLES_MK): $(ini_file_wp) $(ini_file_app) $(bl_updater_old_key_file)
	$(DD)$(MKDIR) $(@D)
	$(D)$(BOOT_CONF) \
	    -i $(ini_file_wp) \
	    -i $(ini_file_app) \
	    -i $(platform_config_ini) \
	    -i $(bl_updater_old_key_file) \
	    -o $(BL_UPDATER_OLD_CONFIG_INI) \
	    -om $(BL_UPDATER_BUILD_VARIABLES_MK) \
	    -ol APP_AREA_ID:$(app_area_id) \
	    -hm 0x$(HW_MAGIC) \
	    -hv 0x$(HW_VARIANT_ID) \
	    -fb $(FLASH_BASE_ADDR) \
	    -bs $(old_bootloader_size) \
	    -as $(BOOTLOADER_SIZES)

# Include a makefile fragment for build-time make
# variables, generated by the rule above
-include $(BL_UPDATER_BUILD_VARIABLES_MK)

.PHONY: all
all: $(STACK_WITH_BL_UPDATER_HEX)

clean:
	$(D)$(RM) -rf $(CLEAN)
