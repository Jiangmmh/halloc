# CMake generated Testfile for 
# Source directory: /Users/minghan/minghan/cs/cpp-projs/halloc
# Build directory: /Users/minghan/minghan/cs/cpp-projs/halloc/build-bench
# 
# This file includes the relevant testing commands required for 
# testing this directory and lists subdirectories to be tested as well.
add_test([=[smoke_halloc_unit_tests]=] "/Users/minghan/minghan/cs/cpp-projs/halloc/build-bench/halloc_unit_tests" "LABELS" "unit;smoke")
set_tests_properties([=[smoke_halloc_unit_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/minghan/minghan/cs/cpp-projs/halloc/CMakeLists.txt;49;add_test;/Users/minghan/minghan/cs/cpp-projs/halloc/CMakeLists.txt;0;")
add_test([=[smoke_halloc_stress_tests]=] "/Users/minghan/minghan/cs/cpp-projs/halloc/build-bench/halloc_stress_tests" "LABELS" "stress;smoke")
set_tests_properties([=[smoke_halloc_stress_tests]=] PROPERTIES  _BACKTRACE_TRIPLES "/Users/minghan/minghan/cs/cpp-projs/halloc/CMakeLists.txt;50;add_test;/Users/minghan/minghan/cs/cpp-projs/halloc/CMakeLists.txt;0;")
