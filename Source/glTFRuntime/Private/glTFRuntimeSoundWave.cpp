// Copyright 2020-2024, Roberto De Ioris.


#include "glTFRuntimeSoundWave.h"
#include "UObject/Package.h"
#include "Async/TaskGraphInterfaces.h"

namespace glTFRuntime
{
	// Copies up to MaxBytes of int16 PCM from [Data, DataSize) starting at InOutOffset
	// (which is advanced). Mirrors the original chunked behaviour: when the offset is at the
	// end, wrap to 0 if looping, otherwise copy nothing. Returns the number of bytes copied.
	int64 CopyPCMChunk(const uint8* Data, int64 DataSize, int64& InOutOffset, bool bLooping, int64 MaxBytes, uint8* OutBytes)
	{
		if (DataSize <= 0 || MaxBytes <= 0)
		{
			return 0;
		}

		if (InOutOffset >= DataSize)
		{
			if (bLooping)
			{
				InOutOffset = 0;
			}
			else
			{
				return 0;
			}
		}

		const int64 BytesToCopy = FMath::Min(DataSize - InOutOffset, MaxBytes);
		FMemory::Memcpy(OutBytes, Data + InOutOffset, BytesToCopy);
		InOutOffset += BytesToCopy;
		return BytesToCopy;
	}

	// Fires the optional PCM data callbacks on the game thread for a chunk of int16 PCM.
	void DispatchPCMCallbacks(const FglTFRuntimeSoundWavePCMData& OnPCMData, const FglTFRuntimeSoundWavePCMDataFloat& OnPCMDataFloat, const uint8* Int16Bytes, int64 NumBytes)
	{
		if (NumBytes <= 0)
		{
			return;
		}

		if (OnPCMData.IsBound())
		{
			TArray<uint8> NewPCMData;
			NewPCMData.Append(Int16Bytes, NumBytes);
			FFunctionGraphTask::CreateAndDispatchWhenReady([OnPCMData, NewPCMData = MoveTemp(NewPCMData)]()
				{
					OnPCMData.ExecuteIfBound(NewPCMData);
				}, TStatId(), nullptr, ENamedThreads::GameThread);
		}

		if (OnPCMDataFloat.IsBound())
		{
			const int32 NumSamplesFloat = static_cast<int32>(NumBytes / static_cast<int64>(sizeof(int16)));
			TArray<float> NewPCMData;
			NewPCMData.AddUninitialized(NumSamplesFloat);
			const int16* SamplesInt16 = reinterpret_cast<const int16*>(Int16Bytes);
			for (int32 Index = 0; Index < NumSamplesFloat; ++Index)
			{
				NewPCMData[Index] = static_cast<float>(SamplesInt16[Index]) / 32768.0f;
			}
			FFunctionGraphTask::CreateAndDispatchWhenReady([OnPCMDataFloat, NewPCMData = MoveTemp(NewPCMData)]()
				{
					OnPCMDataFloat.ExecuteIfBound(NewPCMData);
				}, TStatId(), nullptr, ENamedThreads::GameThread);
		}
	}
}

UglTFRuntimeSoundWave::UglTFRuntimeSoundWave()
{
	bProcedural = true;
	RuntimeAudioOffset = 0;
}

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
// 5.7+ "Direct Procedural Rendering" renders procedural sources through an ISoundGenerator.
// This reuses the same chunked reader / callback dispatcher as the legacy GeneratePCMData path,
// just emitting float instead of int16 and signalling completion via IsFinished().
class FglTFRuntimeSoundGenerator : public ISoundGenerator
{
public:
	FglTFRuntimeSoundGenerator(TArray64<uint8> InAudioData, int32 InNumChannels, bool bInLooping, int64 InStartOffset,
		FglTFRuntimeSoundWavePCMData InOnPCMData, FglTFRuntimeSoundWavePCMDataFloat InOnPCMDataFloat)
		: AudioData(MoveTemp(InAudioData))
		, NumChannels(FMath::Max(1, InNumChannels))
		, bLooping(bInLooping)
		, Offset(FMath::Clamp<int64>(InStartOffset, 0, AudioData.Num()))
		, OnPCMData(InOnPCMData)
		, OnPCMDataFloat(InOnPCMDataFloat)
	{
	}

	// Required to be usable with a generator source (the direct procedural path).
	virtual int32 GetNumChannels() override { return NumChannels; }

	// Ends the source (and fires OnAudioFinished) once the buffer is exhausted (non-looping).
	virtual bool IsFinished() const override { return bFinished; }

	virtual int32 OnGenerateAudio(float* OutAudio, int32 NumSamples) override
	{
		int32 SamplesWritten = 0;
		while (SamplesWritten < NumSamples)
		{
			const int32 SamplesRemaining = NumSamples - SamplesWritten;
			ScratchBytes.SetNumUninitialized(SamplesRemaining * static_cast<int32>(sizeof(int16)), EAllowShrinking::No);

			const int64 BytesCopied = glTFRuntime::CopyPCMChunk(AudioData.GetData(), AudioData.Num(), Offset, bLooping,
				static_cast<int64>(SamplesRemaining) * static_cast<int64>(sizeof(int16)), ScratchBytes.GetData());

			if (BytesCopied <= 0)
			{
				bFinished = true;   // non-looping end (or empty buffer)
				break;
			}

			const int32 SamplesCopied = static_cast<int32>(BytesCopied / static_cast<int64>(sizeof(int16)));
			const int16* Samples = reinterpret_cast<const int16*>(ScratchBytes.GetData());
			for (int32 Index = 0; Index < SamplesCopied; ++Index)
			{
				OutAudio[SamplesWritten + Index] = static_cast<float>(Samples[Index]) / 32768.0f;
			}

			glTFRuntime::DispatchPCMCallbacks(OnPCMData, OnPCMDataFloat, ScratchBytes.GetData(), BytesCopied);
			SamplesWritten += SamplesCopied;
		}

		// Zero-pad the tail of the final buffer once the (non-looping) clip is done.
		if (SamplesWritten < NumSamples)
		{
			FMemory::Memzero(OutAudio + SamplesWritten, (NumSamples - SamplesWritten) * sizeof(float));
		}

		return NumSamples;
	}

private:
	TArray64<uint8> AudioData;   // owned snapshot (render-thread safe)
	int32 NumChannels;
	bool bLooping;
	int64 Offset;
	bool bFinished = false;
	FglTFRuntimeSoundWavePCMData OnPCMData;
	FglTFRuntimeSoundWavePCMDataFloat OnPCMDataFloat;
	TArray<uint8> ScratchBytes;
};

ISoundGeneratorPtr UglTFRuntimeSoundWave::CreateSoundGenerator(const FSoundGeneratorInitParams& InParams)
{
	// Snapshot current data + offset so the generator is self-contained on the render thread
	// and independent of later SetRuntimeAudioData()/ResetAudioOffset() calls.
	return MakeShared<FglTFRuntimeSoundGenerator, ESPMode::ThreadSafe>(
		RuntimeAudioData, NumChannels, bLooping, RuntimeAudioOffset, OnPCMData, OnPCMDataFloat);
}
#endif

int32 UglTFRuntimeSoundWave::GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded)
{
	const int64 BytesCopied = glTFRuntime::CopyPCMChunk(RuntimeAudioData.GetData(), RuntimeAudioData.Num(), RuntimeAudioOffset,
		bLooping, static_cast<int64>(SamplesNeeded) * static_cast<int64>(sizeof(int16)), PCMData);

	glTFRuntime::DispatchPCMCallbacks(OnPCMData, OnPCMDataFloat, PCMData, BytesCopied);

	return static_cast<int32>(BytesCopied);
}

void UglTFRuntimeSoundWave::SetOnPCMData(const FglTFRuntimeSoundWavePCMData& InOnPCMData)
{
	OnPCMData = InOnPCMData;
}

void UglTFRuntimeSoundWave::SetOnPCMDataFloat(const FglTFRuntimeSoundWavePCMDataFloat& InOnPCMDataFloat)
{
	OnPCMDataFloat = InOnPCMDataFloat;
}

void UglTFRuntimeSoundWave::ResetAudioOffset()
{
	RuntimeAudioOffset = 0;
}

UglTFRuntimeSoundWave* UglTFRuntimeSoundWave::DuplicateRuntimeSoundWave()
{
	UglTFRuntimeSoundWave* RuntimeSound = NewObject<UglTFRuntimeSoundWave>(GetTransientPackage(), NAME_None, RF_Public);

	RuntimeSound->NumChannels = NumChannels;

	RuntimeSound->Duration = Duration;

	RuntimeSound->SetSampleRate(SampleRate);
	RuntimeSound->TotalSamples = TotalSamples;

	RuntimeSound->bLooping = bLooping;

	RuntimeSound->Volume = Volume;

	RuntimeSound->SetRuntimeAudioData(RuntimeAudioData.GetData(), RuntimeAudioData.Num());

	return RuntimeSound;
}

TArray<uint8> UglTFRuntimeSoundWave::GetPCMData() const
{
	return TArray<uint8>(RuntimeAudioData);
}

int32 UglTFRuntimeSoundWave::GetNumChannels() const
{
	return NumChannels;
}

int32 UglTFRuntimeSoundWave::GetSampleRate() const
{
	return SampleRate;
}
