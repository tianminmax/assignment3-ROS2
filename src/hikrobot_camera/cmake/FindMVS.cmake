# - FindMVS ---------------------------------------------------------------
# Locates the HIKROBOT Machine Vision Camera SDK (MvCameraControl).
#
# The SDK ships as a vendor tarball, it is *not* a rosdep dependency. Point
# CMake at it with -DMVS_ROOT_DIR=/path/to/MVS, or make sure MVCAM_SDK_PATH is
# exported (the MVS installer does that by default).
#
# Provides the imported-style variables:
#   MVS_FOUND
#   MVS_INCLUDE_DIRS
#   MVS_LIBRARIES
#   MVS_LIBRARY_DIRS   (handy for BUILD_RPATH / INSTALL_RPATH)
# -------------------------------------------------------------------------

set(MVS_HINT_PATHS "")
if(DEFINED MVS_ROOT_DIR)
  list(APPEND MVS_HINT_PATHS "${MVS_ROOT_DIR}")
endif()
if(DEFINED ENV{MVCAM_SDK_PATH})
  list(APPEND MVS_HINT_PATHS "$ENV{MVCAM_SDK_PATH}")
endif()
list(APPEND MVS_HINT_PATHS /opt/MVS /usr/local/MVS)

if(CMAKE_SIZEOF_VOID_P EQUAL 8)
  set(MVS_LIB_SUFFIXES lib/64 lib/x64 lib)
else()
  set(MVS_LIB_SUFFIXES lib/32 lib/x86 lib)
endif()

find_path(MVS_INCLUDE_DIR
  NAMES MvCameraControl.h
  HINTS ${MVS_HINT_PATHS}
  PATH_SUFFIXES include inc Include
)

find_library(MVS_LIBRARY
  NAMES MvCameraControl
  HINTS ${MVS_HINT_PATHS}
  PATH_SUFFIXES ${MVS_LIB_SUFFIXES}
)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(MVS
  REQUIRED_VARS MVS_INCLUDE_DIR MVS_LIBRARY
)

if(MVS_FOUND)
  set(MVS_INCLUDE_DIRS "${MVS_INCLUDE_DIR}")
  set(MVS_LIBRARIES "${MVS_LIBRARY}")
  get_filename_component(MVS_LIBRARY_DIRS "${MVS_LIBRARY}" DIRECTORY)
  mark_as_advanced(MVS_INCLUDE_DIR MVS_LIBRARY)
endif()
