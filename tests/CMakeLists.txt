# SPDX-License-Identifier: GPL-2.0-or-later
add_executable(
  unit_tests
  smoke_test.cpp
  layout_model_test.cpp
  click_target_test.cpp
  tile_assignment_test.cpp
  label_metrics_test.cpp
  safe_area_geometry_test.cpp
  status_format_test.cpp
  audio_meter_test.cpp
  mixer_columns_test.cpp
  settings_test.cpp
  appearance_test.cpp
  update_logic_test.cpp
  locale_test.cpp
  tile_meters_test.cpp
  collection_gate_test.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/layout_model.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/click_target.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/tile_assignment.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/label_metrics.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/safe_area_geometry.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/status_format.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/audio_meter_math.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/mixer_columns.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/settings.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/appearance.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/update_logic.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/tile_meters.cpp
  ${CMAKE_CURRENT_SOURCE_DIR}/../src/collection_gate.cpp
)
# doctest is third-party: SYSTEM keeps its headers out of the -Werror warning set.
target_include_directories(unit_tests SYSTEM PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/third_party/doctest)
target_include_directories(unit_tests PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/../src)
target_compile_features(unit_tests PRIVATE cxx_std_17)
# locale_test reads data/locale/*.ini and src/ from the source tree.
target_compile_definitions(unit_tests PRIVATE MVPRO_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
add_test(NAME unit_tests COMMAND unit_tests)
add_test(NAME layout_tests COMMAND unit_tests -tc=layout_*)
add_test(NAME state_tests COMMAND unit_tests -tc=state_*)
