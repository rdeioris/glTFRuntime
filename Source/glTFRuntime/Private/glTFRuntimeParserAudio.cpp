// Copyright 2021, Roberto De Ioris.

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

	return true;
}

bool FglTFRuntimeParser::LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter)
{
	const FString AudioWav = "audio/wav";
	const FString AudioOgg = "audio/ogg";

	TSharedPtr<FJsonObject> JsonEmitterObject = GetJsonObjectFromRootExtensionIndex("MSFT_audio_emitter", "emitters", EmitterIndex);
	if (!JsonEmitterObject)
	{
		AddError("LoadAudioEmitter()", "Invalid Emitter index.");
		return false;
	}

	Emitter.Name = GetJsonObjectString(JsonEmitterObject.ToSharedRef(), "name", "");
	Emitter.Volume = GetJsonObjectNumber(JsonEmitterObject.ToSharedRef(), "volume", 1);

	const TArray<TSharedPtr<FJsonValue>>* JsonClips;
	if (JsonEmitterObject->TryGetArrayField("clips", JsonClips))
	{
		for (const TSharedPtr<FJsonValue>& JsonClipItem : *JsonClips)
		{
			const TSharedPtr<FJsonObject>* JsonClipObject;
			if (JsonClipItem->TryGetObject(JsonClipObject))
			{
				const int32 ClipIndex = GetJsonObjectNumber(JsonClipObject->ToSharedRef(), "clip", INDEX_NONE);
				if (ClipIndex > INDEX_NONE)
				{
					TSharedPtr<FJsonObject> JsonClip = GetJsonObjectFromRootExtensionIndex("MSFT_audio_emitter", "clips", ClipIndex);
					if (JsonClip)
					{
						FString MimeType = GetJsonObjectString(JsonClip.ToSharedRef(), "mimeType", "");
						TArray64<uint8> Bytes;
						if (GetJsonObjectBytes(JsonClip.ToSharedRef(), Bytes))
						{
							if (MimeType.IsEmpty())
							{
								const FString Uri = GetJsonObjectString(JsonClip.ToSharedRef(), "uri", "");
								if (!Uri.IsEmpty())
								{
									if (Uri.StartsWith("data:"))
									{
										if (Uri.StartsWith("data:audio/wav;"))
										{
											MimeType = AudioWav;
										}
										else if (Uri.StartsWith("data:audio/ogg;"))
										{
											MimeType = AudioOgg;
										}
									}
									else if (Uri.EndsWith(".wav"))
									{
										MimeType = AudioWav;
									}
									else if (Uri.EndsWith(".ogg"))
									{
										MimeType = AudioOgg;
									}
								}
							}

							if (MimeType == AudioWav)
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
			}
		}
	}

	return true;
}