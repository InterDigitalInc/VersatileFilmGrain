# minimum required cmake version
cmake_minimum_required( VERSION 3.5 FATAL_ERROR )

# project name
project( vfgs )

set( EXE_NAME vfgs )

# get source files
file( GLOB SRC_FILES  "${CMAKE_SOURCE_DIR}/src/*.c" )

# use ccache
find_program( CCACHE_FOUND ccache )
if( CCACHE_FOUND )
  message( STATUS "ccache found. using it." )
  set_property( GLOBAL PROPERTY RULE_LAUNCH_COMPILE ccache )
  set_property( GLOBAL PROPERTY RULE_LAUNCH_LINK ccache )
endif()

# set default CMAKE_BUILD_TYPE to Release if not set
if( NOT CMAKE_BUILD_TYPE )
  set( CMAKE_BUILD_TYPE "Release" CACHE STRING "Choose the type of build, options are: Debug Release RelWithDebInfo MinSizeRel." FORCE )
endif()

if( CMAKE_SYSTEM_NAME STREQUAL "Linux" )
  if( CMAKE_CXX_COMPILER_ID STREQUAL "GNU" )
    set( USE_ADDRESS_SANITIZER OFF CACHE BOOL "Compiles with -sanitize=address and links to libasan" )
  endif()
endif()

if( CMAKE_COMPILER_IS_GNUCC )
  set( BUILD_STATIC OFF CACHE BOOL "Build static executables" )
endif()

include_directories(${CMAKE_SOURCE_DIR}/src)

# Enable warnings for some generators and toolsets.

if( MSVC )
  # CMake 3.11.0 introduces support for generator expression COMPILE_LANGUAGE.
  # MSVC generator does not support the generator expression COMPILE_LANGUAGE yet.
  #string( APPEND CMAKE_CXX_FLAGS " warnings-as-errors /wd4996" )
  string( APPEND CMAKE_C_FLAGS " /WX /wd4996" )
else()
  add_compile_options( "-Werror" )
endif()


# enable sse4.1 build for all source files for gcc and clang
if( UNIX OR MINGW )
  add_compile_options( "-msse4.1" )
endif()

# enable parallel build for Visual Studio
if( MSVC )
  add_compile_options( "/MP" )
  add_compile_options( "/EHsc" )
endif()

add_executable( ${EXE_NAME} ${SRC_FILES})

