// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeFunctionLibrary.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(FString Filename)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromFilename(Filename))
		return nullptr;

	return Asset;
}