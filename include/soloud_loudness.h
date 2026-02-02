/*
SoLoud audio engine - LUFS calculation utility
Copyright (c) 2013-2020 Jari Komppa
Copyright (c) 2025-2026 William Horvath

This software is provided 'as-is', without any express or implied
warranty. In no event will the authors be held liable for any damages
arising from the use of this software.

Permission is granted to anyone to use this software for any purpose,
including commercial applications, and to alter it and redistribute it
freely, subject to the following restrictions:

   1. The origin of this software must not be misrepresented; you must not
   claim that you wrote the original software. If you use this software
   in a product, an acknowledgment in the product documentation would be
   appreciated but is not required.

   2. Altered source versions must be plainly marked as such, and must not be
   misrepresented as being the original software.

   3. This notice may not be removed or altered from any source
   distribution.
*/

#ifndef SOLOUD_LOUDNESS_H
#define SOLOUD_LOUDNESS_H

#include "soloud_config.h"

namespace SoLoud
{
class AudioSource;

namespace Loudness
{
// Compute integrated loudness (LUFS) per ITU-R BS.1770-4.
// Reads all samples from the source (creates a temporary instance internally).
// On success, writes the integrated loudness in LUFS to aLoudness.
// Returns -HUGE_VAL via aLoudness for silent/near-silent audio.
result integratedLoudness(AudioSource &aSource, float &aLoudness);
} // namespace Loudness
} // namespace SoLoud

#endif
