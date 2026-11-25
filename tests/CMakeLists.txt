cmake_minimum_required(VERSION 3.19)
set(TARGET test_playback_source)

# ==================================================================================================
# Tests
# ==================================================================================================
# The progressive source is tested against the media fixtures of the extractor module, which is where
# the builders of the container formats live. They are shared rather than duplicated: a standalone
# checkout carries CoralMedia under external/, and an application tree keeps it beside this repository.
set(EXTRACTOR_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/CoralMedia/modules/extractor/tests")
if (NOT EXISTS "${EXTRACTOR_TEST_DIR}/TestMedia.h")
  set(EXTRACTOR_TEST_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CoralMedia/modules/extractor/tests")
endif ()

# The ruby geometry is measured by a pure function of the subtitle renderer, so the vertical ruby case
# can be checked without rasterising a frame. The private header of CoralSubtitle is not installed, so
# the tests name the checkout directly.
set(CORAL_SUBTITLE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../external/CoralSubtitle/src")
if (NOT EXISTS "${CORAL_SUBTITLE_SRC_DIR}/HorizontalTextLayout.h")
  set(CORAL_SUBTITLE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../../CoralSubtitle/src")
endif ()

# The Lottie fixture and the fonts come with CoralGraphics, which the tests use through the effect
# graph; the checkout is under external/ for a standalone build and beside this repository in an
# application tree. CoralGraphics keeps its resources at the repository root as a standalone project
# and under modules/graphics in the older module layout, so both are probed.
set(CORAL_GRAPHICS_RESOURCE_DIR "")
foreach (_coral_graphics_root
    "${CMAKE_CURRENT_SOURCE_DIR}/../external/CoralGraphics"
    "${CMAKE_CURRENT_SOURCE_DIR}/../../CoralGraphics")
  foreach (_coral_graphics_resources IN ITEMS "resources" "modules/graphics/resources")
    if (EXISTS "${_coral_graphics_root}/${_coral_graphics_resources}/apitest/lottie/gear.json")
      set(CORAL_GRAPHICS_RESOURCE_DIR "${_coral_graphics_root}/${_coral_graphics_resources}")
      break()
    endif ()
  endforeach ()
  if (CORAL_GRAPHICS_RESOURCE_DIR)
    break()
  endif ()
endforeach ()
unset(_coral_graphics_root)
unset(_coral_graphics_resources)

add_executable(${TARGET}
    test_main.cpp
    test_AnalyticsCollector.cpp
    test_AudioPlayback.cpp
    test_AvLivePlayer.cpp
    test_DefaultLoadControl.cpp
    test_DefaultProgressiveMediaExtractor.cpp
    test_MediaSourceList.cpp
    test_MergePlayer.cpp
    test_MergingMediaSource.cpp
    test_MetadataLivePlayer.cpp
    test_MetadataRenderer.cpp
    test_LottieOverlayNode.cpp
    test_TextRenderer.cpp
    test_ThumbnailProvider.cpp
    test_ProgressiveMediaSource.cpp
    test_SilenceMediaSource.cpp
    test_SubtitleTracks.cpp
    test_SubtitleOverlayNode.cpp
    test_UpstreamAndSelection.cpp
    test_VideoEffectGraph.cpp
    test_VideoFrameConverter.cpp
    test_VideoColorInfo.cpp
    test_VideoFrameSink.cpp
    test_VideoLayoutPass.cpp
)

target_include_directories(${TARGET} PRIVATE
    ${EXTRACTOR_TEST_DIR}
    # The playlist that the player drives is an internal type of the library, and the tests of the
    # event dispatching need to build one.
    ${CMAKE_CURRENT_SOURCE_DIR}/../src
    ${CORAL_SUBTITLE_SRC_DIR}
)
target_link_libraries(${TARGET} PRIVATE playback extractor muxer gtest)
target_compile_definitions(${TARGET} PRIVATE CORAL_ASSETS_DIR="${CORAL_TEST_DATA_DIR}")
target_compile_definitions(${TARGET} PRIVATE CORAL_GRAPHICS_RESOURCE_DIR="${CORAL_GRAPHICS_RESOURCE_DIR}")

if (WIN32 AND NOT ANDROID)
  target_sources(${TARGET} PRIVATE test_WindowsOutputBinding.cpp test_LivePlayerEffect.cpp)
  target_link_libraries(${TARGET} PRIVATE effect_win d3d11 dxgi)
endif ()

if (APPLE)
  # The Apple bridge: the IOSurface wrapper, the output factory and the IOSurface output node. The
  # platform headers come from `effect_apple`, which `playback` links publicly.
  target_sources(${TARGET} PRIVATE test_AppleOutputBinding.cpp)
  target_link_libraries(${TARGET} PRIVATE effect_apple "-framework CoreVideo")
endif ()

set_target_properties(${TARGET} PROPERTIES FOLDER Tests)

# The audio path opens a real output device, so it is a separate CTest case. The regular suite
# excludes it, which keeps `check` usable on a build machine with no audio device; `check-audio` runs
# it explicitly.
add_test(NAME ${TARGET}
    COMMAND ${TARGET} --gtest_filter=-AudioPlaybackTest.*)
set_tests_properties(${TARGET} PROPERTIES LABELS playback)

add_test(NAME test_playback_audio
    COMMAND ${TARGET} --gtest_filter=AudioPlaybackTest.*)
set_tests_properties(test_playback_audio PROPERTIES LABELS audio)

# The dependency trees register their own CTest cases even when their targets are EXCLUDE_FROM_ALL and
# are not built here, so a plain `ctest` would report those as "Not Run". These targets run only this
# repository's suites.
add_custom_target(check
    COMMAND ${CMAKE_CTEST_COMMAND} -R "^${TARGET}$" --output-on-failure
    DEPENDS ${TARGET}
    USES_TERMINAL)
add_custom_target(check-audio
    COMMAND ${CMAKE_CTEST_COMMAND} -R "^test_playback_audio$" --output-on-failure
    DEPENDS ${TARGET}
    USES_TERMINAL)
