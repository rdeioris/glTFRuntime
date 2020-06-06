// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeParser.h"
#include "glTFRuntimeAsset.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class GLTFRUNTIME_API UglTFRuntimeAsset : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FglTFRuntimeScene> GetScenes();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNode(int32 Index, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UStaticMesh* LoadStaticMesh(int32 MeshIndex);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMesh(int32 MeshIndex, int32 SkinIndex, int32 NodeIndex=-1);

	bool LoadFromFilename(const FString Filename);

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
	
};
