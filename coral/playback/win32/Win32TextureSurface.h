/*
 * Copyright (C) 2026 The Coral Project
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

#ifndef CORAL_PLAYBACK_WIN32_WIN32_TEXTURE_SURFACE_H
#define CORAL_PLAYBACK_WIN32_WIN32_TEXTURE_SURFACE_H

#if defined(_WIN32)

#include <codec/MediaCodec.h>

#include <effect/platform/win/WindowsD3D11EncoderOutputNode.h>

#include <cstdint>
#include <memory>

namespace coral {

/**
 * The Windows counterpart of {@code AppleIOSurface}: a player output backed by shared D3D11
 * textures instead of a window swapchain.
 *
 * <p>CoralUI samples external textures, so a player that draws directly into an {@code HWND} cannot
 * be composited by it. This surface owns no pixels itself: the application supplies its D3D11
 * device, and {@link WindowsTextureOutputNodeFactory} creates a
 * {@link WindowsD3D11EncoderOutputNode} whose ring of shared textures the player renders into.
 * {@link Win32VideoFrameSource} imports the current ring texture into the UI device.
 *
 * <p>The D3D11 device is borrowed and must outlive the surface and the player. Call
 * {@link currentTexture()} only after the factory has created the output node.
 */
class Win32TextureSurface final : public VideoSurface {
public:
  /**
   * @param d3d11Device an {@code ID3D11Device*} shared with the output node and the UI.
   * @param width       output width in pixels.
   * @param height      output height in pixels.
   */
  static std::shared_ptr<Win32TextureSurface> create(void* d3d11Device, uint32_t width,
                                                     uint32_t height);

  ~Win32TextureSurface() override;

  /** The borrowed D3D11 device, as an opaque handle. */
  [[nodiscard]] void* getInstance() const override;

  [[nodiscard]] bool isValid() const override;

  /** Does nothing: the device is owned by the application. */
  void release() override;

  /** Does nothing: the surface size is fixed when the device ring is created. */
  void setDefaultBufferSize(int width, int height) override;

  [[nodiscard]] void* device() const noexcept {
    return mDevice;
  }

  [[nodiscard]] uint32_t width() const noexcept {
    return mWidth;
  }

  [[nodiscard]] uint32_t height() const noexcept {
    return mHeight;
  }

  /** Called by WindowsTextureOutputNodeFactory after it creates the output node. */
  void setOutputNode(std::shared_ptr<WindowsD3D11EncoderOutputNode> node);

  [[nodiscard]] const std::shared_ptr<WindowsD3D11EncoderOutputNode>& outputNode() const noexcept {
    return mOutputNode;
  }

  /** The texture of the ring slot the player wrote last, or null before the first frame. */
  [[nodiscard]] ID3D11Texture2D* currentTexture() const noexcept;

  /** The index of {@link currentTexture()} in the output node's ring. */
  [[nodiscard]] uint32_t currentTextureIndex() const noexcept;

private:
  Win32TextureSurface(void* device, uint32_t width, uint32_t height) noexcept;

  void* mDevice = nullptr;
  uint32_t mWidth = 0;
  uint32_t mHeight = 0;
  std::shared_ptr<WindowsD3D11EncoderOutputNode> mOutputNode;
};

} // namespace coral

#endif // defined(_WIN32)

#endif // CORAL_PLAYBACK_WIN32_WIN32_TEXTURE_SURFACE_H
