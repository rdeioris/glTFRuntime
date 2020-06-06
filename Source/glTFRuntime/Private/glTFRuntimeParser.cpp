// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#include "IMeshBuilderModule.h"
#include "MeshUtilities.h"

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

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, FMatrix InBasis, float Scale) : Root(JsonObject), Basis(InBasis), Scale(Scale)
{
	bAllNodesCached = false;
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject) : FglTFRuntimeParser(JsonObject, FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector), 100)
{

}

bool FglTFRuntimeParser::LoadNodes()
{
	if (bAllNodesCached)
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonNodes;

	// no meshes ?
	if (!Root->TryGetArrayField("nodes", JsonNodes))
	{
		return false;
	}

	// first round for getting all nodes
	for (int32 Index = 0; Index < JsonNodes->Num(); Index++)
	{
		TSharedPtr<FJsonObject> JsonNodeObject = (*JsonNodes)[Index]->AsObject();
		if (!JsonNodeObject)
			return false;
		FglTFRuntimeNode Node;
		if (!LoadNode_Internal(Index, JsonNodeObject.ToSharedRef(), JsonNodes->Num(), Node))
			return false;

		AllNodesCache.Add(Node);
	}

	for (FglTFRuntimeNode& Node : AllNodesCache)
	{
		FixNodeParent(Node);
	}

	bAllNodesCached = true;

	return true;
}

void FglTFRuntimeParser::FixNodeParent(FglTFRuntimeNode& Node)
{
	for (int32 Index : Node.ChildrenIndices)
	{
		AllNodesCache[Index].ParentIndex = Node.Index;
		FixNodeParent(AllNodesCache[Index]);
	}
}

bool FglTFRuntimeParser::LoadScenes(TArray<FglTFRuntimeScene>& Scenes)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonScenes;
	// no scenes ?
	if (!Root->TryGetArrayField("scenes", JsonScenes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonScenes->Num(); Index++)
	{
		FglTFRuntimeScene Scene;
		if (!LoadScene(Index, Scene))
			return false;
		Scenes.Add(Scene);
	}

	return true;
}

bool FglTFRuntimeParser::LoadScene(int32 Index, FglTFRuntimeScene& Scene)
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

	Scene.Index = Index;
	Scene.Name = FString::FromInt(Scene.Index);

	FString Name;
	if (JsonSceneObject->TryGetStringField("name", Name))
	{
		Scene.Name = Name;
	}

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
			Scene.RootNodesIndices.Add(SceneNode.Index);
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

bool FglTFRuntimeParser::GetAllNodes(TArray<FglTFRuntimeNode>& Nodes)
{
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
			return false;
	}

	Nodes = AllNodesCache;

	return true;
}

bool FglTFRuntimeParser::LoadNode(int32 Index, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
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

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh(int32 Index, int32 SkinIndex, int32 NodeIndex)
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

	const TArray<TSharedPtr<FJsonValue>>* JsonSkins;
	// no skins ?
	if (!Root->TryGetArrayField("skins", JsonSkins))
	{
		return nullptr;
	}

	if (Index >= JsonSkins->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[Index]->AsObject();
	if (!JsonMeshObject)
		return nullptr;

	TSharedPtr<FJsonObject> JsonSkinObject = (*JsonSkins)[SkinIndex]->AsObject();
	if (!JsonSkinObject)
		return nullptr;

	FTransform RootTransform = FTransform::Identity;
	if (NodeIndex > INDEX_NONE)
	{
		FglTFRuntimeNode Node;
		if (!LoadNode(NodeIndex, Node))
			return nullptr;
		RootTransform = Node.Transform;
		while (Node.ParentIndex != INDEX_NONE)
		{
			if (!LoadNode(Node.ParentIndex, Node))
				return nullptr;
			RootTransform *= Node.Transform;
		}

		RootTransform = RootTransform.Inverse();
	}

	USkeletalMesh* SkeletalMesh = LoadSkeletalMesh_Internal(JsonMeshObject.ToSharedRef(), JsonSkinObject.ToSharedRef(), RootTransform);
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load skeletal mesh"));
		return nullptr;
	}

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

bool FglTFRuntimeParser::LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node)
{
	Node.Index = Index;
	Node.Name = FString::FromInt(Node.Index);

	FString Name;
	if (JsonNodeObject->TryGetStringField("name", Name))
	{
		Node.Name = Name;
	}

	int64 MeshIndex;
	if (JsonNodeObject->TryGetNumberField("mesh", MeshIndex))
	{
		Node.MeshIndex = MeshIndex;
	}

	int64 SkinIndex;
	if (JsonNodeObject->TryGetNumberField("skin", SkinIndex))
	{
		Node.SkinIndex = SkinIndex;
	}


	FMatrix Matrix = FMatrix::Identity;

	const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues;
	if (JsonNodeObject->TryGetArrayField("matrix", JsonMatrixValues))
	{
		if (JsonMatrixValues->Num() != 16)
			return false;

		for (int32 i = 0; i < 16; i++)
		{
			double Value;
			if (!(*JsonMatrixValues)[i]->TryGetNumber(Value))
			{
				return false;
			}

			Matrix.M[i / 4][i % 4] = Value;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonScaleValues;
	if (JsonNodeObject->TryGetArrayField("scale", JsonScaleValues))
	{
		if (JsonScaleValues->Num() != 3)
			return false;

		float X, Y, Z;
		if (!(*JsonScaleValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonScaleValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonScaleValues)[2]->TryGetNumber(Z))
			return false;

		FVector MatrixScale = { X, Y, Z };

		Matrix *= FScaleMatrix(MatrixScale);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonRotationValues;
	if (JsonNodeObject->TryGetArrayField("rotation", JsonRotationValues))
	{
		if (JsonRotationValues->Num() != 4)
			return false;

		float X, Y, Z, W;
		if (!(*JsonRotationValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonRotationValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonRotationValues)[2]->TryGetNumber(Z))
			return false;
		if (!(*JsonRotationValues)[3]->TryGetNumber(W))
			return false;

		FQuat Quat = { X, Y, Z, W };

		Matrix *= FQuatRotationMatrix(Quat);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTranslationValues;
	if (JsonNodeObject->TryGetArrayField("translation", JsonTranslationValues))
	{
		if (JsonTranslationValues->Num() != 3)
			return false;

		float X, Y, Z;
		if (!(*JsonTranslationValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonTranslationValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonTranslationValues)[2]->TryGetNumber(Z))
			return false;

		FVector Translation = { X, Y, Z };

		Matrix *= FTranslationMatrix(Translation);
	}

	Matrix.ScaleTranslation(FVector(Scale, Scale, Scale));
	Node.Transform = FTransform(Basis.Inverse() * Matrix * Basis);
	//Node.Transform = FTransform(Basis * Matrix);


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

			Node.ChildrenIndices.Add(ChildIndex);
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
		double metallicFactor;
		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", metallicFactor))
		{
			Material->SetScalarParameterValue("metallicFactor", metallicFactor);
		}
		double roughnessFactor;
		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", roughnessFactor))
		{
			Material->SetScalarParameterValue("roughnessFactor", roughnessFactor);
		}
	}

	return Material;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, TSharedRef<FJsonObject> JsonSkinObject, FTransform& RootTransform)
{

	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives))
		return nullptr;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>();

	TMap<int32, FName> BoneMap;

	if (!FillReferenceSkeleton(JsonSkinObject, SkeletalMesh->RefSkeleton, BoneMap, RootTransform))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to fill skeleton!"));
		return nullptr;
	}

	TArray<SkeletalMeshImportData::FVertex> Wedges;
	TArray<SkeletalMeshImportData::FTriangle> Triangles;
	TArray<SkeletalMeshImportData::FRawBoneInfluence> Influences;
	TArray<FVector> Points;
	TArray<int32> PointToRawMap;

	int32 MatIndex = 0;
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		int32 Base = Points.Num();
		/*for (FVector Point : Primitive.Positions)
		{
			Points.Add(RootTransform.TransformPosition(Point));
		}*/
		Points.Append(Primitive.Positions);

		int32 TriangleIndex = 0;

		for (int32 i = 0; i < Primitive.Indices.Num(); i++)
		{
			int32 Index = Primitive.Indices[i];

			SkeletalMeshImportData::FVertex Wedge;
			Wedge.VertexIndex = Base + Index;

			int32 WedgeIndex = Wedges.Add(Wedge);

			for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
			{
				FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][Index];
				FVector4 Weights = Primitive.Weights[JointsIndex][Index];


				for (int32 j = 0; j < 4; j++)
				{
					if (BoneMap.Contains(Joints[j]))
					{
						SkeletalMeshImportData::FRawBoneInfluence Influence;
						Influence.VertexIndex = Wedge.VertexIndex;
						Influence.BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMap[Joints[j]]);
						Influence.Weight = Weights[j];
						Influences.Add(Influence);
						//if (j == 1 && Influence.Weight != 0)
						//	UE_LOG(LogTemp, Error, TEXT("Influence: %d %d %f"), Influence.VertexIndex, Influence.BoneIndex, Influence.Weight);
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Unable to find map for bone %u"), Joints[j]);
					}
				}
			}

			TriangleIndex++;
			if (TriangleIndex == 3)
			{
				SkeletalMeshImportData::FTriangle Triangle;

				Triangle.WedgeIndex[0] = WedgeIndex - 2;
				Triangle.WedgeIndex[1] = WedgeIndex - 1;
				Triangle.WedgeIndex[2] = WedgeIndex;

				if (Primitive.Normals.Num() > 0)
				{
					Triangle.TangentZ[0] = Primitive.Normals[Primitive.Indices[i - 2]];
					Triangle.TangentZ[1] = Primitive.Normals[Primitive.Indices[i - 1]];
					Triangle.TangentZ[2] = Primitive.Normals[Primitive.Indices[i]];
				}

				Triangle.MatIndex = MatIndex;

				Triangles.Add(Triangle);
				TriangleIndex = 0;
			}
		}

		MatIndex++;
	}

	FSkeletalMeshImportData ImportData;

	for (int32 i = 0; i < Points.Num(); i++)
		PointToRawMap.Add(i);

	ImportData.bHasNormals = true;
	ImportData.bHasVertexColors = false;
	ImportData.bHasTangents = false;
	ImportData.Faces = Triangles;
	ImportData.Points = Points;
	ImportData.PointToRawMap = PointToRawMap;
	ImportData.NumTexCoords = 1;
	ImportData.Wedges = Wedges;
	ImportData.Influences = Influences;

	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
	FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];

	SkeletalMesh->SaveLODImportedData(0, ImportData);

	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LODInfo.BuildSettings.bRecomputeNormals = false;
	LODInfo.LODHysteresis = 0.02f;

	SkeletalMesh->CalculateInvRefMatrices();

	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(Points.GetData(), Points.Num()));

	SkeletalMesh->bHasVertexColors = false;
	SkeletalMesh->VertexColorGuid = SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();


	for (MatIndex = 0; MatIndex < Primitives.Num(); MatIndex++)
	{
		LODInfo.LODMaterialMap.Add(MatIndex);
		SkeletalMesh->Materials.Add(Primitives[MatIndex].Material);
		SkeletalMesh->Materials[MatIndex].UVChannelData.bInitialized = true;
	}

	IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMesh, 0, false))
		return nullptr;

	SkeletalMesh->Build();

	SkeletalMesh->Skeleton = NewObject<USkeleton>();
	SkeletalMesh->Skeleton->RecreateBoneTree(SkeletalMesh);
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	return SkeletalMesh;
}

bool FglTFRuntimeParser::HasRoot(int32 Index, int32 RootIndex)
{
	if (Index == RootIndex)
		return true;

	FglTFRuntimeNode Node;
	if (!LoadNode(Index, Node))
		return false;

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!LoadNode(Node.ParentIndex, Node))
			return false;
		if (Node.Index == RootIndex)
			return true;
	}

	return false;
}

int32 FglTFRuntimeParser::FindCommonRoot(TArray<int32> Indices)
{
	int32 CurrentRootIndex = Indices[0];
	bool bTryNextParent = true;

	while (bTryNextParent)
	{
		FglTFRuntimeNode Node;
		if (!LoadNode(CurrentRootIndex, Node))
			return INDEX_NONE;

		bTryNextParent = false;
		for (int32 Index : Indices)
		{
			if (!HasRoot(Index, CurrentRootIndex))
			{
				bTryNextParent = true;
				CurrentRootIndex = Node.ParentIndex;
				break;
			}
		}
	}

	return CurrentRootIndex;
}

bool FglTFRuntimeParser::FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, FTransform& RootTransform)
{
	//RootTransform = FTransform::Identity;

	// get the list of valid joints	
	const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
	TArray<int32> Joints;
	if (JsonSkinObject->TryGetArrayField("joints", JsonJoints))
	{
		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
				return false;
			Joints.Add(JointIndex);
		}
	}

	if (Joints.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No Joints available"));
		return false;
	}

	TMap<int32, int32> ChildrenMap;
	bool bHasRoot = true;
	// fill the root bone
	FglTFRuntimeNode RootNode;
	int64 RootBoneIndex;
	if (!JsonSkinObject->TryGetNumberField("skeleton", RootBoneIndex))
	{
		RootBoneIndex = FindCommonRoot(Joints);
		if (RootBoneIndex == INDEX_NONE)
			return false;
		bHasRoot = false;
	}

	if (!LoadNode(RootBoneIndex, RootNode))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load joint node"));
		return false;
	}

	/*if (!bHasRoot)
	{
		while (RootNode.ParentIndex != INDEX_NONE && Joints.Contains(RootNode.ParentIndex))
		{
			//ChildrenMap.Add(RootNode->ParentIndex, RootNode->Index);
			if (!LoadNode(RootNode.ParentIndex, RootNode))
			{
				UE_LOG(LogTemp, Error, TEXT("Unable to load parent joint node"));
				return false;
			}
		}
	}*/

	TMap<int32, FMatrix> InverseBindMatricesMap;
	int64 inverseBindMatricesIndex;
	if (JsonSkinObject->TryGetNumberField("inverseBindMatrices", inverseBindMatricesIndex))
	{
		TArray<uint8> InverseBindMatricesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(inverseBindMatricesIndex, ComponentType, Stride, Elements, ElementSize, Count, InverseBindMatricesBytes))
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to load accessor: %lld"), inverseBindMatricesIndex);
			return false;
		}

		if (Elements != 16 && ComponentType != 5126)
			return false;

		UE_LOG(LogTemp, Warning, TEXT("Striding: %lld"), Stride);

		for (int64 i = 0; i < Count; i++)
		{
			FMatrix Matrix;
			int64 MatrixIndex = i * Stride;

			float* MatrixCell = (float*)&InverseBindMatricesBytes[MatrixIndex];

			for (int32 j = 0; j < 16; j++)
			{
				float Value = MatrixCell[j];

				Matrix.M[j / 4][j % 4] = Value;
			}

			if (i < Joints.Num())
			{
				InverseBindMatricesMap.Add(Joints[i], Matrix);
			}
		}
	}

	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	// now traverse from the root and check if the node is in the "joints" list
	if (!TraverseJoints(Modifier, INDEX_NONE, RootNode, Joints, BoneMap, InverseBindMatricesMap, RootTransform, bHasRoot))
		return false;

	return true;
}

bool FglTFRuntimeParser::TraverseJoints(FReferenceSkeletonModifier& Modifier, int32 Parent, FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, FTransform& RootTransform, const bool bHasRoot)
{
	FName BoneName = FName(*Node.Name);

	// first check if a bone with the same name exists, on collision, append an underscore
	int32 CollidingIndex = Modifier.FindBoneIndex(BoneName);
	while (CollidingIndex != INDEX_NONE)
	{
		BoneName = FName(BoneName.ToString() + "_");
		CollidingIndex = Modifier.FindBoneIndex(BoneName);
	}

	FTransform Transform = FTransform(Basis.Inverse() * FMatrix::Identity * Basis);// Node.Transform;
	if (InverseBindMatricesMap.Contains(Node.Index))
	{
		UE_LOG(LogTemp, Warning, TEXT("Using Bind matrix for %d %s"), Node.Index, *Node.Name);

		FMatrix M = InverseBindMatricesMap[Node.Index].Inverse();
		if (Node.ParentIndex != INDEX_NONE && Joints.Contains(Node.ParentIndex))
		{
			M *= InverseBindMatricesMap[Node.ParentIndex];
		}

		UE_LOG(LogTemp, Error, TEXT("***** %d *****"), Node.Index);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]);


		M.ScaleTranslation(FVector(Scale, Scale, Scale));
		//Transform = FTransform(M);
		Transform = FTransform(Basis.Inverse() * M * Basis);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("No bind transform for node %d"), Node.Index);
	}

	/*if (Parent == INDEX_NONE && !bHasRoot)
	{
		FglTFRuntimeNode RootNode = Node;
		while (RootNode.ParentIndex != INDEX_NONE)
		{
			if (!LoadNode(RootNode.ParentIndex, RootNode))
				return false;
			Transform *= RootNode.Transform.Inverse();
		}
		//RootTransform = Node.Transform;
	}*/

	if (Parent == INDEX_NONE && !bHasRoot)
	{
		//Transform *= RootTransform;
	}

	Modifier.Add(FMeshBoneInfo(BoneName, Node.Name, Parent), Transform);

	int32 NewParentIndex = Modifier.FindBoneIndex(BoneName);
	// something horrible happened...
	if (NewParentIndex == INDEX_NONE)
		return false;

	if (Joints.Contains(Node.Index))
	{
		BoneMap.Add(Joints.IndexOfByKey(Node.Index), BoneName);
	}

	/*if (ChildrenMap.Contains(Node->Index))
	{
		TSharedPtr<FglTFRuntimeNode> ChildNode = LoadNode(ChildrenMap[Node->Index]);
		if (!ChildNode)
			return false;
		if (!TraverseJoints(Modifier, NewParentIndex, ChildNode.ToSharedRef(), Joints, BoneMap, InverseBindMatricesMap, ChildrenMap, bHasRoot))
			return false;
		return true;
	}*/


	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		//if (!Joints.Contains(ChildNode->Index))
		//	continue;

		FglTFRuntimeNode ChildNode;
		if (!LoadNode(ChildIndex, ChildNode))
			return false;

		if (!TraverseJoints(Modifier, NewParentIndex, ChildNode, Joints, BoneMap, InverseBindMatricesMap, RootTransform, bHasRoot))
			return false;
	}

	return true;
}

USkeletalMesh* FglTFRuntimeParser::_LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject)
{
	// get skin
	int64 SkinIndex = 0;
	/*if (!JsonMeshObject->TryGetNumberField("skin", SkinIndex))
		return nullptr;*/

	USkeleton* Skeleton = nullptr;

	// do we already have a skeleton cached ?
	if (SkeletonsCache.Contains(SkinIndex))
	{
		Skeleton = SkeletonsCache[SkinIndex];
	}

	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives))
		return nullptr;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>();


	//SkeletalMesh->PreEditChange(nullptr);

#if WITH_EDITOR
	IMeshUtilities& MeshUtilities = FModuleManager::Get().LoadModuleChecked<IMeshUtilities>("MeshUtilities");
	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());

	FSkeletalMeshLODModel& SkeletalMeshLOD = ImportedResource->LODModels[0];
#else
	FSkeletalMeshLODRenderData* SkeletalMeshLOD = new FSkeletalMeshLODRenderData();
	SkeletalMesh->AllocateResourceForRendering();
	SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(SkeletalMeshLOD);
#endif
	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& LodInfo = SkeletalMesh->AddLODInfo();
	//LodInfo.ScreenSize = 0.3f;
	//LodInfo.LODHysteresis = 0.2f;

	SkeletalMesh->RefSkeleton.Empty(3);

	LodInfo.BuildSettings.bUseFullPrecisionUVs = true;
	LodInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LodInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LodInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LodInfo.LODHysteresis = 0.02f;

	SkeletalMesh->bHasBeenSimplified = false;
	SkeletalMesh->bHasVertexColors = false;

	for (uint32 BoneIndex = 0; BoneIndex < 3; BoneIndex++)
	{
		SkeletalMeshLOD.RequiredBones.Add(BoneIndex);
		SkeletalMeshLOD.ActiveBoneIndices.Add(BoneIndex);

		FString BoneString = FString::FromInt(BoneIndex);

		FName BoneName = FName(*BoneString);

		FTransform Transform = FTransform::Identity;

		FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(SkeletalMesh->RefSkeleton, nullptr);
		int32 ParentIndex = BoneIndex > 0 ? 0 : INDEX_NONE;
		Modifier.Add(FMeshBoneInfo(BoneName, BoneString, ParentIndex), Transform);
	}

	//SkeletalMeshLOD->Sections.SetNumUninitialized(1);// Primitives.Num());

#if !WITH_EDITOR
	TArray<FSkinWeightInfo> InWeights;
	InWeights.AddUninitialized(6);
	TMap<int32, TArray<int32>> OverlappingVertices;

	LodRenderData->RenderSections.SetNumUninitialized(SubmeshCount);
	LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(6);
	LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(6, 1);
#endif

	TArray<FVector> Points;
	TArray<int32> PointsOriginalMap;
	TArray<SkeletalMeshImportData::FMeshWedge> Wedges;
	TArray<SkeletalMeshImportData::FMeshFace> Faces;
	TArray<SkeletalMeshImportData::FVertInfluence> Influences;

	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		UE_LOG(LogTemp, Warning, TEXT("%d %d %d"), Primitive.Indices.Num(), Primitive.Positions.Num(), Primitive.Normals.Num());
	}


	bool bHasNormals = false;

	for (int32 submeshCounter = 0; submeshCounter < Primitives.Num(); submeshCounter++)
	{
		FglTFRuntimePrimitive& Primitive = Primitives[submeshCounter];
		LodInfo.LODMaterialMap.Add(submeshCounter);
		SkeletalMesh->Materials.Add(Primitive.Material);
		SkeletalMesh->Materials[submeshCounter].UVChannelData.bInitialized = true;

#if WITH_EDITOR
		//new(&SkeletalMeshLOD->Sections[submeshCounter]) FSkelMeshSection();
		//FSkelMeshSection& MeshSection = SkeletalMeshLOD->Sections[submeshCounter];
#else
		new(&SkeletalMeshLOD->Sections[submeshCounter]) FSkelMeshSection();
		FSkelMeshSection& MeshSection = SkeletalMeshLOD->Sections[submeshCounter];
#endif

		/*MeshSection.MaterialIndex = submeshCounter;
		MeshSection.BaseIndex = Wedges.Num();
		MeshSection.NumTriangles = Primitive.Indices.Num() / 3;
		MeshSection.BaseVertexIndex = Points.Num();
		MeshSection.MaxBoneInfluences = 2;

		MeshSection.NumVertices = Primitive.Positions.Num();*/


		int32 Base = Points.Num();
		Points.Append(Primitive.Positions);


#if WITH_EDITOR

#else
		MeshSection.DuplicatedVerticesBuffer.Init(6, OverlappingVertices);
#endif

		if (Primitive.Indices.Num() % 3 != 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Invalid number of indices: %d"), Primitive.Indices.Num());
			return nullptr;
		}

		int32 TriangleCounter = 0;
		for (int32 VertIndex = 0; VertIndex < Primitive.Indices.Num(); VertIndex++)
		{

#if WITH_EDITOR
			SkeletalMeshImportData::FMeshWedge Wedge;
			Wedge.iVertex = Base + Primitive.Indices[VertIndex];
			Wedge.UVs[0] = { 0, 0 };



			int32 WedgeIndex = Wedges.Add(Wedge);


			SkeletalMeshImportData::FVertInfluence Influence;
			Influence.VertIndex = WedgeIndex;
			Influence.BoneIndex = 0;
			Influence.Weight = 1.0;
			Influences.Add(Influence);

			TriangleCounter++;
			if (TriangleCounter == 3)
			{
				SkeletalMeshImportData::FMeshFace Face;
				Face.iWedge[0] = WedgeIndex - 2;
				Face.iWedge[1] = WedgeIndex - 1;
				Face.iWedge[2] = WedgeIndex;
				if (Primitive.Normals.Num() > 0)
				{
					Face.TangentZ[0] = Primitive.Normals[Primitive.Indices[VertIndex - 2]];
					Face.TangentZ[1] = Primitive.Normals[Primitive.Indices[VertIndex - 1]];
					Face.TangentZ[2] = Primitive.Normals[Primitive.Indices[VertIndex]];
					bHasNormals = true;
				}
				Face.MeshMaterialIndex = submeshCounter;
				Face.SmoothingGroups = 0;
				Faces.Add(Face);
				TriangleCounter = 0;
			}

#else
			FModelVertex ModelVertex;
			ModelVertex.Position = Points[VertIndex];
			FVector rn = FVector::ForwardVector;
			FVector rt = FVector::UpVector;
			ModelVertex.TangentX = rt;
			ModelVertex.TangentZ = rn;
			ModelVertex.TexCoord = FVector2D(0.2, 0.3);

			LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertIndex) = ModelVertex.Position;
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertIndex, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(VertIndex, 0, ModelVertex.TexCoord);

			InWeights[VertIndex].InfluenceWeights[0] = 255;
			InWeights[VertIndex].InfluenceBones[0] = 1;

#endif
		}

		/*for (uint32 BoneIndex = 0; BoneIndex < 3; BoneIndex++)
		{
			MeshSection.BoneMap.Add(BoneIndex);
		}*/

		//MeshSection.bRecomputeTangent = true;

		//MeshSection.CalcMaxBoneInfluences();
		//MeshSection.CalcUse16BitBoneIndex();
	}

	SkeletalMeshLOD.NumTexCoords = 1;

#if WITH_EDITOR
	UE_LOG(LogTemp, Error, TEXT("SkeletalMesh Builder: %d %d %d %d"), Wedges.Num(), Faces.Num(), Points.Num(), PointsOriginalMap.Num());

	IMeshUtilities::MeshBuildOptions BuildSettings;
	BuildSettings.bUseMikkTSpace = true;
	BuildSettings.bComputeNormals = !bHasNormals;
	BuildSettings.bComputeTangents = true;
	BuildSettings.bComputeWeightedNormals = false;
	BuildSettings.bRemoveDegenerateTriangles = false;

	FBox BoundingBox(Points.GetData(), Points.Num());
	FBox Temp = BoundingBox;
	FVector MidMesh = 0.5f * (Temp.Min + Temp.Max);
	BoundingBox.Min = Temp.Min + 1.0f * (Temp.Min - MidMesh);
	BoundingBox.Max = Temp.Max + 1.0f * (Temp.Max - MidMesh);
	BoundingBox.Min[2] = Temp.Min[2] + 0.1f * (Temp.Min[2] - MidMesh[2]);

	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

	//SkeletalMesh->ClearAllCachedCookedPlatformData();

	for (int32 i = 0; i < Points.Num(); i++)
		PointsOriginalMap.Add(i);

	if (MeshUtilities.BuildSkeletalMesh(SkeletalMeshLOD, SkeletalMesh->RefSkeleton, Influences, Wedges, Faces, Points, PointsOriginalMap, BuildSettings))
	{
		UE_LOG(LogTemp, Error, TEXT("SkeletalMesh LOD successfully generated"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to generate LOD!!!"));
	}
#else
	LodRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(2);
	LodRenderData->SkinWeightVertexBuffer = InWeights;
	LodRenderData->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint16_t));

	for (uint32_t index = 0; index < 6; index++)
	{
		LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(index);
	}
#endif

	//LodInfo.BuildSettings.bBuildAdjacencyBuffer = true;
	//LodInfo.BuildSettings.bRemoveDegenerates = true;

	//SkeletalMesh->CalculateRequiredBones(*LodRenderData, SkeletalMesh->RefSkeleton, nullptr);

	//SkeletalMesh->AllocateResourceForRendering();

	//UE_LOG(LogTemp, Warning, TEXT("Rendering: %p"), SkeletalMesh->GetResourceForRendering());

	//SkeletalMesh->Build();


	//UE_LOG(LogTemp, Error, TEXT("RefNum: %d %d %d"), SkeletalMesh->RefSkeleton.GetNum(), SkeletalMesh->GetResourceForRendering()->LODRenderData.Num(), SkeletalMesh->GetLODNum());

#if WITH_EDITOR
	//SkeletalMesh->PostEditChange();
#endif

	//UE_LOG(LogTemp, Error, TEXT("RefNum: %d %d %d"), SkeletalMesh->RefSkeleton.GetNum(), SkeletalMesh->GetResourceForRendering()->LODRenderData.Num(), SkeletalMesh->GetLODNum());

	SkeletalMesh->CalculateInvRefMatrices();
	SkeletalMesh->PostEditChange();

	//SkeletalMesh->PostLoad();

	SkeletalMesh->Skeleton = NewObject<USkeleton>();
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	//SkeletalMesh->AllocateResourceForRendering();
	//SkeletalMesh->InitResources();
	SkeletalMesh->PostLoad();

	//SkeletalMesh->GetImportedModel()->LODModels.Add(new FSkeletalMeshLODModel());
	//SkeletalMesh->GetImportedModel()->LODModels[0].Sections.Add(FSkelMeshSection());
	//SkeletalMesh->GetImportedModel()->LODModels[0].Sections.Add(FSkelMeshSection());

	//SkeletalMesh->PostLoad();

	/*SkeletalMesh->CalculateRequiredBones(*SkeletalMeshLOD, SkeletalMesh->RefSkeleton, nullptr);
	SkeletalMesh->CalculateInvRefMatrices();

	SkeletalMesh->Skeleton->RecreateBoneTree(SkeletalMesh);
	SkeletalMesh->Skeleton->SetPreviewMesh(SkeletalMesh);*/
	//SkeletalMesh->PostEditChange();

	SkeletonsCache.Add(SkinIndex, Skeleton);

	//UE_LOG(LogTemp, Error, TEXT("RefNum: %d %d %d"), SkeletalMesh->RefSkeleton.GetNum(), SkeletalMesh->GetResourceForRendering()->LODRenderData.Num(), SkeletalMesh->GetLODNum());

	return SkeletalMesh;
}

bool FglTFRuntimeParser::LoadPrimitives(const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives, TArray<FglTFRuntimePrimitive>& Primitives)
{
	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
			return false;

		FglTFRuntimePrimitive Primitive;
		if (!LoadPrimitive(JsonPrimitiveObject.ToSharedRef(), Primitive))
			return false;

		Primitives.Add(Primitive);
	}
	return true;
}

bool FglTFRuntimeParser::LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive)
{
	const TSharedPtr<FJsonObject>* JsonAttributesObject;
	if (!JsonPrimitiveObject->TryGetObjectField("attributes", JsonAttributesObject))
		return false;

	// POSITION is required for generating a valid Mesh
	if (!(*JsonAttributesObject)->HasField("POSITION"))
	{
		UE_LOG(LogTemp, Error, TEXT("Error loading position"));
		return false;
	}

	if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "POSITION", Primitive.Positions,
		{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector {return Basis.TransformPosition(Value) * Scale; }))
		return false;

	if ((*JsonAttributesObject)->HasField("NORMAL"))
	{
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "NORMAL", Primitive.Normals,
			{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector { return Basis.TransformVector(Value); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading normals"));
			return false;
		}

	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_0"))
	{
		TArray<FVector2D> UV;
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "TEXCOORD_0", UV,
			{ 2 }, { 5126, 5121, 5123 }, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, 1 - Value.Y); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading uvs 0"));
			return false;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_1"))
	{
		TArray<FVector2D> UV;
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "TEXCOORD_1", UV,
			{ 2 }, { 5126, 5121, 5123 }, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, 1 - Value.Y); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading uvs 1"));
			return false;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField("JOINTS_0"))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "JOINTS_0", Joints,
			{ 4 }, { 5121, 5123 }, false))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading joints 0"));
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField("WEIGHTS_0"))
	{
		TArray<FVector4> Weights;
		if (!BuildPrimitiveAttribute(JsonAttributesObject->ToSharedRef(), "WEIGHTS_0", Weights,
			{ 4 }, { 5126, 5121, 5123 }, true))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading weights 0"));
			return false;
		}
		Primitive.Weights.Add(Weights);
	}

	int64 IndicesAccessorIndex;
	if (JsonPrimitiveObject->TryGetNumberField("indices", IndicesAccessorIndex))
	{
		TArray<uint8> IndicesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(IndicesAccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, IndicesBytes))
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to load accessor: %lld"), IndicesAccessorIndex);
			return false;
		}

		if (Elements != 1)
			return false;

		for (int64 i = 0; i < Count; i++)
		{
			int64 IndexIndex = i * Stride;

			uint32 VertexIndex;
			if (ComponentType == 5121)
			{
				VertexIndex = IndicesBytes[IndexIndex];
			}
			else if (ComponentType == 5123)
			{
				uint16* IndexPtr = (uint16*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else if (ComponentType == 5125)
			{
				uint32* IndexPtr = (uint32*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Invalid component type for indices: %lld"), ComponentType);
				return false;
			}

			Primitive.Indices.Add(VertexIndex);
		}
	}
	else
	{
		for (int32 VertexIndex = 0; VertexIndex < Primitive.Positions.Num(); VertexIndex++)
		{
			Primitive.Indices.Add(VertexIndex);
		}
	}


	int64 MaterialIndex;
	if (JsonPrimitiveObject->TryGetNumberField("material", MaterialIndex))
	{
		Primitive.Material = LoadMaterial(MaterialIndex);
		if (!Primitive.Material)
		{
			UE_LOG(LogTemp, Error, TEXT("unable to load material %lld"), MaterialIndex);
			return false;
		}
	}
	else
	{
		Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	UE_LOG(LogTemp, Error, TEXT("Primitive with indices: %d %d %d %d %d"), Primitive.Indices.Num(), Primitive.Positions.Num(), Primitive.Normals.Num(), Primitive.Joints[0].Num(), Primitive.Weights[0].Num());

	return true;
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

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives))
		return nullptr;

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>();

	UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

	TArray<FStaticMaterial> StaticMaterials;

	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		FPolygonGroupID PolygonGroupID = MeshDescription->CreatePolygonGroup();

		TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames = MeshDescription->GetPolygonGroupMaterialSlotNames();
		PolygonGroupMaterialSlotNames[PolygonGroupID] = Primitive.Material->GetFName();
		FStaticMaterial StaticMaterial(Primitive.Material, Primitive.Material->GetFName());
		StaticMaterial.UVChannelData.bInitialized = true;
		StaticMaterials.Add(StaticMaterial);

		TVertexAttributesRef<FVector> PositionsAttributesRef = MeshDescription->GetVertexPositions();
		TVertexInstanceAttributesRef<FVector> NormalsInstanceAttributesRef = MeshDescription->GetVertexInstanceNormals();

		TArray<FVertexInstanceID> VertexInstancesIDs;
		TArray<FVertexID> VerticesIDs;
		TArray<FVertexID> TriangleVerticesIDs;


		for (FVector& Position : Primitive.Positions)
		{
			FVertexID VertexID = MeshDescription->CreateVertex();
			PositionsAttributesRef[VertexID] = Position;
			VerticesIDs.Add(VertexID);
		}

		for (uint32 VertexIndex : Primitive.Indices)
		{
			if (VertexIndex >= (uint32)VerticesIDs.Num())
				return false;

			FVertexInstanceID NewVertexInstanceID = MeshDescription->CreateVertexInstance(VerticesIDs[VertexIndex]);
			if (Primitive.Normals.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Normals.Num())
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Normals[VertexIndex];
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

	}

	StaticMesh->StaticMaterials = StaticMaterials;

	TArray<UStaticMeshDescription*> MeshDescriptions = { MeshDescription };
	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, false);

	return StaticMesh;
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

bool FglTFRuntimeParser::GetBufferView(int32 Index, TArray<uint8>& Bytes, int64& Stride)
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

	if (!JsonBufferObject->TryGetNumberField("byteStride", Stride))
		Stride = 0;

	if (ByteOffset + ByteLength > WholeData.Num())
		return false;

	Bytes.Append(&WholeData[ByteOffset], ByteLength);
	return true;
}

bool FglTFRuntimeParser::GetAccessor(int32 Index, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, TArray<uint8>& Bytes)
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

	if (!GetBufferView(BufferViewIndex, Bytes, Stride))
		return false;

	if (Stride == 0)
	{
		Stride = ElementSize * Elements;
	}

	FinalSize = Stride * Count;

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
	Collector.AddReferencedObjects(SkeletalMeshesCache);
}