// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Dom/JsonObject.h"
#include "Engine/StaticMesh.h"
#include "Engine/SkeletalMesh.h"
#include "Camera/CameraComponent.h"
#include "glTFRuntimeAnimationCurve.h"

#include "glTFRuntimeParser.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogGLTFRuntime, Log, All);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FglTFRuntimeError, const FString, ErrorContext, const FString, ErrorMessage);

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnStaticMeshCreated, UStaticMesh*, StaticMesh);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FglTFRuntimeOnSkeletalMeshCreated, USkeletalMesh*, SkeletalMesh);

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
	Bottom
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
};

USTRUCT(BlueprintType)
struct FglTFRuntimeMorphTarget
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector> Positions;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TArray<FVector> Normals;
};

USTRUCT(BlueprintType)
struct FglTFRuntimeMaterialsConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> UberMaterialsOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UMaterialInterface*> MaterialsOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UTexture2D*> TexturesOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<int32, UTexture2D*> ImagesOverrideMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bDisableVertexColors;
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

	FglTFRuntimeStaticMeshConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bReverseWinding = false;
		bBuildSimpleCollision = false;
		Outer = nullptr;
		CollisionComplexity = ECollisionTraceFlag::CTF_UseDefault;
		bAllowCPUAccess = true;
		PivotPosition = EglTFRuntimePivotPosition::Asset;
	}
};

USTRUCT(BlueprintType)
struct FglTFRuntimeSkeletonConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bAddRootBone;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FString RootBoneName;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FString> BonesNameMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FTransform> BonesTransformMap;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bNormalizeSkeletonScale;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	int32 RootNodeIndex;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<FString, FglTFRuntimeSocket> Sockets;

	FglTFRuntimeSkeletonConfig()
	{
		RootNodeIndex = INDEX_NONE;
		bNormalizeSkeletonScale = false;
		bAddRootBone = false;
	}
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
	TArray<FglTFRuntimeBone> CustomSkeleton;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	bool bIgnoreSkin;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeSkeletonConfig SkeletonConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	FglTFRuntimeMaterialsConfig MaterialsConfig;

	FglTFRuntimeSkeletalMeshConfig()
	{
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
		bOverwriteRefSkeleton = false;
		Skeleton = nullptr;
		bIgnoreSkin = false;
	}
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

	FglTFRuntimeSkeletalAnimationConfig()
	{
		RootNodeIndex = INDEX_NONE;
		bRootMotion = false;
		bRemoveRootMotion = false;
		CacheMode = EglTFRuntimeCacheMode::ReadWrite;
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

	uint16& operator[](int32 Index)
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
		;
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
};

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeParser : public FGCObject
{
public:
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, const FMatrix& InSceneBasis, float InSceneScale);
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject);

	static TSharedPtr<FglTFRuntimeParser> FromFilename(const FString& Filename);
	static TSharedPtr<FglTFRuntimeParser> FromBinary(const uint8* DataPtr, int64 DataNum);
	static TSharedPtr<FglTFRuntimeParser> FromString(const FString& JsonData);
	static TSharedPtr<FglTFRuntimeParser> FromData(const uint8* DataPtr, int64 DataNum);

	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromBinary(const TArray<uint8> Data) { return FromBinary(Data.GetData(), Data.Num()); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromBinary(const TArray64<uint8> Data) { return FromBinary(Data.GetData(), Data.Num()); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromData(const TArray<uint8> Data) { return FromData(Data.GetData(), Data.Num()); }
	static FORCEINLINE TSharedPtr<FglTFRuntimeParser> FromData(const TArray64<uint8> Data) { return FromData(Data.GetData(), Data.Num()); }

	UStaticMesh* LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	bool LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UStaticMesh* LoadStaticMeshByName(const FString MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);

	UMaterialInterface* LoadMaterial(const int32 MaterialIndex, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors);
	UTexture2D* LoadTexture(UObject* Outer, const int32 TextureIndex, const TEnumAsByte<TextureCompressionSettings> Compression, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	bool LoadNodes();
	bool LoadNode(int32 NodeIndex, FglTFRuntimeNode& Node);
	bool LoadNodeByName(FString NodeName, FglTFRuntimeNode& Node);

	bool LoadScenes(TArray<FglTFRuntimeScene>& Scenes);
	bool LoadScene(int32 SceneIndex, FglTFRuntimeScene& Scene);

	USkeletalMesh* LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	UAnimSequence* LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig);
	UAnimSequence* LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString AnimationName, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig);
	UAnimSequence* LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig);
	USkeleton* LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	UglTFRuntimeAnimationCurve* LoadNodeAnimationCurve(const int32 NodeIndex);
	TArray<UglTFRuntimeAnimationCurve*> LoadAllNodeAnimationCurves(const int32 NodeIndex);

	bool GetBuffer(int32 BufferIndex, TArray64<uint8>& Bytes);
	bool GetBufferView(int32 BufferViewIndex, TArray64<uint8>& Bytes, int64& Stride);
	bool GetAccessor(int32 AccessorIndex, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, TArray64<uint8>& Bytes);

	bool GetAllNodes(TArray<FglTFRuntimeNode>& Nodes);

	TArray<FString> GetCamerasNames();

	bool LoadCameraIntoCameraComponent(const int32 CameraIndex, UCameraComponent* CameraComponent);

	int64 GetComponentTypeSize(const int64 ComponentType) const;
	int64 GetTypeSize(const FString& Type) const;

	bool ParseBase64Uri(const FString& Uri, TArray64<uint8>& Bytes);

	void AddReferencedObjects(FReferenceCollector& Collector);

	bool LoadPrimitives(const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives, TArray<FglTFRuntimePrimitive>& Primitives, const FglTFRuntimeMaterialsConfig& MaterialsConfig);
	bool LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig);

	void AddError(const FString& ErrorContext, const FString& ErrorMessage);
	void ClearErrors();

	bool NodeIsBone(const int32 NodeIndex);

	FglTFRuntimeError OnError;
	FglTFRuntimeOnStaticMeshCreated OnStaticMeshCreated;
	FglTFRuntimeOnSkeletalMeshCreated OnSkeletalMeshCreated;

	void SetBinaryBuffer(const TArray64<uint8>& InBinaryBuffer)
	{
		BinaryBuffer = InBinaryBuffer;
	}

protected:
	TSharedRef<FJsonObject> Root;

	TMap<int32, UStaticMesh*> StaticMeshesCache;
	TMap<int32, UMaterialInterface*> MaterialsCache;
	TMap<int32, USkeleton*> SkeletonsCache;
	TMap<int32, USkeletalMesh*> SkeletalMeshesCache;
	TMap<int32, UTexture2D*> TexturesCache;

	TMap<int32, TArray64<uint8>> BuffersCache;

	TArray<FglTFRuntimeNode> AllNodesCache;
	bool bAllNodesCached;

	TArray64<uint8> BinaryBuffer;

	UStaticMesh* LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig);
	UMaterialInterface* LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors);
	bool LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node);

	USkeletalMesh* LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, TSharedRef<FJsonObject> JsonSkinObject, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	bool LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>& Tracks, float& Duration, TFunctionRef<bool(const FglTFRuntimeNode& Node)> Filter);

	bool LoadAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, float& Duration, FString& Name, TFunctionRef<void(const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)> Callback, TFunctionRef<bool(const FglTFRuntimeNode& Node)> NodeFilter);

	bool FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig);
	bool FillFakeSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig);
	bool TraverseJoints(FReferenceSkeletonModifier& Modifier, int32 Parent, FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig);

	void FixNodeParent(FglTFRuntimeNode& Node);

	int32 FindCommonRoot(const TArray<int32>& NodeIndices);
	int32 FindTopRoot(int32 NodeIndex);
	bool HasRoot(int32 NodeIndex, int32 RootIndex);

	bool CheckJsonIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems);
	bool CheckJsonRootIndex(const FString FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems) { return CheckJsonIndex(Root, FieldName, Index, JsonItems); }
	TSharedPtr<FJsonObject> GetJsonObjectFromIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index);
	TSharedPtr<FJsonObject> GetJsonObjectFromRootIndex(const FString& FieldName, const int32 Index) { return GetJsonObjectFromIndex(Root, FieldName, Index); }

	FString GetJsonObjectString(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FString& DefaultValue);
	int32 GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 DefaultValue);

	bool FillJsonMatrix(const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues, FMatrix& Matrix);

	float FindBestFrames(const TArray<float>& FramesTimes, float WantedTime, int32& FirstIndex, int32& SecondIndex);

	void NormalizeSkeletonScale(FReferenceSkeleton& RefSkeleton);
	void NormalizeSkeletonBoneScale(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FVector BoneScale);

	bool AssignTexCoord(TSharedPtr<FJsonObject> JsonTextureObject, UMaterialInstanceDynamic* Material, const FName MaterialParam);

	bool CanReadFromCache(const EglTFRuntimeCacheMode CacheMode) { return CacheMode == EglTFRuntimeCacheMode::Read || CacheMode == EglTFRuntimeCacheMode::ReadWrite; }
	bool CanWriteToCache(const EglTFRuntimeCacheMode CacheMode) { return CacheMode == EglTFRuntimeCacheMode::Write || CacheMode == EglTFRuntimeCacheMode::ReadWrite; }

	FMatrix SceneBasis;
	float SceneScale;

	TMap<EglTFRuntimeMaterialType, UMaterialInterface*> MaterialsMap;

	TArray<FString> Errors;

	template<typename T, typename Callback>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedElements, const TArray<int64>& SupportedTypes, const bool bNormalized, Callback Filter)
	{
		int64 AccessorIndex;
		if (!JsonObject->TryGetNumberField(Name, AccessorIndex))
			return false;

		TArray64<uint8> Bytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(AccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, Bytes))
			return false;

		if (!SupportedElements.Contains(Elements))
			return false;

		if (!SupportedTypes.Contains(ComponentType))
			return false;

		for (int64 ElementIndex = 0; ElementIndex < Count; ElementIndex++)
		{
			int64 Index = ElementIndex * Stride;
			T Value;
			// FLOAT
			if (ComponentType == 5126)
			{
				float* Ptr = (float*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = Ptr[i];
				}
			}
			// BYTE
			else if (ComponentType == 5120)
			{
				int8* Ptr = (int8*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? FMath::Max(((float)Ptr[i]) / 127.f, -1.f) : Ptr[i];
				}

			}
			// UNSIGNED_BYTE
			else if (ComponentType == 5121)
			{
				uint8* Ptr = (uint8*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? ((float)Ptr[i]) / 255.f : Ptr[i];
				}
			}
			// SHORT
			else if (ComponentType == 5122)
			{
				int16* Ptr = (int16*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? FMath::Max(((float)Ptr[i]) / 32767.f, -1.f) : Ptr[i];
				}
			}
			// UNSIGNED_SHORT
			else if (ComponentType == 5123)
			{
				uint16* Ptr = (uint16*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? ((float)Ptr[i]) / 65535.f : Ptr[i];
				}
			}
			else
			{
				return false;
			}

			Data.Add(Filter(Value));
		}

		return true;
	}

	template<typename T, typename Callback>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedTypes, const bool bNormalized, Callback Filter)
	{
		int64 AccessorIndex;
		if (!JsonObject->TryGetNumberField(Name, AccessorIndex))
			return false;

		TArray64<uint8> Bytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(AccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, Bytes))
			return false;

		if (Elements != 1)
			return false;

		if (!SupportedTypes.Contains(ComponentType))
			return false;

		for (int64 ElementIndex = 0; ElementIndex < Count; ElementIndex++)
		{
			int64 Index = ElementIndex * Stride;
			T Value;
			// FLOAT
			if (ComponentType == 5126)
			{
				float* Ptr = (float*)&(Bytes[Index]);
				Value = *Ptr;
			}
			// BYTE
			else if (ComponentType == 5120)
			{
				int8* Ptr = (int8*)&(Bytes[Index]);
				Value = bNormalized ? FMath::Max(((float)(*Ptr)) / 127.f, -1.f) : *Ptr;

			}
			// UNSIGNED_BYTE
			else if (ComponentType == 5121)
			{
				uint8* Ptr = (uint8*)&(Bytes[Index]);
				Value = bNormalized ? ((float)(*Ptr)) / 255.f : *Ptr;

			}
			// SHORT
			else if (ComponentType == 5122)
			{
				int16* Ptr = (int16*)&(Bytes[Index]);
				Value = bNormalized ? FMath::Max(((float)(*Ptr)) / 32767.f, -1.f) : *Ptr;
			}
			// UNSIGNED_SHORT
			else if (ComponentType == 5123)
			{
				uint16* Ptr = (uint16*)&(Bytes[Index]);
				Value = bNormalized ? ((float)(*Ptr)) / 65535.f : *Ptr;
			}
			else
			{
				return false;
			}

			Data.Add(Filter(Value));
		}

		return true;
	}

	template<typename T>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedElements, const TArray<int64>& SupportedTypes, const bool bNormalized)
	{
		return BuildFromAccessorField(JsonObject, Name, Data, SupportedElements, SupportedTypes, bNormalized, [&](T InValue) -> T {return InValue; });
	}

	template<typename T>
	bool BuildFromAccessorField(TSharedRef<FJsonObject> JsonObject, const FString& Name, TArray<T>& Data, const TArray<int64>& SupportedTypes, const bool bNormalized)
	{
		return BuildFromAccessorField(JsonObject, Name, Data, SupportedTypes, bNormalized, [&](T InValue) -> T {return InValue; });
	}

	template<int32 Num, typename T>
	bool GetJsonVector(const TArray<TSharedPtr<FJsonValue>>* JsonValues, T& Value)
	{
		if (JsonValues->Num() != Num)
			return false;

		for (int32 i = 0; i < Num; i++)
		{
			if (!(*JsonValues)[i]->TryGetNumber(Value[i]))
				return false;
		}
		
		return true;
	}
};