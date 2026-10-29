#!/usr/bin/make -f
NAME = puls

FILES_DSP = PulsPlugin.cpp ../engine/Sequencer.cpp ../state/Serialization.cpp ../state/Templates.cpp ../bridge/Bridge.cpp
FILES_UI  = PulsUI.cpp MenuView.cpp ../gui/EditorLayout.cpp ../gui/Commands.cpp ../gui/Menu.cpp ../gui/Selection.cpp ../gui/Help.cpp ../gui/Keyboard.cpp ../gui/TemplateList.cpp

DPF_PATH = ../external/dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR = ../build/puls

CLAP_BUNDLE = true
UI_TYPE = opengl

include $(DPF_PATH)/Makefile.plugins.mk

BUILD_CXX_FLAGS += -I../engine -I../model -I../state -I../bridge -I../gui -I../third_party -I. -I$(BUILD_DIR)
# DPF's Linux code uses GNU typeof, so Linux needs gnu++20.
ifeq ($(LINUX),true)
BUILD_CXX_FLAGS += -std=gnu++20
else
BUILD_CXX_FLAGS += -std=c++20
endif
# DPF builds with -ffast-math, which would drop the engine's isfinite() guards and break json.hpp's infinity handling.
BUILD_CXX_FLAGS += -fno-fast-math
BUILD_CXX_FLAGS += -Wno-unused-parameter -Wno-deprecated-declarations

TARGETS += clap

# Built-in templates: every templates/*.json, embedded as string literals (see state/Templates.cpp).
TEMPLATE_FILES = $(sort $(wildcard ../templates/*.json))
$(BUILD_DIR)/Templates.inc: $(TEMPLATE_FILES) ../scripts/embed-templates.sh
	@sh ../scripts/embed-templates.sh $@ $(TEMPLATE_FILES)
$(BUILD_DIR)/../state/Templates.cpp.o: $(BUILD_DIR)/Templates.inc

all: $(TARGETS)
