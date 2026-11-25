/*
 * Copyright (C) 2024 The Coral Project
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#ifndef CORAL_PLAYBACK_SELECTION_CAPABILITIES_H
#define CORAL_PLAYBACK_SELECTION_CAPABILITIES_H

#include <media/Format.h>

namespace coral {

/**
 * Defines the capabilities of a {@link TrackSelector}.
 */
class SelectionCapabilities {
public:
  virtual ~SelectionCapabilities() = default;

  /**
   * Level of renderer support for a format.
   * One of {@link #FORMAT_HANDLED},
   * {@link #FORMAT_EXCEEDS_CAPABILITIES},
   * {@link #FORMAT_UNSUPPORTED_DRM},
   * {@link #FORMAT_UNSUPPORTED_SUBTYPE}
   * or {@link #FORMAT_UNSUPPORTED_TYPE}.
   */
  enum FormatSupport : int32_t {
    /**
     * The {@link TrackSelector} is capable of rendering the format.
     */
    FORMAT_HANDLED = 0b100,
    /**
     * The {@link TrackSelector} is capable of rendering formats with the same mime
     * type, but the properties of the format exceed the renderer's
     * capabilities. There is a chance the renderer will be able to play the
     * format in practice because some renderers report their capabilities
     * conservatively, but the expected outcome is that playback will fail. <p>
     * Example: The {@link TrackSelector} is capable of rendering H264 and the
     * format's mime type is
     * {@link VideoCodecType::kVideoCodecTypeH264}, but the format's resolution
     * exceeds the maximum limit supported by the underlying H264 decoder.
     */
    FORMAT_EXCEEDS_CAPABILITIES = 0b011,
    /**
     * The {@link TrackSelector} is capable of rendering formats with the same mime
     * type, but is not capable of rendering the format because the format's drm
     * protection is not supported. <p> Example: The {@link TrackSelector} is capable
     * of rendering H264 and the format's mime type is
     * {@link VideoCodecType::kVideoCodecTypeH264}, but the format indicates
     * PlayReady drm protection where-as the renderer only supports Widevine.
     */
    FORMAT_UNSUPPORTED_DRM = 0b010,
    /**
     * The {@link TrackSelector} is a general purpose renderer for formats of the
     * same top-level type, but is not capable of rendering the format or any
     * other format with the same mime type because the sub-type is not
     * supported. <p> Example: The {@link TrackSelector} is a general purpose audio
     * renderer and the format's mime type matches audio/[subtype], but there
     * does not exist a suitable decoder for [subtype].
     */
    FORMAT_UNSUPPORTED_SUBTYPE = 0b001,
    /**
     * The {@link TrackSelector} is not capable of rendering the format, either
     * because it does not support the format's top-level type, or because it's
     * a specialized renderer for a different mime type. <p> Example: The {@link
     * TrackSelector} is a general purpose video renderer, but the format has an
     * audio mime type.
     */
    FORMAT_UNSUPPORTED_TYPE = 0b000,
  };

  /** A mask to apply to {@link Capabilities} to obtain the {@link
   * FormatSupport} only. */
  static constexpr int32_t FORMAT_SUPPORT_MASK = 0b111;

  /**
   * Level of renderer support for adaptive format switches.
   * One of {@link #ADAPTIVE_SEAMLESS},
   * {@link #ADAPTIVE_NOT_SEAMLESS}
   * or {@link #ADAPTIVE_NOT_SUPPORTED}.
   */
  enum AdaptiveSupport : int32_t {
    /**
     * The {@link TrackSelector} can seamlessly adapt between formats.
     */
    ADAPTIVE_SEAMLESS = 0b10000,
    /**
     * The {@link TrackSelector} can adapt between formats, but may suffer a brief
     * discontinuity
     * (~50-100ms) when adaptation occurs.
     */
    ADAPTIVE_NOT_SEAMLESS = 0b01000,
    /**
     * The {@link TrackSelector} does not support adaptation between formats.
     */
    ADAPTIVE_NOT_SUPPORTED = 0b00000,
  };

  /** A mask to apply to {@link Capabilities} to obtain the {@link
   * AdaptiveSupport} only. */
  static constexpr int32_t ADAPTIVE_SUPPORT_MASK = 0b11000;

  /**
   * Level of renderer support for tunneling.
   * One of {@link #TUNNELING_SUPPORTED} or {@link #TUNNELING_NOT_SUPPORTED}.
   */
  enum TunnelingSupport : int32_t {
    /**
     * The {@link TrackSelector} supports tunneled output.
     */
    TUNNELING_SUPPORTED = 0b100000,
    /**
     * The {@link TrackSelector} does not support tunneled output.
     */
    TUNNELING_NOT_SUPPORTED = 0b000000,
  };

  /** A mask to apply to {@link Capabilities} to obtain the {@link
   * TunnelingSupport} only. */
  static constexpr int32_t TUNNELING_SUPPORT_MASK = 0b100000;

  /**
   * Combined renderer capabilities.
   *
   * <p>This is a bitwise OR of {@link FormatSupport}, {@link AdaptiveSupport}
   * and {@link TunnelingSupport}. Use {@link #getFormatSupport(int)}, {@link
   * #getAdaptiveSupport(int)} or
   * {@link #getTunnelingSupport(int)} to obtain the individual flags. And use
   * {@link #create(int)} or {@link #create(int, int, int)} to create the
   * combined capabilities.
   *
   * <p>Possible values:
   *
   * <ul>
   *   <li>{@link FormatSupport}: The level of support for the format itself.
   * One of {@link #FORMAT_HANDLED}, {@link #FORMAT_EXCEEDS_CAPABILITIES},
   * {@link #FORMAT_UNSUPPORTED_DRM},
   *       {@link #FORMAT_UNSUPPORTED_SUBTYPE} and {@link
   * #FORMAT_UNSUPPORTED_TYPE}. <li>{@link AdaptiveSupport}: The level of
   * support for adapting from the format to another format of the same mime
   * type. One of {@link #ADAPTIVE_SEAMLESS}, {@link #ADAPTIVE_NOT_SEAMLESS} and
   * {@link #ADAPTIVE_NOT_SUPPORTED}. Only set if the level of support for the
   * format itself is {@link #FORMAT_HANDLED} or {@link
   *       #FORMAT_EXCEEDS_CAPABILITIES}.
   *   <li>{@link TunnelingSupport}: The level of support for tunneling. One of
   * {@link #TUNNELING_SUPPORTED} and {@link #TUNNELING_NOT_SUPPORTED}. Only set
   * if the level of support for the format itself is {@link #FORMAT_HANDLED} or
   * {@link #FORMAT_EXCEEDS_CAPABILITIES}.
   * </ul>
   */
  enum Capabilities : int32_t {
  };

  /**
   * Returns {@link Capabilities} for the given {@link FormatSupport}.
   *
   * <p>The {@link AdaptiveSupport} is set to {@link #ADAPTIVE_NOT_SUPPORTED}
   * and {{@link TunnelingSupport} is set to {@link #TUNNELING_NOT_SUPPORTED}.
   *
   * @param formatSupport The {@link FormatSupport}.
   * @return The combined {@link Capabilities} of the given {@link
   * FormatSupport}, {@link #ADAPTIVE_NOT_SUPPORTED} and {@link
   * #TUNNELING_NOT_SUPPORTED}.
   */
  static Capabilities create(FormatSupport formatSupport) {
    return create(formatSupport, ADAPTIVE_NOT_SUPPORTED,
        TUNNELING_NOT_SUPPORTED);
  }

  /**
   * Returns {@link Capabilities} combining the given {@link FormatSupport},
   * {@link AdaptiveSupport} and {@link TunnelingSupport}.
   *
   * @param formatSupport The {@link FormatSupport}.
   * @param adaptiveSupport The {@link AdaptiveSupport}.
   * @param tunnelingSupport The {@link TunnelingSupport}.
   * @return The combined {@link Capabilities}.
   */
  static Capabilities create(FormatSupport formatSupport,
      AdaptiveSupport adaptiveSupport,
      TunnelingSupport tunnelingSupport) {
    return static_cast<Capabilities>((int32_t)formatSupport | (int32_t)adaptiveSupport
                                     | (int32_t)tunnelingSupport);
  }

  /**
   * Returns the {@link FormatSupport} from the combined {@link Capabilities}.
   *
   * @param supportFlags The combined {@link Capabilities}.
   * @return The {@link FormatSupport} only.
   */
  static FormatSupport getFormatSupport(Capabilities supportFlags) {
    return static_cast<FormatSupport>(supportFlags & FORMAT_SUPPORT_MASK);
  }

  /**
   * Returns the {@link AdaptiveSupport} from the combined {@link Capabilities}.
   *
   * @param supportFlags The combined {@link Capabilities}.
   * @return The {@link AdaptiveSupport} only.
   */
  static AdaptiveSupport getAdaptiveSupport(Capabilities supportFlags) {
    return static_cast<AdaptiveSupport>(supportFlags & ADAPTIVE_SUPPORT_MASK);
  }

  /**
   * Returns the {@link TunnelingSupport} from the combined {@link
   * Capabilities}.
   *
   * @param supportFlags The combined {@link Capabilities}.
   * @return The {@link TunnelingSupport} only.
   */
  static TunnelingSupport getTunnelingSupport(Capabilities supportFlags) {
    return static_cast<TunnelingSupport>(supportFlags & TUNNELING_SUPPORT_MASK);
  }

  /**
   * Returns string representation of a {@link FormatSupport} flag.
   *
   * @param formatSupport A {@link FormatSupport} flag.
   * @return A string representation of the flag.
   */
  static const char* getFormatSupportString(FormatSupport formatSupport) {
    switch (formatSupport) {
      case FORMAT_HANDLED: {
        return "YES";
      }
      case FORMAT_EXCEEDS_CAPABILITIES: {
        return "NO_EXCEEDS_CAPABILITIES";
      }
      case FORMAT_UNSUPPORTED_DRM: {
        return "NO_UNSUPPORTED_DRM";
      }
      case FORMAT_UNSUPPORTED_SUBTYPE: {
        return "NO_UNSUPPORTED_SUBTYPE";
      }
      case FORMAT_UNSUPPORTED_TYPE: {
        return "NO";
      }
      default: {
        return "";
      }
    }
  }

  /**
   * Returns the name of the track.
   */
  [[nodiscard]] virtual char const* getName() const = 0;

  /**
   * Returns the media type that the track handles.
   */
  [[nodiscard]] virtual MediaType getMediaType() const = 0;

  /**
   * Returns the extent to which the renderer supports a given format.
   *
   * @param format The format.
   * @return The {@link Capabilities} for this format.
   */
  virtual Capabilities supportsFormat(const std::shared_ptr<MediaFormat>& format) = 0;

  /**
   * Returns the extent to which the renderer supports adapting between
   * supported formats that have different MIME types.
   *
   * @return The AdaptiveSupport for adapting between supported formats that
   * have different MIME types.
   */
  virtual AdaptiveSupport supportsMixedMimeTypeAdaptation() {
    return ADAPTIVE_NOT_SUPPORTED;
  }
};

} // namespace coral

#endif // CORAL_PLAYBACK_SELECTION_CAPABILITIES_H
