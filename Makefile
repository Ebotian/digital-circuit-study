BUILD ?= build
CXX ?= clang++
FILC ?= filc++
CMAKE ?= cmake
SOURCES := $(wildcard src/*.cpp) $(wildcard include/dcstudy/*.hpp) \
           tests/unit_tests.cpp
FILC_SOURCES := src/main.cpp src/bdd_switch.cpp src/boolfunc.cpp src/cdl.cpp src/cmos.cpp \
                src/designspace.cpp src/driver_profile.cpp src/math_client.cpp \
                src/multi_path.cpp src/multistage.cpp src/path_search.cpp \
                src/limits.cpp src/partial.cpp src/static_exact.cpp src/template_synth.cpp \
                src/switchlevel.cpp \
                src/synthesis.cpp \
                src/verilog.cpp src/verify.cpp src/worker_client.cpp \
                src/z3_client.cpp src/render_client.cpp src/z3_protocol.cpp

.PHONY: all build test verify integration filc filc-smoke clean

all: build

build: $(BUILD)/dcstudy

$(BUILD)/CMakeCache.txt: CMakeLists.txt
	$(CMAKE) -S . -B $(BUILD) -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=$(CXX)

$(BUILD)/dcstudy: $(SOURCES) $(BUILD)/CMakeCache.txt
	$(CMAKE) --build $(BUILD) -j

test: $(BUILD)/dcstudy
	ctest --test-dir $(BUILD) --output-on-failure

verify: $(BUILD)/dcstudy
	BIN=$(abspath $(BUILD))/dcstudy bash tests/verify/run.sh instances

integration: $(BUILD)/dcstudy
	BIN=$(abspath $(BUILD))/dcstudy bash tests/integration.sh

# Full safe CLI under Fil-C; it spawns the host-built Z3 worker.
filc: $(FILC_SOURCES) $(wildcard include/dcstudy/*.hpp)
	@mkdir -p $(BUILD)
	$(FILC) --filc-no-ape -std=c++20 -O1 -Iinclude $(FILC_SOURCES) \
	  -o $(BUILD)/dcstudy-filc
	@echo "run: DCSTUDY_Z3_HELPER=$(abspath $(BUILD))/dcstudy-z3 $(BUILD)/dcstudy-filc ..."

# Optional memory-safety run of the Z3-free core under Fil-C.
filc-smoke: tests/filc_smoke.cpp src/boolfunc.cpp src/cmos.cpp \
            src/designspace.cpp src/verilog.cpp
	@mkdir -p $(BUILD)
	$(FILC) --filc-no-ape -std=c++20 -O1 -Iinclude tests/filc_smoke.cpp \
	  src/boolfunc.cpp src/cmos.cpp src/designspace.cpp src/verilog.cpp \
	  -o $(BUILD)/filc_smoke
	$(BUILD)/filc_smoke

clean:
	rm -rf $(BUILD)
