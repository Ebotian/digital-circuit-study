BUILD ?= build
CXX ?= clang++
ifeq ($(origin CXX),default)
CXX := clang++
endif
FILC ?= filc++
CMAKE ?= cmake
SOURCES := $(shell find src -name "*.cpp") $(shell find include -name "*.hpp") \
           $(wildcard tests/unit/*) $(wildcard tests/smoke/*)
FILC_SOURCES := src/cli/main.cpp src/switch/bdd_switch.cpp src/core/boolfunc.cpp \
                src/core/cdl.cpp src/core/cmos.cpp src/core/designspace.cpp \
                src/core/driver_profile.cpp src/cli/math_client.cpp \
                src/synthesis/multi_path.cpp src/synthesis/multistage.cpp \
                src/synthesis/path_search.cpp src/core/limits.cpp \
                src/core/partial.cpp src/synthesis/static_exact.cpp src/synthesis/compose.cpp \
                src/synthesis/template_synth.cpp src/switch/switchlevel.cpp \
                src/synthesis/synthesis.cpp src/core/verilog.cpp \
                src/core/verify.cpp src/cli/worker_client.cpp \
                src/cli/z3_client.cpp src/cli/render_client.cpp \
                src/z3/z3_protocol.cpp

.PHONY: all build test bench bench-save bench-micro bench-perf bench-profile bench-size format format-check lint ci ci-full asan werror coverage test-gcc fuzz shellcheck pylint tslint smoke smoke-cli smoke-filc filc filc-smoke verify verify-all integration clean

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
	clice format src include tests

format-check: $(CCDB)
	clice format --check src include tests

# Full gate for CI: format, lint, unit+smoke, instances+formal+integration.
ci: format-check lint werror test verify-all

# clice lint (LLVM/Clang toolkit; needs build/compile_commands.json).
lint: $(CCDB)
	@clice lint -j 0 | tee $(BUILD)/clice-lint.txt
	@grep -q ": 0 findings" $(BUILD)/clice-lint.txt

bench: $(BUILD)/dcstudy
	bench/run.sh --suite all

bench-save: $(BUILD)/dcstudy
	bench/run.sh --suite all --save

bench-micro:
	bench/micro.sh

bench-perf: $(BUILD)/dcstudy
	perf stat -e cycles:u,instructions:u,branches:u,branch-misses:u,cache-misses:u \
	  $(BUILD)/dcstudy multi exact --spec tests/verify/instances/specs/hex7seg.json --json

bench-profile: $(BUILD)/dcstudy
	valgrind --tool=callgrind --callgrind-out-file=$(BUILD)/callgrind.out \
	  $(BUILD)/dcstudy multi compose --spec tests/verify/instances/specs/mux4_3bit.json --json
	callgrind_annotate $(BUILD)/callgrind.out | head -30

bench-size: $(BUILD)/dcstudy
	bloaty $(BUILD)/dcstudy -d sections

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
# --- extended quality gates (P0/P1) ---------------------------------------
asan:
	$(CMAKE) -S . -B build-asan -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_BUILD_TYPE=Debug -DDCSTUDY_SANITIZE=ON
	$(CMAKE) --build build-asan -j
	ctest --test-dir build-asan --output-on-failure

werror:
	$(CMAKE) -S . -B build-werror -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DCMAKE_CXX_COMPILER=$(CXX) -DCMAKE_BUILD_TYPE=Release -DDCSTUDY_WERROR=ON
	$(CMAKE) --build build-werror -j

coverage:
	$(CMAKE) -S . -B build-cov -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Debug -DDCSTUDY_COVERAGE=ON
	$(CMAKE) --build build-cov -j
	LLVM_PROFILE_FILE=$(abspath build-cov)/cov-%p.profraw ctest --test-dir build-cov --output-on-failure
	llvm-profdata merge -sparse build-cov/*.profraw -o build-cov/cov.profdata
	llvm-cov report build-cov/dcstudy_tests -instr-profile=build-cov/cov.profdata > build-cov/coverage.txt
	@grep -E '^TOTAL' build-cov/coverage.txt

test-gcc:
	$(CMAKE) -S . -B build-gcc -DCMAKE_EXPORT_COMPILE_COMMANDS=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_CXX_COMPILER=g++
	$(CMAKE) --build build-gcc -j
	ctest --test-dir build-gcc --output-on-failure

FUZZ_TIME ?= 30
fuzz:
	clang++ -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude \
	  fuzz/fuzz_json.cpp -o $(BUILD)/fuzz_json
	clang++ -std=c++20 -O1 -g -fsanitize=fuzzer,address,undefined -Iinclude \
	  fuzz/fuzz_expr.cpp src/core/boolfunc.cpp -o $(BUILD)/fuzz_expr
	$(BUILD)/fuzz_json -max_total_time=$(FUZZ_TIME) fuzz/corpus/json
	$(BUILD)/fuzz_expr -max_total_time=$(FUZZ_TIME) fuzz/corpus/expr

# Non-C++ assets: skip when the tool is missing (fail under CI=1).
shellcheck:
	shellcheck -x -S warning $$(find . -name '*.sh' -not -path './build*' -not -path './.git/*')

pylint:
	mypy --ignore-missing-imports workers/render_worker.py

tslint:
	cd workers && npx tsc --noEmit

ci-full: ci asan werror coverage test-gcc fuzz shellcheck pylint
