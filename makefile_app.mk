include makefile_common.mk

.DEFAULT_GOAL := all

#=============================================================================
# PHASE 1: CONFIGURATION AND SETUP
# INPUTS:
#   - MCU configuration (MCU_PATH, MCU_FAMILY, MCU, MCU_SUB, MCU_MEM_VAR)
#   - Build paths (BUILDPREFIX_APP, APP_NAME, APP_SRCS_PATH)
#   - Board validation (TARGET_BOARDS, AVAILABLE_BOARDS)
#   - Optional MCU RAM variant (MCU_RAM_VAR)
#   - Target board selection (target_board)
# OUTPUTS:
#   - Linker script path (LDSCRIPT)
#   - Validated build environment
#   - File paths for version generation and cleanup
#=============================================================================

# Linker script
ifndef MCU_RAM_VAR
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_app_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR).ld
else
LDSCRIPT = $(MCU_PATH)$(MCU_FAMILY)/$(MCU)/linker/gcc_app_$(MCU)$(MCU_SUB)$(MCU_MEM_VAR)_$(MCU_RAM_VAR).ld
endif

# Application-specific libraries, e.g., my_lib.a
LIBS :=

# Compiler-provided libraries, e.g., -lm
LDLIBS :=

ifeq ($(filter $(TARGET_BOARDS),$(target_board)),)
 $(error Board $(target_board) is not in supported board list: ($(TARGET_BOARDS)))
endif

# File to store app version
VERSION_FILE := $(BUILDPREFIX_APP)version.txt

# App different formats
APP_ELF := $(BUILDPREFIX_APP)$(APP_NAME).elf

# For backward compatibility as app makefile except SRCS_PATH variable
SRCS_PATH := $(APP_SRCS_PATH)

#=============================================================================
# PHASE 2: COMPILER FLAGS GENERATION
# INPUTS:
#   - Network configuration (default_network_address, default_network_channel)
#   - Security keys (default_network_cipher_key, default_network_authen_key)
#   - Application version (app_major, app_minor, app_maintenance, app_development)
#   - MAC profile selection (mac_profile)
#   - Base compiler flags (CFLAGS)
#   - Security control flag (allow_insecure_key_injection)
# OUTPUTS:
#   - Preprocessor definitions (-D flags in CFLAGS)
#   - Security warnings/errors to console
# SECURITY POLICY:
#   - Network keys require allow_insecure_key_injection=yes flag to compile
#   - Build fails with error if keys defined without security flag
#   - Build succeeds with warnings if keys defined with security flag
#   - Address/channel settings have no security restrictions
#=============================================================================

# Convert default network settings to CFLAGS to be used in code
ifneq ($(default_network_address),)
CFLAGS += -DNETWORK_ADDRESS=$(default_network_address)
endif
ifneq ($(default_network_channel),)
CFLAGS += -DNETWORK_CHANNEL=$(default_network_channel)
endif

# Security-controlled key injection
ifneq ($(default_network_cipher_key),)
ifeq ($(allow_insecure_key_injection),yes)
$(warning ***********************************************************************)
$(warning WARNING: Insecure key injection is enabled!)
$(warning Network cipher key is being compiled into the binary.)
$(warning This is NOT recommended for production builds.)
$(warning Use app setup application with key provisioning library instead.)
$(warning ***********************************************************************)
CFLAGS += -DNET_CIPHER_KEY=$(default_network_cipher_key)
else
$(error ERROR: Network cipher key is defined but insecure key injection is not allowed. To compile keys into binary (NOT RECOMMENDED), use: make allow_insecure_key_injection=yes ... For secure key management, use app setup application with key provisioning library.)
endif
endif

ifneq ($(default_network_authen_key),)
ifeq ($(allow_insecure_key_injection),yes)
$(warning ***********************************************************************)
$(warning WARNING: Insecure key injection is enabled!)
$(warning Network authentication key is being compiled into the binary.)
$(warning This is NOT recommended for production builds.)
$(warning Use app setup application with key provisioning library instead.)
$(warning ***********************************************************************)
CFLAGS += -DNET_AUTHEN_KEY=$(default_network_authen_key)
else
$(error ERROR: Network authentication key is defined but insecure key injection is not allowed. To compile keys into binary (NOT RECOMMENDED), use: make allow_insecure_key_injection=yes ... For secure key management, use app setup application with key provisioning library.)
endif
endif

# And version numbers
CFLAGS += -DVER_MAJOR=$(app_major) -DVER_MINOR=$(app_minor) -DVER_MAINT=$(app_maintenance) -DVER_DEV=$(app_development)

# Mac profile
ifeq ("$(mac_profile)","ism_24_ghz")
    CFLAGS += -DMAC_PROFILE_ISM24
else ifeq ("$(mac_profile)","subg")
    CFLAGS += -DMAC_PROFILE_SUBG
else
    CFLAGS += -DMAC_PROFILE_DECTNR
endif

#=============================================================================
# PHASE 3: SOURCE COLLECTION AND DEPENDENCY SETUP
# INPUTS:
#   - Board-specific sources and configuration
#   - Application-specific sources and settings
#   - Debug functionality (INTERNAL_USE_ONLY)
#   - Library configuration and dependencies
#   - Generic utility functions (api.c for all apps)
#   - Wirepas library sources and includes
#   - MCU-specific configuration
#   - Hardware Abstraction Layer drivers
#   - Common MCU sources
# OUTPUTS:
#   - Source file lists (SRCS, ASM_SRCS)
#   - Include paths (INCLUDES)
#   - Object and dependency file lists (OBJS, DEPS)
#   - Cleanup file list (CLEAN)
#   - Collected libraries for linking
#=============================================================================

# Include board init part
-include board/makefile

# Include app specific makefile
-include $(APP_SRCS_PATH)makefile


# Include Libraries config first (dependencies)
-include $(WP_LIB_PATH)config.mk

# Generic util functions are needed for all apps (api.c)
-include $(UTIL_PATH)makefile

# Include libraries code
-include $(WP_LIB_PATH)makefile
INCLUDES += -I$(WP_LIB_PATH)

# Include MCU config first
-include $(MCU_PATH)config.mk

# Include MCU HAL drivers code
-include $(HAL_API_PATH)makefile

# Include common MCU sources
include $(MCU_PATH)common/makefile

#
# Sources & includes paths
#
SRCS += $(APP_SRCS_PATH)app.c
INCLUDES += -I$(API_PATH) -I$(APP_SRCS_PATH)include -I$(UTIL_PATH)

# Objects list
OBJS_ = $(SRCS:.c=.o) $(ASM_SRCS:.s=.o)
OBJS = $(addprefix $(BUILDPREFIX_APP), $(OBJS_))

# Dependent list
DEPS_ = $(SRCS:.c=.d)
DEPS = $(addprefix $(BUILDPREFIX_APP), $(DEPS_))

# Files to be cleaned
CLEAN := $(OBJS) $(APP_ELF) $(APP_HEX) $(DEPS)

#=============================================================================
# PHASE 4: COMPILATION RULES
# INPUTS:
#   - Individual source files (.c and .s files)
#   - Configuration files (APP_CONFIG, BOARD_CONFIG, MCU_CONFIG)
#   - Compiler flags with preprocessor definitions (CFLAGS)
#   - Include paths (-I flags for header locations)
#   - Build tools (CC compiler, MKDIR, color definitions)
# OUTPUTS:
#   - Compiled object files (.o files)
#   - Dependency files (.d files) for incremental builds
#   - Build directory structure
#   - Compilation progress messages with color coding
#=============================================================================

$(BUILDPREFIX_APP)%.o : %.c $(APP_SRCS_PATH)makefile $(APP_CONFIG) $(BOARD_CONFIG) $(MCU_CONFIG)
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -MMD -MP -c $< -o $@

$(BUILDPREFIX_APP)%.o : %.s
	$(DD)$(MKDIR) $(@D)
	@echo "$(COLOR_CC)CC$(COLOR_END) $<"
	$(D)$(CC) $(INCLUDES) $(CFLAGS) -c $< -o $@

#=============================================================================
# PHASE 5: LINKING
# INPUTS:
#   - All compiled object files (OBJS list)
#   - Linker script with memory layout (LDSCRIPT)
#   - Compiler and linker flags (CFLAGS, LDFLAGS)
#   - Libraries collected from various components, e.g., my_lib.a (LIBS)
#   - Linker flags for compiler-provided libraries, e.g., -lm (LDLIBS)
#   - Build tools (CC compiler/linker)
# OUTPUTS:
#   - Executable ELF file (APP_ELF)
#   - Memory layout map file (.map)
#   - Linking progress and memory usage information
#=============================================================================

$(APP_ELF): $(OBJS) $(LIBS)
	@echo "$(COLOR_LINK)Linking$(COLOR_END) $@"
	$(D)$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ \
	      -Wl,-Map=$(BUILDPREFIX_APP)$(APP_NAME).map \
	      -Wl,-T,$(LDSCRIPT),--print-memory-usage $(LIBS) $(LDLIBS)

#=============================================================================
# PHASE 6: FORMAT CONVERSION
# INPUTS:
#   - ELF executable file (APP_ELF)
#   - Compiler and linker flags (for console display)
#   - Object copy tool and color definitions
# OUTPUTS:
#   - Intel HEX file for microcontroller programming (APP_HEX)
#   - OTAP file generation support
#   - Build configuration summary
#   - Generation status messages
#=============================================================================

$(APP_HEX): $(APP_ELF)
	@echo "$(COLOR_DETAILS)CFLAGS are:\n $(CFLAGS)$(COLOR_END)"
	@echo "$(COLOR_DETAILS)LDFLAGS are:\n $(LDFLAGS)$(COLOR_END)"
	@echo "$(COLOR_INFO)Generating:$(COLOR_END) $@"
	$(D)@$(OBJCOPY) $< -O ihex $@

#=============================================================================
# PHASE 7: VERSION FILE GENERATION
# INPUTS:
#   - Application version variables (app_major, app_minor, app_maintenance, app_development)
#   - Git repository commit history
#   - Version file path (VERSION_FILE)
# OUTPUTS:
#   - Version tracking file (version.txt)
#   - Git SHA for build traceability
#   - Version information for deployment scripts
#=============================================================================

.PHONY: $(VERSION_FILE)
$(VERSION_FILE):
	@echo "app_version=$(app_major).$(app_minor).$(app_maintenance).$(app_development)" > $(@)
	@echo "sha1=$(shell git log -1 --pretty=format:"%h")" >> $(@)

#=============================================================================
# MAIN TARGETS AND CLEANUP
# INPUTS: All previous phases
# OUTPUTS:
#   - Main build target dependencies
#   - Clean target for removing generated files
#   - Automatic dependency file inclusion
#=============================================================================

.PHONY: all
all: $(APP_HEX) $(VERSION_FILE)

clean:
	$(D)$(RM) -rf $(CLEAN)

-include $(DEPS)
