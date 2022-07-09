// Copyright 2020-2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "glTFRuntimeParser.h"

#include "glTFRuntimeWriter.generated.h"

UENUM()
enum class EglTFRuntimeWriterMode : uint8
{
	Text,
	TextEmbedded,
	Binary,
};

UENUM()
enum class EglTFRuntimeCompressionMode : uint8
{
	None,
	GZip,
	Zip
};

USTRUCT(BlueprintType)
struct FglTFRuntimeWriterConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeWriterMode WriterMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCompressionMode CompressionMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bExportSkin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bExportMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString PivotToBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector PivotDelta;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBakeMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBakePose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, FTransform> OverrideBonesByIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FName, FTransform> OverrideBonesByName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bExportNormals;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bExportTangents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bExportUVs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddParentNode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FTransform ParentNodeTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ForceRootBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 MaxBonesInfluences;

	FglTFRuntimeWriterConfig()
	{
		bExportSkin = true;
		bExportMorphTargets = true;
		PivotDelta = FVector::ZeroVector;
		bBakeMorphTargets = false;
		bBakePose = false;
		bExportNormals = true;
		bExportTangents = true;
		bExportUVs = true;
		ParentNodeTransform = FTransform::Identity;
		MaxBonesInfluences = 12;
	}
};

struct FglTFRuntimeAccessor
{
	FString Type;
	int32 ComponentType;
	int64 Count;
	int64 ByteOffset;
	int64 ByteLength;
	bool bNormalized;

	TArray<TSharedPtr<FJsonValue>> Min;
	TArray<TSharedPtr<FJsonValue>> Max;

	FglTFRuntimeAccessor() = delete;
	FglTFRuntimeAccessor(const FString& InType, const int32 InComponentType, const int64 InCount, const int64 InByteOffset, const int64 InByteLength, const bool bInNormalized) :
		Type(InType),
		ComponentType(InComponentType),
		Count(InCount),
		ByteOffset(InByteOffset),
		ByteLength(InByteLength),
		bNormalized(bInNormalized)
	{
	}
};


/**
 * 
 */
class GLTFRUNTIME_API FglTFRuntimeWriter
{
public:
	FglTFRuntimeWriter(const FglTFRuntimeWriterConfig& InConfig);
	~FglTFRuntimeWriter();

	bool AddSkeletalMesh(UWorld* World, USkeletalMesh* SkeletalMesh, const int32 LOD, const TArray<UAnimSequence*>& Animations, USkeletalMeshComponent* SkeletalMeshComponent);
	bool AddStaticMesh(UWorld* World, UStaticMesh* StaticMesh, const int32 LOD, UStaticMeshComponent* StaticMeshComponent);

	bool WriteToFile(const FString& Filename);

protected:
	TSharedPtr<FJsonObject> JsonRoot;
	TArray<TSharedPtr<FJsonValue>> JsonMeshes;
	TArray<TSharedPtr<FJsonValue>> JsonAnimations;
	TArray<TSharedPtr<FJsonValue>> JsonMaterials;
	TArray<TSharedPtr<FJsonValue>> JsonImages;
	TArray<TSharedPtr<FJsonValue>> JsonTextures;
	TArray<TSharedPtr<FJsonValue>> JsonNodes;
	TArray<FglTFRuntimeAccessor> Accessors;
	TArray<TPair<int64, int64>> ImagesBuffers;

	TArray<uint8> BinaryData;

	FglTFRuntimeWriterConfig Config;
};
