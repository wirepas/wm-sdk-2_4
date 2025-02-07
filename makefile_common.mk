include makefile_color.mk

# Minimum binaries version required by this SDK version
MIN_BOOTLOADER_VERSION := 7
MIN_STACK_VERSION := 5.4.0.0

# SDK itself
SDK_PATH := .
INCLUDES := -I$(SDK_PATH)

build_directory?= build/
board_directory?= board/

# Manage verbosity
# Command are prefixed by @(D) or @(DD)
# By default all is silent
D=@
DD=@

ifeq ($(V),2)
# Explicitly print everything
D=
DD=
else ifeq ($(V), 1)
# Explicitly print level 1
D=
endif

# General SDK folder structure
API_PATH := api/
UTIL_PATH := util/
HAL_API_PATH := mcu/hal_api/
WP_LIB_PATH := libraries/
GLOBAL_BUILD := $(build_directory)
BOARDS_PATH := $(board_directory)
BOARDS_PATH_INTERNAL := board_internal/
MCU_PATH := mcu/

# General compiler flags (Define it before specific makefile in order to allow app to overwrite it)
CFLAGS  := -Wall -Werror -Wextra
CFLAGS  += -std=gnu99 -mthumb -nostartfiles -lgcc -lnosys -ggdb --specs=nano.specs
CFLAGS  += -Os -ffunction-sections -fdata-sections

# Flags for linker
LDFLAGS := -Wl,--gc-sections -Wl,-L$(MCU_PATH)common/linker

# include global config file
-include config.mk

# Check that a correct version of python is installed by trying to launch check_python
# This script has python3 shebang so try it without specifying interpreter
PYTHON_STATUS := $(shell tools/check_python.py > /dev/null 2>&1; echo $$?)
ifneq ($(PYTHON_STATUS),0)
ifeq ($(python_interpreter),)
python=python
else
python=$(python_interpreter)
endif
# It looks like python3 cannot be found or does not exist as a cmd (windows)
# Force the launch with python cmd
PYTHON_STATUS := $(shell $(python) tools/check_python.py > /dev/null; echo $$?)
ifneq ($(PYTHON_STATUS),0)
$(error Cannot find a suitable python version. You can force the python interpreter from config.mk)
endif
# Display a message if python version is 2.
VERSION := $(shell $(python) tools/check_python.py)
ifeq ($(VERSION),2)
$(warning ***********************************************************************)
$(warning "SDK supports python3 and python2 but uses python3 by default.)
$(warning "It looks like python3 is not installed on your system.)
$(warning "Using the python2 fallback for now but python2 support will be removed in a future release.)
$(warning ***********************************************************************)
endif
endif

#
# Tools
#
# Prefix for Arm tools
PREFIX := $(arm_toolchain)arm-none-eabi-

# Toolchain programs
CC          := $(PREFIX)gcc
AR          := $(PREFIX)ar
OBJCOPY     := $(PREFIX)objcopy
RM          := rm
MV          := mv
CP          := cp
MKDIR       := mkdir -p
CMP         := cmp -s
SCRAT_GEN   := $(python) tools/genscratchpad.py
HEX_GEN     := $(python) tools/genhex.py
HEXTOOL     := $(python) tools/hextool.py
FMW_SEL     := $(python) tools/firmware_selector.py
BOOT_CONF   := $(python) tools/bootloader_config.py
BL_UPD_CONF := $(python) tools/bl_updater_config.py
WIZARD      := $(python) tools/sdk_wizard.py
HEX2ARRAY32 := $(python) tools/hextoarray32.py
MAKE        := make

# List of available boards found under board/
AVAILABLE_BOARDS := $(patsubst $(BOARDS_PATH)%/,%,$(sort $(dir $(wildcard $(BOARDS_PATH)*/.))))

# Generic name of stack
FIRMWARE_NAME := wpc_stack

ifeq (,$(filter clean_all,$(MAKECMDGOALS)))
ifeq ($(target_board),)
$(error No board defined, please use target_board=... on your command line. Available boards are: $(AVAILABLE_BOARDS))
endif
endif

BOARD_FOLDER := $(BOARDS_PATH)$(target_board)

ifeq (,$(wildcard $(BOARD_FOLDER)))
$(error Board $(target_board) doesn't exist. Available boards are: $(AVAILABLE_BOARDS))
endif

# Board config file
BOARD_CONFIG := $(BOARD_FOLDER)/config.mk

# Include board specific config
-include $(BOARD_CONFIG)

# Include makefile for mcu family
-include $(MCU_PATH)$(MCU_FAMILY)/makefile

# Folder for Wirepas stack binary image
IMAGE_PATH := image/

# Add new flags as board and mcu are known
CFLAGS += -DTARGET_BOARD=$(target_board)
CFLAGS += -DMCU=$(MCU)
CFLAGS += -DMCU_SUB=$(MCU_SUB)

MCU_UPPER=$(shell echo $(MCU) | tr a-z A-Z)
CFLAGS += -D$(MCU_UPPER)

CFLAGS += -march=$(ARCH)

INCLUDES += -I$(MCU_PATH)common/cmsis -I$(BOARD_FOLDER)

# Folder where the application sources are located (and config file)
# Can be in different folders, try them one by one
APP_POSSIBLE_FOLDER := source/*/$(app_name)/ source/$(app_name)/
APP_POSSIBLE_FOLDER += $(app_extra_folder)/$(app_name)/

APP_SRCS_PATH := $(wildcard $(APP_POSSIBLE_FOLDER))
ifeq (,$(wildcard $(APP_SRCS_PATH)))
$(error App $(app_name) doesn't exist)
endif

# Check if an alternative config is given
ifeq ($(app_config),)
APP_CONFIG_FILE = config.mk
APP_NAME := $(app_name)
else
$(info Using alternate app config: $(app_config))
APP_CONFIG_FILE = $(app_config).mk
# Modify app_name for build folder
APP_NAME := $(app_name)_$(app_config)
endif

APP_CONFIG = $(APP_SRCS_PATH)$(APP_CONFIG_FILE)
ifeq (,$(wildcard $(APP_CONFIG)))
$(error Config file $(APP_CONFIG) doesn't exist)
endif

# Include app specific config
include $(APP_CONFIG)

# Build prefixes
BUILDPREFIX := $(GLOBAL_BUILD)$(target_board)/
BUILDPREFIX_APP := $(BUILDPREFIX)$(APP_NAME)/
# Stack is under an app specific folder as config may depend on app
BUILDPREFIX_STACK := $(BUILDPREFIX_APP)stack/
# Bootloader is under an app specific folder as config may depend on app (unlocked/locked)
BUILDPREFIX_BOOTLOADER := $(BUILDPREFIX_APP)bootloader/
BUILDPREFIX_TEST_BOOTLOADER := $(BUILDPREFIX_APP)bootloader_test/
# Bootloader updater is under an app specific folder as it depends on bootloader
BUILDPREFIX_BL_UPDATER := $(BUILDPREFIX_APP)bl_updater/

BOOTLOADER_HEX := $(BUILDPREFIX_BOOTLOADER)bootloader.hex
BOOTLOADER_TEST_HEX := $(BUILDPREFIX_APP)bootloader_test/bootloader_test.hex
BOOTLOADER_CONFIG_INI := $(BUILDPREFIX_APP)bootloader_full_config.ini

BL_UPDATER_HEX := $(BUILDPREFIX_BL_UPDATER)bl_updater.hex
BL_UPDATER_CMD_LIST := $(APP_SRCS_PATH)bl_updater_cmd_list.conf
BL_UPDATER_NEW_BL_DATA := $(BUILDPREFIX_BL_UPDATER)new_bootloader_data.c
BL_UPDATER_CONFIG := $(BUILDPREFIX_BL_UPDATER)bl_updater_config.c
BL_UPDATER_STACK_BACKUP := $(BUILDPREFIX_BL_UPDATER)stack_backup.c
BL_UPDATER_OLD_CONFIG_INI := $(BUILDPREFIX_BL_UPDATER)old_bootloader_config.ini
BL_UPDATER_STACK_HEX := $(BUILDPREFIX_STACK)$(FIRMWARE_NAME)_for_bl_updater.hex
BL_UPDATER_BUILD_VARIABLES_MK := $(BUILDPREFIX_BL_UPDATER)old_bootloader_build_variables.mk

STACK_HEX := $(BUILDPREFIX_STACK)$(FIRMWARE_NAME).hex
STACK_WITH_BL_UPDATER_HEX := $(BUILDPREFIX_STACK)$(FIRMWARE_NAME)_with_bl_updater.hex
STACK_CONF := $(BUILDPREFIX_STACK)$(FIRMWARE_NAME).conf

APP_HEX := $(BUILDPREFIX_APP)$(APP_NAME).hex

# Build-time make variables that depend on the selected configuration
BUILD_VARIABLES_MK := $(BUILDPREFIX_APP)build_variables.mk

# Files to track make command line parameters, to do a rebuild after any changes
MAKE_PARAMS_FILE := $(BUILDPREFIX_APP)make_params.txt
MAKE_PARAMS_TEMP_FILE := $(BUILDPREFIX_APP)make_params_temp.txt

mac_profile?=ism_24_ghz
