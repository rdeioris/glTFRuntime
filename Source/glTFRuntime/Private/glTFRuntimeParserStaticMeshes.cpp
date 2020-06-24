// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	FglTFRuntimeMaterialsConfig MaterialsConfig;

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives, MaterialsConfig))
		return nullptr;

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Public);

	UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

	TArray<FStaticMaterial> StaticMaterials;

	int32 NumUVs = 1;
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.UVs.Num() > NumUVs)
		{
			NumUVs = Primitive.UVs.Num();
		}
	}

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
		TVertexInstanceAttributesRef<FVector> TangentsInstanceAttributesRef = MeshDescription->GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<FVector2D> UVsInstanceAttributesRef = MeshDescription->GetVertexInstanceUVs();

		UVsInstanceAttributesRef.SetNumIndices(NumUVs);

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

			if (Primitive.Tangents.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Tangents.Num())
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Tangents[VertexIndex];
				}
			}

			for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
			{
				if (VertexIndex >= (uint32)Primitive.UVs[UVIndex].Num())
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, FVector2D::ZeroVector);
				}
				else
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, Primitive.UVs[UVIndex][VertexIndex]);
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
				// fix winding ?
				if (StaticMeshConfig.bReverseWinding)
				{
					VertexInstancesIDs.Swap(1, 2);
				}
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
	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, StaticMeshConfig.bBuildSimpleCollision);

	bool bRebuildPhysicsMeshes = false;

#if WITH_EDITOR
	StaticMesh->ComplexCollisionMesh = StaticMeshConfig.ComplexCollisionMesh;
#else
	if (StaticMeshConfig.ComplexCollisionMesh)
	{
		StaticMesh->RenderData->AllocateLODResources(2);
		StaticMesh->RenderData->LODResources.Insert(&StaticMeshConfig.ComplexCollisionMesh->RenderData->LODResources[0], 1);
		StaticMesh->LODForCollision = 1;
		bRebuildPhysicsMeshes = true;
	}
#endif

	for (const FBox& Box : StaticMeshConfig.BoxCollisions)
	{
		if (!StaticMesh->BodySetup)
		{
			StaticMesh->CreateBodySetup();
		}
		FKBoxElem BoxElem;
		BoxElem.Center = Box.GetCenter();
		FVector BoxSize = Box.GetSize();
		BoxElem.X = BoxSize.X;
		BoxElem.Y = BoxSize.Y;
		BoxElem.Z = BoxSize.Z;
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);
		bRebuildPhysicsMeshes = true;
	}

	for (const FVector4 Sphere : StaticMeshConfig.SphereCollisions)
	{
		if (!StaticMesh->BodySetup)
		{
			StaticMesh->CreateBodySetup();
		}
		FKSphereElem SphereElem;
		SphereElem.Center = Sphere;
		SphereElem.Radius = Sphere.W;
		StaticMesh->BodySetup->AggGeom.SphereElems.Add(SphereElem);
		bRebuildPhysicsMeshes = true;
	}

	if (bRebuildPhysicsMeshes)
	{
		StaticMesh->BodySetup->CreatePhysicsMeshes();
	}

	return StaticMesh;
}

bool FglTFRuntimeParser::LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonMeshes->Num(); Index++)
	{
		UStaticMesh* StaticMesh = LoadStaticMesh(Index, StaticMeshConfig);
		if (!StaticMesh)
		{
			return false;
		}
		StaticMeshes.Add(StaticMesh);
	}

	return true;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{

	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		return nullptr;
	}

	if (CanReadFromCache(StaticMeshConfig.CacheMode) && StaticMeshesCache.Contains(MeshIndex))
	{
		return StaticMeshesCache[MeshIndex];
	}

	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObject.ToSharedRef(), StaticMeshConfig);
	if (!StaticMesh)
	{
		return nullptr;
	}

	if (CanWriteToCache(StaticMeshConfig.CacheMode))
	{
		StaticMeshesCache.Add(MeshIndex, StaticMesh);
	}

	return StaticMesh;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMeshByName(const FString Name, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	for (int32 MeshIndex = 0; MeshIndex < JsonMeshes->Num(); MeshIndex++)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[MeshIndex]->AsObject();
		if (!JsonMeshObject)
		{
			return nullptr;
		}
		FString MeshName;
		if (JsonMeshObject->TryGetStringField("name", MeshName))
		{
			if (MeshName == Name)
			{
				return LoadStaticMesh(MeshIndex, StaticMeshConfig);
			}
		}
	}

	return nullptr;
}
