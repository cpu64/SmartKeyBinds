BUILD_DIR := build
CMAKE := cmake
CC := gcc
CXX := clang++
CMAKE_ARGS ?=
PARALLEL ?= --parallel
CLANG_FORMAT := clang-format
CLANG_TIDY := clang-tidy

TARGETS := all common plugin smartkeybinds
ACTIONS := build install run format lint commands clean

.DEFAULT_GOAL := all

# Configuration options for each build.
CONFIG_ARGS_all := \
	-DSKB_BUILD_COMMON=ON \
	-DSKB_BUILD_PLUGIN=ON \
	-DSKB_BUILD_ENGINE=ON

CONFIG_ARGS_common := \
	-DSKB_BUILD_COMMON=ON \
	-DSKB_BUILD_PLUGIN=OFF \
	-DSKB_BUILD_ENGINE=OFF

CONFIG_ARGS_plugin := \
	-DSKB_BUILD_COMMON=ON \
	-DSKB_BUILD_PLUGIN=ON \
	-DSKB_BUILD_ENGINE=OFF

CONFIG_ARGS_smartkeybinds := \
	-DSKB_BUILD_COMMON=ON \
	-DSKB_BUILD_PLUGIN=OFF \
	-DSKB_BUILD_ENGINE=ON

# Actual CMake target names.
BUILD_TARGET_common := smartkeybinds-common
BUILD_TARGET_plugin := smartkeybinds-kwin-plugin
BUILD_TARGET_smartkeybinds := smartkeybinds

# Source directories for formatting and linting.
FORMAT_DIR_all := common engine kwin-plugin macros
FORMAT_DIR_common := common
FORMAT_DIR_plugin := kwin-plugin
FORMAT_DIR_smartkeybinds := engine macros

LINT_DIR_all := common engine kwin-plugin macros
LINT_DIR_common := common
LINT_DIR_plugin := kwin-plugin
LINT_DIR_smartkeybinds := engine

# Configure a particular build.
define configure
	$(CMAKE) -S . -B $(BUILD_DIR)/$(1) \
		-DCMAKE_C_COMPILER=$(CC) \
		-DCMAKE_CXX_COMPILER=$(CXX) \
		$(CONFIG_ARGS_$(1)) $(CMAKE_ARGS)
endef

# Build a component.
define build_target
	$(call configure,$(1))
	$(CMAKE) --build $(BUILD_DIR)/$(1) $(PARALLEL) \
		$(if $(filter all,$(1)),,--target $(BUILD_TARGET_$(1)))
endef

# Generate compilation database.
define generate_commands
	$(call configure,$(1))
	ln -sfn $(BUILD_DIR)/$(1)/compile_commands.json compile_commands.json
endef

# Clean a particular build.
define clean_build
	rm -rf $(BUILD_DIR)/$(1)
	if [ -L compile_commands.json ] && \
	   [ "$$(readlink compile_commands.json)" = "$(BUILD_DIR)/$(1)/compile_commands.json" ]; then \
		rm compile_commands.json; \
	fi
endef

# Clean all builds.
define clean_all
	rm -rf $(BUILD_DIR)
	rm -f compile_commands.json
endef

# Format source files using the root .clang-format.
define format_sources
	find $(FORMAT_DIR_$(1)) \
		-type f \
		\( -name '*.cpp' -o -name '*.hpp' -o -name '*.h' -o -name '*.cc' -o -name '*.cxx' \) \
		-exec $(CLANG_FORMAT) -i {} +
endef

# Lint source files using the root .clang-tidy.
define lint_sources
	$(call configure,$(1))
	$(CLANG_TIDY) \
		--config-file=.clang-tidy \
		--extra-arg-before=-Wno-unknown-warning-option \
		-p $(BUILD_DIR)/$(1) \
		$$(find $(LINT_DIR_$(1)) \
			-type f \
			\( -name '*.cpp' -o -name '*.cc' -o -name '*.cxx' \))
endef

# Generate common rules.
define target_rules

$(1)-build:
	$$(call build_target,$(1))

$(1)-commands:
	$$(call generate_commands,$(1))

$(1)-format:
	$$(call format_sources,$(1))

$(1)-lint:
	$$(call lint_sources,$(1))

$(1)-clean:
	$$(if $$(filter all,$(1)),$$(call clean_all),$$(call clean_build,$(1)))

endef

$(foreach t,$(TARGETS),$(eval $(call target_rules,$(t))))

.PHONY: all build install run format lint commands clean
.PHONY: $(foreach t,$(TARGETS),$(foreach a,$(ACTIONS),$(t)-$(a)))

# Default aliases.
all: all-build
build: all-build
install: all-install
run: all-run
format: all-format
lint: all-lint
commands: all-commands
clean: all-clean

# Installation.
all-install: plugin-install smartkeybinds-install

common-install:
	@echo "Error: 'common' is part of SmartKeyBinds and does not need to be installed separately."
	@exit 1

plugin-install:
	$(call configure,plugin)
	$(CMAKE) --install $(BUILD_DIR)/plugin

smartkeybinds-install:
	$(call configure,smartkeybinds)
	$(CMAKE) --install $(BUILD_DIR)/smartkeybinds

# Running.
all-run: plugin-run smartkeybinds-run

common-run:
	@echo "Error: 'common' is a library and cannot be run."
	@exit 1

plugin-run: plugin-build plugin-install
	kwin_wayland --replace

smartkeybinds-run: smartkeybinds-build
	$(BUILD_DIR)/smartkeybinds/engine/smartkeybinds
