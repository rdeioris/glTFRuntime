// Copyright 2020-2021, Roberto De Ioris.


#include "glTFRuntimeSoundWave.h"

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
	RuntimeAudioOffset += BytesToCopy;
	return BytesToCopy;
}