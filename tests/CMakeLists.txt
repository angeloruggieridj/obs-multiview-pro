add_executable(
  unit_tests
  smoke_test.cpp
  layout_model_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/layout_model.cpp
)
target_include_directories(
  unit_tests
  PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest ${CMAKE_CURRENT_SOURCE_DIR}/../src
)
target_compile_features(unit_tests PRIVATE cxx_std_17)
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME layout_tests COMMAND unit_tests -tc=layout_*)
