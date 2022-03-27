// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "glTFRuntimeParser.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeImageLoader.generated.h"

/**
 * 
 */
UCLASS(Abstract)
class GLTFRUNTIME_API UglTFRuntimeImageLoader : public UObject
{
	GENERATED_BODY()

public:

	virtual bool LoadImage(TSharedRef<FglTFRuntimeParser> Parser, const int32 ImageIndex, TSharedRef<FJsonObject> JsonImageObject, const TArray64<uint8>& Data, TArray64<uint8>& OutData, int32& Width, int32& Height) { return false; };
	
};
