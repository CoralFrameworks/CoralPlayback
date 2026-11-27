# ==================================================================================================
# CoralPlayback
# ==================================================================================================
# The player: media sources, renderers, the playback engine and the media/effect adapter. It depends
# on CoralCore, CoralAudio, CoralMedia, CoralGraphics (with effect enabled) and CoralSubtitle; it does
# not depend on CoralUI or CoralCompose.
#
# The repository is both a standalone project and the source of the playback library an application
# links. The public headers live in coral/ at this root, the implementation in src/, and the tests in
# tests/. The standalone demos live in examples/, a separate project that this root adds when it is
# the top-level project.
#
# As a subproject the application has already added those frameworks. A standalone checkout resolves
# the sibling frameworks and the shared third-party tree through scripts/bootstrap.sh (or
# scripts/bootstrap.ps1), which clones the repositories declared in .gitmodules.

cmake_minimum_required(VERSION 3.19)
project(CoralPlayback LANGUAGES CXX)

# CoralCore, CoralMedia and CoralSubtitle contain Objective-C++ platform code on Apple. The language
# has to be enabled here, before the dependencies are added, so a subproject's enable_language(OBJCXX)
# does not end up in a child directory scope only.
if (APPLE)
  enable_language(OBJCXX)
endif ()

# ==================================================================================================
# Build settings
# ==================================================================================================
# These settings apply to CoralPlayback and, through directory scope, to the dependencies added below.
# The standalone examples project is the exception: it is the parent directory there, so it calls
# `coralplayback_apply_build_settings()` after adding this repository.
#
# Keep the block free of paths and third-party discovery: those are the concerns of the sections
# below, while this part is the C++ standard, the MSVC runtime and the warning baseline.
function(coralplayback_apply_build_settings)
  option(USE_STATIC_CRT "Link against the static runtime libraries." ON)

  if (MSVC)
    if (USE_STATIC_CRT)
      add_compile_options(
          $<$<CONFIG:>:/MT>
          $<$<CONFIG:Debug>:/MTd>
          $<$<CONFIG:Release>:/MT>
      )
    else ()
      add_compile_options(
          $<$<CONFIG:>:/MD>
          $<$<CONFIG:Debug>:/MDd>
          $<$<CONFIG:Release>:/MD>
      )
    endif ()
  endif ()

  # Windows: generate the .def that makes a DLL import library. CL.EXE does not produce one from
  # __declspec(dllexport) alone in every configuration.
  if (WIN32)
    set(CMAKE_WINDOWS_EXPORT_ALL_SYMBOLS ON PARENT_SCOPE)
  endif ()

  set(CMAKE_CXX_STANDARD 20 PARENT_SCOPE)
  set(CMAKE_CXX_STANDARD_REQUIRED ON PARENT_SCOPE)

  if (MSVC)
    # The project builds with C++20 (set above); keep the historical /W0 and the __cplusplus value
    # that libraries such as nlohmann/json check.
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} /W0 /Zc:__cplusplus"
        PARENT_SCOPE)
  else ()
    set(CMAKE_CXX_FLAGS
        "${CMAKE_CXX_FLAGS} -fstrict-aliasing -Wno-unknown-pragmas -Wno-unused-function -Wno-deprecated-declarations"
        PARENT_SCOPE)
    if (APPLE)
      set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -Wno-nullability-extension" PARENT_SCOPE)
      set(CMAKE_OBJCXX_FLAGS "${CMAKE_CXX_FLAGS}" PARENT_SCOPE)
    endif ()
  endif ()

  if (WIN32)
    set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -D_USE_MATH_DEFINES=1" PARENT_SCOPE)
  endif ()
endfunction()

coralplayback_apply_build_settings()

# `playback` is one target; this switch decides whether it is libcoral-playback.a or
# libcoral-playback.so (libcoral-playback.dylib on macOS, coral-playback.dll on Windows). A shared
# CoralPlayback links the framework's static dependencies, so they have to be position independent
# as well; the setting is applied before they are added.
option(BUILD_SHARED_LIBS "Build the CoralPlayback library as a shared library" OFF)
if (BUILD_SHARED_LIBS)
  set(CMAKE_POSITION_INDEPENDENT_CODE ON)
endif ()

# Indicates whether CoralPlayback is a standalone project.
if (CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  set(CORAL_PLAYBACK_TOP_LEVEL ON)
else ()
  set(CORAL_PLAYBACK_TOP_LEVEL OFF)
endif ()

if (CORAL_PLAYBACK_TOP_LEVEL)
  if (NOT DEFINED IS_HOST_PLATFORM)
    set(IS_HOST_PLATFORM TRUE)
  endif ()
  # The dependency frameworks register their own CTest suites when CMake's BUILD_TESTING switch is on.
  # This repository builds only its own suite, so the switch is turned off before they are added;
  # `enable_testing()` below still registers the playback test. A vendored library that ignores the
  # switch (libjpeg-turbo and shaderc do) still adds its cases; the `check` target and the documented
  # `-R` filter run only this repository's suite.
  set(BUILD_TESTING OFF CACHE BOOL "Build the dependency test suites" FORCE)
  if (NOT DEFINED CORAL_BUILD_TESTS)
    set(CORAL_BUILD_TESTS ON CACHE BOOL "Build the CoralPlayback tests")
  endif ()
else ()
  if (NOT DEFINED CORAL_BUILD_TESTS)
    set(CORAL_BUILD_TESTS OFF CACHE BOOL "Build the CoralPlayback tests")
  endif ()
endif ()
# The public switch wins: a caller's -DCORAL_BUILD_TESTS=OFF disables the suite even in a standalone
# configure. The module-level name mirrors it and is forced so that a changed public switch takes
# effect on the next configure rather than being masked by the old cache entry.
set(CORAL_PLAYBACK_BUILD_TESTS ${CORAL_BUILD_TESTS} CACHE BOOL "Build the CoralPlayback tests" FORCE)
set(CORAL_BUILD_TESTS ${CORAL_PLAYBACK_BUILD_TESTS})
if (CORAL_BUILD_TESTS)
  enable_testing()
endif ()

if (CORAL_PLAYBACK_TOP_LEVEL)
  # Resolve the shared third-party tree. There are three layouts, and the first candidate that has the
  # wrapper this configure needs wins:
  #
  #   third_party/                      a standalone CoralPlayback checkout (libgtest, libz and libyuv
  #                                     are this repository's own submodules, which CLion and a fresh
  #                                     clone see first)
  #   ../../third_party                 the application tree, two levels above frameworks/CoralPlayback
  #   external/Coral*/third_party       the copies carried by the dependency submodules
  if (NOT DEFINED CORAL_SHARED_THIRD_PARTY_DIR)
    foreach (_coral_third_party_candidate
        "${CMAKE_CURRENT_SOURCE_DIR}/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/../../third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralMedia/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralCore/third_party"
        "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralGraphics/third_party")
      if (CORAL_BUILD_TESTS)
        set(_coral_third_party_probe "${_coral_third_party_candidate}/libgtest/coral/CMakeLists.txt")
      else ()
        set(_coral_third_party_probe "${_coral_third_party_candidate}/libz/coral/CMakeLists.txt")
      endif ()
      if (EXISTS "${_coral_third_party_probe}")
        set(CORAL_SHARED_THIRD_PARTY_DIR "${_coral_third_party_candidate}")
        break()
      endif ()
    endforeach ()
  endif ()
  if (DEFINED CORAL_SHARED_THIRD_PARTY_DIR)
    message(STATUS "CoralPlayback third-party tree: ${CORAL_SHARED_THIRD_PARTY_DIR}")
  endif ()
  # GoogleTest is a test-only dependency: a checkout or IDE profile that did not initialize it still
  # has to be able to configure and build the library, so the tests are disabled with a warning
  # instead of stopping the configure. The cache value keeps the next configure trying again.
  if (CORAL_BUILD_TESTS AND NOT TARGET gtest)
    if (DEFINED CORAL_SHARED_THIRD_PARTY_DIR AND
        EXISTS "${CORAL_SHARED_THIRD_PARTY_DIR}/libgtest/coral/CMakeLists.txt")
      add_subdirectory("${CORAL_SHARED_THIRD_PARTY_DIR}/libgtest/coral" "${CMAKE_BINARY_DIR}/gtest")
    else ()
      message(WARNING
          "GoogleTest was not found, so the CoralPlayback tests are disabled. Initialize the "
          "submodule with\n"
          "  git -c submodule.third_party/libgtest.update=checkout submodule update --init "
          "third_party/libgtest\n"
          "pass -DCORAL_SHARED_THIRD_PARTY_DIR=<third_party>, or configure with "
          "-DCORAL_BUILD_TESTS=OFF.")
      set(CORAL_PLAYBACK_BUILD_TESTS OFF)
      set(CORAL_BUILD_TESTS OFF)
    endif ()
  endif ()
  # The fixtures of a CoralPlayback checkout live in assets/ at the root of the repository; an
  # application tree that carries CoralPlayback under frameworks/ keeps them in assets/ two directories
  # above it. The suites skip a fixture that is not present, so neither layout is required.
  if (NOT DEFINED CORAL_TEST_DATA_DIR)
    if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/assets")
      set(CORAL_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/assets")
    elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../../assets")
      set(CORAL_TEST_DATA_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../assets")
    endif ()
  endif ()
  # zlib is required by the CoralMedia codec whether or not the tests are built.
  if (NOT TARGET z AND DEFINED CORAL_SHARED_THIRD_PARTY_DIR AND
      EXISTS "${CORAL_SHARED_THIRD_PARTY_DIR}/libz/coral/CMakeLists.txt")
    add_subdirectory("${CORAL_SHARED_THIRD_PARTY_DIR}/libz/coral" "${CMAKE_BINARY_DIR}/libz")
  endif ()
  # libyuv converts the decoder's YUV frames for the effect input; the graphics module also links it.
  if (NOT TARGET yuv AND DEFINED CORAL_SHARED_THIRD_PARTY_DIR AND
      EXISTS "${CORAL_SHARED_THIRD_PARTY_DIR}/libyuv/CMakeLists.txt")
    add_subdirectory("${CORAL_SHARED_THIRD_PARTY_DIR}/libyuv" "${CMAKE_BINARY_DIR}/libyuv")
    # libyuv's own CMake lists its include directory at directory scope, which reaches the `yuv`
    # target but not the targets that link it. The effect module includes <libyuv/...> in its own
    # translation units, so the directory has to be part of the target's usage requirements.
    target_include_directories(yuv PUBLIC "${CORAL_SHARED_THIRD_PARTY_DIR}/libyuv/include")
  endif ()
endif ()

# ==================================================================================================
# Dependencies
# ==================================================================================================
# The frameworks derive their own test switches from CORAL_BUILD_TESTS. This repository builds only
# its own suite, so the shared switch is turned off for the add_subdirectory() calls and restored
# afterwards. Minilog defaults its suite on whenever IS_HOST_PLATFORM is set, so it is switched off
# separately. A subproject does not do this: the application's value then applies to every framework
# it adds.
set(_coralplayback_build_tests ${CORAL_BUILD_TESTS})
set(CORAL_BUILD_TESTS OFF)
set(MINILOG_BUILD_TESTS OFF)

# CoralCore provides utils, core, event and minilog.
if (NOT TARGET utils)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralCore/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CoralCore" "${CMAKE_BINARY_DIR}/coral-core")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CoralCore/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CoralCore" "${CMAKE_BINARY_DIR}/coral-core")
  else ()
    message(FATAL_ERROR
        "CoralPlayback needs CoralCore. Run scripts/bootstrap.sh, or add CoralCore before CoralPlayback.")
  endif ()
endif ()

# CoralMedia provides codec, media, datasource, extractor and muxer.
if (NOT TARGET codec OR NOT TARGET extractor)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralMedia/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CoralMedia" "${CMAKE_BINARY_DIR}/coral-media")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CoralMedia/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CoralMedia" "${CMAKE_BINARY_DIR}/coral-media")
  else ()
    message(FATAL_ERROR
        "CoralPlayback needs CoralMedia. Run scripts/bootstrap.sh, or add CoralMedia before CoralPlayback.")
  endif ()
endif ()

# CoralAudio provides audio, which the player's audio renderer links.
if (NOT TARGET audio)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralAudio/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CoralAudio" "${CMAKE_BINARY_DIR}/coral-audio")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CoralAudio/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CoralAudio" "${CMAKE_BINARY_DIR}/coral-audio")
  else ()
    message(FATAL_ERROR
        "CoralPlayback needs CoralAudio. Run scripts/bootstrap.sh, or add CoralAudio before CoralPlayback.")
  endif ()
endif ()

# CoralGraphics provides graphics and, with CORAL_GRAPHICS_WITH_EFFECT=ON, effect. Playback renders
# through the effect graph, so a UI-only build without effect is refused here rather than failing at
# link time.
if (NOT TARGET graphics OR NOT TARGET effect)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralGraphics/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CoralGraphics" "${CMAKE_BINARY_DIR}/coral-graphics")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CoralGraphics/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CoralGraphics" "${CMAKE_BINARY_DIR}/coral-graphics")
  else ()
    message(FATAL_ERROR
        "CoralPlayback needs CoralGraphics. Run scripts/bootstrap.sh, or add CoralGraphics before CoralPlayback.")
  endif ()
endif ()
if (NOT TARGET effect)
  message(FATAL_ERROR "CoralPlayback requires CoralGraphics with CORAL_GRAPHICS_WITH_EFFECT=ON.")
endif ()

# CoralSubtitle provides subtitle, which burns the cues into the video frames.
if (NOT TARGET subtitle)
  if (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/external/CoralSubtitle/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/external/CoralSubtitle" "${CMAKE_BINARY_DIR}/coral-subtitle")
  elseif (EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/../CoralSubtitle/CMakeLists.txt")
    add_subdirectory("${CMAKE_CURRENT_SOURCE_DIR}/../CoralSubtitle" "${CMAKE_BINARY_DIR}/coral-subtitle")
  else ()
    message(FATAL_ERROR
        "CoralPlayback needs CoralSubtitle. Run scripts/bootstrap.sh, or add CoralSubtitle before CoralPlayback.")
  endif ()
endif ()

# Restore this repository's own test switch; see the note above the dependency blocks.
set(CORAL_BUILD_TESTS ${_coralplayback_build_tests})
unset(_coralplayback_build_tests)

# ==================================================================================================
# CoralPlayback library
# ==================================================================================================
# The target is named `playback`, which is what the rest of Coral links; the artifact is named
# `coral-playback` so the installed library is libcoral-playback.a or libcoral-playback.so.
set(CORAL_PLAYBACK_TARGET playback)
set(CORAL_PLAYBACK_PUBLIC_HDR_DIR ${CMAKE_CURRENT_SOURCE_DIR})

set(PUBLIC_HDRS
    coral/playback/AudioRenderStats.h
    coral/playback/ChapterThumbnailProvider.h
    coral/playback/Chapters.h
    coral/playback/DefaultMediaClock.h
    coral/playback/DiskThumbnailProvider.h
    coral/playback/LivePlayer.h
    coral/playback/LoadControl.h
    coral/playback/MediaClock.h
    coral/playback/LottieOverlay.h
    coral/playback/MetadataOutput.h
    coral/playback/PlaybackInfo.h
    coral/playback/Parameters.h
    coral/playback/Player.h
    coral/playback/Renderer.h
    coral/playback/StandaloneClock.h
    coral/playback/TextOutput.h
    coral/playback/Thumbnail.h
    coral/playback/ThumbnailCache.h
    coral/playback/TimedFormatQueue.h
    coral/playback/Tracks.h
    coral/playback/VideoColorInfo.h
    coral/playback/VideoFrameOutput.h
    coral/playback/VideoFrameSink.h
    coral/playback/VideoOutput.h
    coral/playback/Volume.h
    coral/playback/win32/Win32WindowSurface.h
    # selection
    coral/selection/AdaptiveTrackSelection.h
    coral/selection/BaseTrackSelection.h
    coral/selection/Configuration.h
    coral/selection/DefaultParameters.h
    coral/selection/DefaultTrackSelector.h
    coral/selection/FixedTrackSelection.h
    coral/selection/MappingTrackSelector.h
    coral/selection/RandomTrackSelection.h
    coral/selection/SelectionCapabilities.h
    coral/selection/TrackGroup.h
    coral/selection/TrackSelection.h
    coral/selection/TrackSelector.h
    coral/selection/TrackSelectorResult.h
    # source
    coral/analytics/AnalyticsCollector.h
    coral/analytics/AnalyticsListener.h
    coral/source/AbstractConcatenatedTimeline.h
    coral/source/BaseMediaSource.h
    coral/source/ClippingMediaPeriod.h
    coral/source/ClippingMediaSource.h
    coral/source/DefaultProgressiveMediaExtractor.h
    coral/source/EmptySampleStream.h
    coral/source/FormatHolder.h
    coral/source/ForwardingTimeline.h
    coral/source/LoadEventInfo.h
    coral/source/MaskingMediaPeriod.h
    coral/source/MaskingMediaSource.h
    coral/source/MediaItem.h
    coral/source/MediaLoadData.h
    coral/source/MediaPeriod.h
    coral/source/MediaPeriodId.h
    coral/source/MediaPeriodInfo.h
    coral/source/MediaSource.h
    coral/source/MediaSourceEventListener.h
    coral/source/MediaSourceFactory.h
    coral/source/PlatformProgressiveMediaExtractor.h
    coral/source/ProgressiveMediaExtractor.h
    coral/source/ProgressiveMediaPeriod.h
    coral/source/ProgressiveMediaSource.h
    coral/source/RepeatMode.h
    coral/source/SampleDataQueue.h
    coral/source/SampleQueue.h
    coral/source/SampleStream.h
    coral/source/SeekParameters.h
    coral/source/SequenceableLoader.h
    coral/source/ShuffleOrder.h
    coral/source/SilenceMediaPeriod.h
    coral/source/SilenceMediaSource.h
    coral/source/SilenceSampleStream.h
    coral/source/SinglePeriodTimeline.h
    coral/source/Timeline.h
    # upstream
    coral/upstream/Allocation.h
    coral/upstream/Allocator.h
    coral/upstream/BandwidthMeter.h
    coral/upstream/DefaultAllocator.h
    coral/upstream/DefaultBandwidthMeter.h
    coral/upstream/DefaultLoadErrorHandlingPolicy.h
    coral/upstream/LoadErrorHandlingPolicy.h
    coral/upstream/Loader.h
    coral/upstream/SlidingPercentile.h
    coral/upstream/TransferListener.h
    # util
    coral/util/SystemLanguage.h
)

if (ANDROID)
  list(APPEND PUBLIC_HDRS
      coral/playback/android/AndroidJniSurface.h
      coral/playback/android/AndroidSurface.h
      coral/playback/android/AndroidVideoInput.h
      coral/playback/android/AndroidVideoOutputNodeFactory.h
  )
endif ()

if (WIN32 AND NOT ANDROID)
  list(APPEND PUBLIC_HDRS
      coral/playback/win32/Win32TextureSurface.h
      coral/playback/win32/Win32VideoFrameSource.h
      coral/playback/win32/WindowsHardwareVideoSurface.h
      coral/playback/win32/WindowsThumbnailProvider.h
      coral/playback/win32/WindowsTextureOutputNodeFactory.h
      coral/playback/win32/WindowsVideoDevice.h
      coral/playback/win32/WindowsVideoFrameSink.h
      coral/playback/win32/WindowsVideoInput.h
      coral/playback/win32/WindowsVideoOutputNodeFactory.h
  )
endif ()

if (APPLE)
  list(APPEND PUBLIC_HDRS
      coral/playback/apple/AppleHardwareVideoSurface.h
      coral/playback/apple/AppleIOSurface.h
      coral/playback/apple/AppleVideoFrameSource.h
      coral/playback/apple/AppleVideoInput.h
      coral/playback/apple/AppleThumbnailProvider.h
      coral/playback/apple/AppleVideoOutputNodeFactory.h
  )
endif ()

set(SRCS
    src/details/AvLivePlayer.cpp
    src/details/AvLivePlayer.h
    src/details/BaseRenderer.cpp
    src/details/BaseRenderer.h
    src/details/ChapterThumbnailProvider.cpp
    src/details/CpuVideoFrameSink.cpp
    src/details/CpuVideoFrameSink.h
    src/details/DecodeAudioRenderer.cpp
    src/details/DecodeAudioRenderer.h
    src/details/DecoderCounters.h
    src/details/DecodeVideoRenderer.cpp
    src/details/DecodeVideoRenderer.h
    src/details/MetadataRenderer.cpp
    src/details/MetadataRenderer.h
    src/details/DefaultLoadControl.cpp
    src/details/DiskThumbnailProvider.cpp
    src/details/DefaultLoadControl.h
    src/details/SubtitleOverlayNode.cpp
    src/details/SubtitleOverlayNode.h
    src/details/TextRenderer.cpp
    src/details/ThumbnailCache.cpp
    src/details/TextRenderer.h
    src/details/VideoLayoutPass.cpp
    src/details/VideoLayoutPass.h
    src/details/VideoEffectGraph.cpp
    src/details/VideoEffectGraph.h
    src/details/LottieOverlayNode.cpp
    src/details/LottieOverlayNode.h
    src/details/VideoFrameConverter.cpp
    src/details/VideoFrameConverter.h
    src/details/ImageRenderer.cpp
    src/details/ImageRenderer.h
    src/details/MediaPeriodHolder.cpp
    src/details/MediaPeriodHolder.h
    src/details/MediaPeriodQueue.cpp
    src/details/MediaPeriodQueue.h
    src/details/MediaSourceList.cpp
    src/details/MediaSourceList.h
    src/details/PlaylistTimeline.cpp
    src/details/PlaylistTimeline.h
    src/details/TraceUtil.h
    # selection
    src/selection/AdaptiveTrackSelection.cpp
    src/selection/BaseTrackSelection.cpp
    src/selection/DefaultTrackSelector.cpp
    src/selection/MappingTrackSelector.cpp
    src/selection/RandomTrackSelection.cpp
    src/selection/TrackGroup.cpp
    # source
    src/analytics/AnalyticsCollector.cpp
    src/source/AbstractConcatenatedTimeline.cpp
    src/source/BaseMediaSource.cpp
    src/source/ClippingMediaPeriod.cpp
    src/source/ClippingMediaSource.cpp
    src/source/DefaultProgressiveMediaExtractor.cpp
    src/source/EmptySampleStream.cpp
    src/source/MaskingMediaPeriod.cpp
    src/source/MaskingMediaSource.cpp
    src/source/MediaItem.cpp
    src/source/MediaSourceFactory.cpp
    src/source/PlatformProgressiveMediaExtractor.cpp
    src/source/ProgressiveMediaPeriod.cpp
    src/source/ProgressiveMediaSource.cpp
    src/source/MergingMediaPeriod.cpp
    src/source/MergingMediaSourceFactory.cpp
    src/source/MergingMediaSource.cpp
    src/source/SampleDataQueue.cpp
    src/source/SampleQueue.cpp
    src/source/ShuffleOrder.cpp
    src/source/SilenceMediaPeriod.cpp
    src/source/SilenceMediaSource.cpp
    src/source/SilenceSampleStream.cpp
    src/source/SinglePeriodTimeline.cpp
    src/source/Timeline.cpp
    # upstream
    src/upstream/Allocation.cpp
    src/upstream/DefaultAllocator.cpp
    src/upstream/DefaultBandwidthMeter.cpp
    src/upstream/DefaultLoadErrorHandlingPolicy.cpp
    src/upstream/Loader.cpp
    src/upstream/SlidingPercentile.cpp
    # util
    src/util/SystemLanguage.cpp
    # src
    src/DefaultMediaClock.cpp
    src/details/VideoColorInfo.cpp
    src/LivePlayer.cpp
    src/ostream.cpp
    src/TimedFormatQueue.cpp
    src/UidGenerator.h
)

if (WIN32 AND NOT ANDROID)
  list(APPEND SRCS
      src/platform/win32/Win32TextureSurface.cpp
      src/platform/win32/Win32VideoFrameSource.cpp
      src/platform/win32/WindowsHardwareVideoSurface.cpp
      src/platform/win32/WindowsThumbnailProvider.cpp
      src/platform/win32/Win32WindowSurface.cpp
      src/platform/win32/WindowsTextureOutputNodeFactory.cpp
      src/platform/win32/WindowsVideoDevice.cpp
      src/platform/win32/WindowsVideoFrameSink.cpp
      src/platform/win32/WindowsVideoInput.cpp
      src/platform/win32/WindowsVideoOutputNodeFactory.cpp
  )
endif ()

if (ANDROID)
  list(APPEND SRCS
      src/platform/android/AndroidJniSurface.cpp
      src/platform/android/AndroidSurface.cpp
      src/platform/android/AndroidVideoFrameSink.cpp
      src/platform/android/AndroidVideoFrameSink.h
      src/platform/android/AndroidVideoInput.cpp
      src/platform/android/AndroidVideoOutputNodeFactory.cpp
  )
endif ()

if (APPLE)
  list(APPEND SRCS
      src/platform/apple/AppleHardwareVideoSurface.mm
      src/platform/apple/AppleIOSurface.mm
      src/platform/apple/AppleVideoFrameSink.h
      src/platform/apple/AppleThumbnailProvider.mm
      src/platform/apple/AppleVideoFrameSink.mm
      src/platform/apple/AppleVideoFrameSource.mm
    src/platform/apple/AppleVideoInput.mm
      src/platform/apple/AppleVideoOutputNodeFactory.mm
  )
endif ()

# ==================================================================================================
# Includes and target definition
# ==================================================================================================
include_directories(${CORAL_PLAYBACK_PUBLIC_HDR_DIR})
include_directories(src)

add_library(${CORAL_PLAYBACK_TARGET} ${PUBLIC_HDRS} ${SRCS})
set_target_properties(${CORAL_PLAYBACK_TARGET} PROPERTIES
    OUTPUT_NAME coral-playback
    FOLDER Libs)

target_include_directories(${CORAL_PLAYBACK_TARGET} PUBLIC ${CORAL_PLAYBACK_PUBLIC_HDR_DIR})

# ==================================================================================================
# Dependencies
# ==================================================================================================
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC audio)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC codec)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC core)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC datasource)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC effect)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC event)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC extractor)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC graphics)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC media)
# The media engine's registry wraps the built-in codec/extractor factories with the optional FFmpeg
# backend, so the renderer creates decoders through it.
if (TARGET media-registry)
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC media-registry)
endif ()
target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC subtitle)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PRIVATE minilog)
target_link_libraries(${CORAL_PLAYBACK_TARGET} PRIVATE yuv)

if (WIN32 AND NOT ANDROID)
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC effect_win)
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PRIVATE mfplat mfreadwrite mfuuid ole32)
elseif (ANDROID)
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC effect_android)
elseif (APPLE)
  # The Apple bridge builds its output from `AppleIOSurfaceOutputNode` and its zero-copy input from
  # `AppleIOSurfaceInputNode`; both live in CoralGraphics' `effect_apple` target.
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PUBLIC effect_apple)
  target_link_libraries(${CORAL_PLAYBACK_TARGET} PRIVATE
      "-framework AVFoundation"
      "-framework CoreFoundation"
      "-framework CoreGraphics"
      "-framework CoreVideo"
      "-framework IOSurface"
      "-framework VideoToolbox")
endif ()

# ==================================================================================================
# Installation
# ==================================================================================================
# The artifact keeps the `coral-playback` name, while the target stays `playback` for consumers that add
# this repository with add_subdirectory(). A shared build installs the import library next to the static
# libraries and the DLL under bin/.
install(TARGETS ${CORAL_PLAYBACK_TARGET}
    ARCHIVE DESTINATION lib
    LIBRARY DESTINATION lib
    RUNTIME DESTINATION bin)
# The public headers are included as <coral/playback/...>, <coral/source/...> and <coral/selection/...>,
# so `coral/` is what goes under include/.
install(DIRECTORY ${CORAL_PLAYBACK_PUBLIC_HDR_DIR}/coral DESTINATION include)

# ==================================================================================================
# Unit Tests
# ==================================================================================================
if (CORAL_BUILD_TESTS)
  add_subdirectory(tests)
endif ()

# ==================================================================================================
# Examples
# ==================================================================================================
# The demo is a separate project that can be configured on its own (`cmake -S examples ...`); it is
# built as part of this repository only when it is the top-level project. An application that adds
# CoralPlayback as a submodule gets the library it links and no example executables.
if (CORAL_PLAYBACK_TOP_LEVEL)
  add_subdirectory(examples)
endif ()
