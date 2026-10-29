#!/usr/bin/make -f
NAME = puls

FILES_DSP = PulsPlugin.cpp ../engine/Sequencer.cpp
FILES_UI  = PulsUI.cpp

DPF_PATH = ../external/dpf
DPF_TARGET_DIR = ../bin
DPF_BUILD_DIR = ../build/puls

CLAP_BUNDLE = true
UI_TYPE = webview

include $(DPF_PATH)/Makefile.plugins.mk

# The DGL static lib is built by a sub-make that must also see USE_WEB_VIEW (else link-time sentinel mismatch).
export USE_WEB_VIEW

BUILD_CXX_FLAGS += -std=c++20 -I../engine -I.
BUILD_CXX_FLAGS += -Wno-unused-parameter -Wno-deprecated-declarations

TARGETS += clap

# UI bundle (built by Vite into ui/dist/index.html) is shipped inside the CLAP bundle.
ifeq ($(MACOS),true)
CLAP_RES_DIR = $(TARGET_DIR)/$(NAME).clap/Contents/Resources
else
CLAP_RES_DIR = $(TARGET_DIR)/$(NAME).clap/resources
endif
clapfiles += $(CLAP_RES_DIR)/index.html

all: $(TARGETS) $(clapfiles)

$(CLAP_RES_DIR)/index.html: ../ui/dist/index.html
	-$(SILENT)mkdir -p "$(dir $(abspath $@))"
	install -m 644 $< "$(abspath $@)"
