.DEFAULT_GOAL := test
MODULE_ROOT := $(abspath $(dir $(lastword $(MAKEFILE_LIST)))/../..)
MODULE_BUILD := $(MODULE_ROOT)/build/modules
MODULE_GENERATED := $(shell python3 $(MODULE_ROOT)/scripts/modules.py --output $(MODULE_BUILD) || echo failed)
ifneq ($(MODULE_GENERATED),)
$(error Module generation failed)
endif
include $(MODULE_BUILD)/modules.mk
CPPFLAGS += -D_POSIX_C_SOURCE=200809L -I$(MODULE_ROOT)/firmware/modules/include -I$(MODULE_ROOT)/firmware/components/curio_layout/include -I$(MODULE_ROOT)/firmware/components/curio_alerts/include -I$(MODULE_ROOT)/firmware/components/curio_core/include -I$(MODULE_ROOT)/.deps/cJSON
SOURCES += $(MODULE_SOURCES)

MODULE_DEPENDENCIES := $(MODULE_BUILD)/widgets.c $(MODULE_BUILD)/alerts.c $(wildcard $(MODULE_ROOT)/firmware/modules/include/*.h $(MODULE_ROOT)/firmware/modules/widgets/*/*.h $(MODULE_ROOT)/firmware/modules/alerts/*/*.h)
