cmake_minimum_required(VERSION 3.25)
project(dcstudy LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE RelWithDebInfo CACHE STRING "" FORCE)
endif()

add_compile_options(-Wall -Wextra -Wpedantic)

option(DCSTUDY_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
option(DCSTUDY_BUILD_Z3_HELPER "Build the out-of-process Z3 worker" ON)
if(DCSTUDY_SANITIZE)
  add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer)
  add_link_options(-fsanitize=address,undefined)
endif()

add_library(dcstudy_core STATIC
  src/boolfunc.cpp
  src/cdl.cpp
  src/bdd_switch.cpp
  src/cmos.cpp
  src/driver_profile.cpp
  src/limits.cpp
  src/partial.cpp
  src/static_exact.cpp
  src/template_synth.cpp
  src/designspace.cpp
  src/math_client.cpp
  src/multi_path.cpp
  src/multistage.cpp
  src/path_search.cpp
  src/switchlevel.cpp
  src/synthesis.cpp
  src/verilog.cpp
  src/verify.cpp
  src/worker_client.cpp
  src/z3_client.cpp
  src/render_client.cpp
  src/z3_protocol.cpp
)
target_include_directories(dcstudy_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)

add_executable(dcstudy src/main.cpp)
target_link_libraries(dcstudy PRIVATE dcstudy_core)

if(DCSTUDY_BUILD_Z3_HELPER)
  find_package(Z3 REQUIRED)
  find_library(BLISS_LIBRARY bliss REQUIRED)
  find_library(GRAPHBLAS_LIBRARY graphblas REQUIRED)
  find_library(FLINT_LIBRARY flint REQUIRED)
  find_library(GMP_LIBRARY gmp REQUIRED)
  add_executable(dcstudy_z3
    src/z3_helper_main.cpp
    src/synthesis_z3.cpp
    src/switchlevel_z3.cpp
    src/math_engine.cpp
    src/symmetry.cpp
    src/z3_protocol.cpp
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
  set(DCSTUDY_UNIT_MODULES core switchlevel cdl infra)
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
