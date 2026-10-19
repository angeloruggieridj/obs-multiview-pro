add_executable(unit_tests smoke_test.cpp)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest)
target_compile_features(unit_tests PRIVATE cxx_std_17)
add_test(NAME unit_tests COMMAND unit_tests)
