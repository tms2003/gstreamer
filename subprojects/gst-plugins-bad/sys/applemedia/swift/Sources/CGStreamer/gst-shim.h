// #include <gst/gst.h>
// #include <gst/base/base.h>
// #include <gst/audio/audio.h>

/* We currently have no way to pass include dirs to swiftc, so ugly relative paths it is...
 * Maybe we can improve this once we improve Meson support for Swift. */
#include <gst/gst.h>
#include <gst/base/base.h>
#include <gst/audio/audio.h>

/* Needs to be a hardcoded relative path because this is not public API */
#include "../../../coremediabuffer.h"
