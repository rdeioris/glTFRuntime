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

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime")
	bool LoadMeshAsRuntimeLOD(const int32 MeshIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig, SkeletonConfig, OverrideSkinIndex", AutoCreateRefTerm = "MaterialsConfig, SkeletonConfig, ExcludeNodes"), Category = "glTFRuntime")
	bool LoadSkinnedMeshRecursiveAsRuntimeLOD(const FString& NodeName, const TArray<FString>& ExcludeNodes, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, int32& SkinIndex, const int32 OverrideSkinIndex = -1, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode = EglTFRuntimeRecursiveMode::Ignore);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig, SkeletonConfig, OverrideSkinIndex", AutoCreateRefTerm = "MaterialsConfig, SkeletonConfig, ExcludeNodes"), Category = "glTFRuntime")
	void LoadSkinnedMeshRecursiveAsRuntimeLODAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, int32& SkinIndex, const int32 OverrideSkinIndex = -1, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode = EglTFRuntimeRecursiveMode::Ignore);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshLODs(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByName(const FString& MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshByNodeName(const FString& NodeName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	TArray<UStaticMesh*> LoadStaticMeshesFromPrimitives(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "ExcludeNodes, StaticMeshConfig"), Category = "glTFRuntime")
	UStaticMesh* LoadStaticMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "ExcludeNodes, StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	void LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "ExcludeNodes, SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode = EglTFRuntimeRecursiveMode::Ignore);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "ExcludeNodes, SkeletalMeshConfig"), Category = "glTFRuntime")
	void LoadSkeletalMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode = EglTFRuntimeRecursiveMode::Ignore);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	void LoadSkeletalMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletonConfig", AutoCreateRefTerm = "SkeletonConfig"), Category = "glTFRuntime")
	USkeleton* LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletonConfig", AutoCreateRefTerm = "SkeletonConfig"), Category = "glTFRuntime")
	USkeleton* LoadSkeletonFromNodeTree(const int32 NodeIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletonConfig", AutoCreateRefTerm = "SkeletonConfig"), Category = "glTFRuntime")
	USkeleton* LoadSkeletonFromNodeTreeByName(const FString& NodeName, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalMeshConfig", AutoCreateRefTerm = "SkeletalMeshConfig"), Category = "glTFRuntime")
	USkeletalMesh* LoadSkeletalMeshLODs(const TArray<int32>& MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UFUNCTION(BlueprintCallable, meta=(AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString& AnimationName, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	TMap<FString, UAnimSequence*> LoadNodeSkeletalAnimationsMap(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

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

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetNumAnimations() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetAnimationsNames(const bool bIncludeUnnameds = true) const;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadCamera(const int32 CameraIndex, UCameraComponent* CameraComponent);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool NodeIsBone(const int32 NodeIndex);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNodeGPUInstancingTransforms(const int32 NodeIndex, TArray<FTransform>& Transforms);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNodeExtensionIndices(const int32 NodeIndex, const FString& ExtensionName, const FString& FieldName, TArray<int32>& Indices);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNodeExtensionIndex(const int32 NodeIndex, const FString& ExtensionName, const FString& FieldName, int32& Index);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNodeExtrasNumbers(const int32 NodeIndex, const FString& Key, TArray<float>& Values);

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

	bool SetParser(TSharedRef<FglTFRuntimeParser> InParser);

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

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	FString GetStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	int64 GetIntegerFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	float GetFloatFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	bool GetBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	int32 GetArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	FVector4 GetVectorFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "LightConfig", AutoCreateRefTerm = "LightConfig"), Category = "glTFRuntime")
	ULightComponent* LoadPunctualLight(const int32 PunctualLightIndex, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	bool LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshAsync(const int32 MeshIndex, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "StaticMeshConfig", AutoCreateRefTerm = "StaticMeshConfig"), Category = "glTFRuntime")
	void LoadStaticMeshLODsAsync(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "MaterialsConfig", AutoCreateRefTerm = "MaterialsConfig"), Category = "glTFRuntime")
	void LoadMeshAsRuntimeLODAsync(const int32 MeshIndex, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTexture2D* LoadImage(const int32 ImageIndex, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTextureCube* LoadCubeMap(const int32 ImageIndexXP, const int32 ImageIndexXN, const int32 ImageIndexYP, const int32 ImageIndexYN, const int32 ImageIndexZP, const int32 ImageIndexZN, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTexture2DArray* LoadImageArray(const TArray<int32>& ImageIndices, const FglTFRuntimeImagesConfig& ImagesConfig);
	
	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTexture2D* LoadImageFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	void LoadImageFromBlobAsync(const FglTFRuntimeTexture2DAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTexture2D* LoadMipsFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTextureCube* LoadCubeMapFromBlob(const bool bSpherical, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	void LoadCubeMapFromBlobAsync(const bool bSpherical, const bool bAutoRotate, const FglTFRuntimeTextureCubeAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	UTexture2DArray* LoadImageArrayFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "ImagesConfig", AutoCreateRefTerm = "ImagesConfig"), Category = "glTFRuntime")
	void LoadImageArrayFromBlobAsync(const FglTFRuntimeTexture2DArrayAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetExtensionsUsed() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetExtensionsRequired() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetMaterialsVariants() const;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* RuntimeContextObject;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString RuntimeContextString;

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* CreateAnimationFromPose(USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig, const int32 SkinIndex = -1);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetStringMapFromExtras(const FString& Key, TMap<FString, FString>& StringMap) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetStringArrayFromExtras(const FString& Key, TArray<FString>& StringArray) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetNumberFromExtras(const FString& Key, float& Value) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetStringFromExtras(const FString& Key, FString& Value) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetBooleanFromExtras(const FString& Key, bool& Value) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	float GetDownloadTime() const;

	FORCEINLINE TSharedPtr<FglTFRuntimeParser> GetParser() const
	{
		return Parser;
	}

	UFUNCTION(BlueprintCallable, meta = (AdvancedDisplay = "SkeletalAnimationConfig", AutoCreateRefTerm = "BonesPath,MorphTargetsPath,SkeletalAnimationConfig"), Category = "glTFRuntime")
	UAnimSequence* CreateSkeletalAnimationFromPath(USkeletalMesh* SkeletalMesh, const TArray<FglTFRuntimePathItem>& BonesPath, const TArray<FglTFRuntimePathItem>& MorphTargetsPath, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void AddUsedExtension(const FString& ExtensionName);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void AddRequiredExtension(const FString& ExtensionName);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void AddUsedExtensions(const TArray<FString>& ExtensionsNames);

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void AddRequiredExtensions(const TArray<FString>& ExtensionsNames);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	FString ToJsonString() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	FString GetVersion() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	FString GetGenerator() const;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void ClearCache();

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool IsArchive() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetArchiveItems() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool HasErrors() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetErrors() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool MeshHasMorphTargets(const int32 MeshIndex) const;

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
	
};
