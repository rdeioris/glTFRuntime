// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromFilename(FString Filename)
{
	FString JsonData;
	// TODO: spit out errors
	if (!FFileHelper::LoadFileToString(JsonData, *Filename))
		return nullptr;

	TSharedPtr<FJsonValue> RootValue;

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(JsonReader, RootValue))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonObject = RootValue->AsObject();
	if (!JsonObject)
		return nullptr;

	return MakeShared<FglTFRuntimeParser>(JsonObject.ToSharedRef());
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, FMatrix InBasis) : Root(JsonObject), Basis(InBasis)
{
	bAllNodesCached = false;
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject) : FglTFRuntimeParser(JsonObject, FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector)* FScaleMatrix(100))
{

}

bool FglTFRuntimeParser::LoadNodes(TArray<FglTFRuntimeNode>& Nodes)
{
	if (bAllNodesCached)
	{
		Nodes = AllNodesCache;
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonNodes;

	// no meshes ?
	if (!Root->TryGetArrayField("nodes", JsonNodes))
	{
		return false;
	}

	// first round for getting all nodes (no children management)
	for (int32 Index = 0; Index < JsonNodes->Num(); Index++)
	{
		TSharedPtr<FJsonObject> JsonNodeObject = (*JsonNodes)[Index]->AsObject();
		if (!JsonNodeObject)
			return false;
		FglTFRuntimeNode Node;
		if (!LoadNode_Internal(JsonNodeObject.ToSharedRef(), Node, JsonNodes->Num()))
		{
			return false;
		}
		Nodes.Add(Node);
	}

	AllNodesCache = Nodes;
	bAllNodesCached = true;

	// fix children
	for (FglTFRuntimeNode& CachedNode : AllNodesCache)
	{
		FixNodeChildren(CachedNode);
	}

	return true;
}

void FglTFRuntimeParser::FixNodeChildren(FglTFRuntimeNode& Node)
{
	for (int32 Index : Node.ChildrenIndexes)
	{
		// no need to check for index validity, it has already been managed on the previous run
		int32 NewIndex = Node.Children.Add(AllNodesCache[Index]);
		FixNodeChildren(Node.Children[NewIndex]);
	}
}

bool FglTFRuntimeParser::LoadScene(int32 Index, TArray<FglTFRuntimeNode>& Nodes)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonScenes;

	// no scenes ?
	if (!Root->TryGetArrayField("scenes", JsonScenes))
	{
		return false;
	}

	if (Index >= JsonScenes->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonSceneObject = (*JsonScenes)[Index]->AsObject();
	if (!JsonSceneObject)
		return nullptr;

	const TArray<TSharedPtr<FJsonValue>>* JsonSceneNodes;

	if (JsonSceneObject->TryGetArrayField("nodes", JsonSceneNodes))
	{
		for (TSharedPtr<FJsonValue> JsonSceneNode : *JsonSceneNodes)
		{
			int64 NodeIndex;
			if (!JsonSceneNode->TryGetNumber(NodeIndex))
				return false;
			FglTFRuntimeNode SceneNode;
			if (!LoadNode(NodeIndex, SceneNode))
				return false;
			Nodes.Add(SceneNode);
		}
	}

	return true;
}

bool FglTFRuntimeParser::LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes)
{

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonMeshes->Num(); Index++)
	{
		UStaticMesh* StaticMesh = LoadStaticMesh(Index);
		if (!StaticMesh)
		{
			return false;
		}
		StaticMeshes.Add(StaticMesh);
	}

	return true;
}

bool FglTFRuntimeParser::LoadNode(int32 Index, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		TArray<FglTFRuntimeNode> Nodes;
		if (!LoadNodes(Nodes))
			return false;
	}

	if (Index >= AllNodesCache.Num())
		return false;

	Node = AllNodesCache[Index];
	return true;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(int32 Index)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (MaterialsCache.Contains(Index))
	{
		return MaterialsCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMaterials;

	// no materials ?
	if (!Root->TryGetArrayField("materials", JsonMaterials))
	{
		return nullptr;
	}

	if (Index >= JsonMaterials->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMaterialObject = (*JsonMaterials)[Index]->AsObject();
	if (!JsonMaterialObject)
		return nullptr;

	UMaterialInterface* Material = LoadMaterial_Internal(JsonMaterialObject.ToSharedRef());
	if (!Material)
		return nullptr;

	MaterialsCache.Add(Index, Material);

	return Material;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh(int32 Index)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (SkeletalMeshesCache.Contains(Index))
	{
		return SkeletalMeshesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	if (Index >= JsonMeshes->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[Index]->AsObject();
	if (!JsonMeshObject)
		return nullptr;

	USkeletalMesh* SkeletalMesh = LoadSkeletalMesh_Internal(JsonMeshObject.ToSharedRef());
	if (!SkeletalMesh)
		return nullptr;

	SkeletalMeshesCache.Add(Index, SkeletalMesh);

	return SkeletalMesh;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh(int32 Index)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (StaticMeshesCache.Contains(Index))
	{
		return StaticMeshesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	if (Index >= JsonMeshes->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[Index]->AsObject();
	if (!JsonMeshObject)
		return nullptr;

	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObject.ToSharedRef());
	if (!StaticMesh)
		return nullptr;

	StaticMeshesCache.Add(Index, StaticMesh);

	return StaticMesh;
}

bool FglTFRuntimeParser::LoadNode_Internal(TSharedRef<FJsonObject> JsonNodeObject, FglTFRuntimeNode& Node, int32 NodesCount)
{
	int64 MeshIndex;
	if (JsonNodeObject->TryGetNumberField("mesh", MeshIndex))
	{
		Node.StaticMesh = LoadStaticMesh(MeshIndex);
		if (!Node.StaticMesh)
			return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues;
	if (JsonNodeObject->TryGetArrayField("matrix", JsonMatrixValues))
	{
		if (JsonMatrixValues->Num() != 16)
			return false;

		FMatrix Matrix;

		for (int32 i = 0; i < 16; i++)
		{
			double Value;
			if (!(*JsonMatrixValues)[i]->TryGetNumber(Value))
			{
				return false;
			}

			Matrix.M[i / 4][i % 4] = Value;
		}

		Node.Transform = FTransform(Basis.Inverse() * Matrix * Basis);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonChildren;
	if (JsonNodeObject->TryGetArrayField("children", JsonChildren))
	{
		for (int32 i = 0; i < JsonChildren->Num(); i++)
		{
			int64 ChildIndex;
			if (!(*JsonChildren)[i]->TryGetNumber(ChildIndex))
			{
				return false;
			}

			if (ChildIndex >= NodesCount)
				return false;

			Node.ChildrenIndexes.Add(ChildIndex);
		}
	}

	return true;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject)
{
	UMaterialInterface* BaseMaterial = (UMaterialInterface*)StaticLoadObject(UMaterialInterface::StaticClass(), nullptr, TEXT("/glTFRuntime/M_glTFRuntimeBase"));
	if (!BaseMaterial)
		return nullptr;

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
	if (!Material)
		return nullptr;

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField("pbrMetallicRoughness", JsonPBRObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* baseColorFactorValues;
		if ((*JsonPBRObject)->TryGetArrayField("baseColorFactor", baseColorFactorValues))
		{
			if (baseColorFactorValues->Num() != 4)
				return nullptr;

			double R, G, B, A;
			if (!(*baseColorFactorValues)[0]->TryGetNumber(R))
				return nullptr;
			if (!(*baseColorFactorValues)[1]->TryGetNumber(G))
				return nullptr;
			if (!(*baseColorFactorValues)[2]->TryGetNumber(B))
				return nullptr;
			if (!(*baseColorFactorValues)[3]->TryGetNumber(A))
				return nullptr;

			Material->SetVectorParameterValue("baseColorFactor", FLinearColor(R, G, B, A));
		}
	}

	return Material;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject)
{
	// get skin
	int64 SkinIndex;
	if (!JsonMeshObject->TryGetNumberField("skin", SkinIndex))
		return nullptr;

	USkeleton* Skeleton = nullptr;

	// do we already have a skeleton cached ?
	if (SkeletonsCache.Contains(SkinIndex))
	{
		Skeleton = SkeletonsCache[SkinIndex];
	}

	return nullptr;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject)
{
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>();

	UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

	StaticMaterials.Empty();

	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
			return nullptr;

		if (!BuildPrimitive(MeshDescription, JsonPrimitiveObject.ToSharedRef()))
			return nullptr;

	}

	StaticMesh->StaticMaterials = StaticMaterials;

	TArray<UStaticMeshDescription*> MeshDescriptions = { MeshDescription };
	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, false);

	return StaticMesh;
}

bool FglTFRuntimeParser::BuildPrimitive(UStaticMeshDescription* MeshDescription, TSharedRef<FJsonObject> JsonPrimitiveObject)
{
	const TSharedPtr<FJsonObject>* JsonAttributesObject;
	if (!JsonPrimitiveObject->TryGetObjectField("attributes", JsonAttributesObject))
		return false;

	UMaterialInterface* StaticMeshMaterial = UMaterial::GetDefaultMaterial(MD_Surface);

	int64 MaterialIndex;
	if (JsonPrimitiveObject->TryGetNumberField("material", MaterialIndex))
	{
		StaticMeshMaterial = LoadMaterial(MaterialIndex);
		if (!StaticMeshMaterial)
			return false;
	}

	FPolygonGroupID PolygonGroupID = MeshDescription->CreatePolygonGroup();

	TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames = MeshDescription->GetPolygonGroupMaterialSlotNames();
	PolygonGroupMaterialSlotNames[PolygonGroupID] = StaticMeshMaterial->GetFName();
	StaticMaterials.Add(FStaticMaterial(StaticMeshMaterial, StaticMeshMaterial->GetFName()));

	TVertexAttributesRef<FVector> PositionsAttributesRef = MeshDescription->GetVertexPositions();
	TVertexInstanceAttributesRef<FVector> NormalsInstanceAttributesRef = MeshDescription->GetVertexInstanceNormals();

	TArray<FVertexInstanceID> VertexInstancesIDs;
	TArray<FVertexID> VerticesIDs;
	TArray<FVertexID> TriangleVerticesIDs;

	TArray<FVector> Positions;
	TArray<FVector> Normals;
	TArray<FVector2D> TexCoords0;
	TArray<FVector2D> TexCoords1;

	// POSITION is required for generating a valid MeshDescription
	if (!(*JsonAttributesObject)->HasField("POSITION"))
		return false;

	if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "POSITION", Positions,
		[&](FVector Value) -> FVector {return Basis.TransformPosition(Value); }))
		return false;

	if ((*JsonAttributesObject)->HasField("NORMAL"))
	{
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "NORMAL", Normals,
			[&](FVector Value) -> FVector { return Basis.TransformVector(Value).GetSafeNormal(); }))
			return false;
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_0"))
	{
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "TEXCOORD_0", TexCoords0,
			[&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, 1 - Value.Y); }))
			return false;
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_1"))
	{
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "TEXCOORD_1", TexCoords1,
			[&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, 1 - Value.Y); }))
			return false;
	}

	TArray<uint32> Indices;
	int64 IndicesAccessorIndex;
	if (JsonPrimitiveObject->TryGetNumberField("indices", IndicesAccessorIndex))
	{
		TArray<uint8> IndicesBytes;
		int64 Elements, ElementSize, Count;
		if (!GetAccessor(IndicesAccessorIndex, Elements, ElementSize, Count, IndicesBytes))
			return false;

		if (Elements != 1)
			return false;

		for (int64 i = 0; i < Count; i++)
		{
			int64 IndexIndex = i * (Elements * ElementSize);

			uint32 VertexIndex;
			if (ElementSize == 1)
			{
				VertexIndex = IndicesBytes[IndexIndex];
			}
			else if (ElementSize == 2)
			{
				uint16* IndexPtr = (uint16*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else if (ElementSize == 4)
			{
				uint32* IndexPtr = (uint32*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else
			{
				return false;
			}

			Indices.Add(VertexIndex);
		}
	}
	else
	{
		// no indices ?
		for (int32 i = 0; i < Positions.Num(); i++)
		{
			Indices.Add(i);
		}
	}

	for (FVector& Position : Positions)
	{
		FVertexID VertexID = MeshDescription->CreateVertex();
		PositionsAttributesRef[VertexID] = Position;
		VerticesIDs.Add(VertexID);
	}


	for (uint32 VertexIndex : Indices)
	{
		if (VertexIndex >= (uint32)VerticesIDs.Num())
			return false;

		FVertexInstanceID NewVertexInstanceID = MeshDescription->CreateVertexInstance(VerticesIDs[VertexIndex]);
		if (Normals.Num() > 0)
		{
			if (VertexIndex >= (uint32)Normals.Num())
			{
				NormalsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
			}
			else
			{
				NormalsInstanceAttributesRef[NewVertexInstanceID] = Normals[VertexIndex];
			}
		}

		VertexInstancesIDs.Add(NewVertexInstanceID);
		TriangleVerticesIDs.Add(VerticesIDs[VertexIndex]);

		if (VertexInstancesIDs.Num() == 3)
		{
			// degenerate ?
			if (TriangleVerticesIDs[0] == TriangleVerticesIDs[1] ||
				TriangleVerticesIDs[1] == TriangleVerticesIDs[2] ||
				TriangleVerticesIDs[0] == TriangleVerticesIDs[2])
			{
				VertexInstancesIDs.Empty();
				TriangleVerticesIDs.Empty();
				continue;
			}

			TArray<FEdgeID> Edges;
			// fix winding
			//VertexInstancesIDs.Swap(1, 2);
			FTriangleID TriangleID = MeshDescription->CreateTriangle(PolygonGroupID, VertexInstancesIDs, Edges);
			if (TriangleID == FTriangleID::Invalid)
			{
				return false;
			}
			VertexInstancesIDs.Empty();
			TriangleVerticesIDs.Empty();
		}
	}

	return true;
}

bool FglTFRuntimeParser::GetBuffer(int32 Index, TArray<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	// first check cache
	if (BuffersCache.Contains(Index))
	{
		Bytes = BuffersCache[Index];
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonBuffers;

	// no buffers ?
	if (!Root->TryGetArrayField("buffers", JsonBuffers))
	{
		return false;
	}

	if (Index >= JsonBuffers->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferObject = (*JsonBuffers)[Index]->AsObject();
	if (!JsonBufferObject)
		return false;

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField("byteLength", ByteLength))
		return false;

	FString Uri;
	if (!JsonBufferObject->TryGetStringField("uri", Uri))
		return false;

	// check it is a valid base64 data uri
	if (!Uri.StartsWith("data:"))
		return false;

	FString Base64Signature = ";base64,";

	int32 StringIndex = Uri.Find(Base64Signature, ESearchCase::IgnoreCase, ESearchDir::FromStart, 5);

	if (StringIndex < 5)
		return false;

	StringIndex += Base64Signature.Len();

	if (FBase64::Decode(Uri.Mid(StringIndex), Bytes))
	{
		BuffersCache.Add(Index, Bytes);
		return true;
	}

	return false;
}

bool FglTFRuntimeParser::GetBufferView(int32 Index, TArray<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonBufferViews;

	// no bufferViews ?
	if (!Root->TryGetArrayField("bufferViews", JsonBufferViews))
	{
		return false;
	}

	if (Index >= JsonBufferViews->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferObject = (*JsonBufferViews)[Index]->AsObject();
	if (!JsonBufferObject)
		return false;


	int64 BufferIndex;
	if (!JsonBufferObject->TryGetNumberField("buffer", BufferIndex))
		return false;

	TArray<uint8> WholeData;
	if (!GetBuffer(BufferIndex, WholeData))
		return false;

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField("byteLength", ByteLength))
		return false;

	int64 ByteOffset;
	if (!JsonBufferObject->TryGetNumberField("byteOffset", ByteOffset))
		ByteOffset = 0;

	if (ByteOffset + ByteLength > WholeData.Num())
		return false;

	Bytes.Append(&WholeData[ByteOffset], ByteLength);
	return true;
}

bool FglTFRuntimeParser::GetAccessor(int32 Index, int64& Elements, int64& ElementSize, int64& Count, TArray<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonAccessors;

	// no accessors ?
	if (!Root->TryGetArrayField("accessors", JsonAccessors))
	{
		return false;
	}

	if (Index >= JsonAccessors->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonAccessorObject = (*JsonAccessors)[Index]->AsObject();
	if (!JsonAccessorObject)
		return false;

	bool bInitWithZeros = false;

	int64 BufferViewIndex;
	if (!JsonAccessorObject->TryGetNumberField("bufferView", BufferViewIndex))
		bInitWithZeros = true;

	int64 ByteOffset;
	if (!JsonAccessorObject->TryGetNumberField("byteOffset", ByteOffset))
		ByteOffset = 0;

	int64 ComponentType;
	if (!JsonAccessorObject->TryGetNumberField("componentType", ComponentType))
		return false;

	if (!JsonAccessorObject->TryGetNumberField("count", Count))
		return false;

	FString Type;
	if (!JsonAccessorObject->TryGetStringField("type", Type))
		return false;

	ElementSize = GetComponentTypeSize(ComponentType);
	if (ElementSize == 0)
		return false;

	Elements = GetTypeSize(Type);
	if (Elements == 0)
		return false;

	uint64 FinalSize = ElementSize * Elements * Count;

	if (bInitWithZeros)
	{
		Bytes.AddZeroed(FinalSize);
		return true;
	}

	if (!GetBufferView(BufferViewIndex, Bytes))
		return false;

	if (ByteOffset > 0)
	{
		TArray<uint8> OffsetBytes;
		OffsetBytes.Append(&Bytes[ByteOffset], FinalSize);
		Bytes = OffsetBytes;
	}

	return (FinalSize <= Bytes.Num());
}

int64 FglTFRuntimeParser::GetComponentTypeSize(const int64 ComponentType) const
{
	switch (ComponentType)
	{
	case(5120):
		return 1;
	case(5121):
		return 1;
	case(5122):
		return 2;
	case(5123):
		return 2;
	case(5125):
		return 4;
	case(5126):
		return 4;
	default:
		break;
	}

	return 0;
}

int64 FglTFRuntimeParser::GetTypeSize(const FString Type) const
{
	if (Type == "SCALAR")
		return 1;
	else if (Type == "VEC2")
		return 2;
	else if (Type == "VEC3")
		return 3;
	else if (Type == "VEC4")
		return 4;
	else if (Type == "MAT2")
		return 4;
	else if (Type == "MAT3")
		return 9;
	else if (Type == "MAT4")
		return 16;

	return 0;
}

void FglTFRuntimeParser::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(StaticMeshesCache);
	Collector.AddReferencedObjects(MaterialsCache);
	Collector.AddReferencedObjects(SkeletonsCache);
}