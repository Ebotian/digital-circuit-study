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
  src/cmos.cpp
  src/designspace.cpp
  src/multistage.cpp
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
  add_executable(dcstudy_z3
    src/z3_helper_main.cpp
    src/synthesis_z3.cpp
    src/switchlevel_z3.cpp
    src/symmetry.cpp
    src/z3_protocol.cpp
  )
  set_target_properties(dcstudy_z3 PROPERTIES OUTPUT_NAME dcstudy-z3)
  target_include_directories(dcstudy_z3 PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include)
  target_link_libraries(dcstudy_z3 PRIVATE z3::libz3 ${BLISS_LIBRARY})
endif()

enable_testing()
find_package(Catch2 3 QUIET)
if(Catch2_FOUND AND DCSTUDY_BUILD_Z3_HELPER)
  add_executable(dcstudy_tests tests/unit_tests.cpp)
  target_link_libraries(dcstudy_tests PRIVATE dcstudy_core Catch2::Catch2WithMain)
  add_test(NAME unit COMMAND dcstudy_tests)
  set_tests_properties(unit PROPERTIES
    ENVIRONMENT "DCSTUDY_Z3_HELPER=$<TARGET_FILE:dcstudy_z3>")
endif()
