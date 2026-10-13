cmake_minimum_required(VERSION 3.20)
project(cae_preprocess_demo VERSION 0.1.0 LANGUAGES CXX)

# Header and shared library must describe the same Gmsh API.
find_path(GMSH_INCLUDE_DIR gmsh.h
  HINTS "${CMAKE_CURRENT_SOURCE_DIR}/.deps/gmsh-4.12.1/include")
find_library(GMSH_LIBRARY NAMES gmsh gmsh.so.4.12 libgmsh.so.4.12)
if(NOT GMSH_INCLUDE_DIR OR NOT GMSH_LIBRARY)
  message(FATAL_ERROR "Gmsh development files missing. See DEPENDENCIES.md; run python3 tools/fetch_gmsh_header.py for the matching 4.12.1 header.")
endif()
add_library(Gmsh::Gmsh UNKNOWN IMPORTED)
set_target_properties(Gmsh::Gmsh PROPERTIES
  IMPORTED_LOCATION "${GMSH_LIBRARY}"
  INTERFACE_INCLUDE_DIRECTORIES "${GMSH_INCLUDE_DIR}")

add_library(cae_geometry src/geometry.cpp)
target_include_directories(cae_geometry PUBLIC include)
target_compile_features(cae_geometry PUBLIC cxx_std_17)
target_link_libraries(cae_geometry PUBLIC Gmsh::Gmsh)
add_executable(geometry_demo src/main.cpp)
target_link_libraries(geometry_demo PRIVATE cae_geometry)
foreach(target cae_geometry geometry_demo)
  target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
endforeach()

include(CTest)
if(BUILD_TESTING)
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  add_test(NAME stage01_geometry_acceptance
    COMMAND "${Python3_EXECUTABLE}" "${CMAKE_CURRENT_SOURCE_DIR}/tests/test_geometry.py"
      --executable "$<TARGET_FILE:geometry_demo>")
  set_tests_properties(stage01_geometry_acceptance PROPERTIES TIMEOUT 120)
endif()
message(STATUS "Gmsh header: ${GMSH_INCLUDE_DIR}")
message(STATUS "Gmsh runtime: ${GMSH_LIBRARY}")
