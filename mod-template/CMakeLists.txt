# Example mod for the iOS MCPE (v0.16.2) Emulator.
# Builds an arm64 dylib and copies it to the emulator's mods folder.
cmake_minimum_required(VERSION 3.20)
project(ExampleMod C CXX)

set(CMAKE_CXX_STANDARD 20)

add_library(example_mod SHARED src/example_mod.cpp)
target_include_directories(example_mod PRIVATE include)
set_target_properties(example_mod PROPERTIES PREFIX "" SUFFIX ".dylib")

# Emulator data folder (the mod goes to <data>/mods). Change it if you start the
# emulator with --data.
set(MCPE_DATA_DIR "$ENV{LOCALAPPDATA}/iOS-MCPE-0.16.2" CACHE PATH "iOS MCPE emulator data folder")
add_custom_command(TARGET example_mod POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${MCPE_DATA_DIR}/mods"
    COMMAND ${CMAKE_COMMAND} -E copy "$<TARGET_FILE:example_mod>" "${MCPE_DATA_DIR}/mods/"
    COMMENT "Installing the mod into ${MCPE_DATA_DIR}/mods")
