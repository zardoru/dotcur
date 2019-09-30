#include "av-io-common-pch.h"
#include "Audiofile.h"
#include "IMixer.h"
#include "AudioSourceSFM.h"
#include "AudioSourceOGG.h"

#include "pa_ringbuffer.h"

#include "AudioSourceMP3.h"

#include "Utility.h"
#include "rmath.h"

#include <soxr.h>

class AudioStream::AudioStreamInternal
{
    public:
    PaUtilRingBuffer mRingBuf;
    soxr_t			 mResampler;
};

template<class T>
void s16tof32(const T& iterable_start, const T& iterable_end, float* output)
{
    for (auto s = iterable_start; s != iterable_end; ++s)
    {
        if (*s < 0) *output = -float(*s) / std::numeric_limits<short>::min();
        else *output = float(*s) / std::numeric_limits<short>::max();
        output++;
    }
}

// Buffer -> buffer to convert to stereo (interleaved) cnt -> current samples max_len -> Maximum samples
template<class T>
void monoToStereo(T* Buffer, size_t cnt, size_t max_len)
{
    if (!cnt)
        return;

    if (cnt <= max_len / 2) // We're within boundaries.
    {
        // Okay, we have to transform all the samples from 0, 1, 2, 3...
        // to 0, 2, 4, 6... We start at the end so we don't have anything to overwrite.
        // We also need, after that is done, to copy all samples 0, 2, 4, 6 into 1, 3, 5, 7 and so on.
        // So interleave them...
        for (int i = cnt + 1; i >= 0; i--)
        {
            Buffer[i * 2 + 1] = Buffer[i];
            Buffer[i * 2] = Buffer[i];
        }
    }
}

std::unique_ptr<AudioDataSource> SourceFromExt(std::filesystem::path Filename)
{
    std::unique_ptr<AudioDataSource> Ret = nullptr;
    auto ext = Conversion::ToU8(Filename.extension().wstring());
	auto u8fn = Conversion::ToU8(Filename.wstring());
    
    if (u8fn.length() == 0 || ext.length() == 0)
    {
        // Log::Printf("Invalid filename. (%s) (%s)\n", u8fn.c_str(), u8fn.c_str());
        return nullptr;
    }

    Utility::ToLower(ext);

    const char* xt = ext.c_str();
    if (strstr(xt, "wav") || strstr(xt, "flac"))
        Ret = std::make_unique<AudioSourceSFM>();
    else if (strstr(xt, "mp3") || strstr(xt, "ftb")) // az: ftb hax lol
        Ret = std::make_unique<AudioSourceMP3>();
    else if (strstr(xt, "ogg"))
        Ret = std::make_unique<AudioSourceOGG>();

    if (Ret && Ret->Open(Filename))
		return Ret;
    else
    {
        // Log::Printf("extension %s has no audiosource associated\n", ext.c_str());
        return nullptr;
    }
}

void Sound::SetPitch(double Pitch)
{
    mPitch = Pitch;
}

double Sound::GetPitch() const
{
    return mPitch;
}

void Sound::SetLoop(bool Loop)
{
    mIsLooping = Loop;
}

bool Sound::IsLooping() const
{
    return mIsLooping;
}

uint32_t Sound::GetChannels() const
{
    return Channels;
}

AudioSample::AudioSample()
{
    mPitch = 1;
    mIsPlaying = false;
    mIsValid = false;
    mIsLooping = false;
    mOwnerMixer = nullptr;

    mAudioStart = 0;
    mAudioEnd = std::numeric_limits<float>::infinity();
    // MixerAddSample(this);
}

AudioSample::AudioSample(IMixer* owner_mixer) : AudioSample()
{
    mOwnerMixer = owner_mixer;
    mOwnerMixer->AddSample(this);
}

AudioSample::AudioSample(const AudioSample& Other)
{
    mPitch = Other.mPitch;
    mIsValid = (bool)Other.mIsValid;
    mIsLooping = Other.mIsLooping;
	mIsLoaded = (bool)Other.mIsLoaded;
    mOwnerMixer = Other.mOwnerMixer;
    mAudioStart = Other.mAudioStart;
    mAudioEnd = Other.mAudioEnd;
    mRate = Other.mRate;
    mData = Other.mData;
    mCounter = 0;
    Channels = Other.Channels;
    mIsPlaying = false;
    if (mOwnerMixer)
        mOwnerMixer->AddSample(this);
}

AudioSample::AudioSample(AudioSample&& Other)
{
    mPitch = Other.mPitch;
    mIsValid = (bool)Other.mIsValid;
    mIsLooping = Other.mIsLooping;
    mOwnerMixer = Other.mOwnerMixer;

	if (!Other.mIsLoaded)
		Other.mThread.wait();

	mIsLoaded = true;

    mAudioStart = Other.mAudioStart;
    mAudioEnd = Other.mAudioEnd;
    mRate = Other.mRate;
    mData = Other.mData;
    mCounter = 0;
    Channels = Other.Channels;
    mIsPlaying = false;
    if (mOwnerMixer)
        mOwnerMixer->AddSample(this);
}

AudioSample::~AudioSample()
{
    if (mOwnerMixer)
        mOwnerMixer->RemoveSample(this);
}

void AudioSample::Seek(size_t offs)
{
	mCounter = Clamp(offs, (size_t)0, mData->size());
}


std::mutex soxr_lock;
bool AudioSample::Open(AudioDataSource* Src, bool async)
{
    if (Src && Src->IsValid())
    {
		auto fn = [=]() {
			this->Channels = Src->GetChannels();
			size_t mSampleCount = Src->GetLength() * this->Channels;

			if (!mSampleCount) // Huh what why?
				return false;

			this->mData = std::make_shared<std::vector<short>>(mSampleCount);
			size_t total = Src->Read(mData->data(), mSampleCount);

			if (total < mSampleCount) // Oh, odd. Oh well.
				mSampleCount = total;

			this->mRate = Src->GetRate();

			if (this->Channels == 1) // Mono? We'll need to duplicate information for both channels.
			{
				size_t size = mSampleCount * 2;
				auto mDataNew = std::make_shared<std::vector<short>>(size);

				for (size_t i = 0, j = 0; i < mSampleCount; i++, j += 2)
				{
					(*mDataNew)[j] = (*mData)[i];
					(*mDataNew)[j + 1] = (*mData)[i];
				}

				mSampleCount = size;
				this->mData = mDataNew;
				this->Channels = 2;
			}

            double rate = mRate;
            if (mOwnerMixer)
                rate = mOwnerMixer->GetRate();

			if (mRate != rate || mPitch != 1)
			{
				size_t done;
				size_t doneb;
				double DstRate = rate / mPitch;
				double ResamplingRate = DstRate / mRate;
				soxr_io_spec_t spc;
				size_t size = size_t(ceil(mSampleCount * ResamplingRate));
				auto mDataNew = std::make_shared<std::vector<short>>(size);

				spc.e = nullptr;
				spc.itype = SOXR_INT16_I;
				spc.otype = SOXR_INT16_I;
				spc.scale = 1;
				spc.flags = 0;

				soxr_lock.lock();
				soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_QQ, SOXR_VR);

				auto resampler = soxr_create(mRate, rate, 2, nullptr, &spc, &q_spec, nullptr);
				
				soxr_process(resampler, this->mData->data(), mSampleCount / this->Channels, &done,
					mDataNew->data(), size / this->Channels, &doneb);

				soxr_delete(resampler);
				soxr_lock.unlock();

				this->mData = mDataNew;
				this->mRate = rate;
			}

			this->mCounter = 0;
			this->mIsValid = true;

			this->mAudioEnd = (float(this->mData->size()) / (float(this->mRate) * this->Channels));
			this->mIsLoaded = true;

			return true;
		};

		if (async)
			mThread = std::async(std::launch::async, fn);
		else
			fn();


        return true;
    }
    return false;
}

uint32_t AudioSample::Read(float* buffer, size_t count)
{
    size_t limit = (mRate * Channels * mAudioEnd);

    if (!mIsPlaying || !mIsLoaded)
        return 0;


_read:
    if (mIsValid && count && mData->size())
    {
		size_t bufferLeft = limit - mCounter;
        uint32_t ReadAmount = std::min(bufferLeft, count);

        if (mCounter < limit)
        {
            s16tof32(mData->begin() + mCounter, 
					 mData->begin() + mCounter + ReadAmount, 
				     buffer);
            mCounter += ReadAmount;
			count -= ReadAmount;
        }

		if (mCounter == limit || count) {
			if (!mIsLooping)
				mIsPlaying = false;
			else
			{
				SeekTime(mAudioStart);
				buffer += ReadAmount;

				// note: implicit - count gets checked again
				// and it was already updated
				// basically call itself again. a goto is less expensive than a recursive call
				// and less likely to overflow (impossible, probably).
				// TODO: audio cracks a bit. not a big deal since
				// as of now (27-05-2016) I don't use loop with samples.
				goto _read;
			}
		}

        return ReadAmount;
    }
    else
        return 0;
}

double AudioSample::GetDuration()
{
	return mAudioEnd - mAudioStart;
}

bool AudioSample::IsPlaying()
{
    return mIsPlaying;
}

void AudioSample::Slice(float audio_start, float audio_end)
{
	if (!mIsLoaded) mThread.wait();

    float audioDuration = float(mData->size()) / (float(mRate) * Channels);
    mAudioStart = Clamp(float(audio_start / mPitch), 0.0f, audioDuration);
    mAudioEnd = Clamp(float(audio_end / mPitch), mAudioStart, audioDuration);
}

std::shared_ptr<AudioSample> AudioSample::CopySlice()
{
	if (!mIsLoaded)
		mThread.wait();

    size_t start = Clamp(size_t(mAudioStart * mRate * Channels), size_t(0), mData->size());
    size_t end = Clamp(size_t(mAudioEnd * mRate * Channels), start, mData->size());

    if (!mAudioEnd) throw std::runtime_error("No buffer available");
    if (end < start) throw std::runtime_error("warning copy slice: end < start");

    std::shared_ptr<AudioSample> out = std::make_shared<AudioSample>(*this);
    return out;
}
/*
void AudioSample::Mix(AudioSample& Other)
{
size_t start = Clamp(size_t(mAudioStart * mRate * Channels), size_t(0), mData->size() - 1);
size_t end = Clamp(size_t(mAudioEnd * mRate * Channels), start, mData->size() - 1);
size_t startB = Clamp(size_t(Other.mAudioStart * Other.mRate * Other.Channels), size_t(0), Other.mData->size() - 1);
size_t endB = Clamp(size_t(Other.mAudioEnd * Other.mRate * Other.Channels), startB, Other.mData->size() - 1);
auto buf = make_shared<vector<float>>(max(endB - startB, end - start));

for (size_t i = 0; i < buf->size(); i++)
{
size_t ai = start + i;
size_t bi = startB + i;
if (bi < Other.mData->size() && ai < mData->size())
(*buf)[i] = (*Other.mData)[bi] + (*mData)[ai];
if (bi >= Other.mData->size() && ai < mData->size())
(*buf)[i] = (*mData)[ai];
if (bi < Other.mData->size() && ai >= mData->size())
(*buf)[i] = (*Other.mData)[bi];
}

mAudioStart = 0;
mAudioEnd = float(buf->size()) / (mRate * Channels);
mData = buf;
}
*/

bool AudioSample::IsValid() const
{
    return mData != nullptr && mData->size() != 0;
}

std::filesystem::path RearrangeFilename(std::filesystem::path Fn)
{
    std::filesystem::path Ret;
    if (std::filesystem::exists(Fn))
        return Fn;
    else
    {
        auto Ext = Conversion::ToU8(Fn.extension().wstring());
        Utility::ToLower(Ext);

        if (Ext == ".wav")
            Ret = Fn.parent_path() / (Fn.stem().wstring() + L".ogg");
        else
            Ret = Fn.parent_path() / (Fn.stem().wstring() + L".wav");

        if (!std::filesystem::exists(Ret))
            return Fn;
        else
            return Ret;
    }
}

bool AudioSample::Open(std::filesystem::path Filename)
{
    auto FilenameFixed = RearrangeFilename(Filename);
    std::unique_ptr<AudioDataSource> Src = SourceFromExt(FilenameFixed);
    return Open(Src.get());
}

bool AudioSample::Open(std::filesystem::path Filename, bool async)
{
	auto fn = [=]() {
		auto FilenameFixed = RearrangeFilename(Filename);
		std::unique_ptr<AudioDataSource> Src = SourceFromExt(FilenameFixed);
		return this->Open(Src.get(), false);
	};

	if (async)
	{
		mThread = std::async(std::launch::async, fn);
		return true;
	}
	else {
		return fn();
	}
}

void AudioSample::Play()
{
    if (!IsValid()) return;

	if (!mIsLoaded && mThread.valid())
		mThread.wait();

    mIsPlaying = true;
    SeekTime(mAudioStart);
}

void AudioSample::SeekTime(float Second)
{
    mCounter = mRate * Second * Channels;

	if (!mData) return;

    if (mCounter >= mData->size())
        mCounter = mData->size();
}

void AudioSample::SeekSample(uint32_t Sample)
{
    mCounter = Sample;

    if (mCounter >= mData->size())
        mCounter = mData->size();
}

void AudioSample::Stop()
{
    mIsPlaying = false;
}

bool AudioSample::AwaitLoad()
{
	if (!mIsLoaded && mThread.valid())
	{
		mThread.wait();
		return true;
	}

	return false;
}

AudioStream::AudioStream()
{
    mPitch = 1;
    mIsPlaying = false;
    mIsLooping = false;
    mSource = nullptr;
    mOwnerMixer = nullptr;
    internal = std::make_unique<AudioStreamInternal>();
    

    mStreamTime = 0;

    // MixerAddStream(this);
}

AudioStream::~AudioStream()
{
    if (mOwnerMixer)
        mOwnerMixer->RemoveStream(this);

    soxr_delete(internal->mResampler);
}

uint32_t AudioStream::Read(float* buffer, size_t count)
{
    size_t cnt;
    ring_buffer_size_t toRead = count; // Count is the amount of samples.
    size_t outcnt;

    if (!mSource || !mSource->IsValid())
    {
        mIsPlaying = false;
        return 0;
    }

    if (Channels == 1) // We just want half the samples.
        toRead >>= 1;

    if (PaUtil_GetRingBufferReadAvailable(&internal->mRingBuf) < toRead || !mIsPlaying)
        toRead = PaUtil_GetRingBufferReadAvailable(&internal->mRingBuf);

    double dstrate = mSource->GetRate();
    if (mOwnerMixer)
        dstrate = mOwnerMixer->GetRate();

    if (mIsPlaying)
    {
        // This is what our destination rate will be
        double origRate = mSource->GetRate();

        // This is what our destination rate is.
        double resRate = dstrate / mPitch;
        double RateRatio = resRate / origRate;

        // This is how many samples we want to read from the source buffer
        size_t scount = ceil(origRate * toRead / resRate);

        cnt = PaUtil_ReadRingBuffer(&internal->mRingBuf, mResampleBuffer.data(), scount);
        // cnt now contains how many samples we actually read...

        if (!cnt) return 0; // ????

        // This is how many resulting samples we can output with what we read...
        outcnt = floor(cnt * resRate / origRate);

        size_t odone;

        if (Channels == 1) // Turn mono audio to stereo audio.
            monoToStereo(mResampleBuffer.data(), cnt, BUFF_SIZE);

        soxr_set_io_ratio(internal->mResampler, 1 / RateRatio, cnt / 2);

        // The count that soxr asks for I think, is frames, not samples. Thus, the division by channels.
        soxr_process(internal->mResampler,
            mResampleBuffer.data(), cnt / Channels, nullptr,
            mOutputBuffer.data(), outcnt / Channels, &odone);

        outcnt = odone;

        // * 2 from * Channels since we mono -> stereo mono signals.
        s16tof32(mOutputBuffer.begin(), mOutputBuffer.begin() + odone * 2, buffer);

        mStreamTime += double(cnt / Channels) / mSource->GetRate();
        // mPlaybackTime = mStreamTime - MixerGetLatency();
        return outcnt * 2;
    }

    return 0;
}


bool AudioStream::Open(std::filesystem::path Filename)
{
    mSource = SourceFromExt(RearrangeFilename(Filename));

    if (mSource)
    {
        Channels = mSource->GetChannels();

        mResampleBuffer.resize(BUFF_SIZE);
        mOutputBuffer.resize(BUFF_SIZE);

        soxr_io_spec_t sis;
        sis.flags = 0;
        sis.itype = SOXR_INT16_I;
        sis.otype = SOXR_INT16_I;
        sis.scale = 1;

        double dstrate = mSource->GetRate();
        if (mOwnerMixer) dstrate = mOwnerMixer->GetRate();

        soxr_quality_spec_t q_spec = soxr_quality_spec(SOXR_VHQ, SOXR_VR);
        internal->mResampler = soxr_create(
            mSource->GetRate(), 
            dstrate,
            2, 
            nullptr, 
            &sis,
            &q_spec,
            nullptr
        );

        mBufferSize = BUFF_SIZE;
        mData.resize(mBufferSize);
        assert(mData.size() == mBufferSize);
        PaUtil_InitializeRingBuffer(&internal->mRingBuf, sizeof(short), mBufferSize, mData.data());

        mStreamTime = mPlaybackTime = 0;

        SeekTime(0);

        return true;
    }

    return false;
}

bool AudioStream::IsPlaying()
{
    return mIsPlaying;
}

void AudioStream::Play()
{
	if (mSource && mSource->IsValid()) {
		mIsPlaying = true;
		mStreamStartTime = -std::numeric_limits<float>::infinity();
	}
}

void AudioStream::SeekTime(float Second)
{
    if (mSource)
        mSource->Seek(Second);
    mStreamTime = Second;
}

double AudioStream::GetStreamedTime() const
{
    return mStreamTime;
}

double AudioStream::GetPlayedTime() const
{
    return mPlaybackTime;
}

void AudioStream::SeekSample(uint32_t Sample)
{
    mSource->Seek(float(Sample) / mSource->GetRate());
}

void AudioStream::Stop()
{
    mIsPlaying = false;
}

uint32_t AudioStream::Update()
{
    uint32_t eCount = PaUtil_GetRingBufferWriteAvailable(&internal->mRingBuf);
    uint32_t ReadTotal;

    if (!mSource || !mSource->IsValid()) return 0;

    mSource->SetLooping(IsLooping());

    if ((ReadTotal = mSource->Read(tbuf, eCount)))
    {
        PaUtil_WriteRingBuffer(&internal->mRingBuf, tbuf, ReadTotal);
    }
    else
    {
        if (!PaUtil_GetRingBufferReadAvailable(&internal->mRingBuf) && !mSource->HasDataLeft())
            mIsPlaying = false;
    }

    return ReadTotal;
}

double AudioStream::GetPlayedTimeDAC() const
{
    if (mOwnerMixer)
	    return std::max(mOwnerMixer->GetTime() - mStreamStartTime, 0.0);
    else
        return GetStreamedTime();
}

uint32_t AudioStream::GetRate() const
{
    return mSource->GetRate();
}

AudioDataSource::AudioDataSource()
{
	mSourceLoop = false;
}

AudioDataSource::~AudioDataSource()
{
}

void AudioDataSource::SetLooping(bool Loop)
{
    mSourceLoop = Loop;
}
