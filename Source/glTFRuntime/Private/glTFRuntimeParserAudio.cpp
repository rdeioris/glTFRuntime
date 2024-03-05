// Copyright 2021-2022, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "Audio.h"
#include "glTFRuntimeSoundWave.h"

bool FglTFRuntimeParser::LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent)
{
	if (!AudioComponent)
	{
		AddError("LoadEmitterIntoAudioComponent()", "No valid AudioComponent specified.");
		return false;
	}

	AudioComponent->Sound = Emitter.Sound;
	AudioComponent->SetVolumeMultiplier(Emitter.Volume);

	// TODO add spatialization parameters
	/* AudioComponent->bAllowSpatialization = true;
	AudioComponent->bOverrideAttenuation = true;
	AudioComponent->AttenuationOverrides.AttenuationShape = EAttenuationShape::Cone; */

	return true;
}

bool FglTFRuntimeParser::LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter)
{
	TSharedPtr<FJsonObject> JsonEmitterObject = GetJsonObjectFromRootExtensionIndex("MSFT_audio_emitter", "emitters", EmitterIndex);
	if (!JsonEmitterObject)
	{
		AddError("LoadAudioEmitter()", "Invalid Emitter index.");
		return false;
	}

	Emitter.Name = GetJsonObjectString(JsonEmitterObject.ToSharedRef(), "name", "");
	Emitter.Volume = GetJsonObjectNumber(JsonEmitterObject.ToSharedRef(), "volume", 1);

	for (const TSharedRef<FJsonObject>& JsonClipObject : GetJsonObjectArrayOfObjects(JsonEmitterObject.ToSharedRef(), "clips"))
	{
		const int32 ClipIndex = GetJsonObjectNumber(JsonClipObject, "clip", INDEX_NONE);
		if (ClipIndex > INDEX_NONE)
		{
			if (TSharedPtr<FJsonObject> JsonClip = GetJsonObjectFromRootExtensionIndex("MSFT_audio_emitter", "clips", ClipIndex))
			{
				TArray64<uint8> Bytes;
				if (GetJsonObjectBytes(JsonClip.ToSharedRef(), Bytes))
				{
					FWaveModInfo WaveModInfo;
					if (WaveModInfo.ReadWaveInfo(Bytes.GetData(), Bytes.Num()))
					{
						UglTFRuntimeSoundWave* RuntimeSound = NewObject<UglTFRuntimeSoundWave>(GetTransientPackage(), NAME_None, RF_Public);

						RuntimeSound->NumChannels = *WaveModInfo.pChannels;

						int32 SizeOfSample = (*WaveModInfo.pBitsPerSample) / 8;
						int32 NumSamples = WaveModInfo.SampleDataSize / SizeOfSample;
						int32 NumFrames = NumSamples / RuntimeSound->NumChannels;

						RuntimeSound->Duration = (float)NumFrames / *WaveModInfo.pSamplesPerSec;
						RuntimeSound->SetSampleRate(*WaveModInfo.pSamplesPerSec);
						RuntimeSound->TotalSamples = *WaveModInfo.pSamplesPerSec * RuntimeSound->Duration;

						RuntimeSound->bLooping = GetJsonObjectBool(JsonClip.ToSharedRef(), "loop", false);

						RuntimeSound->SetRuntimeAudioData(WaveModInfo.SampleDataStart, WaveModInfo.SampleDataSize);

						Emitter.Sound = RuntimeSound;
						break;
					}
				}
			}
		}
	}

	return true;
}