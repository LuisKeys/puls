#!/usr/bin/make -f
NAME = puls

FILES_DSP = PulsPlugin.cpp ../engine/Sequencer.cpp ../state/Serialization.cpp ../bridge/Bridge.cpp
FILES_UI  = PulsUI.cpp MenuView.cpp ../gui/EditorLayout.cpp ../gui/Commands.cpp ../gui/Menu.cpp ../gui/Selection.cpp

DPF_PATH = ../external/dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR = ../build/puls

CLAP_BUNDLE = true
UI_TYPE = opengl

include $(DPF_PATH)/Makefile.plugins.mk

BUILD_CXX_FLAGS += -I../engine -I../model -I../state -I../bridge -I../gui -I../third_party -I.
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

all: $(TARGETS)
