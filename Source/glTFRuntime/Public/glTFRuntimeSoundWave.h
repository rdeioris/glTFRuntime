// Copyright 2020-2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWave.h"
#include "glTFRuntimeSoundWave.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeSoundWavePCMData, const TArray<uint8>&, PCMData);
DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeSoundWavePCMDataFloat, const TArray<float>&, PCMData);

/**
 * 
 */
UCLASS()
class GLTFRUNTIME_API UglTFRuntimeSoundWave : public USoundWave
{
	GENERATED_BODY()

public:
	UglTFRuntimeSoundWave();

	virtual int32 GeneratePCMData(uint8* PCMData, const int32 SamplesNeeded) override;
	virtual bool HasCompressedData(FName Format, ITargetPlatform* TargetPlatform) const override { return false; }

	virtual void SetRuntimeAudioData(const uint8* AudioData, const int64 AudioDataSize)
	{
		RuntimeAudioData.Empty();
		RuntimeAudioData.Append(AudioData, AudioDataSize);
		RuntimeAudioOffset = 0;
	}

	UFUNCTION(BlueprintCallable, Category="glTFRuntime")
	void ResetAudioOffset();

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UglTFRuntimeSoundWave* DuplicateRuntimeSoundWave();

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void SetOnPCMData(const FglTFRuntimeSoundWavePCMData& InOnPCMData);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void SetOnPCMDataFloat(const FglTFRuntimeSoundWavePCMDataFloat& InOnPCMDataFloat);

protected:
	TArray64<uint8> RuntimeAudioData;
	int64 RuntimeAudioOffset;

	FglTFRuntimeSoundWavePCMData OnPCMData;
	FglTFRuntimeSoundWavePCMDataFloat OnPCMDataFloat;
	
};
