# Verification targets (run from the repo root with `make -C tests/verify ...`).
BIN ?= $(abspath ../../build/dcstudy)

INSTANCE_CASES := add4s decoder38 encoder42 eq4 fa1 hex7seg hex7seg_sel pe42 nor2 xor2 xnor2 mux2 mux4_3bit rca4_7seg prio8 prio16 lzcount16
INSTANCE_TARGETS := $(addprefix instance-,$(INSTANCE_CASES))

.PHONY: all instances simulation formal optional render $(INSTANCE_TARGETS)

# instance-<name> is one end-to-end design with a known-answer assertion.
# A static pattern rule (not a generic one): .PHONY targets skip implicit rules.
$(INSTANCE_TARGETS): instance-%:
	BIN=$(BIN) bash instances/scripts/$*.sh

render:
	BIN=$(BIN) INSTANCE="$(INSTANCE)" VARIANT="$(VARIANT)" bash instances/render.sh

instances: $(INSTANCE_TARGETS) simulation

simulation:
	BIN=$(BIN) bash instances/simulation/verilator-xor6/run.sh

formal:
	ctest --test-dir ../../build --output-on-failure

optional:
	BIN=$(BIN) bash ../../tests/integration.sh

all: instances formal optional
