BUILD ?= build
CXX ?= clang++
FILC ?= filc++
CMAKE ?= cmake
SOURCES := $(shell find src -name "*.cpp") $(shell find include -name "*.hpp") \
           $(wildcard tests/unit/*) $(wildcard tests/smoke/*)
FILC_SOURCES := src/cli/main.cpp src/switch/bdd_switch.cpp src/core/boolfunc.cpp \
                src/core/cdl.cpp src/core/cmos.cpp src/core/designspace.cpp \
                src/core/driver_profile.cpp src/cli/math_client.cpp \
                src/synthesis/multi_path.cpp src/synthesis/multistage.cpp \
                src/synthesis/path_search.cpp src/core/limits.cpp \
                src/core/partial.cpp src/synthesis/static_exact.cpp \
                src/synthesis/template_synth.cpp src/switch/switchlevel.cpp \
                src/synthesis/synthesis.cpp src/core/verilog.cpp \
                src/core/verify.cpp src/cli/worker_client.cpp \
                src/cli/z3_client.cpp src/cli/render_client.cpp \
                src/z3/z3_protocol.cpp

.PHONY: all build test format format-check lint ci smoke smoke-cli smoke-filc filc filc-smoke verify verify-all integration clean

all: build

build: $(BUILD)/dcstudy

$(BUILD)/CMakeCache.txt: CMakeLists.txt
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX)

$(BUILD)/dcstudy: $(SOURCES) $(BUILD)/CMakeCache.txt
	$(CMAKE) --build $(BUILD) -j

test: $(BUILD)/dcstudy
	ctest --test-dir $(BUILD) --output-on-failure
	$(MAKE) smoke-cli

verify: $(BUILD)/dcstudy
	$(MAKE) -C tests/verify instances

verify-all: $(BUILD)/dcstudy
	$(MAKE) -C tests/verify all

integration: $(BUILD)/dcstudy
	BIN=$(abspath $(BUILD))/dcstudy bash tests/integration.sh

CCDB = compile_commands.json
$(CCDB):
	@ln -sf $(BUILD)/compile_commands.json $(CCDB)

# Formatting: clice's default style (no .clang-format in the repo).
format: $(CCDB)
	clice format

format-check: $(CCDB)
	clice format --check

# Full gate for CI: format, lint, unit+smoke, instances+formal+integration.
ci: format-check lint test verify-all

# clice lint (LLVM/Clang toolkit; needs build/compile_commands.json).
lint:
	@command -v clice >/dev/null 2>&1 || { echo "clice not installed" >&2; exit 1; }
	@[ -e compile_commands.json ] || ln -sf $(BUILD)/compile_commands.json compile_commands.json
	@clice lint -j 0 | tee $(BUILD)/clice-lint.txt
	@grep -q ": 0 findings" $(BUILD)/clice-lint.txt || { echo "clice lint findings" >&2; exit 1; }

smoke: smoke-cli
	@if command -v $(FILC) >/dev/null 2>&1; then \
	  $(MAKE) smoke-filc; \
	else \
	  echo "skip filc smoke ($(FILC) not found)"; \
	fi

smoke-cli: $(BUILD)/dcstudy
	BIN=$(abspath $(BUILD))/dcstudy bash tests/smoke/cli.sh

# Full safe CLI under Fil-C; it spawns the host-built Z3 worker.
filc: $(FILC_SOURCES) $(shell find include -name "*.hpp")
	@mkdir -p $(BUILD)
	$(FILC) --filc-no-ape -std=c++20 -O1 -Iinclude $(FILC_SOURCES) \
	  -o $(BUILD)/dcstudy-filc
	@echo "run: DCSTUDY_Z3_HELPER=$(abspath $(BUILD))/dcstudy-z3 $(BUILD)/dcstudy-filc ..."

# Optional memory-safety run of the Z3-free core under Fil-C.
smoke-filc: tests/smoke/filc_smoke.cpp src/core/boolfunc.cpp src/core/cmos.cpp \
            src/core/designspace.cpp src/core/verilog.cpp
	@mkdir -p $(BUILD)
	$(FILC) --filc-no-ape -std=c++20 -O1 -Iinclude tests/smoke/filc_smoke.cpp \
	  src/core/boolfunc.cpp src/core/cmos.cpp src/core/designspace.cpp src/core/verilog.cpp \
	  -o $(BUILD)/filc_smoke
	$(BUILD)/filc_smoke

# Backwards-compatible alias for the old target name.
filc-smoke: smoke-filc

clean:
	rm -rf $(BUILD)
