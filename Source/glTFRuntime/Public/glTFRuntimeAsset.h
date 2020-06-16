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
	TArray<FglTFRuntimeNode> GetNodes();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNode(const int32 NodeIndex, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNodeByName(const FString NodeName, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UStaticMesh* LoadStaticMesh(const int32 MeshIndex);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByName(const FString MeshName);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool BuildTransformFromNodeBackward(const int32 NodeIndex, FTransform& Transform);

	bool LoadFromFilename(const FString Filename);

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
	
};
