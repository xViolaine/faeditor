#pragma once

#include <RtMidi.h>

namespace faeditor {

/** RtMidi backend for this platform: CoreMIDI on Apple, the default (Windows MM / ALSA) elsewhere. */
inline RtMidi::Api midiApi()
{
#if defined(__APPLE__)
    return RtMidi::MACOSX_CORE;
#else
    return RtMidi::UNSPECIFIED;
#endif
}

} // namespace faeditor
