// Solver implementations that live in the dcstudy-z3 worker process.
#pragma once

#include "z3/z3_protocol.hpp"

namespace dcstudy {

SynthSolution z3_solve_synth(const SynthProblem &p);
SwitchSolution z3_solve_switch(const SwitchProblem &p);

} // namespace dcstudy
