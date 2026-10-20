cmake_minimum_required(VERSION 3.28...3.30)

include("${CMAKE_CURRENT_SOURCE_DIR}/cmake/common/bootstrap.cmake" NO_POLICY_SCOPE)

project(${_name} VERSION ${_version})

option(ENABLE_FRONTEND_API "Use obs-frontend-api for UI functionality" OFF)
option(ENABLE_QT "Use Qt functionality" OFF)

include(compilerconfig)
include(defaults)
include(helpers)

add_library(${CMAKE_PROJECT_NAME} MODULE)

find_package(libobs REQUIRED)
target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::libobs)

if(ENABLE_FRONTEND_API)
  find_package(obs-frontend-api REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE OBS::obs-frontend-api)
endif()

if(ENABLE_QT)
  find_package(Qt6 COMPONENTS Widgets Core Gui REQUIRED)
  target_link_libraries(${CMAKE_PROJECT_NAME} PRIVATE Qt6::Core Qt6::Gui Qt6::Widgets)
  target_compile_options(
    ${CMAKE_PROJECT_NAME}
    PRIVATE $<$<C_COMPILER_ID:Clang,AppleClang>:-Wno-quoted-include-in-framework-header -Wno-comma>
  )
  set_target_properties(
    ${CMAKE_PROJECT_NAME}
    PROPERTIES AUTOMOC ON AUTOUIC ON AUTORCC ON
  )
endif()

target_sources(
  ${CMAKE_PROJECT_NAME}
  PRIVATE src/plugin.cpp src/multiview_window.cpp src/multiview_window.hpp
    src/obs_state.cpp src/obs_state.hpp src/click_target.cpp src/click_target.hpp src/layout_model.cpp
    src/layout_model.hpp src/tile_assignment.cpp src/tile_assignment.hpp src/scene_ref.hpp src/multiview_renderer.cpp src/multiview_renderer.hpp src/preset_picker.cpp src/preset_picker.hpp
    src/label_metrics.cpp src/label_metrics.hpp src/label_cache.cpp src/label_cache.hpp
    src/safe_areas.cpp src/safe_areas.hpp src/safe_area_geometry.cpp src/safe_area_geometry.hpp
    src/status_format.cpp src/status_format.hpp src/status_tiles.cpp src/status_tiles.hpp
    src/status_monitor.cpp src/status_monitor.hpp
    src/audio_meter_math.cpp src/audio_meter_math.hpp src/audio_meter.cpp src/audio_meter.hpp
    src/meter_scale.cpp src/meter_scale.hpp
    src/mixer_columns.cpp src/mixer_columns.hpp src/mixer_feeds.cpp src/mixer_feeds.hpp
    src/settings.cpp src/settings.hpp src/settings_store.cpp src/settings_store.hpp
    src/appearance.cpp src/appearance.hpp src/settings_dialog.cpp src/settings_dialog.hpp
)
target_compile_features(${CMAKE_PROJECT_NAME} PRIVATE cxx_std_17)

set_target_properties_plugin(${CMAKE_PROJECT_NAME} PROPERTIES OUTPUT_NAME ${_name})

# Unit tests: pure modules only, no OBS required.
option(ENABLE_TESTS "Build unit tests" ON)
if(ENABLE_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()
