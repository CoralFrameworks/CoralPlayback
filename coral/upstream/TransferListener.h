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

#ifndef CORAL_PLAYBACK_UPSTREAM_TRANSFER_LISTENER_H
#define CORAL_PLAYBACK_UPSTREAM_TRANSFER_LISTENER_H

#include <string>

namespace coral {

/**
 * A listener of the progress of the transfers a source makes.
 *
 * <p>This is the counterpart of ExoPlayer's {@code TransferListener}, which
 * {@code DefaultBandwidthMeter} implements to derive a bitrate estimate from what the sources
 * actually read.
 *
 * <p>ExoPlayer identifies a transfer by the {@code DataSource} and the {@code DataSpec} it reads.
 * This module reads its media through {@code AVDataSource} of `libs/datasource` and has no data
 * spec, so a transfer is identified by the url it reads and by whether it goes through the network.
 * Only network transfers are measured, exactly like ExoPlayer: a local file says nothing about the
 * bandwidth of the device.
 */
class TransferListener {
public:
  virtual ~TransferListener() = default;

  /**
   * Called when a transfer is being initialized.
   *
   * @param url The url being read.
   * @param isNetwork Whether the transfer goes through the network.
   */
  virtual void onTransferInitializing(const std::string& url, bool isNetwork) {}

  /**
   * Called when a transfer starts.
   *
   * @param url The url being read.
   * @param isNetwork Whether the transfer goes through the network.
   */
  virtual void onTransferStart(const std::string& url, bool isNetwork) {}

  /**
   * Called when bytes were transferred.
   *
   * @param url The url being read.
   * @param isNetwork Whether the transfer goes through the network.
   * @param bytesTransferred The number of bytes that were transferred since the last callback.
   */
  virtual void onBytesTransferred(const std::string& url, bool isNetwork, int bytesTransferred) {}

  /**
   * Called when a transfer ends, whether it completed or failed.
   *
   * @param url The url that was read.
   * @param isNetwork Whether the transfer went through the network.
   */
  virtual void onTransferEnd(const std::string& url, bool isNetwork) {}
};

} // namespace coral

#endif // CORAL_PLAYBACK_UPSTREAM_TRANSFER_LISTENER_H
