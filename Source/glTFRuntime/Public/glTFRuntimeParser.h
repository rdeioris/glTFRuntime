// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"

#include "glTFRuntimeParser.generated.h"

USTRUCT(BlueprintType)
struct FglTFRuntimeScene
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	int32 Index;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	TArray<int32> RootNodesIndices;
};


USTRUCT(BlueprintType)
struct FglTFRuntimeNode
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	int32 Index;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	FString Name;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	FTransform Transform;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	int32 MeshIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	int32 SkinIndex;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	TArray<int32> ChildrenIndices;

	UPROPERTY(VisibleAnywhere, BlueprintReadonly)
	int32 ParentIndex;

	FglTFRuntimeNode()
	{
		Transform = FTransform::Identity;
		ParentIndex = INDEX_NONE;
		MeshIndex = INDEX_NONE;
		SkinIndex = INDEX_NONE;
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
	TArray<TArray<FVector2D>> UVs;
	TArray<uint32> Indices;
	UMaterialInterface* Material;
	TArray<TArray<FglTFRuntimeUInt16Vector4>> Joints;
	TArray<TArray<FVector4>> Weights;
};

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeParser : public FGCObject
{
public:
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, FMatrix InBasis, float Scale);
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject);
	static TSharedPtr<FglTFRuntimeParser> FromFilename(FString Filename);

	UStaticMesh* LoadStaticMesh(int32 Index);
	bool LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes);

	UMaterialInterface* LoadMaterial(int32 Index);

	bool LoadNodes();
	bool LoadNode(int32 Index, FglTFRuntimeNode& Node);
	bool LoadNodeByName(FString Name, FglTFRuntimeNode& Node);

	bool LoadScenes(TArray<FglTFRuntimeScene>& Scenes);
	bool LoadScene(int32 Index, FglTFRuntimeScene& Scene);

	USkeletalMesh* LoadSkeletalMesh(int32 Index, int32 SkinIndex, int32 NodeIndex);

	bool GetBuffer(int32 Index, TArray<uint8>& Bytes);
	bool GetBufferView(int32 Index, TArray<uint8>& Bytes, int64& Stride);
	bool GetAccessor(int32 Index, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, TArray<uint8>& Bytes);

	bool GetAllNodes(TArray<FglTFRuntimeNode>& Nodes);

	int64 GetComponentTypeSize(const int64 ComponentType) const;
	int64 GetTypeSize(const FString Type) const;

	template<typename T, typename Callback>
	bool BuildPrimitiveAttribute(TSharedRef<FJsonObject> JsonAttributesObject, const FString Name, TArray<T>& Data, const TArray<int64> SupportedElements, const TArray<int64> SupportedTypes, const bool bNormalized, Callback Filter)
	{
		int64 AccessorIndex;
		if (!JsonAttributesObject->TryGetNumberField(Name, AccessorIndex))
			return false;

		TArray<uint8> Bytes;
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
			// UNSIGNED_BYTE
			else if (ComponentType == 5121)
			{
				uint8* Ptr = (uint8*)&(Bytes[Index]);
				for (int32 i = 0; i < Elements; i++)
				{
					Value[i] = bNormalized ? ((float)Ptr[i]) / 255.f : Ptr[i];
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

	template<typename T>
	bool BuildPrimitiveAttribute(TSharedRef<FJsonObject> JsonAttributesObject, const FString Name, TArray<T>& Data, const TArray<int64> SupportedElements, const TArray<int64> SupportedTypes, const bool bNormalized)
	{
		return BuildPrimitiveAttribute(JsonAttributesObject, Name, Data, SupportedElements, SupportedTypes, bNormalized, [&](T InValue) -> T {return InValue;});
	}

	void AddReferencedObjects(FReferenceCollector& Collector);

	bool LoadPrimitives(const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives, TArray<FglTFRuntimePrimitive>& Primitives);
	bool LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive);

protected:
	TSharedRef<FJsonObject> Root;

	TMap<int32, UStaticMesh*> StaticMeshesCache;
	TMap<int32, UMaterialInterface*> MaterialsCache;
	TMap<int32, USkeleton*> SkeletonsCache;
	TMap<int32, USkeletalMesh*> SkeletalMeshesCache;

	TMap<int32, TArray<uint8>> BuffersCache;

	TArray<FglTFRuntimeNode> AllNodesCache;
	bool bAllNodesCached;

	UStaticMesh* LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject);
	UMaterialInterface* LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject);
	bool LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node);

	USkeletalMesh* LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, TSharedRef<FJsonObject> JsonSkinObject, FTransform& RootTransform);

	bool FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, FTransform& RootTransform);
	bool TraverseJoints(FReferenceSkeletonModifier& Modifier, int32 Parent, FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, FTransform& RootTransform, const bool bHasRoot);

	void FixNodeParent(FglTFRuntimeNode& Node);

	int32 FindCommonRoot(TArray<int32> Indices);
	int32 FindTopRoot(int32 Index);
	bool HasRoot(int32 Index, int32 RootIndex);

	FMatrix Basis;
	float Scale;
};