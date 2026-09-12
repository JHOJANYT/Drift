cmake_minimum_required(VERSION 3.21)
project(HelloDriftPlugin LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)

find_package(Qt6 REQUIRED COMPONENTS Core Qml)

# DRIFT_SRC_DIR must point at the `src/` folder of your Drift checkout, so this plugin can see
# engine/DriftPluginInterface.h — the same header the host app was built against. If the ABI
# version there does not match the running app, the plugin refuses to load (by design).
if(NOT DRIFT_SRC_DIR)
    message(FATAL_ERROR "Pass -DDRIFT_SRC_DIR=/path/to/Drift/src")
endif()

add_library(hello_plugin SHARED HelloPlugin.h hello_plugin.cpp)
target_include_directories(hello_plugin PRIVATE ${DRIFT_SRC_DIR})
target_link_libraries(hello_plugin PRIVATE Qt6::Core Qt6::Qml)

# Output name must match the platform stem NativePluginManager looks for.
if(WIN32)
    set_target_properties(hello_plugin PROPERTIES OUTPUT_NAME "plugin-win")
elseif(APPLE)
    set_target_properties(hello_plugin PROPERTIES OUTPUT_NAME "plugin-macos")
else()
    set_target_properties(hello_plugin PROPERTIES OUTPUT_NAME "plugin-linux")
endif()

# Install (manually, for now — no packaging step yet):
#   <AppDataLocation>/plugins/native/com.example.hello/plugin-linux.so
