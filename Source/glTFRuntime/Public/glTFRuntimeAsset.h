// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeParser.h"
#include "Animation/AnimMontage.h"
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

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool FindNodeByNameInArray(const TArray<int32> NodeIndices, const FString NodeName, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByName(const FString MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletonConfig", AutoCreateRefTerm = "SkeletonConfig"), Category = "glTFRuntime")
	USkeleton* LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig SkeletonConfig);

	UFUNCTION(BlueprintCallable, meta=(AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString AnimationName, const FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimMontage* LoadSkeletalAnimationAsMontage(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FString SlotNodeName, const FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UglTFRuntimeAnimationCurve* LoadNodeAnimationCurve(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	TArray<UglTFRuntimeAnimationCurve*> LoadAllNodeAnimationCurves(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool NodeIsBone(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool BuildTransformFromNodeBackward(const int32 NodeIndex, FTransform& Transform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool BuildTransformFromNodeForward(const int32 NodeIndex, const int32 LastNodeIndex, FTransform& Transform);

	bool LoadFromFilename(const FString Filename);
	bool LoadFromString(const FString JsonData);
	bool LoadFromData(const TArray64<uint8> Data);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> MaterialsMap;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeError OnError;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeOnStaticMeshCreated OnStaticMeshCreated;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeOnSkeletalMeshCreated OnSkeletalMeshCreated;

	UFUNCTION()
	void OnErrorProxy(const FString ErrorContext, const FString ErrorMessage);

	UFUNCTION()
	void OnStaticMeshCreatedProxy(UStaticMesh* StaticMesh);

	UFUNCTION()
	void OnSkeletalMeshCreatedProxy(USkeletalMesh* SkeletalMesh);

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
	
};
