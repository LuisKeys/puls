#!/usr/bin/make -f
NAME = puls

FILES_DSP = PulsPlugin.cpp ../engine/Sequencer.cpp ../state/Serialization.cpp ../state/Templates.cpp ../bridge/Bridge.cpp
FILES_UI  = PulsUI.cpp MenuView.cpp WindowIcon.cpp ../gui/EditorLayout.cpp ../gui/Commands.cpp ../gui/Menu.cpp ../gui/Selection.cpp ../gui/Help.cpp ../gui/Keyboard.cpp ../gui/TemplateList.cpp

DPF_PATH = ../external/dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR = ../build/puls

CLAP_BUNDLE = true
UI_TYPE = opengl
# X11 half of the window icon comes from vs-ui; the drum picture is WindowIcon.cpp. (No trailing comment on the
# assignment line: make would keep the spaces in the value and the ifeq in plugin.mk would not match.)
VS_WINDOW_ICON = true

# Shared DPF settings (gnu++20, -fno-fast-math, include paths of the vs-* libraries, the `clap` target and `all`).
include ../../vs-plugin-kit/mk/plugin.mk

BUILD_CXX_FLAGS += -I../engine -I../model -I../state -I../bridge -I../gui -I. -I$(BUILD_DIR)

# Built-in templates: every templates/*.json, embedded as string literals (see state/Templates.cpp).
TEMPLATE_FILES = $(sort $(wildcard ../templates/*.json))
$(BUILD_DIR)/Templates.inc: $(TEMPLATE_FILES) ../scripts/embed-templates.sh
	@sh ../scripts/embed-templates.sh $@ $(TEMPLATE_FILES)
$(BUILD_DIR)/../state/Templates.cpp.o: $(BUILD_DIR)/Templates.inc
