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

	UPROPERTY(EditAnywhere)
	FString Name;

	UPROPERTY(EditAnywhere)
	FTransform Transform;

	UPROPERTY(EditAnywhere)
	UStaticMesh* StaticMesh;

	TArray<int32> Children;
};

/**
 *
 */
class GLTFRUNTIME_API FglTFRuntimeParser
{
public:
	FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject);
	static TSharedPtr<FglTFRuntimeParser> FromFilename(FString Filename);

	UStaticMesh* LoadStaticMesh(int32 Index);
	bool LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes);

	UMaterialInterface* LoadMaterial(int32 Index);

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

protected:
	TSharedRef<FJsonObject> Root;

	TMap<int32, UStaticMesh*> StaticMeshesCache;
	TMap<int32, UMaterialInterface*> MaterialsCache;

	TMap<int32, TArray<uint8>> BuffersCache;

	TArray<FStaticMaterial> StaticMaterials;

	UStaticMesh* LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject);
	UMaterialInterface* LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject);

	TArray<FglTFRuntimeNode> Nodes;
};