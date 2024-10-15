# Global makefile to build images to be flashed
# or updated through OTAP

include makefile_common.mk

# Version of GCC used for Wirepas testing
GCC_TESTED_VERSION := 10.3.1

# Check the toolchain version with GCC
GCC_VERSION := $(shell $(CC) -dumpversion)
ifneq ($(GCC_VERSION), $(findstring $(GCC_VERSION), $(GCC_TESTED_VERSION)))
$(warning ***********************************************************************)
$(warning "GCC version used is not the recommended and tested by Wirepas )
$(warning "Recommended version is : $(GCC_TESTED_VERSION))
$(warning ***********************************************************************)
endif

#
# Targets
#

# By default the key file is positioned at root of SDK and used for all apps/boards
# But it can be overwritten by any app (it will be generated at first execution if needed)
KEY_FILE ?= ./custom_bootloader_keys.ini

PLATFORM_CONFIG_INI := $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/ini_files/$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_platform.ini

CLEAN += $(BUILD_VARIABLES_MK) $(BOOTLOADER_CONFIG_INI) $(SCRATCHPAD_CONFIG_INI)

# Default bootloader size is 32 kB, but some architectures support
# other sizes, e.g., 16 kB for legacy network installations
bootloader_size ?= 32k

# Build and embed Generic Bootloader Updater Tool
# in the stack binary, if requested
bl_updater ?= no
bl_updater_old_key_file ?= $(KEY_FILE)
bl_updater_match_app_area_id ?= no
ifneq ($(filter $(bl_updater),no ""),$(bl_updater))
# If Generic Bootloader Updater Tool is requested, the parameter bl_updater
# specifies the old bootloader size, e.g., "16k" or "32k"
#
# MODIFIED_STACK_HEX is the stack hex file with the bootloader updater embedded,
# and SCRATCHPAD_CONFIG_INI has the old bootloader area IDs and optionally old
# bootloader keys, set with the bl_updater_old_key_file command line option
OLD_BOOTLOADER_SIZE := $(bl_updater)
MODIFIED_STACK_HEX := $(STACK_WITH_BL_UPDATER_HEX)
SCRATCHPAD_CONFIG_INI := $(BL_UPDATER_OLD_CONFIG_INI)
SCRATCHPAD_BUILD_VARIABLES_MK := $(BL_UPDATER_BUILD_VARIABLES_MK)
CLEAN += $(STACK_WITH_BL_UPDATER_HEX)
else
# If Generic Bootloader Updater Tool is not requested, MODIFIED_STACK_HEX is
# just the stack binary as-is, and SCRATCHPAD_CONFIG_INI is the same as
# BOOTLOADER_CONFIG_INI
MODIFIED_STACK_HEX := $(STACK_HEX)
SCRATCHPAD_CONFIG_INI := $(BOOTLOADER_CONFIG_INI)
SCRATCHPAD_BUILD_VARIABLES_MK := $(BUILD_VARIABLES_MK)
endif

# Final image for programming
FINAL_IMAGE_NAME := final_image_$(APP_NAME)
FINAL_IMAGE_HEX := $(BUILDPREFIX_APP)$(FINAL_IMAGE_NAME).hex

# Hidden file to know if the License was at least displayed one time
# and accepted
LICENSE_ACCEPTED := .license_accepted

# Hidden file to enable the Generic Bootloader Updater Tool feature
BL_UPDATER_ENABLED := .bl_updater_enabled

CLEAN += $(FINAL_IMAGE_HEX)

# Add targets
TARGETS += $(FINAL_IMAGE_HEX)

#
# Manage area id and ini files
#

# Define the app_area as a combination of app_area and HW_VARIANT_ID
ifeq ($(app_specific_area_id),)
$(error You must define a specific area id in your application config.mk file)
endif
app_area_id=$(app_specific_area_id)$(HW_VARIANT_ID)

# Set Default scratchpad ini file if not overriden by app makefile
ifneq ($(INI_FILE),)
$(error You are overriding the default flash partitioning with INI_FILE variable. \
	You must now override INI_FILE_APP variable with a modified version of \
	$(MCU_PATH)$(MCU_FAMILY)/$(MCU)/ini_files/$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_app.ini)
endif

INI_FILE_WP ?= $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/ini_files/$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_wp.ini
INI_FILE_APP ?= $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/ini_files/$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_app.ini

#
# Functions
#

define SAVE_MAKE_PARAMS
    echo "unprotected=$(unprotected)" > $(1); \
    echo "INI_FILE_WP=$(INI_FILE_WP)" >> $(1); \
    echo "INI_FILE_APP=$(INI_FILE_APP)" >> $(1); \
    echo "KEY_FILE=$(KEY_FILE)" >> $(1); \
    echo "bootloader_size=$(bootloader_size)" >> $(1); \
    echo "bl_updater=$(bl_updater)" >> $(1); \
    echo "bl_updater_old_key_file=$(bl_updater_old_key_file)" >> $(1); \
    echo "bl_updater_match_app_area_id=$(bl_updater_match_app_area_id)" >> $(1); \
    echo "app_area_id=$(app_area_id)" >> $(1)
endef

define CHECK_MAKE_PARAMS
    if [ \! -f $(MAKE_PARAMS_FILE) ]; then \
        $(call SAVE_MAKE_PARAMS,$(MAKE_PARAMS_FILE)); \
    else \
        $(call SAVE_MAKE_PARAMS,$(MAKE_PARAMS_TEMP_FILE)); \
        if $(CMP) $(MAKE_PARAMS_TEMP_FILE) $(MAKE_PARAMS_FILE); then \
            $(RM) $(MAKE_PARAMS_TEMP_FILE); \
        else \
            $(RM) $(MAKE_PARAMS_TEMP_FILE); \
            echo -e "$(COLOR_ERROR)Make parameters changed, please run\n    make clean app_name=$(app_name) target_board=$(target_board)$(COLOR_END)"; \
	    false; \
	fi; \
    fi
endef

define BUILD_HEX
	@echo "$(COLOR_INFO)Creating Flashable Hex:$(COLOR_END) $(2) + $(3) + $(4) + $(5) -> $(1)"
	$(D)$(HEX_GEN) --configfile=$(BOOTLOADER_CONFIG_INI) \
	               --bootloader=$(2) \
	               $(1) \
	               $(STACK_CONF):$(stack_area_id):$(3) \
	               $(app_major).$(app_minor).$(app_maintenance).$(app_development):$(app_area_id):$(4) \
	               $(5)
endef

# Params: (1) Final image (2) bootloader (3) Ini file (4) Test app
define BUILD_BOOTLOADER_TEST_APP
	@echo "$(COLOR_INFO)Creating test application for bootloader:$(COLOR_END) $(2) + $(3) + $(4) -> $(1)"
	$(eval output_file:=$(BUILDPREFIX_TEST_BOOTLOADER)temp_file.hex)
	$(D)$(HEX_GEN) --configfile=$(3) \
	               --bootloader=$(2) \
	               $(1) \
	               1.0.0.0:$(stack_area_id):$(4)
endef

.PHONY: all
all: $(TARGETS) otap_all

.PHONY: app_only
app_only: $(APP_HEX) otap_app_only

.PHONY: bootloader
bootloader: $(BOOTLOADER_HEX)

.PHONY: need_board
need_board:
	@# Check if target board is defined
	$(if $(target_board),,$(error No target_board defined.\
	        Please specify one with target_board=<..> argument from command line.\
	        Available boards are: $(AVAILABLE_BOARDS)\
	        A default value can be set in main config.mk file))

	@# Check if board really exist
	@test -s $(BOARD_FOLDER)/config.mk || \
	        { echo "Specified target board $(target_board) doesn't exist. Available boards are: $(AVAILABLE_BOARDS)"; exit 1; }

.PHONY: check_make_params
check_make_params: | $(BUILDPREFIX_APP)
	@# Rebuild app if any make command line parameters changed
	$(DD)$(call CHECK_MAKE_PARAMS)

.PHONY: initial_setup
initial_setup: $(LICENSE_ACCEPTED) $(KEY_FILE) check_make_params $(BOOTLOADER_CONFIG_INI) $(BUILD_VARIABLES_MK)
	@# Rule to ensure initial setup is done

# Prevent initial setup steps happening in the wrong order
.NOTPARALLEL: initial_setup check_make_params

$(KEY_FILE): | $(LICENSE_ACCEPTED)
	@# Run the wizard to create key file
	@# It depends on LICENSE to avoid error when building with -j option
	@# | (pipe) is intentional to avoid regenerating key file if license is newer
	$(WIZARD) --gen_keys -o $@

$(LICENSE_ACCEPTED):
	@cat LICENSE.txt
	@echo -e "\n\n$(COLOR_INFO)This SDK is covered by the License outlined above, which you must read\n\
	and accept. For additional questions or clarifications, please contact \n\
	sales@wirepas.com.$(COLOR_END)\n"

	@echo -n -e "\nDo you accept the License Terms? [y/N] " && read ans && [ $${ans:-N} = y ]
	@touch $@

$(BL_UPDATER_ENABLED):
	@echo -e "$(COLOR_ERROR)Error: Before using the Generic Bootloader Updater Tool, contact Wirepas\n\
	       support. Using the tool without prior guidance can cause a network to\n\
	       become irrecoverably disabled.$(COLOR_END)"
	@false

# Add $(STACK_HEX) to PHONY to always call stack makefile
.PHONY: $(STACK_HEX)
$(STACK_HEX): initial_setup need_board
	@# Call app makefile to get the hex file of stack
	$(DD)$(MAKE) --no-print-directory -f makefile_stack.mk

# Add $(APP_HEX) to PHONY to always call app makefile
.PHONY: $(APP_HEX)
$(APP_HEX):: initial_setup need_board
	@echo "$(COLOR_INFO)Building $(app_name) for board $(target_board)$(COLOR_END)"
	$(DD)$(MAKE) --no-print-directory -f makefile_app.mk

# Add $(BOOTLOADER_HEX) to PHONY to always call bootloader makefile
.PHONY: $(BOOTLOADER_HEX)
$(BOOTLOADER_HEX): initial_setup need_board
	@echo "$(COLOR_INFO)Building bootloader$(COLOR_END)"
	$(DD)$(MAKE) --no-print-directory -f makefile_bootloader.mk

.PHONY: $(BOOTLOADER_TEST_HEX)
$(BOOTLOADER_TEST_HEX): initial_setup need_board
	@# Call bootloader test makefile to get the test application hex file
	$(DD)$(MAKE) --no-print-directory -f makefile_bootloader_test.mk

$(STACK_WITH_BL_UPDATER_HEX): $(BL_UPDATER_ENABLED) $(STACK_HEX) $(BOOTLOADER_HEX) $(APP_HEX) $(bl_updater_old_key_file)
	@# Call bootloader updater makefile to get the hex file of stack with bootloader updater embedded
	@echo "$(COLOR_INFO)Building Generic Bootloader Updater Tool$(COLOR_END)"
	$(DD)$(MAKE) \
	    --no-print-directory \
	    -f makefile_bl_updater.mk \
	    old_bootloader_size=$(OLD_BOOTLOADER_SIZE) \
	    ini_file_wp=$(INI_FILE_WP) \
	    ini_file_app=$(INI_FILE_APP) \
	    platform_config_ini=$(PLATFORM_CONFIG_INI) \
	    bl_updater_old_key_file=$(bl_updater_old_key_file) \
	    bl_updater_match_app_area_id=$(bl_updater_match_app_area_id) \
	    app_area_id=$(app_area_id)

$(BL_UPDATER_OLD_CONFIG_INI): $(STACK_WITH_BL_UPDATER_HEX)

$(BL_UPDATER_BUILD_VARIABLES_MK): $(STACK_WITH_BL_UPDATER_HEX)

.PHONY: otap_all
otap_all: $(MODIFIED_STACK_HEX) $(APP_HEX)

.PHONY: otap_app_only
otap_app_only: $(APP_HEX)

otap_all otap_app_only: initial_setup $(SCRATCHPAD_CONFIG_INI) $(SCRATCHPAD_BUILD_VARIABLES_MK)
	@# Call scratchpad makefile to generate the scratchpad (*.otap) files
	@echo "$(COLOR_INFO)Generating scratchpad files$(COLOR_END)"
	$(DD)$(MAKE) \
	    --no-print-directory \
	    -f makefile_scratchpad.mk \
	    stack_hex=$(MODIFIED_STACK_HEX) \
	    app_hex=$(APP_HEX) \
	    app_area_id=$(app_area_id) \
	    modemfw_area_id=$(modemfw_area_id) \
	    radio=$(radio) \
	    config_ini=$(SCRATCHPAD_CONFIG_INI) \
	    build_variables_mk=$(SCRATCHPAD_BUILD_VARIABLES_MK) \
	    bl_updater=$(bl_updater) \
	    $(patsubst otap_%,%,$@)

# Final image always uses STACK_HEX instead of MODIFIED_STACK_HEX, as the
# included bootloader is already up to date and will not have the Generic
# Bootloader Updater Tool embedded
$(FINAL_IMAGE_HEX): initial_setup $(STACK_HEX) $(APP_HEX) $(BOOTLOADER_HEX)
	$(call BUILD_HEX,$(FINAL_IMAGE_HEX),$(BOOTLOADER_HEX),$(STACK_HEX),$(APP_HEX),$(EXTRA_HEX))

# Generate full bootloader configuration and makefile
# fragment that depend on the selected configuration
$(BOOTLOADER_CONFIG_INI) $(BUILD_VARIABLES_MK): $(INI_FILE_WP) $(INI_FILE_APP) $(KEY_FILE) | $(BUILDPREFIX_APP)
	@# Rule to create the full config file based on multiple ini files and store it per build folder
	$(D)$(BOOT_CONF) \
	    -i $(INI_FILE_WP) \
	    -i $(INI_FILE_APP) \
	    -i $(KEY_FILE) \
	    -i $(PLATFORM_CONFIG_INI) \
	    -o $(BOOTLOADER_CONFIG_INI) \
	    -om $(BUILD_VARIABLES_MK) \
	    -ol APP_AREA_ID:$(app_area_id) \
	    -hm 0x$(HW_MAGIC) \
	    -hv 0x$(HW_VARIANT_ID) \
	    -fb $(FLASH_BASE_ADDR) \
	    -bs $(bootloader_size) \
	    -as $(BOOTLOADER_SIZES)

# Include a makefile fragment for build-time make
# variables, generated by the rule above
-include $(BUILD_VARIABLES_MK)

bootloader_test: $(BOOTLOADER_HEX) $(BOOTLOADER_TEST_HEX) $(BOOTLOADER_CONFIG_INI)
	$(call BUILD_BOOTLOADER_TEST_APP,$(BUILDPREFIX_APP)final_bootloader_test.hex,$<,$(BOOTLOADER_CONFIG_INI),$(word 2,$^))

$(BUILDPREFIX_APP):
	$(DD)$(MKDIR) $@

.PHONY: doxygen
doxygen:
	@# If build folder does not exist, create it
	$(DD)$(MKDIR) $(GLOBAL_BUILD)
	$(D)doxygen projects/doxygen/Doxyfile.template
	@# Replace search engine
	$(D)$(CP) -rf projects/doxygen/search.js $(GLOBAL_BUILD)html/search/search.js
	$(D)$(RM) $(GLOBAL_BUILD)html/search/search*.png
	$(D)echo "<script id=\"searchdata\" type=\"text/xmldata\">" >> $(GLOBAL_BUILD)html/search.html
	$(D)cat searchdata.xml >> $(GLOBAL_BUILD)html/search.html
	$(D)echo "</script>" >> $(GLOBAL_BUILD)html/search.html
	$(D)$(RM) searchdata.xml

# clean the specified app
.PHONY: clean
clean: need_board
	@echo "$(COLOR_INFO)Cleaning for $(app_name) and board $(target_board)$(COLOR_END)"
	$(D)$(RM) -rf $(BUILDPREFIX_APP)

# clean all the apps
.PHONY: clean_all
clean_all:
	@echo "$(COLOR_INFO)Cleaning everything$(COLOR_END)"
	$(D)$(RM) -rf $(GLOBAL_BUILD)
