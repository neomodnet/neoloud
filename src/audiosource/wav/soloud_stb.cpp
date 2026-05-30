/*
SoLoud audio engine - stb_vorbis interop
Copyright (c) 2013-2018 Jari Komppa
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

#include "soloud_stb.h"

#include "stb_vorbis.h"

namespace SoLoud
{

unsigned int STBDecoder::decodeOggFrames(float *buffer, unsigned int samplesToRead, unsigned int bufferSize, unsigned int channels)
{
	unsigned int totalSamples = 0;

	while (totalSamples < samplesToRead)
	{
		// check if we need a new frame
		if (mFrameOffset >= mFrameSize)
		{
			mFrameSize = stb_vorbis_get_frame_float(vorbis, nullptr, &mOutputs);
			mFrameOffset = 0;

			if (mFrameSize == 0)
			{
				mEnded = true;
				break; // end of stream
			}
			else
			{
				mEnded = false;
			}
		}

		// calculate how many samples we can copy from current frame
		unsigned int samplesInFrame = mFrameSize - mFrameOffset;
		unsigned int samplesToCopy = samplesToRead - totalSamples;
		if (samplesToCopy > samplesInFrame)
			samplesToCopy = samplesInFrame;

		// copy samples with planar layout
		for (unsigned int s = 0; s < samplesToCopy; s++)
		{
			for (unsigned int ch = 0; ch < channels; ch++)
			{
				buffer[ch * bufferSize + totalSamples + s] = mOutputs[ch][mFrameOffset + s];
			}
		}

		totalSamples += samplesToCopy;
		mFrameOffset += samplesToCopy;
	}

	return totalSamples;
}
} // namespace SoLoud
