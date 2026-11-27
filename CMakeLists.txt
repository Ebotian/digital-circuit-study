cmake_minimum_required(VERSION 3.25)
project(dcstudy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(DCSTUDY_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(DCSTUDY_BUILD_Z3_HELPER "Build the out-of-process Z3 worker" ON)
if(DCSTUDY_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

option(DCSTUDY_WERROR "Treat warnings as errors" OFF)
if(DCSTUDY_WERROR)
  add_compile_options(-Werror)
endif()

option(DCSTUDY_COVERAGE "Instrument for LLVM source-based coverage" OFF)
if(DCSTUDY_COVERAGE AND NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
  message(FATAL_ERROR "DCSTUDY_COVERAGE requires Clang")
endif()
if(DCSTUDY_COVERAGE)
  add_compile_options(-fprofile-instr-generate -fcoverage-mapping)
  add_link_options(-fprofile-instr-generate -fcoverage-mapping)
endif()

add_library(dcstudy_core STATIC
  src/core/boolfunc.cpp
  src/core/cdl.cpp
  src/core/cmos.cpp
  src/core/driver_profile.cpp
  src/core/limits.cpp
  src/core/partial.cpp
  src/core/designspace.cpp
  src/core/verilog.cpp
  src/core/verify.cpp
  src/synthesis/static_exact.cpp
  src/synthesis/template_synth.cpp
  src/synthesis/synthesis.cpp
  src/synthesis/multi_path.cpp
  src/synthesis/multistage.cpp
  src/synthesis/path_search.cpp
  src/synthesis/compose.cpp
  src/synthesis/abc_backend.cpp
  src/switch/switchlevel.cpp
  src/switch/static_cells.cpp
  src/switch/bdd_switch.cpp
  src/cli/math_client.cpp
  src/cli/worker_client.cpp
  src/cli/z3_client.cpp
  src/cli/render_client.cpp
  src/z3/z3_protocol.cpp
)
target_include_directories(dcstudy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
option(DCSTUDY_WITH_EPFL "Enable mockturtle-based library mapping (needs the submodule)" OFF)
if(DCSTUDY_WITH_EPFL)
  if(NOT EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/third_party/mockturtle/include/mockturtle/algorithms/emap.hpp")
    message(FATAL_ERROR "mockturtle submodule missing; run: git submodule update --init --recursive")
  endif()
  set(MOCKTURTLE_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
  set(MOCKTURTLE_BUILD_TESTS OFF CACHE BOOL "" FORCE)
  add_subdirectory(third_party/mockturtle EXCLUDE_FROM_ALL)
  target_link_libraries(dcstudy_core PUBLIC mockturtle)
endif()


add_executable(dcstudy src/cli/main.cpp src/cli/commands.cpp
  src/cli/cli_common.cpp src/cli/cmd_multi.cpp
  src/cli/cmd_basic.cpp src/cli/cmd_design.cpp src/cli/cmd_render.cpp src/cli/cmd_pipeline.cpp src/cli/cli_help.cpp)
target_link_libraries(dcstudy PRIVATE dcstudy_core)

if(DCSTUDY_BUILD_Z3_HELPER)
  find_package(Z3 REQUIRED)
  find_library(BLISS_LIBRARY bliss REQUIRED)
  find_library(GRAPHBLAS_LIBRARY graphblas REQUIRED)
  find_library(FLINT_LIBRARY flint REQUIRED)
  find_library(GMP_LIBRARY gmp REQUIRED)
  add_executable(dcstudy_z3
    src/z3/z3_helper_main.cpp
    src/z3/synthesis_z3.cpp
    src/z3/switchlevel_z3.cpp
    src/z3/math_engine.cpp
    src/z3/symmetry.cpp
    src/z3/z3_protocol.cpp
  )
  set_target_properties(dcstudy_z3 PROPERTIES OUTPUT_NAME dcstudy-z3)
  target_include_directories(dcstudy_z3 PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/include
    /usr/include/suitesparse)
  target_link_libraries(dcstudy_z3 PRIVATE
    z3::libz3
    ${BLISS_LIBRARY}
    ${GRAPHBLAS_LIBRARY}
    ${FLINT_LIBRARY}
    ${GMP_LIBRARY})
  if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    target_compile_options(dcstudy_z3 PRIVATE -Wno-c99-extensions)
  endif()
endif()

enable_testing()
find_package(Catch2 3 QUIET)
if(Catch2_FOUND)
  file(GLOB DCSTUDY_UNIT_SOURCES CONFIGURE_DEPENDS
       ${CMAKE_CURRENT_SOURCE_DIR}/tests/unit/*_test.cpp)
  add_executable(dcstudy_tests ${DCSTUDY_UNIT_SOURCES})
  target_link_libraries(dcstudy_tests PRIVATE dcstudy_core Catch2::Catch2WithMain)
  # One ctest entry per module: tags in tests/unit/<module>_test.cpp select it.
  set(DCSTUDY_UNIT_MODULES core switchlevel cdl infra abc)
  foreach(mod ${DCSTUDY_UNIT_MODULES})
    add_test(NAME unit_${mod} COMMAND dcstudy_tests "[${mod}]")
    if(DCSTUDY_BUILD_Z3_HELPER)
      set_tests_properties(unit_${mod} PROPERTIES
        ENVIRONMENT "DCSTUDY_Z3_HELPER=$<TARGET_FILE:dcstudy_z3>")
    endif()
  endforeach()
else()
  # Missing dependency must not look like "all tests passed".
  add_test(NAME unit_missing_catch2
    COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/require_catch2.cmake)
endif()
