/*
 * Copyright (C) 2023 The Coral Project
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

#ifndef CORAL_PLAYBACK_SOURCE_SEQUENCEABLE_LOADER_H
#define CORAL_PLAYBACK_SOURCE_SEQUENCEABLE_LOADER_H

#include <memory>

namespace coral {

/**
 * A loader that can proceed in approximate synchronization with other loaders.
 */
class SequenceableLoader {
public:
  virtual ~SequenceableLoader() = default;

  template<typename T>
  class Callback {
  public:
    virtual ~Callback() = default;

    /**
     * Called by the loader to indicate that it wishes for its {@link
     * #continueLoading(long)} method to be called when it can continue to load
     * data. Called on the playback thread.
     */
    virtual void onContinueLoadingRequested(const std::shared_ptr<T>& period) = 0;
  };

  /**
   * Returns an estimate of the position up to which data is buffered.
   *
   * @return An estimate of the absolute position in microseconds up to which
   * data is buffered, or {@link Definitions::kTimeEndOfSource} if the data is
   * fully buffered.
   */
  virtual int64_t getBufferedPositionUs() = 0;

  /**
   * Returns the next load time, or {@link Definitions::kTimeEndOfSource} if
   * loading has finished.
   */
  virtual int64_t getNextLoadPositionUs() = 0;

  /**
   * Attempts to continue loading.
   *
   * @param positionUs The current playback position in microseconds. If
   * playback of the period to which this loader belongs has not yet started,
   * the value will be the starting position in the period minus the duration of
   * any media in previous periods still to be played.
   * @return True if progress was made, meaning that {@link
   * #getNextLoadPositionUs()} will return a different value than prior to the
   * call. False otherwise.
   */
  virtual bool continueLoading(int64_t positionUs) = 0;

  /**
   * Returns whether the loader is currently loading.
   */
  virtual bool isLoading() = 0;

  /**
   * Re-evaluates the buffer given the playback position.
   *
   * <p>Re-evaluation may discard buffered media or cancel ongoing loads so that
   * media can be re-buffered in a different quality.
   *
   * @param positionUs The current playback position in microseconds. If
   * playback of this period has not yet started, the value will be the starting
   * position in this period minus the duration of any media in previous periods
   * still to be played.
   */
  virtual void reevaluateBuffer(int64_t positionUs) = 0;
};

} // namespace coral

#endif // CORAL_PLAYBACK_SOURCE_SEQUENCEABLE_LOADER_H
