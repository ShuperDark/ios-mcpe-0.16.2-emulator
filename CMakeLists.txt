# iOS MCPE (v0.16.2) Emulator - top level CMake project
cmake_minimum_required (VERSION 3.20)

if (POLICY CMP0141)
  cmake_policy(SET CMP0141 NEW)
  set(CMAKE_MSVC_DEBUG_INFORMATION_FORMAT "$<IF:$<AND:$<C_COMPILER_ID:MSVC>,$<CXX_COMPILER_ID:MSVC>>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>,$<$<CONFIG:Debug,RelWithDebInfo>:ProgramDatabase>>")
endif()
if (POLICY CMP0167)
  cmake_policy(SET CMP0167 NEW)
endif()

project ("iOS MCPE(v0.16.2) Emulator" C CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# ---- dynarmic (ARM64 -> x86-64 JIT) ----
set(Boost_DIR "${CMAKE_CURRENT_SOURCE_DIR}/cmake/boost" CACHE PATH "" FORCE)
set(DYNARMIC_TESTS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_WARNINGS_AS_ERRORS OFF CACHE BOOL "" FORCE)
set(DYNARMIC_USE_BUNDLED_EXTERNALS ON CACHE BOOL "" FORCE)
set(DYNARMIC_FRONTENDS "A64" CACHE STRING "" FORCE)
set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
# Debug builds (Visual Studio's default "x64 Debug"): the emulator itself stays
# unoptimized and debuggable, but the JIT (dynarmic and its libraries) is compiled with
# optimizations - unoptimized it needs seconds to translate code the game runs for the
# first time (startup 15 s instead of 1 s, world loading 45 s instead of 8 s).
# Mixing optimization levels needs /RTC off and the same iterator debug level everywhere.
if (MSVC)
  foreach(lang C CXX)
    string(REPLACE "/RTC1" "" CMAKE_${lang}_FLAGS_DEBUG "${CMAKE_${lang}_FLAGS_DEBUG}")
  endforeach()
  add_compile_definitions($<$<CONFIG:Debug>:_ITERATOR_DEBUG_LEVEL=0>)
endif()
add_subdirectory(external/dynarmic EXCLUDE_FROM_ALL)
if (MSVC)
  foreach(t dynarmic fmt mcl Zydis Zycore)
    if (TARGET ${t})
      target_compile_options(${t} PRIVATE $<$<CONFIG:Debug>:/O2 /Ob2 /Oi /GS->)
    endif()
  endforeach()
endif()

add_subdirectory ("iOS MCPE(v0.16.2) Emulator")
