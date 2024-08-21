// Copyright 2020-2023, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Animation/AnimEnums.h"
#include "Animation/PoseAsset.h"
#include "Animation/Skeleton.h"
#include "Async/Async.h"
#include "Async/ParallelFor.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Engine/DataAsset.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/Texture.h"
#include "Engine/Texture2D.h"
#include "Engine/Texture2DArray.h"
#include "Engine/TextureCube.h"
#include "Engine/TextureMipDataProviderFactory.h"
#include "Engine/VolumeTexture.h"
#include "Camera/CameraComponent.h"
#include "Components/AudioComponent.h"
#include "Components/LightComponent.h"
#include "glTFRuntimeAnimationCurve.h"
#include "ProceduralMeshComponent.h"
#if WITH_EDITOR
#include "Rendering/SkeletalMeshLODImporterData.h"
#endif
#include "Serialization/ArrayReader.h"
#include "Streaming/TextureMipDataProvider.h"
#include "UObject/Package.h"
#include "glTFRuntimeParser.generated.h"

GLTFRUNTIME_API DECLARE_LOG_CATEGORY_EXTERN(LogGLTFRuntime, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FglTFRuntimeError, const FString, ErrorContext, const FString, ErrorMessage);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnStaticMeshCreated, UStaticMesh*, StaticMesh);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnSkeletalMeshCreated, USkeletalMesh*, SkeletalMesh);

#define GLTFRUNTIME_IMAGE_API_1

/*
* Credits for giving me the idea for the blob structure
* definitely go to Benjamin MICHEL (SBRK)
*
*/
struct FglTFRuntimeBlob
{
	uint8* Data;
	int64 Num;

	FglTFRuntimeBlob()
	{
		Data = nullptr;
		Num = 0;
	}
};

UENUM()
enum class EglTFRuntimeTransformBaseType : uint8
{
	Default,
	Matrix,
	Transform,
	YForward,
	BasisMatrix,
	Identity,
	LeftHanded,
	IdentityXInverted,
	ForwardInverted
};

UENUM()
enum class EglTFRuntimeNormalsGenerationStrategy : uint8
{
	IfMissing,
	Never,
	Always
};

UENUM()
enum class EglTFRuntimeTangentsGenerationStrategy : uint8
{
	IfMissing,
	Never,
	Always
};

UENUM()
enum class EglTFRuntimeMorphTargetsDuplicateStrategy : uint8
{
	Ignore,
	Merge,
	AppendMorphIndex,
	AppendDuplicateCounter
};

UENUM()
enum class EglTFRuntimePhysicsAssetAutoBodyCollisionType : uint8
{
	Capsule,
	Sphere,
	Box
};

UENUM()
enum class EglTFRuntimeRecursiveMode : uint8
{
	Ignore,
	Node,
	Tree
};

USTRUCT(BlueprintType)
struct FglTFRuntimeBasisMatrix
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector XAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector YAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector ZAxis;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector Origin;

	FMatrix GetMatrix() const
	{
		return FBasisVectorMatrix(XAxis, YAxis, ZAxis, Origin);
	}

	FglTFRuntimeBasisMatrix()
	{
		XAxis = FVector::ZeroVector;
		YAxis = FVector::ZeroVector;
		ZAxis = FVector::ZeroVector;
		Origin = FVector::ZeroVector;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeTransformBaseType TransformBaseType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FMatrix BasisMatrix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FTransform BaseTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeBasisMatrix BasisVectorMatrix;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float SceneScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FString> ContentPluginsToScan;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAllowExternalFiles;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString OverrideBaseDirectory;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bOverrideBaseDirectoryFromContentDir;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ArchiveEntryPoint;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ArchiveAutoEntryPointExtensions;

	bool bSearchContentDir;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* RuntimeContextObject;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString RuntimeContextString;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAsBlob;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString PrefixForUnnamedNodes;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString EncryptionKey;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<TSubclassOf<class UglTFRuntimeAssetUserData>> AssetUserDataClasses;

	FglTFRuntimeConfig()
	{
		TransformBaseType = EglTFRuntimeTransformBaseType::Default;
		BasisMatrix = FMatrix::Identity;
		BaseTransform = FTransform::Identity;
		SceneScale = 100;
		bSearchContentDir = false;
		bAllowExternalFiles = true;
		bOverrideBaseDirectoryFromContentDir = false;
		ArchiveAutoEntryPointExtensions = ".glb .gltf .json .js";
		RuntimeContextObject = nullptr;
		bAsBlob = false;
		PrefixForUnnamedNodes = "node";
	}

	FMatrix GetMatrix() const
	{
		const FMatrix DefaultMatrix = FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);

		if (TransformBaseType == EglTFRuntimeTransformBaseType::Default)
		{
			return DefaultMatrix;
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::Matrix)
		{
			return BasisMatrix;
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::BasisMatrix)
		{
			return BasisVectorMatrix.GetMatrix();
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::Transform)
		{
			return BaseTransform.ToMatrixWithScale();
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::YForward)
		{
			return FBasisVectorMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::Identity)
		{
			return FMatrix::Identity;
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::LeftHanded)
		{
			return FBasisVectorMatrix(FVector(0, 0, 1), FVector(-1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::IdentityXInverted)
		{
			return FBasisVectorMatrix(FVector(-1, 0, 0), FVector(0, 1, 0), FVector(0, 0, 1), FVector::ZeroVector);
		}

		if (TransformBaseType == EglTFRuntimeTransformBaseType::ForwardInverted)
		{
			return FBasisVectorMatrix(FVector(0, 0, 1), FVector(-1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector);
		}

		return DefaultMatrix;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeScene
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 Index;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	TArray<int32> RootNodesIndices;

	FglTFRuntimeScene()
	{
		Index = INDEX_NONE;
	}
};


USTRUCT(BlueprintType)
struct FglTFRuntimeNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 Index;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	FTransform Transform;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 MeshIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 SkinIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 CameraIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	TArray<int32> ChildrenIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly, Category = "glTFRuntime")
	int32 ParentIndex;

	FglTFRuntimeNode()
	{
		Index = INDEX_NONE;
		Transform = FTransform::Identity;
		ParentIndex = INDEX_NONE;
		MeshIndex = INDEX_NONE;
		SkinIndex = INDEX_NONE;
		CameraIndex = INDEX_NONE;
	}
};

UENUM()
enum class EglTFRuntimeMaterialType : uint8
{
	Opaque,
	Translucent,
	TwoSided,
	TwoSidedTranslucent,
	Masked,
	TwoSidedMasked
};

UENUM()
enum class EglTFRuntimeCacheMode : uint8
{
	ReadWrite,
	None,
	Read,
	Write
};

UENUM()
enum class EglTFRuntimePivotPosition : uint8
{
	Asset,
	Center,
	Top,
	Bottom,
	CustomTransform
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSocket
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString BoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FTransform Transform;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeBone
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString BoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 ParentIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FTransform Transform;

	FglTFRuntimeBone()
	{
		ParentIndex = INDEX_NONE;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeMorphTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector> Positions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector> Normals;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeImagesConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureCompressionSettings> Compression;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureGroup> Group;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSRGB;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 MaxWidth;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 MaxHeight;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bVerticalFlip;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bForceHDR;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bCompressMips;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bStreaming;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 LODBias;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bForceAutoDetect;

	FglTFRuntimeImagesConfig()
	{
		Compression = TextureCompressionSettings::TC_Default;
		Group = TextureGroup::TEXTUREGROUP_World;
		bSRGB = false;
		MaxWidth = 0;
		MaxHeight = 0;
		bVerticalFlip = false;
		bForceHDR = false;
		bCompressMips = false;
		bStreaming = false;
		LODBias = 0;
		bForceAutoDetect = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeTextureSampler
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureAddress> TileX;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureAddress> TileY;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureAddress> TileZ;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureFilter> MinFilter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<TextureFilter> MagFilter;

	FglTFRuntimeTextureSampler()
	{
		TileX = TextureAddress::TA_Wrap;
		TileY = TextureAddress::TA_Wrap;
		TileZ = TextureAddress::TA_Wrap;
		MinFilter = TextureFilter::TF_Default;
		MagFilter = TextureFilter::TF_Default;;
	}
};

UENUM()
enum class EglTFRuntimePointsTriangulationMode : uint8
{
	Triangle,
	TriangleWithXYInUV1,
	TriangleWithXYInUV1ZWInUV2,
	Quad,
	QuadWithXYInUV1,
	QuadWithXYInUV1ZWInUV2,
	Tetrahedron,
	TetrahedronWithXYInUV1ZWInUV2,
	OpenedTetrahedron,
	OpenedTetrahedronWithXYInUV1ZWInUV2,
	Cube,
	CubeWithXYInUV1ZWInUV2,
	Custom
};

UENUM()
enum class EglTFRuntimeLinesTriangulationMode : uint8
{
	Rectangle,
	RectangleWithXYInUV1ZWInUV2,
	TriangularPrism,
	TriangularPrismWithXYInUV1ZWInUV2,
	OpenedTriangularPrism,
	OpenedTriangularPrismWithXYInUV1ZWInUV2,
	Custom
};

USTRUCT(BlueprintType)
struct FglTFRuntimeMaterialsConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCacheMode CacheMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> UberMaterialsOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UMaterialInterface*> MaterialsOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, UMaterialInterface*> MaterialsOverrideByNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UTexture2D*> TexturesOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UTexture2D*> ImagesOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableVertexColors;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bGeneratesMipMaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bMergeSectionsByMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float SpecularFactor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bMaterialsOverrideMapInjectParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, float> ParamsMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeImagesConfig ImagesConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString Variant;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSkipLoad;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UMaterialInterface* VertexColorOnlyMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, float> ScalarParamsOverrides;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bLoadMipMaps;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UMaterialInterface* ForceMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSkipPoints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimePointsTriangulationMode PointsTriangulationMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UMaterialInterface* PointsBaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float PointsScaleFactor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSkipLines;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeLinesTriangulationMode LinesTriangulationMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UMaterialInterface* LinesBaseMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float LinesScaleFactor;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, float> CustomScalarParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FLinearColor> CustomVectorParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, UTexture*> CustomTextureParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddEpicInterchangeParams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> MetallicRoughnessOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> SpecularGlossinessOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> ClearCoatOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> TransmissionOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> UnlitOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> SheenOverrideMap;

	FglTFRuntimeMaterialsConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bGeneratesMipMaps = false;
		bMergeSectionsByMaterial = false;
		SpecularFactor = 0;
		bDisableVertexColors = false;
		bMaterialsOverrideMapInjectParams = false;
		bSkipLoad = false;
		VertexColorOnlyMaterial = nullptr;
		bLoadMipMaps = false;
		ForceMaterial = nullptr;
		bSkipPoints = true;
		PointsTriangulationMode = EglTFRuntimePointsTriangulationMode::OpenedTetrahedronWithXYInUV1ZWInUV2;
		PointsBaseMaterial = nullptr;
		PointsScaleFactor = 1;
		bSkipLines = true;
		LinesTriangulationMode = EglTFRuntimeLinesTriangulationMode::OpenedTriangularPrismWithXYInUV1ZWInUV2;
		LinesBaseMaterial = nullptr;
		LinesScaleFactor = 1;
		bAddEpicInterchangeParams = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeStaticMeshConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCacheMode CacheMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bReverseWinding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBuildSimpleCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBuildComplexCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FBox> BoxCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector4> SphereCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<ECollisionTraceFlag> CollisionComplexity;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAllowCPUAccess;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimePivotPosition PivotPosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Outer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeMaterialsConfig MaterialsConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> Sockets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ExportOriginalPivotToSocket;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, float> LODScreenSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeNormalsGenerationStrategy NormalsGenerationStrategy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeTangentsGenerationStrategy TangentsGenerationStrategy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bReverseTangents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bUseHighPrecisionUVs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bGenerateStaticMeshDescription;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBuildNavCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FString> CustomConfigMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<UDataAsset*> CustomConfigObjects;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float LODScreenSizeMultiplier;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBuildLumenCards;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FTransform CustomPivotTransform;

	template<typename T>
	T* GetCustomConfig() const
	{
		for (UDataAsset* Config : CustomConfigObjects)
		{
			if (Config->IsA<T>())
			{
				return Cast<T>(Config);
			}
		}
		return nullptr;
	}

	FglTFRuntimeStaticMeshConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bReverseWinding = false;
		bBuildSimpleCollision = false;
		bBuildComplexCollision = false;
		Outer = nullptr;
		CollisionComplexity = ECollisionTraceFlag::CTF_UseDefault;
		bAllowCPUAccess = false;
		PivotPosition = EglTFRuntimePivotPosition::Asset;
		NormalsGenerationStrategy = EglTFRuntimeNormalsGenerationStrategy::IfMissing;
		TangentsGenerationStrategy = EglTFRuntimeTangentsGenerationStrategy::IfMissing;
		bReverseTangents = false;
		bUseHighPrecisionUVs = false;
		bGenerateStaticMeshDescription = false;
		bBuildNavCollision = false;
		LODScreenSizeMultiplier = 2;
		bBuildLumenCards = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeProceduralMeshConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bReverseWinding;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBuildSimpleCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FBox> BoxCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector4> SphereCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bUseComplexAsSimpleCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimePivotPosition PivotPosition;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeMaterialsConfig MaterialsConfig;

	FglTFRuntimeProceduralMeshConfig()
	{
		bReverseWinding = false;
		bBuildSimpleCollision = false;
		bUseComplexAsSimpleCollision = false;
		PivotPosition = EglTFRuntimePivotPosition::Asset;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeLightConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float DefaultAttenuationMultiplier;

	FglTFRuntimeLightConfig()
	{
		DefaultAttenuationMultiplier = 1.0;
	}
};

DECLARE_DYNAMIC_DELEGATE_RetVal_ThreeParams(FString, FglTFRuntimeBoneRemapper, const int32, NodeIndex, const FString&, BoneName, UObject*, Context);

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletonBoneRemapperHook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeBoneRemapper Remapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Context = nullptr;
};


USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletonConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCacheMode CacheMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddRootBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString RootBoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FString> BonesNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAssignUnmappedBonesToParent;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> BonesTransformMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bNormalizeSkeletonScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 RootNodeIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FglTFRuntimeSocket> Sockets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bClearRotations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	USkeleton* CopyRotationsFrom;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSkipAlreadyExistentBoneNames;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ForceRootNode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> BonesDeltaTransformMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletonBoneRemapperHook BoneRemapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAppendNodeIndexOnNameCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bFallbackToNodesTree;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bApplyParentNodesTransformsToRoot;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 MaxNodesTreeDepth;

	int32 CachedNodeIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bApplyUnmappedBonesTransforms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> NodeBonesDeltaTransformMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddRootNodeIfMissing;

	FglTFRuntimeSkeletonConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		RootNodeIndex = INDEX_NONE;
		bNormalizeSkeletonScale = false;
		bAddRootBone = false;
		bClearRotations = false;
		CopyRotationsFrom = nullptr;
		bSkipAlreadyExistentBoneNames = false;
		bAssignUnmappedBonesToParent = false;
		bAppendNodeIndexOnNameCollision = false;
		bFallbackToNodesTree = false;
		bApplyParentNodesTransformsToRoot = false;
		CachedNodeIndex = INDEX_NONE;
		MaxNodesTreeDepth = -1;
		bApplyUnmappedBonesTransforms = false;
		bAddRootNodeIfMissing = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeCapsule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float Radius;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float Length;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FRotator Rotation;

	FglTFRuntimeCapsule()
	{
		Radius = 0;
		Length = 0;
		Center = FVector::ZeroVector;
		Rotation = FRotator::ZeroRotator;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSphere
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector Center;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float Radius;

	FglTFRuntimeSphere()
	{
		Center = FVector::ZeroVector;
		Radius = 0;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimePhysicsConstraint
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ConstraintBone1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString ConstraintBone2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector ConstraintPos1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector ConstraintPos2;

	FglTFRuntimePhysicsConstraint()
	{
		ConstraintPos1 = FVector::ZeroVector;
		ConstraintPos2 = FVector::ZeroVector;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimePhysicsBody
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<ECollisionTraceFlag> CollisionTraceFlag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<EPhysicsType> PhysicsType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bConsiderForBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FglTFRuntimeCapsule> CapsuleCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FglTFRuntimeSphere> SphereCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FBox> BoxCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bSphereAutoCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bBoxAutoCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bCapsuleAutoCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float CollisionScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableCollision;

	FglTFRuntimePhysicsBody()
	{
		CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
		PhysicsType = EPhysicsType::PhysType_Default;
		bConsiderForBounds = true;
		bSphereAutoCollision = false;
		bBoxAutoCollision = false;
		bCapsuleAutoCollision = false;
		CollisionScale = 1.01;
		bDisableCollision = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimePhysicsAssetAutoBodyConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimePhysicsAssetAutoBodyCollisionType CollisionType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float MinBoneSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableOverlappingCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableAllCollisions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<ECollisionTraceFlag> CollisionTraceFlag;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<EPhysicsType> PhysicsType;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bConsiderForBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float CollisionScale;

	FglTFRuntimePhysicsAssetAutoBodyConfig()
	{
		CollisionType = EglTFRuntimePhysicsAssetAutoBodyCollisionType::Capsule;
		MinBoneSize = 20;
		bDisableOverlappingCollisions = true;
		bDisableAllCollisions = false;
		CollisionTraceFlag = ECollisionTraceFlag::CTF_UseDefault;
		PhysicsType = EPhysicsType::PhysType_Default;
		bConsiderForBounds = true;
		CollisionScale = 1.01;
	}
};

DECLARE_DYNAMIC_DELEGATE_RetVal_ThreeParams(FBox, FglTFRuntimeBoneBoundsFilter, const FString&, BoneName, const FBox&, Box, UObject*, Context);

USTRUCT(BlueprintType)
struct FglTFRuntimeBoneBoundsFilterHook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeBoneBoundsFilter Filter;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Context = nullptr;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletalMeshConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCacheMode CacheMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	USkeleton* Skeleton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bOverwriteRefSkeleton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bMergeAllBonesToBoneTree;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FglTFRuntimeBone> CustomSkeleton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bIgnoreSkin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 OverrideSkinIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletonConfig SkeletonConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeMaterialsConfig MaterialsConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, float> LODScreenSize;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector BoundsScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bShiftBoundsByRootBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bIgnoreMissingBones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FglTFRuntimePhysicsBody> PhysicsBodies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Outer;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString SaveToPackage;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bPerPolyCollision;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bIgnoreEmptyMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeMorphTargetsDuplicateStrategy MorphTargetsDuplicateStrategy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FVector ShiftBounds;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bUseHighPrecisionUVs;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UPhysicsAsset* PhysicsAssetTemplate;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddVirtualBones;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeNormalsGenerationStrategy NormalsGenerationStrategy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeTangentsGenerationStrategy TangentsGenerationStrategy;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bReverseTangents;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAutoGeneratePhysicsAssetBodies;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimePhysicsAssetAutoBodyConfig PhysicsAssetAutoBodyConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAutoGeneratePhysicsAssetConstraints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FglTFRuntimePhysicsConstraint> PhysicsConstraints;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeBoneBoundsFilterHook BoneBoundsFilter;

	FglTFRuntimeSkeletalMeshConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bOverwriteRefSkeleton = false;
		Skeleton = nullptr;
		bIgnoreSkin = false;
		OverrideSkinIndex = -1;
		BoundsScale = FVector::OneVector;
		bShiftBoundsByRootBone = false;
		bIgnoreMissingBones = false;
		bMergeAllBonesToBoneTree = false;
		Outer = nullptr;
		bPerPolyCollision = false;
		bDisableMorphTargets = false;
		bIgnoreEmptyMorphTargets = true;
		MorphTargetsDuplicateStrategy = EglTFRuntimeMorphTargetsDuplicateStrategy::Ignore;
		ShiftBounds = FVector::ZeroVector;
		bUseHighPrecisionUVs = false;
		PhysicsAssetTemplate = nullptr;
		bAddVirtualBones = false;
		NormalsGenerationStrategy = EglTFRuntimeNormalsGenerationStrategy::IfMissing;
		TangentsGenerationStrategy = EglTFRuntimeTangentsGenerationStrategy::IfMissing;
		bReverseTangents = false;
		bAutoGeneratePhysicsAssetBodies = false;
		bAutoGeneratePhysicsAssetConstraints = false;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimePathItem
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString Path;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 Index;

	FglTFRuntimePathItem()
	{
		Index = INDEX_NONE;
	}
};

DECLARE_DYNAMIC_DELEGATE_RetVal_FourParams(FString, FglTFRuntimeAnimationCurveRemapper, const int32, NodeIndex, const FString&, CurveName, const FString&, Path, UObject*, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_FourParams(FVector, FglTFRuntimeAnimationFrameTranslationRemapper, const FString&, CurveName, const int32, FrameNumber, FVector, Translation, UObject*, Context);
DECLARE_DYNAMIC_DELEGATE_RetVal_FourParams(FRotator, FglTFRuntimeAnimationFrameRotationRemapper, const FString&, CurveName, const int32, FrameNumber, FRotator, Rotation, UObject*, Context);

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletalAnimationCurveRemapperHook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeAnimationCurveRemapper Remapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Context = nullptr;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletalAnimationFrameTranslationRemapperHook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeAnimationFrameTranslationRemapper Remapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Context = nullptr;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletalAnimationFrameRotationRemapperHook
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeAnimationFrameRotationRemapper Remapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UObject* Context = nullptr;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletalAnimationConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	EglTFRuntimeCacheMode CacheMode;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 RootNodeIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRootMotion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRemoveRootMotion;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TEnumAsByte<ERootMotionRootLock::Type> RootMotionRootLock;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRemoveTranslations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRemoveRotations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRemoveScales;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bRemoveMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FglTFRuntimePathItem> OverrideTrackNameFromExtension;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FString> RemoveTracks;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletalAnimationCurveRemapperHook CurveRemapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	USkeleton* RetargetTo;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	USkeletalMesh* RetargetToSkeletalMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> TransformPose;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletalAnimationFrameTranslationRemapperHook FrameTranslationRemapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletalAnimationFrameRotationRemapperHook FrameRotationRemapper;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float FramesPerSecond;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bFillAllCurves;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FString> CurvesNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 RetargetSkinIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	UPoseAsset* PoseForRetargeting;

	FglTFRuntimeSkeletalAnimationConfig()
	{
		RootNodeIndex = INDEX_NONE;
		bRootMotion = false;
		RootMotionRootLock = ERootMotionRootLock::RefPose;
		bRemoveRootMotion = false;
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bRemoveTranslations = false;
		bRemoveRotations = false;
		bRemoveScales = false;
		bRemoveMorphTargets = false;
		RetargetTo = nullptr;
		FramesPerSecond = 30.0f;
		bFillAllCurves = false;
		RetargetToSkeletalMesh = nullptr;
		RetargetSkinIndex = INDEX_NONE;
	}
};

struct FglTFRuntimeUInt16Vector4
{
	uint16 X;
	uint16 Y;
	uint16 Z;
	uint16 W;

	FglTFRuntimeUInt16Vector4()
	{
		X = 0;
		Y = 0;
		Z = 0;
		W = 0;
	}

	const uint16& operator[](const int32 Index) const
	{
		check(Index >= 0 && Index < 4);
		switch (Index)
		{
		case 0:
			return X;
		case 1:
			return Y;
		case 2:
			return Z;
		case 3:
		default:
			return W;
		}
	}

	uint16& operator[](const int32 Index)
	{
		check(Index >= 0 && Index < 4);
		switch (Index)
		{
		case 0:
			return X;
		case 1:
			return Y;
		case 2:
			return Z;
		case 3:
		default:
			return W;
		}
	}
};

struct FglTFRuntimePrimitive
{
	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector4> Tangents;
	TArray<TArray<FVector2D>> UVs;
	TArray<uint32> Indices;
	UMaterialInterface* Material;
	TArray<TArray<FglTFRuntimeUInt16Vector4>> Joints;
	TArray<TArray<FVector4>> Weights;
	TArray<FVector4> Colors;
	TArray<FglTFRuntimeMorphTarget> MorphTargets;
	TMap<int32, FName> OverrideBoneMap;
	TMap<int32, int32> BonesCache;
	FString MaterialName;
	int64 AdditionalBufferView;
	int32 Mode;
	bool bHasMaterial;
	bool bHighPrecisionUVs;
	bool bHighPrecisionWeights;

	bool bDisableShadows;
	bool bHasIndices;

	FglTFRuntimePrimitive()
	{
		AdditionalBufferView = INDEX_NONE;
		bHasMaterial = false;
		bHighPrecisionUVs = false;
		bHighPrecisionWeights = false;
		Material = nullptr;
		Mode = 4;
		bDisableShadows = false;
		bHasIndices = false;
	}
};

struct FglTFRuntimeSkeletalMeshContext : public FGCObject
{
	TSharedRef<class FglTFRuntimeParser> Parser;

	TArray<FglTFRuntimeMeshLOD*> LODs;

	const FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TObjectPtr<USkeletalMesh> SkeletalMesh;
#else
	USkeletalMesh* SkeletalMesh;
#endif

	int32 SkinIndex;

	FBox BoundingBox;

	TMap<int32, FBox> PerBoneBoundingBoxCache;

	// here we cache per-context LODs
	TArray<FglTFRuntimeMeshLOD> CachedRuntimeMeshLODs;

	// for LOD generators
	TArray<FglTFRuntimeMeshLOD> ContextLODs;
	TMap<int32, int32> ContextLODsMap;

	const int32 MeshIndex;

	FglTFRuntimeSkeletalMeshContext(TSharedRef<FglTFRuntimeParser> InParser, const int32 InMeshIndex, const FglTFRuntimeSkeletalMeshConfig& InSkeletalMeshConfig) : Parser(InParser), SkeletalMeshConfig(InSkeletalMeshConfig), MeshIndex(InMeshIndex)
	{
		EObjectFlags Flags = RF_Public;
		UObject* Outer = InSkeletalMeshConfig.Outer ? InSkeletalMeshConfig.Outer : GetTransientPackage();
#if WITH_EDITOR
		// TODO: get rid of this, it was a bad idea!
		// a generic plugin for saving transient assets will be a better (and saner) approach
		if (!InSkeletalMeshConfig.SaveToPackage.IsEmpty())
		{
			if (FindPackage(nullptr, *InSkeletalMeshConfig.SaveToPackage) || LoadPackage(nullptr, *InSkeletalMeshConfig.SaveToPackage, RF_Public | RF_Standalone))
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("UPackage %s already exists. Falling back to Transient."), *InSkeletalMeshConfig.SaveToPackage);
				Outer = GetTransientPackage();
			}
			else
			{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 26
				Outer = CreatePackage(*InSkeletalMeshConfig.SaveToPackage);
#else
				Outer = CreatePackage(nullptr, *InSkeletalMeshConfig.SaveToPackage);
#endif
				if (!Outer)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to get UPackage %s. Falling back to Transient."), *InSkeletalMeshConfig.SaveToPackage);
					Outer = GetTransientPackage();
				}
				else
				{
					Flags |= RF_Standalone;
				}
			}
		}
#endif
		SkeletalMesh = NewObject<USkeletalMesh>(Outer, NAME_None, Flags);
		SkeletalMesh->NeverStream = true;
		BoundingBox = FBox(EForceInit::ForceInitToZero);
		SkinIndex = -1;
	}

	FString GetReferencerName() const override
	{
		return "FglTFRuntimeSkeletalMeshContext_Referencer";
	}

	void AddReferencedObjects(FReferenceCollector& Collector) override
	{
		Collector.AddReferencedObject(SkeletalMesh);
	}

	const FReferenceSkeleton& GetRefSkeleton() const
	{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		return SkeletalMesh->GetRefSkeleton();
#else
		return SkeletalMesh->RefSkeleton;
#endif
	}

	USkeleton* GetSkeleton() const
	{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		return SkeletalMesh->GetSkeleton();
#else
		return SkeletalMesh->Skeleton;
#endif
	}

	int32 GetBoneIndex(const FString& BoneName) const
	{
		return GetRefSkeleton().FindBoneIndex(*BoneName);
	}

	int32 GetBoneIndex(const FName& BoneName) const
	{
		return GetRefSkeleton().FindBoneIndex(BoneName);
	}

	int32 GetNumBones() const
	{
		return GetRefSkeleton().GetNum();
	}

	int32 GetBoneParentIndex(const int32 BoneIndex) const
	{
		return GetRefSkeleton().GetParentIndex(BoneIndex);
	}

	FName GetBoneName(const int32 BoneIndex) const
	{
		return GetRefSkeleton().GetBoneName(BoneIndex);
	}

	FTransform GetBoneLocalTransform(const int32 BoneIndex) const
	{
		return GetRefSkeleton().GetRefBonePose()[BoneIndex];
	}

	FTransform GetBoneWorldTransform(const int32 BoneIndex) const
	{
		FTransform Transform = GetBoneLocalTransform(BoneIndex);
		int32 ParentIndex = GetBoneParentIndex(BoneIndex);
		while (ParentIndex > INDEX_NONE)
		{
			Transform *= GetBoneLocalTransform(ParentIndex);
			ParentIndex = GetBoneParentIndex(ParentIndex);
		}
		return Transform;
	}

	FTransform GetBoneDeltaTransform(const int32 BoneIndex, const int32 InParentIndex) const
	{
		FTransform Transform = GetBoneLocalTransform(BoneIndex);
		int32 ParentIndex = GetBoneParentIndex(BoneIndex);
		while (ParentIndex > INDEX_NONE && ParentIndex != InParentIndex)
		{
			Transform *= GetBoneLocalTransform(ParentIndex);
			ParentIndex = GetBoneParentIndex(ParentIndex);
		}
		return Transform;
	}

	FglTFRuntimeMeshLOD& AddContextLOD()
	{
		const int32 NewIndex = ContextLODs.AddDefaulted();
		const int32 NewPtrIndex = LODs.AddUninitialized();
		ContextLODsMap.Add(NewIndex, NewPtrIndex);
		// rebuild ContextLODs pointers (as they could have changed)
		for (const TPair<int32, int32>& Pair : ContextLODsMap)
		{
			LODs[Pair.Value] = &ContextLODs[Pair.Key];
		}
		return ContextLODs[NewIndex];
	}

	bool BoneHasChildren(const int32 BoneIndex) const
	{
		const int32 NumBones = GetNumBones();
		for (int32 CurrentBoneIndex = 0; CurrentBoneIndex < NumBones; CurrentBoneIndex++)
		{
			if (GetBoneParentIndex(CurrentBoneIndex) == BoneIndex)
			{
				return true;
			}
		}
		return false;
	}

	bool BoneIsChildOf(const int32 BoneIndex, const int32 BoneParentIndex) const
	{
		int32 ParentIndex = GetBoneParentIndex(BoneIndex);
		while (ParentIndex > INDEX_NONE)
		{
			if (ParentIndex == BoneParentIndex)
			{
				return true;
			}
			ParentIndex = GetBoneParentIndex(ParentIndex);
		}
		return false;
	}

	const FBox& GetBoneBox(const int32 BoneIndex);
};

USTRUCT(BlueprintType)
struct FglTFRuntimeMeshLOD
{
	GENERATED_BODY()

	TArray<FglTFRuntimePrimitive> Primitives;
	TArray<FTransform> AdditionalTransforms;
	TArray<FglTFRuntimeBone> Skeleton;

	bool bHasNormals;
	bool bHasTangents;
	bool bHasUV;
	bool bHasVertexColors;

	FglTFRuntimeMeshLOD()
	{
		bHasNormals = false;
		bHasTangents = false;
		bHasUV = false;
		bHasVertexColors = false;
	}

	void Empty()
	{
		Primitives.Empty();
		AdditionalTransforms.Empty();
		Skeleton.Empty();
	}
};

struct FglTFRuntimeStaticMeshContext : public FGCObject
{
	TSharedRef<class FglTFRuntimeParser> Parser;

	TArray<const FglTFRuntimeMeshLOD*> LODs;

	const FglTFRuntimeStaticMeshConfig StaticMeshConfig;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TObjectPtr<UStaticMesh> StaticMesh;
#else
	UStaticMesh* StaticMesh;
#endif
	FStaticMeshRenderData* RenderData;
	FBoxSphereBounds BoundingBoxAndSphere;
	FVector LOD0PivotDelta = FVector::ZeroVector;
	TArray<FStaticMaterial> StaticMaterials;

	TMap<FString, FTransform> AdditionalSockets;
	TArray<FglTFRuntimeMeshLOD> ContextLODs;
	TMap<int32, int32> ContextLODsMap;

	const int32 MeshIndex;

	FglTFRuntimeStaticMeshContext(TSharedRef<FglTFRuntimeParser> InParser, const int32 InMeshIndex, const FglTFRuntimeStaticMeshConfig& InStaticMeshConfig);

	FString GetReferencerName() const override
	{
		return "FglTFRuntimeStaticMeshContext_Referencer";
	}

	void AddReferencedObjects(FReferenceCollector& Collector)
	{
		Collector.AddReferencedObject(StaticMesh);
	}

	FglTFRuntimeMeshLOD& AddContextLOD()
	{
		const int32 NewIndex = ContextLODs.AddDefaulted();
		const int32 NewPtrIndex = LODs.AddUninitialized();
		ContextLODsMap.Add(NewIndex, NewPtrIndex);
		// rebuild ContextLODs pointers (as they could have changed)
		for (const TPair<int32, int32>& Pair : ContextLODsMap)
		{
			LODs[Pair.Value] = &ContextLODs[Pair.Key];
		}
		return ContextLODs[NewIndex];
	}
};

struct FglTFRuntimeMipMap
{
	const int32 TextureIndex;
	TArray64<uint8> Pixels;
	int32 Width;
	int32 Height;
	EPixelFormat PixelFormat;

	FglTFRuntimeMipMap(const int32 InTextureIndex) : TextureIndex(InTextureIndex)
	{
		Width = 0;
		Height = 0;
		PixelFormat = EPixelFormat::PF_B8G8R8A8;
	}

	FglTFRuntimeMipMap(const int32 InTextureIndex, const EPixelFormat InPixelFormat, const int32 InWidth, const int32 InHeight) :
		TextureIndex(InTextureIndex),
		Width(InWidth),
		Height(InHeight),
		PixelFormat(InPixelFormat)
	{
	}

	FglTFRuntimeMipMap(const int32 InTextureIndex, const EPixelFormat InPixelFormat, const int32 InWidth, const int32 InHeight, const TArray64<uint8>& InPixels) :
		TextureIndex(InTextureIndex),
		Pixels(InPixels),
		Width(InWidth),
		Height(InHeight),
		PixelFormat(InPixelFormat)
	{
	}

	FglTFRuntimeMipMap(const int32 InTextureIndex, const int32 InWidth, const int32 InHeight, const TArray64<uint8>& InPixels) :
		TextureIndex(InTextureIndex),
		Pixels(InPixels),
		Width(InWidth),
		Height(InHeight)
	{
		PixelFormat = EPixelFormat::PF_B8G8R8A8;
	}

	bool IsCompressed() const
	{
		return !(GPixelFormats[PixelFormat].BlockSizeX == 1 && GPixelFormats[PixelFormat].BlockSizeY == 1);
	}
};

class FglTFRuntimeTextureMipDataProvider : public FTextureMipDataProvider
{
public:
#if ENGINE_MAJOR_VERSION >= 5 || ENGINE_MINOR_VERSION >= 26
	FglTFRuntimeTextureMipDataProvider(const UTexture* Texture, ETickState InTickState, ETickThread InTickThread) : FTextureMipDataProvider(Texture, InTickState, InTickThread)
#else
	FglTFRuntimeTextureMipDataProvider(ETickState InTickState, ETickThread InTickThread) : FTextureMipDataProvider(InTickState, InTickThread)
#endif
	{
	}

	void Init(const FTextureUpdateContext& Context, const FTextureUpdateSyncOptions& SyncOptions)
	{
		AdvanceTo(ETickState::GetMips, ETickThread::Async);
	}

	int32 GetMips(const FTextureUpdateContext& Context, int32 StartingMipIndex, const FTextureMipInfoArray& MipInfos, const FTextureUpdateSyncOptions& SyncOptions);

	bool PollMips(const FTextureUpdateSyncOptions& SyncOptions)
	{
		AdvanceTo(ETickState::Done, ETickThread::None);
		return true;
	}

	void CleanUp(const FTextureUpdateSyncOptions& SyncOptions)
	{
		AdvanceTo(ETickState::Done, ETickThread::None);
	}

	void Cancel(const FTextureUpdateSyncOptions& SyncOptions)
	{
	}

	virtual ETickThread GetCancelThread() const
	{
		return ETickThread::None;
	}

};

UCLASS()
class UglTFRuntimeTextureMipDataProviderFactory : public UTextureMipDataProviderFactory
{
	GENERATED_BODY()

public:
#if ENGINE_MAJOR_VERSION >= 5 || ENGINE_MINOR_VERSION >= 26
	virtual FTextureMipDataProvider* AllocateMipDataProvider(UTexture* Asset) { return new FglTFRuntimeTextureMipDataProvider(Asset, FTextureMipDataProvider::ETickState::Init, FTextureMipDataProvider::ETickThread::Async); }
#else
	virtual FTextureMipDataProvider* AllocateMipDataProvider() { return new FglTFRuntimeTextureMipDataProvider(FTextureMipDataProvider::ETickState::Init, FTextureMipDataProvider::ETickThread::Async); }
#endif

#if ENGINE_MAJOR_VERSION >= 5
	virtual bool WillProvideMipDataWithoutDisk() const override { return true; }
#endif

};

struct FglTFRuntimeTextureTransform
{
	FLinearColor Offset;
	float Rotation;
	FLinearColor Scale;
	int32 TexCoord;

	FglTFRuntimeTextureTransform()
	{
		Offset = FLinearColor(0, 0, 0, 0);
		Rotation = 0;
		Scale = FLinearColor(1, 1, 1, 1);
		TexCoord = 0;
	}
};

struct FglTFRuntimeMaterial
{
	bool bTwoSided;
	bool bTranslucent;
	float AlphaCutoff;
	EglTFRuntimeMaterialType MaterialType;

	bool bHasBaseColorFactor;
	FLinearColor BaseColorFactor;

	TArray<FglTFRuntimeMipMap> BaseColorTextureMips;
	UTexture2D* BaseColorTextureCache;
	FglTFRuntimeTextureTransform BaseColorTransform;
	FglTFRuntimeTextureSampler BaseColorSampler;

	bool bHasMetallicFactor;
	double MetallicFactor;
	bool bHasRoughnessFactor;
	double RoughnessFactor;

	TArray<FglTFRuntimeMipMap> MetallicRoughnessTextureMips;
	UTexture2D* MetallicRoughnessTextureCache;
	FglTFRuntimeTextureTransform MetallicRoughnessTransform;
	FglTFRuntimeTextureSampler MetallicRoughnessSampler;

	TArray<FglTFRuntimeMipMap> NormalTextureMips;
	UTexture2D* NormalTextureCache;
	FglTFRuntimeTextureTransform NormalTransform;
	FglTFRuntimeTextureSampler NormalSampler;

	TArray<FglTFRuntimeMipMap> OcclusionTextureMips;
	UTexture2D* OcclusionTextureCache;
	FglTFRuntimeTextureTransform OcclusionTransform;
	FglTFRuntimeTextureSampler OcclusionSampler;

	bool bHasEmissiveFactor;
	FLinearColor EmissiveFactor;

	TArray<FglTFRuntimeMipMap> EmissiveTextureMips;
	UTexture2D* EmissiveTextureCache;
	FglTFRuntimeTextureTransform EmissiveTransform;
	FglTFRuntimeTextureSampler EmissiveSampler;

	bool bHasSpecularFactor;
	FLinearColor SpecularFactor;

	bool bHasGlossinessFactor;
	double GlossinessFactor;

	TArray<FglTFRuntimeMipMap> SpecularGlossinessTextureMips;
	UTexture2D* SpecularGlossinessTextureCache;
	FglTFRuntimeTextureTransform SpecularGlossinessTransform;
	FglTFRuntimeTextureSampler SpecularGlossinessSampler;

	bool bKHR_materials_specular;
	double BaseSpecularFactor;
	TArray<FglTFRuntimeMipMap> SpecularTextureMips;
	UTexture2D* SpecularTextureCache;
	FglTFRuntimeTextureTransform SpecularTransform;
	FglTFRuntimeTextureSampler SpecularSampler;

	bool bHasDiffuseFactor;
	FLinearColor DiffuseFactor;

	TArray<FglTFRuntimeMipMap> DiffuseTextureMips;
	UTexture2D* DiffuseTextureCache;
	FglTFRuntimeTextureTransform DiffuseTransform;
	FglTFRuntimeTextureSampler DiffuseSampler;

	bool bKHR_materials_pbrSpecularGlossiness;
	double NormalTextureScale;

	bool bKHR_materials_transmission;
	bool bHasTransmissionFactor;
	double TransmissionFactor;
	TArray<FglTFRuntimeMipMap> TransmissionTextureMips;
	UTexture2D* TransmissionTextureCache;
	FglTFRuntimeTextureTransform TransmissionTransform;
	FglTFRuntimeTextureSampler TransmissionSampler;

	bool bMasked;

	bool bKHR_materials_unlit;

	bool bHasIOR;
	double IOR;

	bool bKHR_materials_clearcoat;
	double ClearCoatFactor;
	double ClearCoatRoughnessFactor;

	bool bKHR_materials_emissive_strength;
	double EmissiveStrength;

	bool bKHR_materials_volume;
	bool bHasThicknessFactor;
	double ThicknessFactor;
	TArray<FglTFRuntimeMipMap> ThicknessTextureMips;
	UTexture2D* ThicknessTextureCache;
	FglTFRuntimeTextureTransform ThicknessTransform;
	FglTFRuntimeTextureSampler ThicknessSampler;
	double AttenuationDistance;
	FLinearColor AttenuationColor;

	FglTFRuntimeMaterial()
	{
		bTwoSided = false;
		bTranslucent = false;
		AlphaCutoff = 0;
		MaterialType = EglTFRuntimeMaterialType::Opaque;
		bHasBaseColorFactor = false;
		BaseColorTextureCache = nullptr;
		bHasMetallicFactor = false;
		bHasRoughnessFactor = false;
		MetallicRoughnessTextureCache = nullptr;
		NormalTextureCache = nullptr;
		OcclusionTextureCache = nullptr;
		bHasEmissiveFactor = false;
		EmissiveTextureCache = nullptr;
		bHasSpecularFactor = false;
		bHasGlossinessFactor = false;
		SpecularGlossinessTextureCache = nullptr;
		BaseSpecularFactor = 0;
		bHasDiffuseFactor = false;
		DiffuseTextureCache = nullptr;
		bKHR_materials_pbrSpecularGlossiness = false;
		NormalTextureScale = 1;
		bKHR_materials_transmission = false;
		bHasTransmissionFactor = true;
		TransmissionFactor = 0;
		TransmissionTextureCache = nullptr;
		bMasked = false;
		bKHR_materials_unlit = false;
		bHasIOR = false;
		IOR = 1;
		bKHR_materials_clearcoat = false;
		bKHR_materials_specular = false;
		SpecularTextureCache = nullptr;
		bKHR_materials_emissive_strength = false;
		EmissiveStrength = 1;
		bKHR_materials_volume = false;
		bHasThicknessFactor = true;
		ThicknessFactor = 0;
		ThicknessTextureCache = nullptr;
		AttenuationDistance = 0;
		AttenuationColor = FLinearColor::White;
	}
};

class FglTFRuntimeArchive
{
public:
	virtual ~FglTFRuntimeArchive() {}

	virtual bool GetFileContent(const FString& Filename, TArray64<uint8>& OutData) = 0;

	bool FileExists(const FString& Filename) const;

	FString GetFirstFilenameByExtension(const FString& Extension) const;

	void GetItems(TArray<FString>& Items) const
	{
		OffsetsMap.GetKeys(Items);
	}

protected:
	TMap<FString, uint32> OffsetsMap;
};

class FglTFRuntimeArchiveZip : public FglTFRuntimeArchive
{
public:
	bool FromData(const uint8* DataPtr, const int64 DataNum);

	bool GetFileContent(const FString& Filename, TArray64<uint8>& OutData) override;

	void SetPassword(const FString& EncryptionKey);

protected:
	FArrayReader Data;
	TArray<uint8> Password;
};

class FglTFRuntimeArchiveMap : public FglTFRuntimeArchive
{
public:
	void FromMap(const TMap<FString, TArray64<uint8>> InMap);

	bool GetFileContent(const FString& Filename, TArray64<uint8>& OutData) override;

protected:
	TArray<TArray64<uint8>> MapItems;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeAudioEmitter
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float Volume;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	USoundBase* Sound;

	FglTFRuntimeAudioEmitter()
	{
		Sound = nullptr;
		Volume = 1.0f;
	}
};

struct FglTFRuntimeAnimationCurve
{
	TArray<float> Timeline;
	TArray<FVector4> Values;
	TArray<FVector4> InTangents;
	TArray<FVector4> OutTangents;
	bool bStep;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeAudioConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bLoop;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	float Volume;

	FglTFRuntimeAudioConfig()
	{
		bLoop = false;
		Volume = 1.0f;
	}
};

class FglTFRuntimeDDS
{
public:
	FglTFRuntimeDDS() = delete;
	FglTFRuntimeDDS(const FglTFRuntimeDDS&) = delete;
	FglTFRuntimeDDS& operator=(const FglTFRuntimeDDS&) = delete;

	FglTFRuntimeDDS(const TArray64<uint8>& InData);
	void LoadMips(const int32 TextureIndex, TArray<FglTFRuntimeMipMap>& Mips, const int32 MaxMip, const FglTFRuntimeImagesConfig& ImagesConfig);

	static bool IsDDS(const TArray64<uint8>& Data);
protected:
	const TArray64<uint8>& Data;
};

// generic struct for plugins cache
struct FglTFRuntimePluginCacheData
{
	bool bValid = false;
};

DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeStaticMeshAsync, UStaticMesh*, StaticMesh);
DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeSkeletalMeshAsync, USkeletalMesh*, SkeletalMesh);
DECLARE_DYNAMIC_DELEGATE_TwoParams(FglTFRuntimeMeshLODAsync, const bool, bValid, const FglTFRuntimeMeshLOD&, MeshLOD);
DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeTextureCubeAsync, UTextureCube*, TextureCube);
DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeTexture2DAsync, UTexture2D*, Texture);
DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeTexture2DArrayAsync, UTexture2DArray*, TextureArray);

using FglTFRuntimeStaticMeshContextRef = TSharedRef<FglTFRuntimeStaticMeshContext, ESPMode::ThreadSafe>;
using FglTFRuntimeSkeletalMeshContextRef = TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>;
using FglTFRuntimePoseTracksMap = TMap<FString, FRawAnimSequenceTrack>;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnPreLoadedPrimitive, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, FglTFRuntimePrimitive&);
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnLoadedPrimitive, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, FglTFRuntimePrimitive&);
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnLoadedRefSkeleton, TSharedRef<FglTFRuntimeParser>, TSharedPtr<FJsonObject>, FReferenceSkeletonModifier&);
DECLARE_TS_MULTICAST_DELEGATE_TwoParams(FglTFRuntimeOnCreatedPoseTracks, TSharedRef<FglTFRuntimeParser>, FglTFRuntimePoseTracksMap&);
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnTextureImageIndex, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, int64&);
DECLARE_TS_MULTICAST_DELEGATE_SevenParams(FglTFRuntimeOnTextureMips, TSharedRef<FglTFRuntimeParser>, const int32, TSharedRef<FJsonObject>, TSharedRef<FJsonObject>, const TArray64<uint8>&, TArray<FglTFRuntimeMipMap>&, const FglTFRuntimeImagesConfig&);
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnTextureFilterMips, TSharedRef<FglTFRuntimeParser>, TArray<FglTFRuntimeMipMap>&, const FglTFRuntimeImagesConfig&);
DECLARE_TS_MULTICAST_DELEGATE_EightParams(FglTFRuntimeOnTexturePixels, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, const TArray64<uint8>&, int32&, int32&, EPixelFormat&, TArray64<uint8>&, const FglTFRuntimeImagesConfig&);
DECLARE_TS_MULTICAST_DELEGATE_FiveParams(FglTFRuntimeOnLoadedTexturePixels, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, const int32, const int32, FColor*);
DECLARE_TS_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPreCreatedStaticMesh, FglTFRuntimeStaticMeshContextRef);
DECLARE_TS_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPostCreatedStaticMesh, FglTFRuntimeStaticMeshContextRef);
DECLARE_TS_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPreCreatedSkeletalMesh, FglTFRuntimeSkeletalMeshContextRef);
DECLARE_TS_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnFinalizedStaticMesh, TSharedRef<FglTFRuntimeParser>, UStaticMesh*, const FglTFRuntimeStaticMeshConfig&);
#else
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnPreLoadedPrimitive, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, FglTFRuntimePrimitive&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnLoadedPrimitive, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, FglTFRuntimePrimitive&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnLoadedRefSkeleton, TSharedRef<FglTFRuntimeParser>, TSharedPtr<FJsonObject>, FReferenceSkeletonModifier&);
DECLARE_MULTICAST_DELEGATE_TwoParams(FglTFRuntimeOnCreatedPoseTracks, TSharedRef<FglTFRuntimeParser>, FglTFRuntimePoseTracksMap&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnTextureImageIndex, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, int64&);
DECLARE_MULTICAST_DELEGATE_SevenParams(FglTFRuntimeOnTextureMips, TSharedRef<FglTFRuntimeParser>, const int32, TSharedRef<FJsonObject>, TSharedRef<FJsonObject>, const TArray64<uint8>&, TArray<FglTFRuntimeMipMap>&, const FglTFRuntimeImagesConfig&);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnTextureFilterMips, TSharedRef<FglTFRuntimeParser>, TArray<FglTFRuntimeMipMap>&, const FglTFRuntimeImagesConfig&);
DECLARE_MULTICAST_DELEGATE_EightParams(FglTFRuntimeOnTexturePixels, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, const TArray64<uint8>&, int32&, int32&, EPixelFormat&, TArray64<uint8>&, const FglTFRuntimeImagesConfig&);
DECLARE_MULTICAST_DELEGATE_FiveParams(FglTFRuntimeOnLoadedTexturePixels, TSharedRef<FglTFRuntimeParser>, TSharedRef<FJsonObject>, const int32, const int32, FColor*);
DECLARE_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPreCreatedStaticMesh, FglTFRuntimeStaticMeshContextRef);
DECLARE_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPostCreatedStaticMesh, FglTFRuntimeStaticMeshContextRef);
DECLARE_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnPreCreatedSkeletalMesh, FglTFRuntimeSkeletalMeshContextRef);
DECLARE_MULTICAST_DELEGATE_ThreeParams(FglTFRuntimeOnFinalizedStaticMesh, TSharedRef<FglTFRuntimeParser>, UStaticMesh*, const FglTFRuntimeStaticMeshConfig&);
#endif

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeParser : public FGCObject, public TSharedFromThis<FglTFRuntimeParser>
{
public:
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, const FMatrix& InSceneBasis, float InSceneScale);

	static TSharedPtr<FglTFRuntimeParser> FromFilename(const FString& Filename, const FglTFRuntimeConfig& LoaderConfig);
	static TSharedPtr<FglTFRuntimeParser> FromBinary(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive = nullptr);
	static TSharedPtr<FglTFRuntimeParser> FromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive = nullptr);
	static TSharedPtr<FglTFRuntimeParser> FromData(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig);
	static TSharedPtr<FglTFRuntimeParser> FromMap(const TMap<FString, TArray64<uint8>> Map, const FglTFRuntimeConfig& LoaderConfig);

	static TSharedPtr<FglTFRuntimeParser> FromRawDataAndArchive(const uint8* DataPtr, int64 DataNum, TSharedPtr<FglTFRuntimeArchive> InArchive, const FglTFRuntimeConfig& LoaderConfig);

	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromBinary(const TArray<uint8> Data, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive = nullptr) { return FromBinary(Data.GetData(), Data.Num(), LoaderConfig, InArchive); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromBinary(const TArray64<uint8> Data, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeArchive> InArchive = nullptr) { return FromBinary(Data.GetData(), Data.Num(), LoaderConfig, InArchive); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromData(const TArray<uint8> Data, const FglTFRuntimeConfig& LoaderConfig) { return FromData(Data.GetData(), Data.Num(), LoaderConfig); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromData(const TArray64<uint8> Data, const FglTFRuntimeConfig& LoaderConfig) { return FromData(Data.GetData(), Data.Num(), LoaderConfig); }

	bool LoadMeshAsRuntimeLOD(const int32 MeshIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	bool LoadSkinnedMeshRecursiveAsRuntimeLOD(const FString& NodeName, int32& SkinIndex, const TArray<FString>& ExcludeNodes, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode);
	void LoadSkinnedMeshRecursiveAsRuntimeLODAsync(const FString& NodeName, int32& SkinIndex, const TArray<FString>& ExcludeNodes, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode);

	UStaticMesh* LoadStaticMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	void LoadStaticMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	UStaticMesh* LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	bool LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	TArray<UStaticMesh*> LoadStaticMeshesFromPrimitives(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UStaticMesh* LoadStaticMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	void LoadStaticMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UStaticMesh* LoadStaticMeshLODs(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UStaticMesh* LoadStaticMeshByName(const FString MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UMaterialInterface* LoadMaterial(const int32 MaterialIndex, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, FString& MaterialName, UMaterialInterface* ForceBaseMaterial);
	UTexture2D* LoadTexture(const int32 TextureIndex, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig, FglTFRuntimeTextureSampler& Sampler);

	bool LoadNodes();
	bool LoadNode(const int32 NodeIndex, FglTFRuntimeNode& Node);
	bool LoadNodeByName(const FString& NodeName, FglTFRuntimeNode& Node);
	bool LoadNodesRecursive(const int32 NodeIndex, TArray<FglTFRuntimeNode>& Nodes);
	bool LoadJointByName(const int64 RootBoneIndex, const FString& Name, FglTFRuntimeNode& Node);
	int32 AddFakeRootNode(const FString& BaseName);

	bool LoadScenes(TArray<FglTFRuntimeScene>& Scenes);
	bool LoadScene(int32 SceneIndex, FglTFRuntimeScene& Scene);

	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	USkeletalMesh* LoadSkeletalMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	void LoadSkeletalMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
	UAnimSequence* LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString AnimationName, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
	UAnimSequence* LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
	TMap<FString, UAnimSequence*> LoadNodeSkeletalAnimationsMap(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);
	USkeleton* LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig);
	USkeleton* LoadSkeletonFromNode(const FglTFRuntimeNode& Node, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UAnimSequence* LoadSkeletalAnimationFromTracksAndMorphTargets(USkeletalMesh* SkeletalMesh, TMap<FString, FRawAnimSequenceTrack>& Tracks, TMap<FName, TArray<TPair<float, float>>>& MorphTargetCurves, const float Duration, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	void LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	void LoadStaticMeshAsync(const int32 MeshIndex, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	void LoadStaticMeshLODsAsync(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	void LoadMeshAsRuntimeLODAsync(const int32 MeshIndex, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	USkeletalMesh* LoadSkeletalMeshLODs(const TArray<int32>& MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);

	USkeletalMesh* LoadSkeletalMeshRecursive(const FString& NodeName, const int32 SkinIndex, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode);
	void LoadSkeletalMeshRecursiveAsync(const FString& NodeName, const int32 SkinIndex, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode);

	UglTFRuntimeAnimationCurve* LoadNodeAnimationCurve(const int32 NodeIndex);
	TArray<UglTFRuntimeAnimationCurve*> LoadAllNodeAnimationCurves(const int32 NodeIndex);

	bool GetBuffer(const int32 BufferIndex, FglTFRuntimeBlob& Blob);
	bool GetBufferView(const int32 BufferViewIndex, FglTFRuntimeBlob& Blob, int64& Stride);
	bool GetAccessor(const int32 AccessorIndex, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, bool& bNormalized, FglTFRuntimeBlob& Blob, const FglTFRuntimeBlob* AdditionalBufferView);

	bool GetAllNodes(TArray<FglTFRuntimeNode>& Nodes);

	TArray<FString> GetCamerasNames();

	bool LoadCameraIntoCameraComponent(const int32 CameraIndex, UCameraComponent* CameraComponent);

	bool LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent);

	int64 GetComponentTypeSize(const int64 ComponentType) const;
	int64 GetTypeSize(const FString& Type) const;

	bool ParseBase64Uri(const FString& Uri, TArray64<uint8>& Bytes);

	FString GetReferencerName() const override
	{
		return "FglTFRuntimeParser_Referencer";
	}

	void AddReferencedObjects(FReferenceCollector& Collector);

	bool LoadPrimitives(TSharedRef<FJsonObject> JsonMeshObject, TArray<FglTFRuntimePrimitive>& Primitives, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bTriangulatePointsAndLines);
	bool LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bTriangulatePointsAndLines);
	UMaterialInterface* TriangulatePoints(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	UMaterialInterface* TriangulateLines(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	UMaterialInterface* TriangulatePointsAndLines(FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	void AddError(const FString& ErrorContext, const FString& ErrorMessage);
	void ClearErrors();
	bool HasErrors() const;
	const TArray<FString>& GetErrors() const;

	bool NodeIsBone(const int32 NodeIndex);

	FTransform GetNodeWorldTransform(const FglTFRuntimeNode& Node);
	FTransform GetParentNodeWorldTransform(const FglTFRuntimeNode& Node);

	int32 GetNumMeshes() const;
	int32 GetNumImages() const;
	int32 GetNumAnimations() const;

	FglTFRuntimeError OnError;
	FglTFRuntimeOnStaticMeshCreated OnStaticMeshCreated;
	FglTFRuntimeOnSkeletalMeshCreated OnSkeletalMeshCreated;

	void SetBinaryBuffer(const TArray64<uint8>& InBinaryBuffer)
	{
		BinaryBuffer = InBinaryBuffer;
	}

	bool LoadStaticMeshIntoProceduralMeshComponent(const int32 MeshIndex, UProceduralMeshComponent* ProceduralMeshComponent, const FglTFRuntimeProceduralMeshConfig& ProceduralMeshConfig);

	USkeletalMesh* FinalizeSkeletalMeshWithLODs(TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext);

	UStaticMesh* FinalizeStaticMesh(TSharedRef<FglTFRuntimeStaticMeshContext, ESPMode::ThreadSafe> StaticMeshContext);

	static TSharedPtr<FJsonValue> GetJSONObjectFromRelativePath(TSharedRef<FJsonObject> JsonObject, const TArray<FglTFRuntimePathItem>& Path);
	TSharedPtr<FJsonValue> GetJSONObjectFromPath(const TArray<FglTFRuntimePathItem>& Path) const;

	FString GetJSONStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;
	double GetJSONNumberFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;
	bool GetJSONBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	int32 GetJSONArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;
	FVector4 GetJSONVectorFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	bool GetStringMapFromExtras(const FString& Key, TMap<FString, FString>& StringMap) const;
	bool GetStringArrayFromExtras(const FString& Key, TArray<FString>& StringArray) const;
	bool GetNumberArrayFromExtras(const FString& Key, TArray<float>& NumberArray) const;
	bool GetNumberFromExtras(const FString& Key, float& Value) const;
	bool GetStringFromExtras(const FString& Key, FString& Value) const;
	bool GetBooleanFromExtras(const FString& Key, bool& Value) const;

	bool LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter);
	ULightComponent* LoadPunctualLight(const int32 PunctualLightIndex, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig);

	TArray<FString> ExtensionsUsed;
	TArray<FString> ExtensionsRequired;

	FString GetVersion() const;
	FString GetGenerator() const;

	bool LoadImageBytes(const int32 ImageIndex, TSharedPtr<FJsonObject>& JsonImageObject, TArray64<uint8>& Bytes);
	bool LoadImage(const int32 ImageIndex, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, EPixelFormat& PixelFormat, const FglTFRuntimeImagesConfig& ImagesConfig);
	bool LoadImageFromBlob(const TArray64<uint8>& Blob, TSharedRef<FJsonObject> JsonImageObject, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, EPixelFormat& PixelFormat, const FglTFRuntimeImagesConfig& ImagesConfig);
	UTexture2D* BuildTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler);
	UTextureCube* BuildTextureCube(UObject* Outer, const TArray<FglTFRuntimeMipMap>& MipsXP, const TArray<FglTFRuntimeMipMap>& MipsXN, const TArray<FglTFRuntimeMipMap>& MipsYP, const TArray<FglTFRuntimeMipMap>& MipsYN, const TArray<FglTFRuntimeMipMap>& MipsZP, const TArray<FglTFRuntimeMipMap>& MipsZN, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler);
	UTexture2DArray* BuildTextureArray(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler);
	UVolumeTexture* BuildVolumeTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const int32 TileZ, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler);


	TArray<FString> MaterialsVariants;

	TSharedPtr<FJsonObject> GetJsonRoot() const { return Root; }

	static FVector4 CubicSpline(const float TC, const float T0, const float T1, const FVector4 Value0, const FVector4 OutTangent, const FVector4 Value1, const FVector4 InTangent);

	UAnimSequence* CreateAnimationFromPose(USkeletalMesh* SkeletalMesh, const int32 SkinIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	UAnimSequence* CreateSkeletalAnimationFromPath(USkeletalMesh* SkeletalMesh, const TArray<FglTFRuntimePathItem>& BonesPath, const TArray<FglTFRuntimePathItem>& MorphTargetsPath, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig);

	TArray<FString> GetAnimationsNames(const bool bIncludeUnnameds = true) const;

	TArray<TSharedRef<FJsonObject>> GetMeshes() const;
	TArray<TSharedRef<FJsonObject>> GetMeshPrimitives(TSharedRef<FJsonObject> Mesh) const;
	TArray<TSharedRef<FJsonObject>> GetMaterials() const;
	TSharedPtr<FJsonObject> GetJsonObjectExtras(TSharedRef<FJsonObject> JsonObject) const;
	TSharedPtr<FJsonObject> GetJsonObjectFromObject(TSharedRef<FJsonObject> JsonObject, const FString& Name) const;
	TSharedPtr<FJsonObject> GetJsonObjectExtension(TSharedRef<FJsonObject> JsonObject, const FString& Name) const;
	int64 GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& Name) const;

	TArray<TSharedRef<FJsonObject>> GetAnimations() const;

	static FglTFRuntimeOnLoadedPrimitive OnPreLoadedPrimitive;
	static FglTFRuntimeOnLoadedPrimitive OnLoadedPrimitive;
	static FglTFRuntimeOnLoadedRefSkeleton OnLoadedRefSkeleton;
	static FglTFRuntimeOnCreatedPoseTracks OnCreatedPoseTracks;
	static FglTFRuntimeOnTextureImageIndex OnTextureImageIndex;
	static FglTFRuntimeOnTextureMips OnTextureMips;
	static FglTFRuntimeOnTextureFilterMips OnTextureFilterMips;
	static FglTFRuntimeOnTexturePixels OnTexturePixels;
	static FglTFRuntimeOnLoadedTexturePixels OnLoadedTexturePixels;
	static FglTFRuntimeOnFinalizedStaticMesh OnFinalizedStaticMesh;
	static FglTFRuntimeOnPreCreatedStaticMesh OnPreCreatedStaticMesh;
	static FglTFRuntimeOnPostCreatedStaticMesh OnPostCreatedStaticMesh;
	static FglTFRuntimeOnPreCreatedSkeletalMesh OnPreCreatedSkeletalMesh;

	const FglTFRuntimeBlob* GetAdditionalBufferView(const int64 Index, const FString& Name) const;

	void AddAdditionalBufferView(const int64 Index, const FString& Name, const FglTFRuntimeBlob& Blob);

	template<typename T>
	void AddAdditionalBufferViewData(const int64 Index, const FString& Name, const T* Data, const int64 Num)
	{
		TArray64<uint8> NewArray;
		NewArray.Append(reinterpret_cast<const uint8*>(Data), Num);

		int32 NewIndex = AdditionalBufferViewsData.Add(MoveTemp(NewArray));

		FglTFRuntimeBlob Blob;
		Blob.Data = AdditionalBufferViewsData[NewIndex].GetData();
		Blob.Num = Num;

		AddAdditionalBufferView(Index, Name, Blob);
	}

	template<typename T>
	void AddAdditionalBufferViewData(const int64 Index, const FString& Name, const T& Array)
	{
		AddAdditionalBufferViewData(Index, Name, Array.GetData(), Array.Num() * Array.GetTypeSize());
	}


	template<typename Callback, typename... Args>
	void ForEachJsonField(TSharedRef<FJsonObject> JsonObject, Callback InCallback, Args... InArgs)
	{
		for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : JsonObject->Values)
		{
			if (Pair.Value.IsValid())
			{
				InCallback(Pair.Key, Pair.Value.ToSharedRef(), InArgs...);
			}
		}
	}

	template<typename Callback, typename... Args>
	void ForEachJsonFieldAsIndex(TSharedRef<FJsonObject> JsonObject, Callback InCallback, Args... InArgs)
	{
		ForEachJsonField(JsonObject, [InCallback, &InArgs...](const FString& Key, TSharedRef<FJsonValue> Value)
			{
				int64 Index = INDEX_NONE;
				if (Value->TryGetNumber(Index))
				{
					InCallback(Key, Index, InArgs...);
				}
			});
	}

	TSharedPtr<FJsonObject> GetNodeExtensionObject(const int32 NodeIndex, const FString& ExtensionName);
	TSharedPtr<FJsonObject> GetNodeObject(const int32 NodeIndex);

	int32 GetNodeDistance(const FglTFRuntimeNode& Node, const int32 Ancestor);

	FString GetJsonObjectString(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FString& DefaultValue) const;
	double GetJsonObjectNumber(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const double DefaultValue);
	int32 GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 DefaultValue);
	int32 GetJsonExtensionObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 DefaultValue);
	double GetJsonExtensionObjectNumber(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const double DefaultValue);
	TArray<int32> GetJsonExtensionObjectIndices(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName);
	TArray<double> GetJsonExtensionObjectNumbers(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName);
	TArray<TSharedRef<FJsonObject>> GetJsonObjectArrayOfObjects(TSharedRef<FJsonObject> JsonObject, const FString& FieldName);
	FVector4 GetJsonObjectVector4(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FVector4 DefaultValue);

	bool GetRootBoneIndex(TSharedRef<FJsonObject> JsonSkinObject, int64& RootBoneIndex, TArray<int32>& Joints, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	void ClearCache();

	void MergePrimitivesByMaterial(TArray<FglTFRuntimePrimitive>& Primitives);

	bool MeshHasMorphTargets(const int32 MeshIndex) const;

	void FillAssetUserData(const int32 Index, IInterface_AssetUserData* InObject);
protected:
	void LoadAndFillBaseMaterials();
	TSharedRef<FJsonObject> Root;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TMap<int32, TObjectPtr<UStaticMesh>> StaticMeshesCache;
	TMap<int32, TObjectPtr<UMaterialInterface>> MaterialsCache;
	TMap<int32, TObjectPtr<USkeleton>> SkeletonsCache;
	TMap<int32, TObjectPtr<USkeletalMesh>> SkeletalMeshesCache;
	TMap<int32, TObjectPtr<UTexture2D>> TexturesCache;
#else
	TMap<int32, UStaticMesh*> StaticMeshesCache;
	TMap<int32, UMaterialInterface*> MaterialsCache;
	TMap<int32, USkeleton*> SkeletonsCache;
	TMap<int32, USkeletalMesh*> SkeletalMeshesCache;
	TMap<int32, UTexture2D*> TexturesCache;
#endif

	TMap<int32, TArray64<uint8>> BuffersCache;
	TMap<int32, TArray64<uint8>> CompressedBufferViewsCache;
	TMap<int32, int64> CompressedBufferViewsStridesCache;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TMap<TObjectPtr<UMaterialInterface>, FString> MaterialsNameCache;
#else
	TMap<UMaterialInterface*, FString> MaterialsNameCache;
#endif

	TArray<FglTFRuntimeNode> AllNodesCache;
	bool bAllNodesCached;

	TMap<TSharedRef<FJsonObject>, FglTFRuntimeMeshLOD> LODsCache;

	TArray64<uint8> BinaryBuffer;

	bool LoadMeshIntoMeshLOD(TSharedRef<FJsonObject> JsonMeshObject, FglTFRuntimeMeshLOD*& LOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	UStaticMesh* LoadStaticMesh_Internal(TSharedRef<FglTFRuntimeStaticMeshContext, ESPMode::ThreadSafe> StaticMeshContext);
	UMaterialInterface* LoadMaterial_Internal(const int32 Index, const FString& MaterialName, TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, UMaterialInterface* ForceBaseMaterial);
	bool LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node);

	bool LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>& Tracks, TMap<FName, TArray<TPair<float, float>>>& MorphTargetCurves, float& Duration, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig, TFunctionRef<bool(const FglTFRuntimeNode& Node)> Filter);

	bool LoadAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, float& Duration, FString& Name, TFunctionRef<void(const FglTFRuntimeNode& Node, const FString& Path, const FglTFRuntimeAnimationCurve& Curve)> Callback, TFunctionRef<bool(const FglTFRuntimeNode& Node)> NodeFilter, const TArray<FglTFRuntimePathItem>& OverrideTrackNameFromExtension);

	USkeletalMesh* CreateSkeletalMeshFromLODs(TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext);

	bool FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig);
	bool FillReferenceSkeletonFromNode(const FglTFRuntimeNode& RootNode, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig);
	bool FillFakeSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	bool FillLODSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const TArray<FglTFRuntimeBone>& Skeleton);
	bool TraverseJoints(FReferenceSkeletonModifier& Modifier, const int32 RootIndex, int32 Parent, const FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	bool GetMorphTargetNames(const int32 MeshIndex, TArray<FName>& MorphTargetNames);

	void FixNodeParent(FglTFRuntimeNode& Node);

	int32 FindCommonRoot(const TArray<int32>& NodeIndices);
	int32 FindTopRoot(int32 NodeIndex);
	bool HasRoot(int32 NodeIndex, int32 RootIndex);

	void GeneratePhysicsAsset_Internal(FglTFRuntimeSkeletalMeshContextRef SkeletalMeshContext);

	TArray<TSubclassOf<class UglTFRuntimeAssetUserData>> AssetUserDataClasses;

public:
	UMaterialInterface* BuildMaterial(const int32 Index, const FString& MaterialName, const FglTFRuntimeMaterial& RuntimeMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, UMaterialInterface* ForceBaseMaterial = nullptr);
	UMaterialInterface* BuildVertexColorOnlyMaterial(const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUnlit);

	bool CheckJsonIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems) const;
	bool CheckJsonRootIndex(const FString FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems) const { return CheckJsonIndex(Root, FieldName, Index, JsonItems); }
	TSharedPtr<FJsonObject> GetJsonObjectFromIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index) const;
	TSharedPtr<FJsonObject> GetJsonObjectFromRootIndex(const FString& FieldName, const int32 Index) const { return GetJsonObjectFromIndex(Root, FieldName, Index); }
	TSharedPtr<FJsonObject> GetJsonObjectFromExtensionIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 Index);
	TSharedPtr<FJsonObject> GetJsonObjectFromRootExtensionIndex(const FString& ExtensionName, const FString& FieldName, const int32 Index) { return GetJsonObjectFromExtensionIndex(Root, ExtensionName, FieldName, Index); }
	TArray<TSharedRef<FJsonObject>> GetJsonObjectArrayFromExtension(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName);
	TArray<TSharedRef<FJsonObject>> GetJsonObjectArrayFromRootExtension(const FString& ExtensionName, const FString& FieldName) { return GetJsonObjectArrayFromExtension(Root, ExtensionName, FieldName); }


	bool GetJsonObjectBytes(TSharedRef<FJsonObject> JsonObject, TArray64<uint8>& Bytes);
	bool GetJsonObjectBool(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const bool DefaultValue);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>>& GetMetallicRoughnessMaterialsMap() { return MetallicRoughnessMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>>& GetSpecularGlossinessMaterialsMap() { return SpecularGlossinessMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>>& GetUnlitMaterialsMap() { return UnlitMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>>& GetTransmissionMaterialsMap() { return TransmissionMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>>& GetClearCoatMaterialsMap() { return ClearCoatMaterialsMap; };
#else
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*>& GetMetallicRoughnessMaterialsMap() { return MetallicRoughnessMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*>& GetSpecularGlossinessMaterialsMap() { return SpecularGlossinessMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*>& GetUnlitMaterialsMap() { return UnlitMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*>& GetTransmissionMaterialsMap() { return TransmissionMaterialsMap; };
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*>& GetClearCoatMaterialsMap() { return ClearCoatMaterialsMap; };
#endif

	FString ToJsonString() const;

	bool FillJsonMatrix(const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues, FMatrix& Matrix);
	FTransform RawMatrixToRebasedTransform(const FMatrix& Matrix) const;

protected:

	float FindBestFrames(const TArray<float>& FramesTimes, float WantedTime, int32& FirstIndex, int32& SecondIndex);

	void NormalizeSkeletonScale(FReferenceSkeleton& RefSkeleton);
	void NormalizeSkeletonBoneScale(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FVector BoneScale);

	void ClearSkeletonRotations(FReferenceSkeleton& RefSkeleton);
	void ApplySkeletonBoneRotation(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FQuat BoneRotation);

	void CopySkeletonRotationsFrom(FReferenceSkeleton& RefSkeleton, const FReferenceSkeleton& SrcRefSkeleton);
	void AddSkeletonDeltaTranforms(FReferenceSkeleton& RefSkeleton, const TMap<FString, FTransform>& Transforms);

	bool CanReadFromCache(const EglTFRuntimeCacheMode CacheMode) { return CacheMode == EglTFRuntimeCacheMode::Read || CacheMode == EglTFRuntimeCacheMode::ReadWrite; }
	bool CanWriteToCache(const EglTFRuntimeCacheMode CacheMode) { return CacheMode == EglTFRuntimeCacheMode::Write || CacheMode == EglTFRuntimeCacheMode::ReadWrite; }

	bool DecompressMeshOptimizer(const FglTFRuntimeBlob& Blob, const int64 Stride, const int64 Elements, const FString& Mode, const FString& Filter, TArray64<uint8>& UncompressedBytes);

	FMatrix SceneBasis;
	float SceneScale;

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>> MetallicRoughnessMaterialsMap;
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>> SpecularGlossinessMaterialsMap;
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>> UnlitMaterialsMap;
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>> TransmissionMaterialsMap;
	TMap<EglTFRuntimeMaterialType, TObjectPtr<UMaterialInterface>> ClearCoatMaterialsMap;
#else
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> MetallicRoughnessMaterialsMap;
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> SpecularGlossinessMaterialsMap;
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> UnlitMaterialsMap;
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> TransmissionMaterialsMap;
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> ClearCoatMaterialsMap;
#endif

	TArray<FString> Errors;

	FString BaseDirectory;
	FString BaseFilename;

	TArray64<uint8> AsBlob;

public:

	FVector TransformVector(const FVector Vector) const;
	FVector TransformPosition(const FVector Position) const;
	FVector4 TransformVector4(const FVector4 Vector) const;
	FTransform TransformTransform(const FTransform& Transform) const;

	const TArray64<uint8>& GetBlob() const { return AsBlob; }
	TArray64<uint8>& GetBlob() { return AsBlob; }

	FTransform RebaseTransform(const FTransform& Transform) const
	{
		FMatrix M = Transform.ToMatrixWithScale();
		M.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
		return FTransform(SceneBasis.Inverse() * M * SceneBasis);
	}

	template<typename T, typename Callback>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedElements, const TArray<int64>& SupportedTypes, Callback Filter, const int64 AdditionalBufferView, const bool bDefaultNormalized, int64* ComponentTypePtr)
	{
		int64 AccessorIndex;
		if (!JsonObject->TryGetNumberField(Name, AccessorIndex))
		{
			return false;
		}

		FglTFRuntimeBlob Blob;
		int64 ComponentType = 0, Stride = 0, Elements = 0, ElementSize = 0, Count = 0;
		bool bNormalized = bDefaultNormalized;

		if (!GetAccessor(AccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, Blob, GetAdditionalBufferView(AdditionalBufferView, Name)))
		{
			return false;
		}

		if (!SupportedElements.Contains(Elements))
		{
			return false;
		}

		if (!SupportedTypes.Contains(ComponentType))
		{
			return false;
		}

		if (ComponentTypePtr)
		{
			*ComponentTypePtr = ComponentType;
		}

		auto ComponentFloat = [](const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				float* Ptr = (float*)&(Blob.Data[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = Ptr[i];
				}
			};

		auto ComponentByte = [](const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				int8* Ptr = (int8*)&(Blob.Data[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? FMath::Max(((float)Ptr[i]) / 127.f, -1.f) : Ptr[i];
				}
			};

		auto ComponentUnsignedByte = [](const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				uint8* Ptr = (uint8*)&(Blob.Data[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? ((float)Ptr[i]) / 255.f : Ptr[i];
				}
			};

		auto ComponentShort = [](const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				int16* Ptr = (int16*)&(Blob.Data[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? FMath::Max(((float)Ptr[i]) / 32767.f, -1.f) : Ptr[i];
				}
			};

		auto ComponentUnsignedShort = [](const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				uint16* Ptr = (uint16*)&(Blob.Data[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? ((float)Ptr[i]) / 65535.f : Ptr[i];
				}
			};

		TFunction<void(const int64 Elements, const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)> ComponentFunction = nullptr;

		switch (ComponentType)
		{
		case(5126):// FLOAT
			ComponentFunction = ComponentFloat;
			break;
		case(5120):// BYTE
			ComponentFunction = ComponentByte;
			break;
		case(5121):// UNSIGNED_BYTE
			ComponentFunction = ComponentUnsignedByte;
			break;
		case(5122):// SHORT
			ComponentFunction = ComponentShort;
			break;
		case(5123):// UNSIGNED_SHORT
			ComponentFunction = ComponentUnsignedShort;
			break;
		default:
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unsupported type %d"), ComponentType);
			return false;
		}

		Data.AddUninitialized(Count);
		ParallelFor(Count, [&](const int64 ElementIndex)
			{
				int64 Index = ElementIndex * Stride;
				T Value;
				ComponentFunction(Elements, Index, Blob, Value, bNormalized);
				Data[ElementIndex] = Filter(Value);
			});

		return true;
	}

	template<typename T, typename Callback>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedTypes, Callback Filter, const int64 AdditionalBufferView, const bool bDefaultNormalized, int64* ComponentTypePtr)
	{
		int64 AccessorIndex;
		if (!JsonObject->TryGetNumberField(Name, AccessorIndex))
		{
			return false;
		}

		FglTFRuntimeBlob Blob;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		bool bNormalized = bDefaultNormalized;

		if (!GetAccessor(AccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, Blob, GetAdditionalBufferView(AdditionalBufferView, Name)))
		{
			return false;
		}

		if (Elements != 1)
		{
			return false;
		}

		if (!SupportedTypes.Contains(ComponentType))
		{
			return false;
		}

		if (ComponentTypePtr)
		{
			*ComponentTypePtr = ComponentType;
		}

		auto ComponentFloat = [](const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				float* Ptr = (float*)&(Blob.Data[Index]);
				Value = *Ptr;
			};

		auto ComponentByte = [](const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				int8* Ptr = (int8*)&(Blob.Data[Index]);
				Value = bNormalized ? FMath::Max(((float)(*Ptr)) / 127.f, -1.f) : *Ptr;
			};

		auto ComponentUnsignedByte = [](const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				uint8* Ptr = (uint8*)&(Blob.Data[Index]);
				Value = bNormalized ? ((float)(*Ptr)) / 255.f : *Ptr;
			};

		auto ComponentShort = [](const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				int16* Ptr = (int16*)&(Blob.Data[Index]);
				Value = bNormalized ? FMath::Max(((float)(*Ptr)) / 32767.f, -1.f) : *Ptr;
			};

		auto ComponentUnsignedShort = [](const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)
			{
				uint16* Ptr = (uint16*)&(Blob.Data[Index]);
				Value = bNormalized ? ((float)(*Ptr)) / 65535.f : *Ptr;
			};

		TFunction<void(const int64 Index, const FglTFRuntimeBlob& Blob, T& Value, const bool bNormalized)> ComponentFunction = nullptr;

		switch (ComponentType)
		{
		case(5126):// FLOAT
			ComponentFunction = ComponentFloat;
			break;
		case(5120):// BYTE
			ComponentFunction = ComponentByte;
			break;
		case(5121):// UNSIGNED_BYTE
			ComponentFunction = ComponentUnsignedByte;
			break;
		case(5122):// SHORT
			ComponentFunction = ComponentShort;
			break;
		case(5123):// UNSIGNED_SHORT
			ComponentFunction = ComponentUnsignedShort;
			break;
		default:
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unsupported type %d"), ComponentType);
			return false;
		}

		Data.AddUninitialized(Count);
		ParallelFor(Count, [&](const int64 ElementIndex)
			{
				int64 Index = ElementIndex * Stride;
				T Value;
				ComponentFunction(Index, Blob, Value, bNormalized);
				Data[ElementIndex] = Filter(Value);
			});

		return true;
	}

	template<typename T>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedElements, const TArray<int64>& SupportedTypes, const int64 AdditionalBufferView, const bool bDefaultNormalized, int64* ComponentTypePtr)
	{
		return BuildFromAccessorField(JsonObject, Name, Data, SupportedElements, SupportedTypes, [&](T InValue) -> T {return InValue; }, AdditionalBufferView, bDefaultNormalized, ComponentTypePtr);
	}

	template<typename T>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedTypes, const int64 AdditionalBufferView, const bool bDefaultNormalized, int64* ComponentTypePtr)
	{
		return BuildFromAccessorField(JsonObject, Name, Data, SupportedTypes, [&](T InValue) -> T {return InValue; }, AdditionalBufferView, bDefaultNormalized, ComponentTypePtr);
	}

	template<int32 Num, typename T>
	bool GetJsonVector(const TArray<TSharedPtr<FJsonValue>>* JsonValues, T& Value)
	{
		if (JsonValues->Num() != Num)
		{
			return false;
		}

		for (int32 i = 0; i < Num; i++)
		{
			if (!(*JsonValues)[i]->TryGetNumber(Value[i]))
			{
				return false;
			}
		}

		return true;
	}

protected:

	bool MergePrimitives(TArray<FglTFRuntimePrimitive> SourcePrimitives, FglTFRuntimePrimitive& OutPrimitive);

	TSharedPtr<FglTFRuntimeArchive> Archive;

	template<typename T>
	T GetSafeValue(const TArray<T>& Values, const int32 Index, const T DefaultValue, bool& bMissing)
	{
		if (Index >= Values.Num() || Index < 0)
		{
			bMissing = true;
			return DefaultValue;
		}
		return Values[Index];
	}

	FVector ComputeTangentY(const FVector Normal, const FVector TangetX);
	FVector ComputeTangentYWithW(const FVector Normal, const FVector TangetX, const float W);

	TArray64<uint8> ZeroBuffer;
	TMap<int32, TArray64<uint8>> SparseAccessorsCache;
	TMap<int32, int64> SparseAccessorsStridesCache;

	TMap<int64, TMap<FString, FglTFRuntimeBlob>> AdditionalBufferViewsCache;
	TArray<TArray64<uint8>> AdditionalBufferViewsData;

	FString DefaultPrefixForUnnamedNodes;

	float DownloadTime;

public:
	bool IsArchive() const;
	TArray<FString> GetArchiveItems() const;
	bool GetBlobByName(const FString& Name, TArray64<uint8>& Blob) const;

	template<typename FUNCTION>
	void LoadAsRuntimeLODAsync(FUNCTION Function, const FglTFRuntimeMeshLODAsync& AsyncCallback)
	{
		Async(EAsyncExecution::Thread, [Function, AsyncCallback]()
			{
				FglTFRuntimeMeshLOD LOD;
				bool bSuccess = Function(LOD);
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([bSuccess, &LOD, AsyncCallback]()
					{
						AsyncCallback.ExecuteIfBound(bSuccess, bSuccess ? LOD : FglTFRuntimeMeshLOD());
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
			}

		);
	}

	TMap<FString, TSharedPtr<FglTFRuntimePluginCacheData>> PluginsCacheData;
	FCriticalSection PluginsCacheDataLock;

	const FString& GetBaseDirectory() const { return BaseDirectory; }
	const FString& GetBaseFilename() const { return BaseFilename; }

	bool LoadPathToBlob(const FString& Path, TArray64<uint8>& Blob);

	bool LoadBlobToMips(const int32 TextureIndex, TSharedRef<FJsonObject> JsonTextureObject, TSharedRef<FJsonObject> JsonImageObject, const TArray64<uint8>& Blob, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	bool LoadBlobToMips(const TArray64<uint8>& Blob, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	void SetDownloadTime(const float Value);
	float GetDownloadTime() const;

};
