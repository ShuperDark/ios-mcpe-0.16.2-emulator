# iOS MCPE(v0.16.2) Emulator - host executable
# The target is named "CMakeTarget" because that is the startup item Visual Studio's
# CMake project template registered for this folder (the exe is iOSMCPE-Emulator.exe).

# Guest runtime libraries (arm64 dylibs, built with zig by guest/build.py; incremental)
find_program(EMU_PY_LAUNCHER py)
if (EMU_PY_LAUNCHER)
  set(EMU_PYTHON "${EMU_PY_LAUNCHER}")
else()
  find_package(Python3 REQUIRED COMPONENTS Interpreter)
  set(EMU_PYTHON "${Python3_EXECUTABLE}")
endif()
add_custom_target(guest_libs ALL
    COMMAND "${EMU_PYTHON}" "${CMAKE_SOURCE_DIR}/guest/build.py" --out "${CMAKE_SOURCE_DIR}/out/guest"
    WORKING_DIRECTORY "${CMAKE_SOURCE_DIR}"
    COMMENT "Building guest runtime libraries (zig)"
    USES_TERMINAL)

add_executable(CMakeTarget
    "iOS MCPE(v0.16.2) Emulator.cpp"
    log.cpp
    vmem.cpp
    macho.cpp
    cpu.cpp
    hle.cpp
    vfs.cpp
    gfmt.cpp
    objc_stubs.cpp
    patches.cpp
    hle_libc.cpp
    hle_io.cpp
    hle_pthread.cpp
    hle_mach.cpp
    hle_gl.cpp
    hle_media.cpp
    hle_net.cpp
    window.cpp
    controls.cpp
    profiler.cpp
    hle_crypto.cpp
    hle_mod.cpp
    remote.cpp
)

set_target_properties(CMakeTarget PROPERTIES OUTPUT_NAME "iOSMCPE-Emulator")
target_compile_features(CMakeTarget PRIVATE cxx_std_20)
target_compile_definitions(CMakeTarget PRIVATE _CRT_SECURE_NO_WARNINGS UNICODE _UNICODE)
if (MSVC)
  target_compile_options(CMakeTarget PRIVATE /utf-8 /W3 /permissive- /Zc:preprocessor /bigobj)
  target_link_options(CMakeTarget PRIVATE /STACK:16777216)
  # symbols for the sampling profiler (EMU_PROFILE) also in release builds
  target_compile_options(CMakeTarget PRIVATE $<$<CONFIG:Release>:/Zi>)
  target_link_options(CMakeTarget PRIVATE $<$<CONFIG:Release>:/DEBUG /OPT:REF /OPT:ICF>)
endif()
target_link_libraries(CMakeTarget PRIVATE dynarmic ws2_32 opengl32 winmm ole32 shell32 user32 gdi32 windowscodecs iphlpapi dbghelp bcrypt comdlg32)

# Ship the guest runtime libraries (built by guest/build.py) next to the executable.
# A separate target (not POST_BUILD) so that it also runs when only the guest libraries change.
add_custom_target(copy_guest_libs ALL
    COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:CMakeTarget>/guest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${CMAKE_SOURCE_DIR}/out/guest/libc++.dylib ${CMAKE_SOURCE_DIR}/out/guest/libobjc.dylib
            ${CMAKE_SOURCE_DIR}/out/guest/libz.dylib ${CMAKE_SOURCE_DIR}/out/guest/libdispatch.dylib
            ${CMAKE_SOURCE_DIR}/out/guest/Foundation.dylib ${CMAKE_SOURCE_DIR}/out/guest/UIKit.dylib
            "$<TARGET_FILE_DIR:CMakeTarget>/guest"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different "${CMAKE_CURRENT_SOURCE_DIR}/remote.html" "$<TARGET_FILE_DIR:CMakeTarget>"
    COMMENT "Copying guest runtime libraries and remote.html")
add_dependencies(copy_guest_libs guest_libs)
add_dependencies(CMakeTarget copy_guest_libs)
