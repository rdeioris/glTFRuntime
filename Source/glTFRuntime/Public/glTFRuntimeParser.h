// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"
#include "Engine/StaticMesh.h"

#include "glTFRuntimeParser.generated.h"

USTRUCT(BlueprintType)
struct FglTFRuntimeNode
{
	GENERATED_BODY()

		UPROPERTY(EditAnywhere, BlueprintReadonly)
		FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadonly)
		FTransform Transform;

	UPROPERTY(EditAnywhere, BlueprintReadonly)
		UStaticMesh* StaticMesh;

	TArray<int32> ChildrenIndexes;
	TArray<FglTFRuntimeNode> Children;

	FglTFRuntimeNode()
	{
		Transform = FTransform::Identity;
		StaticMesh = nullptr;
	}
};

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeParser : public FGCObject
{
public:
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject);
	static TSharedPtr<FglTFRuntimeParser> FromFilename(FString Filename);

	UStaticMesh* LoadStaticMesh(int32 Index);
	bool LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes);

	UMaterialInterface* LoadMaterial(int32 Index);

	bool LoadNodes(TArray<FglTFRuntimeNode>& Nodes);
	bool LoadNode(int32 Index, FglTFRuntimeNode& Node);

	bool LoadScene(int32 Index, TArray<FglTFRuntimeNode>& Nodes);

	bool BuildPrimitive(UStaticMeshDescription* MeshDescription, TSharedRef<FJsonObject> JsonPrimitiveObject);

	bool GetBuffer(int32 Index, TArray<uint8>& Bytes);
	bool GetBufferView(int32 Index, TArray<uint8>& Bytes);
	bool GetAccessor(int32 Index, int64& Elements, int64& ElementSize, int64& Count, TArray<uint8>& Bytes);

	int64 GetComponentTypeSize(const int64 ComponentType) const;
	int64 GetTypeSize(const FString Type) const;

	template<typename T>
	bool BuildPrimitiveAttribute(TSharedRef<FJsonObject> JsonAttributesObject, const FString Name, TArray<T>& Data)
	{
		int64 AccessorIndex;
		if (!JsonAttributesObject->TryGetNumberField(Name, AccessorIndex))
			return false;

		TArray<uint8> Bytes;
		int64 Elements, ElementSize, Count;
		if (!GetAccessor(AccessorIndex, Elements, ElementSize, Count, Bytes))
			return false;

		if ((ElementSize * Elements) < sizeof(T))
			return false;

		for (int64 i = 0; i < Count; i++)
		{
			int64 Index = i * (Elements * ElementSize);
			T* Ptr = (T*)&(Bytes[Index]);
			Data.Add(*Ptr);
		}

		return true;
	}

	void AddReferencedObjects(FReferenceCollector& Collector);

protected:
	TSharedRef<FJsonObject> Root;

	TMap<int32, UStaticMesh*> StaticMeshesCache;
	TMap<int32, UMaterialInterface*> MaterialsCache;

	TMap<int32, TArray<uint8>> BuffersCache;

	TArray<FStaticMaterial> StaticMaterials;

	TArray<FglTFRuntimeNode> AllNodesCache;
	bool bAllNodesCached;

	UStaticMesh* LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject);
	UMaterialInterface* LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject);
	bool LoadNode_Internal(TSharedRef<FJsonObject> JsonNodeObject, FglTFRuntimeNode& Node, int32 NodesCount);

	void FixNodeChildren(FglTFRuntimeNode& Node);
};