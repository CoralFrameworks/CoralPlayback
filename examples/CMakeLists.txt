# ==================================================================================================
# CoralPlayback examples
# ==================================================================================================
# The framework's standalone demo: `video_player_demo` decodes a video with the playback engine, runs
# every frame through the effect graph and presents the result in a Cocoa window.
#
# Two ways to build it:
#
#   standalone, as its own project
#     cmake -S examples -B build-demo -DCMAKE_BUILD_TYPE=Release
#     cmake --build build-demo
#     ./build-demo/video_player_demo assets/video/video720p_5s.mp4
#
#   added by the framework root
#     A top-level configure of the repository adds this directory (see the root CMakeLists), so the
#     standalone build and the repository build share one target. A project that consumes
#     CoralPlayback as a submodule does not get this executable.
#
# The demo needs a media file. It takes one on the command line, and otherwise falls back to
# assets/video/video720p_5s.mp4 in the checkout, which is compiled in as CORAL_ASSETS_DIR. A checkout
# without that directory still builds it; pass a file when running it.
#
# The demo is macOS-specific because the window is Cocoa. The player and the effect graph are not:
# an application on another platform supplies its own VideoOutputNodeFactory and output node (the
# Windows swap chain lives in the library).

set(CORALPLAYBACK_ROOT "${CMAKE_CURRENT_LIST_DIR}/..")

if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  # Standalone. Resolve the dependencies and add the framework the same way the application root does:
  # the dependency graph first, then the framework, then the examples.
  cmake_minimum_required(VERSION 3.19)
  project(CoralPlaybackExamples LANGUAGES CXX)

  # The framework and the Cocoa window both need Objective-C++.
  if (APPLE)
    enable_language(OBJCXX)
  endif ()

  set(IS_HOST_PLATFORM TRUE)
  set(CORAL_BUILD_TESTS OFF CACHE BOOL "Build the CoralPlayback tests" FORCE)
  # The examples link the framework's libraries; they do not build its unit suite. The shared
  # third-party tree still has to be named so that the dependency frameworks find libz and libyuv.
  if (EXISTS "${CORALPLAYBACK_ROOT}/third_party/libz/coral/CMakeLists.txt")
    set(CORAL_SHARED_THIRD_PARTY_DIR "${CORALPLAYBACK_ROOT}/third_party" CACHE PATH
        "Shared libz/libyuv/libgtest tree" FORCE)
  endif ()

  add_subdirectory("${CORALPLAYBACK_ROOT}" "${CMAKE_BINARY_DIR}/coral-playback")

  # The framework's CMakeLists defines this function. Calling it here gives the demo the same C++
  # standard and runtime flags as the libraries it links; the examples project is the parent of the
  # framework in this configure, so it does not inherit them through directory scope.
  coralplayback_apply_build_settings()
endif ()

# The demo plays a file from the assets directory when the command line does not name one, so that
# path is compiled in. A CoralPlayback checkout keeps its fixtures in assets/ at the root of the
# repository; an application tree keeps them in assets/ two directories above it.
if (NOT DEFINED CORAL_TEST_DATA_DIR)
  if (EXISTS "${CORALPLAYBACK_ROOT}/assets")
    set(CORAL_TEST_DATA_DIR "${CORALPLAYBACK_ROOT}/assets")
  elseif (EXISTS "${CORALPLAYBACK_ROOT}/../../assets")
    set(CORAL_TEST_DATA_DIR "${CORALPLAYBACK_ROOT}/../../assets")
  else ()
    set(CORAL_TEST_DATA_DIR "${CORALPLAYBACK_ROOT}/assets")
  endif ()
endif ()

if (APPLE)
  add_executable(video_player_demo video_player_demo.mm)
  target_link_libraries(video_player_demo PRIVATE
      playback
      effect
      graphics
      "-framework Cocoa"
      "-framework IOSurface"
      "-framework QuartzCore")
  target_compile_definitions(video_player_demo PRIVATE CORAL_ASSETS_DIR="${CORAL_TEST_DATA_DIR}")
  target_compile_options(video_player_demo PRIVATE -fobjc-arc -Wall -Wextra -Wno-unused-parameter)
  set_target_properties(video_player_demo PROPERTIES FOLDER Examples)
else ()
  message(STATUS
      "CoralPlayback: video_player_demo is Cocoa-only and is not built on this platform")
endif ()
