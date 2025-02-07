include makefile_common.mk

include $(build_variables_mk)


# Scratchpad files for OTAP

# Append a suffix to the scratchpad file names if
# the Generic Bootloader Updater Tool is embedded
bl_updater ?= no
ifneq ($(bl_updater),no)
BL_UPDATER_SUFFIX := "_bl_updater"
else
BL_UPDATER_SUFFIX := ""
endif

FULL_SCRATCHPAD_NAME := $(APP_NAME)_$(FIRMWARE_NAME)$(BL_UPDATER_SUFFIX)
FULL_SCRATCHPAD_BIN := $(BUILDPREFIX_APP)$(FULL_SCRATCHPAD_NAME).otap
APP_SCRATCHPAD_NAME := $(APP_NAME)
APP_SCRATCHPAD_BIN := $(BUILDPREFIX_APP)$(APP_SCRATCHPAD_NAME).otap
STACK_SCRATCHPAD_NAME := $(FIRMWARE_NAME)$(BL_UPDATER_SUFFIX)
STACK_SCRATCHPAD_BIN := $(BUILDPREFIX_APP)$(STACK_SCRATCHPAD_NAME).otap

# Append a suffix to the scratchpad file names, when modem firmware is included
MODEM_FW_SUFFIX := _modem_fw

ifneq ($(modemfw_area_id),)
ifneq ($(radio),none)
ifneq ($(modem_fw),)
# Name for target to generate a scratchpad with modem + stack + app
FULL_SCRATCHPAD_WITH_MODEMFW_NAME := $(APP_NAME)_$(FIRMWARE_NAME)$(MODEM_FW_SUFFIX)$(BL_UPDATER_SUFFIX)
FULL_SCRATCHPAD_WITH_MODEMFW_BIN := $(BUILDPREFIX_APP)$(FULL_SCRATCHPAD_WITH_MODEMFW_NAME).otap
# Name for target to generate a scratchpad with modem + stack
STACK_SCRATCHPAD_WITH_MODEMFW_NAME := $(FIRMWARE_NAME)$(MODEM_FW_SUFFIX)$(BL_UPDATER_SUFFIX)
STACK_SCRATCHPAD_WITH_MODEMFW_BIN := $(BUILDPREFIX_APP)$(STACK_SCRATCHPAD_WITH_MODEMFW_NAME).otap
endif
endif
endif

.PHONY: all
all: $(FULL_SCRATCHPAD_BIN) $(APP_SCRATCHPAD_BIN) $(STACK_SCRATCHPAD_BIN) \
     $(FULL_SCRATCHPAD_WITH_MODEMFW_BIN) $(STACK_SCRATCHPAD_WITH_MODEMFW_BIN)

.PHONY: app_only
app_only: $(APP_SCRATCHPAD_BIN)


# Functions

define BUILD_FULL_SCRATCHPAD_WITH_MODEMFW
	@echo "$(COLOR_INFO)Creating Full Scratchpad with modem:$(COLOR_END) $(2) + $(3) + $(4) -> $(1)"
	$(D)$(SCRAT_GEN) --configfile=$(config_ini) \
	                 $(1) \
	                 $(modemfw_area_id):$(2) \
	                 $(STACK_CONF):$(stack_area_id):$(3) \
	                 $(app_major).$(app_minor).$(app_maintenance).$(app_development):$(app_area_id):$(4)
endef

define BUILD_FULL_SCRATCHPAD
	@echo "$(COLOR_INFO)Creating Full Scratchpad:$(COLOR_END) $(2) + $(3) -> $(1)"
	$(D)$(SCRAT_GEN) --configfile=$(config_ini) \
	                 $(1) \
	                 $(STACK_CONF):$(stack_area_id):$(2) \
	                 $(app_major).$(app_minor).$(app_maintenance).$(app_development):$(app_area_id):$(3)
endef

define BUILD_APP_SCRATCHPAD
	@echo "$(COLOR_INFO)Creating App Scratchpad:$(COLOR_END) $(2) -> $(1)"
	$(D)$(SCRAT_GEN) --configfile=$(config_ini) \
	                 $(1) \
	                 $(app_major).$(app_minor).$(app_maintenance).$(app_development):$(app_area_id):$(2)
endef

define BUILD_STACK_SCRATCHPAD
	@echo "$(COLOR_INFO)Creating Stack Scratchpad:$(COLOR_END) $(2) -> $(1)"
	$(D)$(SCRAT_GEN) --configfile=$(config_ini) \
	                 $(1) \
	                 $(STACK_CONF):$(stack_area_id):$(2)
endef

define BUILD_STACK_SCRATCHPAD_WITH_MODEMFW
	@echo "$(COLOR_INFO)Creating Stack Scratchpad with modem:$(COLOR_END) $(2) + $(3) -> $(1)"
	$(D)$(SCRAT_GEN) --configfile=$(config_ini) \
	                 $(1) \
	                 $(modemfw_area_id):$(2) \
	                 $(STACK_CONF):$(stack_area_id):$(3)
endef


# Target rules

$(STACK_SCRATCHPAD_BIN): $(stack_hex) $(config_ini)
	$(call BUILD_STACK_SCRATCHPAD,$(STACK_SCRATCHPAD_BIN),$(stack_hex))

$(APP_SCRATCHPAD_BIN): $(APP_HEX) $(config_ini)
	$(call BUILD_APP_SCRATCHPAD,$(APP_SCRATCHPAD_BIN),$(APP_HEX))

$(FULL_SCRATCHPAD_BIN): $(stack_hex) $(APP_HEX) $(config_ini)
	$(call BUILD_FULL_SCRATCHPAD,$(FULL_SCRATCHPAD_BIN),$(stack_hex),$(APP_HEX))

ifneq ($(modemfw_area_id),)
ifneq ($(radio),none)
$(FULL_SCRATCHPAD_WITH_MODEMFW_BIN):
	$(call BUILD_FULL_SCRATCHPAD_WITH_MODEMFW,$(FULL_SCRATCHPAD_WITH_MODEMFW_BIN),${modem_fw},$(stack_hex),$(APP_HEX))

$(STACK_SCRATCHPAD_WITH_MODEMFW_BIN):
	$(call BUILD_STACK_SCRATCHPAD_WITH_MODEMFW,$(STACK_SCRATCHPAD_WITH_MODEMFW_BIN),${modem_fw},$(stack_hex))
endif
endif
