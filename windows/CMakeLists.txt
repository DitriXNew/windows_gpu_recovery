# The Flutter tooling requires that developers have a version of Visual Studio
# installed that includes CMake 3.14 or later. You should not increase this
# version, as doing so will cause the plugin to fail to compile for some
# customers of the plugin.
cmake_minimum_required(VERSION 3.14)

# Project-level configuration.
set(PROJECT_NAME "windows_gpu_recovery")
project(${PROJECT_NAME} LANGUAGES CXX)

# Explicitly opt in to modern CMake behaviors to avoid warnings with recent
# versions of CMake.
cmake_policy(VERSION 3.14...3.25)

# This value is used when generating builds using this plugin, so it must
# not be changed.
set(PLUGIN_NAME "windows_gpu_recovery_plugin")

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Any new source files that you add to the plugin should be added here.
list(APPEND PLUGIN_SOURCES
  "windows_gpu_recovery_plugin.cpp"
  "windows_gpu_recovery_plugin.h"
)

# Define the plugin library target. Its name must not be changed (see comment
# on PLUGIN_NAME above).
add_library(${PLUGIN_NAME} SHARED
  "include/windows_gpu_recovery/windows_gpu_recovery_plugin_c_api.h"
  "windows_gpu_recovery_plugin_c_api.cpp"
  ${PLUGIN_SOURCES}
)

# Apply a standard set of build settings that are configured in the
# application-level CMakeLists.txt. This can be removed for plugins that want
# full control over build settings.
apply_standard_settings(${PLUGIN_NAME})

# Disable warnings as errors for this plugin.
if(MSVC)
  target_compile_options(${PLUGIN_NAME} PRIVATE /WX-)
endif()

# Symbols are hidden by default to reduce the chance of accidental conflicts
# between plugins. This should not be removed; any symbols that should be
# exported should be explicitly exported with the FLUTTER_PLUGIN_EXPORT macro.
set_target_properties(${PLUGIN_NAME} PROPERTIES
  CXX_VISIBILITY_PRESET hidden)
target_compile_definitions(${PLUGIN_NAME} PRIVATE FLUTTER_PLUGIN_IMPL)

# Source include directories and library dependencies.
target_include_directories(${PLUGIN_NAME} INTERFACE
  "${CMAKE_CURRENT_SOURCE_DIR}/include")
target_link_libraries(${PLUGIN_NAME} PRIVATE flutter flutter_wrapper_plugin)

# Link D3D11 and DXGI for device-lost detection.
target_link_libraries(${PLUGIN_NAME} PRIVATE
  dxgi
  d3d11
)

# List of absolute paths to libraries that should be bundled with the plugin.
set(windows_gpu_recovery_bundled_libraries
  ""
  PARENT_SCOPE
)
