#include "Pomme.h"
#include "PommeSound.h"
#include "soundogg.h"
#include <cstdlib>
#include <vector>

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wshadow"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#pragma GCC diagnostic ignored "-Wunused-function"
#pragma GCC diagnostic ignored "-Wsign-compare"
#pragma GCC diagnostic ignored "-Wmissing-field-initializers"
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"
#pragma GCC diagnostic ignored "-Wstrict-aliasing"
#if defined(__clang__)
#pragma GCC diagnostic ignored "-Wsometimes-uninitialized"
#endif
#endif

#define STB_VORBIS_NO_STDIO
#include "stb_vorbis_impl.h"

#if defined(__clang__) || defined(__GNUC__)
#pragma GCC diagnostic pop
#endif

extern "C" SndListHandle LoadOggFileAsSndResource(short fRefNum)
{
	long fileLen = 0;
	if (GetEOF(fRefNum, &fileLen) != noErr || fileLen <= 0)
		return nullptr;

	if (SetFPos(fRefNum, fsFromStart, 0) != noErr)
		return nullptr;

	std::vector<uint8_t> buf((size_t)fileLen);
	long readBytes = fileLen;
	if (FSRead(fRefNum, &readBytes, (Ptr)buf.data()) != noErr || readBytes <= 0)
		return nullptr;

	int channels = 0;
	int sampleRate = 0;
	short* pcm = nullptr;
	int numFrames = stb_vorbis_decode_memory(buf.data(), (int)readBytes, &channels, &sampleRate, &pcm);
	if (numFrames <= 0 || !pcm || channels <= 0 || sampleRate <= 0)
	{
		std::free(pcm);
		return nullptr;
	}

	Pomme::Sound::SampledSoundInfo info = {};
#if __BIG_ENDIAN__
	info.compressionType = 'twos';
	info.bigEndian = true;
#else
	info.compressionType = 'sowt';
	info.bigEndian = false;
#endif
	info.isCompressed = false;
	info.baseNote = 60;
	info.codecBitDepth = 16;
	info.sampleRate = sampleRate;
	info.nChannels = (int16_t)channels;
	info.nPackets = (uint32_t)numFrames;
	info.decompressedLength = numFrames * channels * (int)sizeof(short);
	info.compressedLength = info.decompressedLength;
	info.dataStart = (char*)pcm;

	SndListHandle handle = info.MakeStandaloneResource();
	std::free(pcm);
	return handle;
}
