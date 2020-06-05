// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class GLTFRUNTIME_API UglTFRuntimeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta=(DisplayName="glTF Load Asset from Filename"), Category="glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromFilename(FString Filename);
	
};
