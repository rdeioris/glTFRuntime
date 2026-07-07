// Copyright 2020-2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Sound/SoundWave.h"
#include "Runtime/Launch/Resources/Version.h"   // ENGINE_MAJOR_VERSION / ENGINE_MINOR_VERSION (else C4668 under -WarningsAsErrors)
#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
#include "Sound/SoundGenerator.h"   // ISoundGenerator, ISoundGeneratorPtr, FSoundGeneratorInitParams
#endif
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

#if ENGINE_MAJOR_VERSION > 5 || (ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 7)
	// 5.7+ "Direct Procedural Rendering" renders procedural sources through an ISoundGenerator.
	virtual ISoundGeneratorPtr CreateSoundGenerator(const FSoundGeneratorInitParams& InParams) override;
#endif

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

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<uint8> GetPCMData() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetNumChannels() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetSampleRate() const;

protected:
	TArray64<uint8> RuntimeAudioData;
	int64 RuntimeAudioOffset;

	FglTFRuntimeSoundWavePCMData OnPCMData;
	FglTFRuntimeSoundWavePCMDataFloat OnPCMDataFloat;
	
};
