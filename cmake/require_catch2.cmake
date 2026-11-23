# Fails on purpose when Catch2 is missing, so `ctest` cannot report a silent
# pass with zero tests.
message(FATAL_ERROR "Catch2 3 not found: unit tests were not built")
