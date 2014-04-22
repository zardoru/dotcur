
#ifdef WIN32
#include <Windows.h>
#define ENABLE_SNDFILE_WINDOWS_PROTOTYPES 1
#endif

#include <cstdio>
#include <sndfile.h>
#include "Global.h"
#include "Audio.h"
#include "Audiofile.h"
#include "AudioSourceSFM.h"

AudioSourceSFM::AudioSourceSFM()
{
	mWavFile = NULL;
	info = NULL;
}

AudioSourceSFM::~AudioSourceSFM()
{
	if (mWavFile)
		sf_close(mWavFile);
	if (info)
		delete info;
}

bool AudioSourceSFM::Open(const char* Filename)
{
	info = new SF_INFO;
	info->format = 0;


#ifndef WIN32
	mWavFile = sf_open(Filename, SFM_READ, info);
#else
	mWavFile = sf_wchar_open( Utility::Widen(Filename).c_str(), SFM_READ, info);
#endif

	mRate		= info->samplerate;
	mChannels   = info->channels;

	int err = 0;
	if (!mWavFile || (err = sf_error(mWavFile)))
	{
		wprintf(L"Error %d (wavfile %p)\n", err, mWavFile);
		return false;
	}

	return true;
}

uint32 AudioSourceSFM::Read(void* buffer, size_t count)
{
	uint32 read;
	if (mWavFile)
	{
		read = sf_read_short(mWavFile, (short*)buffer, count);
		int remaining = count - read;

		while (mSourceLoop && (remaining > 0))
		{
			Seek(0);
			read += sf_read_short(mWavFile, (short*)(buffer) + (read), remaining);
			remaining -= read;
		}

	}
	return read;
}

void AudioSourceSFM::Seek(float Time)
{
	if (mWavFile)
		sf_seek(mWavFile, Time * mRate / mChannels, SEEK_SET);
}

size_t AudioSourceSFM::GetLength()
{
	// I'm not sure why- but this is inconsistent.
	return info->frames;
}

uint32 AudioSourceSFM::GetRate()
{
	return mRate;
}

uint32 AudioSourceSFM::GetChannels()
{
	return mChannels;
}

bool AudioSourceSFM::IsValid()
{
	return mWavFile != NULL;
}