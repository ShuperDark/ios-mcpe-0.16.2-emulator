# Minimal header-only Boost package for dynarmic (icl / variant only).
get_filename_component(_BOOST_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../external/boost_1_86_0" ABSOLUTE)
if (NOT TARGET Boost::boost)
  add_library(Boost::boost INTERFACE IMPORTED GLOBAL)
  set_target_properties(Boost::boost PROPERTIES INTERFACE_INCLUDE_DIRECTORIES "${_BOOST_ROOT}")
  add_library(Boost::headers ALIAS Boost::boost)
endif()
set(Boost_FOUND TRUE)
set(Boost_INCLUDE_DIRS "${_BOOST_ROOT}")
