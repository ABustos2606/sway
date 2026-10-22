# SPDX-License-Identifier: GPL-2.0-or-later
# Copyright (c) 2026 Alejandro Bustos

PREFIX ?= /usr/local
LV2DIR ?= $(PREFIX)/lib/lv2
DESTDIR ?=
OPT ?= -O2
PYTHON ?= python3
PKG_CONFIG ?= pkg-config
BUILD_DIR ?= build

BUNDLE := $(BUILD_DIR)/sway.lv2
METADATA := manifest.ttl sway.ttl
TTL_TARGETS := $(addprefix $(BUNDLE)/,$(METADATA))
LV2_CFLAGS = $(shell $(PKG_CONFIG) --cflags lv2)
COMPILE_FLAGS = $(CPPFLAGS) $(LV2_CFLAGS) $(CXXFLAGS) $(OPT) -std=c++17 -fPIC -fvisibility=hidden -Wall -Wextra

.PHONY: all install test lint clean
all: $(BUNDLE)/sway.so $(TTL_TARGETS)

$(BUNDLE)/sway.so: src/lv2_entry.cpp src/motion.hpp src/ports.hpp src/exports.map
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) src/lv2_entry.cpp $(LDFLAGS) -shared -Wl,--no-undefined -Wl,--version-script=src/exports.map -o $@

$(BUNDLE)/%.ttl: sway.lv2/%.ttl
	@mkdir -p "$(@D)"
	install -m 644 $< $@

$(BUILD_DIR)/test_motion: tests/test_motion.cpp src/motion.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -o $@

$(BUILD_DIR)/test_lv2: tests/test_lv2.cpp src/motion.hpp src/ports.hpp
	@mkdir -p "$(@D)"
	$(CXX) $(COMPILE_FLAGS) $< $(LDFLAGS) -ldl -o $@

test: all $(BUILD_DIR)/test_motion $(BUILD_DIR)/test_lv2
	$(PYTHON) tests/ttl_check.py $(BUNDLE)
	$(BUILD_DIR)/test_motion
	$(BUILD_DIR)/test_lv2 $(BUNDLE)/sway.so

install: all
	install -d "$(DESTDIR)$(LV2DIR)/sway.lv2"
	install -m 755 $(BUNDLE)/sway.so "$(DESTDIR)$(LV2DIR)/sway.lv2/"
	install -m 644 $(TTL_TARGETS) LICENSE "$(DESTDIR)$(LV2DIR)/sway.lv2/"

lint:
	@if command -v lv2lint >/dev/null 2>&1; then \
		LV2_PATH="$(DESTDIR)$(LV2DIR):$${LV2_PATH:-/usr/local/lib/lv2:/usr/lib/lv2}" lv2lint 'https://github.com/ABustos2606/sway#sway'; \
	else echo 'SKIP lv2lint: not installed (install the bundle with make install first)'; fi

clean:
	$(RM) -r $(BUILD_DIR)
