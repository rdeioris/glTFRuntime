// Copyright 2020-2024, Roberto De Ioris.


#include "glTFRuntimeSoundWave.h"
#include "UObject/Package.h"

UglTFRuntimeSoundWave::UglTFRuntimeSoundWave()
{
	bProcedural = true;
	RuntimeAudioOffset = 0;
}

int32 UglTFRuntimeSoundWave::GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded)
{
	if (RuntimeAudioData.Num() == 0)
	{
		return 0;
	}

	int64 BytesNeeded = SamplesNeeded * 2;

	if (RuntimeAudioOffset >= RuntimeAudioData.Num())
	{
		if (bLooping)
		{
			RuntimeAudioOffset = 0;
		}
		else
		{
			return 0;
		}
	}

	int64 BytesToCopy = FMath::Min(RuntimeAudioData.Num() - RuntimeAudioOffset, BytesNeeded);
	FMemory::Memcpy(PCMData, RuntimeAudioData.GetData() + RuntimeAudioOffset, BytesToCopy);

	if (OnPCMData.IsBound())
	{
		TArray<uint8> NewPCMData;
		NewPCMData.Append(RuntimeAudioData.GetData() + RuntimeAudioOffset, BytesToCopy);
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([OnPCMData = this->OnPCMData, NewPCMData]()
			{
				OnPCMData.Execute(NewPCMData);
			}, TStatId(), nullptr, ENamedThreads::GameThread);
	}

	RuntimeAudioOffset += BytesToCopy;

	return BytesToCopy;
}

void UglTFRuntimeSoundWave::SetOnPCMData(const FglTFRuntimeSoundWavePCMData& InOnPCMData)
{
	OnPCMData = InOnPCMData;
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