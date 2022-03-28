// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeParser.h"
#include "Animation/AnimMontage.h"
#include "Camera/CameraActor.h"
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
	bool GetNodeByName(const FString& NodeName, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool FindNodeByNameInArray(const TArray<int32>& NodeIndices, const FString& NodeName, FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshLODs(const TArray<int32> MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByName(const FString& MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByNodeName(const FString& NodeName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	TArray<UStaticMesh*> LoadStaticMeshesFromPrimitives(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	void LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "ExcludeNodes, SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "ExcludeNodes, SkeletalMeshConfig"), Category = "glTFRuntime")
	void LoadSkeletalMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletonConfig", AutoCreateRefTerm = "SkeletonConfig"), Category = "glTFRuntime")
	USkeleton* LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMeshLODs(const TArray<int32> MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta=(AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString& AnimationName, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimMontage* LoadSkeletalAnimationAsMontage(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FString& SlotNodeName, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UglTFRuntimeAnimationCurve* LoadNodeAnimationCurve(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	TArray<UglTFRuntimeAnimationCurve*> LoadAllNodeAnimationCurves(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetCamerasNames();

	UFUNCTION(BlueprintCallable, meta = (WorldContext = "WorldContextObject"), Category = "glTFRuntime")
	ACameraActor* LoadNodeCamera(UObject* WorldContextObject, const int32 NodeIndex, TSubclassOf<ACameraActor> CameraActorClass);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<int32> GetCameraNodesIndices();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetNumMeshes() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetNumImages() const;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadCamera(const int32 CameraIndex, UCameraComponent* CameraComponent);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool NodeIsBone(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool BuildTransformFromNodeBackward(const int32 NodeIndex, FTransform& Transform);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool BuildTransformFromNodeForward(const int32 NodeIndex, const int32 LastNodeIndex, FTransform& Transform);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime")
	UMaterialInterface* LoadMaterial(const int32 MaterialIndex, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors);

	bool LoadFromFilename(const FString& Filename, const FglTFRuntimeConfig& LoaderConfig);
	bool LoadFromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig);
	bool LoadFromData(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig);
	FORCEINLINE bool LoadFromData(const TArray<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig) { return LoadFromData(Data.GetData(), Data.Num(), LoaderConfig); }
	FORCEINLINE bool LoadFromData(const TArray64<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig) { return LoadFromData(Data.GetData(), Data.Num(), LoaderConfig); }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> MaterialsMap;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeError OnError;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeOnStaticMeshCreated OnStaticMeshCreated;

	UPROPERTY(BlueprintAssignable, Category = "glTFRuntime")
	FglTFRuntimeOnSkeletalMeshCreated OnSkeletalMeshCreated;

	UFUNCTION()
	void OnErrorProxy(const FString& ErrorContext, const FString& ErrorMessage);

	UFUNCTION()
	void OnStaticMeshCreatedProxy(UStaticMesh* StaticMesh);

	UFUNCTION()
	void OnSkeletalMeshCreatedProxy(USkeletalMesh* SkeletalMesh);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ProceduralMeshConfig", AutoCreateRefTerm = "ProceduralMeshConfig"), Category = "glTFRuntime")
	bool LoadStaticMeshIntoProceduralMeshComponent(const int32 MeshIndex, UProceduralMeshComponent* ProceduralMeshComponent, const FglTFRuntimeProceduralMeshConfig& ProceduralMeshConfig);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	FString GetStringFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int64 GetIntegerFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	float GetFloatFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetBooleanFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetArraySizeFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshAsync(const int32 MeshIndex, FglTFRuntimeStaticMeshAsync AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshLODsAsync(const TArray<int32> MeshIndices, FglTFRuntimeStaticMeshAsync AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	UTexture2D* LoadImage(const int32 ImageIndex, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetExtensionsUsed() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetExtensionsRequired() const;

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
	
};
