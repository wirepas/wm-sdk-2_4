include makefile_common.mk

# Include build-time make variables that depend on the selected configuration
include $(BUILD_VARIABLES_MK)

.DEFAULT_GOAL := all


stack_mode ?= normal
modemfw_name ?=

$(STACK_HEX): FORCE
	@echo "$(COLOR_INFO)Select Stack firmware from the image folder$(COLOR_END)"
	$(DD)$(MKDIR) $(@D)
	$(D)$(FMW_SEL) --firmware_path=$(IMAGE_PATH) \
	               --firmware_type="wp_stack" \
	               --output_path=$(@D) \
	               --output_name="wpc_stack" \
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

.PHONY: all
all: $(STACK_HEX)

clean:
	$(D)$(RM) -rf $(STACK_HEX)

# Special ruel to force other rule to run every time
FORCE:
