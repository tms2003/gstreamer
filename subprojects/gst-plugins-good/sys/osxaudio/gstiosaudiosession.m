/*
 * Copyright (C) 2024 Piotr Brzeziński <piotr@centricular.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Library General Public
 * License as published by the Free Software Foundation; either
 * version 2 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Library General Public License for more details.
 *
 * You should have received a copy of the GNU Library General Public
 * License along with this library; if not, write to the
 * Free Software Foundation, Inc., 51 Franklin St, Fifth Floor,
 * Boston, MA 02110-1301, USA.
 */

#include "gstiosaudiosession.h"
#include <AVFAudio/AVFAudio.h>

void 
gst_ios_audio_session_start ()
{
  AVAudioSession *session = [AVAudioSession sharedInstance];

  if ([session category] == AVAudioSessionCategorySoloAmbient) {
    GST_DEBUG ("Setting iOS audio session category to AVAudioSessionCategoryPlayback");
    [session setCategory:AVAudioSessionCategoryPlayback error:NULL];
  } else {
    /* For example, at least on recent iOS versions, the category is automatically set
     * to AVAudioSessionCategoryPlayAndRecord when the mic is used. We don't wanna override that.
     * Users can also want to override this for other reasons. */
    GST_DEBUG ("Not setting iOS audio session category, already found %s", [[session category] UTF8String]);
  }

  [session setActive:YES error:NULL];
}
