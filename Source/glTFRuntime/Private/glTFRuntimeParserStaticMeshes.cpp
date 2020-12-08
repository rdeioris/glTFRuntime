// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"
#include "StaticMeshOperations.h"
#include "Engine/StaticMeshSocket.h"
#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TArray<TSharedRef<FJsonObject>> JsonMeshObjects, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(StaticMeshConfig.Outer ? StaticMeshConfig.Outer : GetTransientPackage(), NAME_None, RF_Public);
	StaticMesh->bAllowCPUAccess = StaticMeshConfig.bAllowCPUAccess;

	bool bHasVertexColors = false;
	FVector LOD0PivotDelta = FVector::ZeroVector;

	TArray<UStaticMeshDescription*> MeshDescriptions;

	TArray<uint32> LOD0CPUVertexInstancesIDs;

	for (TSharedRef<FJsonObject> JsonMeshObject : JsonMeshObjects)
	{

		TArray<FglTFRuntimePrimitive> Primitives;
		if (!LoadPrimitives(JsonMeshObject, Primitives, StaticMeshConfig.MaterialsConfig))
			return nullptr;

		UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

		TArray<FStaticMaterial> StaticMaterials;

		int32 NumUVs = 1;
		bool bCalculateNormals = false;
		bool bCalculateTangents = false;
		FVector PivotDelta = FVector::ZeroVector;

		for (FglTFRuntimePrimitive& Primitive : Primitives)
		{
			if (Primitive.UVs.Num() > NumUVs)
			{
				NumUVs = Primitive.UVs.Num();
			}

			if (Primitive.Normals.Num() == 0)
			{
				bCalculateNormals = true;
			}

			if (Primitive.Tangents.Num() == 0)
			{
				bCalculateTangents = true;
			}
		}

		for (FglTFRuntimePrimitive& Primitive : Primitives)
		{
			FPolygonGroupID PolygonGroupID = MeshDescription->CreatePolygonGroup();

			TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames = MeshDescription->GetPolygonGroupMaterialSlotNames();
			FName MaterialName = FName(FString::Printf(TEXT("LOD_%d_Section_%d"), MeshDescriptions.Num(), StaticMaterials.Num()));
			PolygonGroupMaterialSlotNames[PolygonGroupID] = MaterialName;
			FStaticMaterial StaticMaterial(Primitive.Material, MaterialName);
			StaticMaterial.UVChannelData.bInitialized = true;
			StaticMaterials.Add(StaticMaterial);

			TVertexAttributesRef<FVector> PositionsAttributesRef = MeshDescription->GetVertexPositions();
			TVertexInstanceAttributesRef<FVector> NormalsInstanceAttributesRef = MeshDescription->GetVertexInstanceNormals();
			TVertexInstanceAttributesRef<FVector> TangentsInstanceAttributesRef = MeshDescription->GetVertexInstanceTangents();
			TVertexInstanceAttributesRef<FVector2D> UVsInstanceAttributesRef = MeshDescription->GetVertexInstanceUVs();
			TVertexInstanceAttributesRef<FVector4> ColorsInstanceAttributesRef = MeshDescription->GetVertexInstanceColors();

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
					return nullptr;

				FVertexInstanceID NewVertexInstanceID = MeshDescription->CreateVertexInstance(VerticesIDs[VertexIndex]);

				// LOD0 ?
				if (StaticMesh->bAllowCPUAccess && MeshDescriptions.Num() == 0)
				{
					LOD0CPUVertexInstancesIDs.Add(NewVertexInstanceID.GetValue());
				}

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

				if (Primitive.Colors.Num() > 0)
				{
					if (VertexIndex >= (uint32)Primitive.Colors.Num())
					{
						ColorsInstanceAttributesRef[NewVertexInstanceID] = FVector4(0, 0, 0, 0);
					}
					else
					{
						ColorsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Colors[VertexIndex];
					}
					bHasVertexColors = true;
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
						VertexInstancesIDs.Reset();
						TriangleVerticesIDs.Reset();
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
						return nullptr;
					}
					VertexInstancesIDs.Reset();
					TriangleVerticesIDs.Reset();
				}
			}

		}


		if (StaticMeshConfig.PivotPosition != EglTFRuntimePivotPosition::Asset)
		{
			FBoxSphereBounds MeshBounds = MeshDescription->GetMeshDescription().GetBounds();
			TVertexAttributesRef<FVector> VertexPositions = MeshDescription->GetVertexPositions();

			if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Center)
			{
				PivotDelta = MeshBounds.GetSphere().Center;
			}
			else if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Top)
			{
				PivotDelta = MeshBounds.GetBox().GetCenter() + FVector(0, 0, MeshBounds.GetBox().GetExtent().Z);
			}
			else if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Bottom)
			{
				PivotDelta = MeshBounds.GetBox().GetCenter() - FVector(0, 0, MeshBounds.GetBox().GetExtent().Z);
			}

			for (const FVertexID VertexID : MeshDescription->Vertices().GetElementIDs())
			{
				VertexPositions[VertexID] -= PivotDelta;
			}

			if (MeshDescriptions.Num() == 0)
			{
				LOD0PivotDelta = PivotDelta;
			}
		}

		StaticMesh->StaticMaterials.Append(StaticMaterials);

		FStaticMeshOperations::ComputePolygonTangentsAndNormals(MeshDescription->GetMeshDescription());
		if (bCalculateNormals || bCalculateTangents)
		{
			EComputeNTBsFlags NTPBsFlags = EComputeNTBsFlags::None;
			if (bCalculateNormals)
			{
				NTPBsFlags |= EComputeNTBsFlags::Normals;
			}
			if (bCalculateTangents)
			{
				NTPBsFlags |= EComputeNTBsFlags::Tangents;
			}
			FStaticMeshOperations::ComputeTangentsAndNormals(MeshDescription->GetMeshDescription(), NTPBsFlags);
		}

		MeshDescriptions.Add(MeshDescription);
	}

	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, false);

	bool bIsMobile = GMaxRHIFeatureLevel == ERHIFeatureLevel::ES3_1;
#if WITH_EDITOR
	UEditorEngine* Editor = (UEditorEngine*)GEngine;
	bIsMobile |= Editor->GetActiveFeatureLevelPreviewType() == ERHIFeatureLevel::ES3_1;
#endif

	if ((bHasVertexColors || bIsMobile) && StaticMesh->RenderData && StaticMesh->RenderData->LODResources.Num() > 0)
	{
		StaticMesh->ReleaseResources();
		for (int32 LODIndex = 0; LODIndex < StaticMesh->RenderData->LODResources.Num(); LODIndex++)
		{
			StaticMesh->RenderData->LODResources[LODIndex].bHasColorVertexData = true;
		}
		StaticMesh->InitResources();
	}

	// Override LODs ScreenSize
	for (const TPair<int32, float>& Pair : StaticMeshConfig.LODScreenSize)
	{
		int32 CurrentLODIndex = Pair.Key;
		if (StaticMesh->RenderData && CurrentLODIndex >= 0 && CurrentLODIndex < StaticMesh->RenderData->LODResources.Num())
		{
			StaticMesh->RenderData->ScreenSize[CurrentLODIndex].Default = Pair.Value;
		}
	}

	if (!StaticMesh->BodySetup)
	{
		StaticMesh->CreateBodySetup();
	}

	StaticMesh->BodySetup->bMeshCollideAll = false;
	StaticMesh->BodySetup->CollisionTraceFlag = StaticMeshConfig.CollisionComplexity;

	StaticMesh->BodySetup->InvalidatePhysicsData();

	// required for building complex collisions
#if !WITH_EDITOR
	if (!bIsMobile && StaticMesh->bAllowCPUAccess && StaticMesh->RenderData && StaticMesh->RenderData->LODResources.Num() > 0)
	{
		FStaticMeshLODResources& LOD = StaticMesh->RenderData->LODResources[0];
		ENQUEUE_RENDER_COMMAND(FixIndexBufferOnCPUCommand)(
			[&LOD, &LOD0CPUVertexInstancesIDs](FRHICommandListImmediate& RHICmdList)
		{
			LOD.IndexBuffer.ReleaseResource();
			LOD.IndexBuffer = FRawStaticIndexBuffer(true);
			LOD.IndexBuffer.SetIndices(LOD0CPUVertexInstancesIDs, EIndexBufferStride::AutoDetect);
			LOD.IndexBuffer.InitResource();
		});

		FlushRenderingCommands();
}
#endif

	if (StaticMeshConfig.bBuildSimpleCollision)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = StaticMesh->RenderData->Bounds.Origin;
		BoxElem.X = StaticMesh->RenderData->Bounds.BoxExtent.X * 2.0f;
		BoxElem.Y = StaticMesh->RenderData->Bounds.BoxExtent.Y * 2.0f;
		BoxElem.Z = StaticMesh->RenderData->Bounds.BoxExtent.Z * 2.0f;
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FBox& Box : StaticMeshConfig.BoxCollisions)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = Box.GetCenter();
		FVector BoxSize = Box.GetSize();
		BoxElem.X = BoxSize.X;
		BoxElem.Y = BoxSize.Y;
		BoxElem.Z = BoxSize.Z;
		StaticMesh->BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FVector4 Sphere : StaticMeshConfig.SphereCollisions)
	{
		FKSphereElem SphereElem;
		SphereElem.Center = Sphere;
		SphereElem.Radius = Sphere.W;
		StaticMesh->BodySetup->AggGeom.SphereElems.Add(SphereElem);
	}

	StaticMesh->BodySetup->CreatePhysicsMeshes();

	for (const TPair<FString, FTransform>& Pair : StaticMeshConfig.Sockets)
	{
		UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(StaticMesh);
		Socket->SocketName = FName(Pair.Key);
		Socket->RelativeLocation = Pair.Value.GetLocation();
		Socket->RelativeRotation = Pair.Value.Rotator();
		Socket->RelativeScale = Pair.Value.GetScale3D();
		StaticMesh->AddSocket(Socket);
	}

	if (!StaticMeshConfig.ExportOriginalPivotToSocket.IsEmpty())
	{
		UStaticMeshSocket* Socket = NewObject<UStaticMeshSocket>(StaticMesh);
		Socket->SocketName = FName(StaticMeshConfig.ExportOriginalPivotToSocket);
		Socket->RelativeLocation = -LOD0PivotDelta;
		StaticMesh->AddSocket(Socket);
	}

	if (OnStaticMeshCreated.IsBound())
	{
		OnStaticMeshCreated.Broadcast(StaticMesh);
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

	TArray<TSharedRef<FJsonObject>> JsonMeshObjects;
	JsonMeshObjects.Add(JsonMeshObject.ToSharedRef());

	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObjects, StaticMeshConfig);
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

UStaticMesh* FglTFRuntimeParser::LoadStaticMeshLODs(const TArray<int32> MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	TArray<TSharedRef<FJsonObject>> JsonMeshObjects;

	for (const int32 MeshIndex : MeshIndices)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
		if (!JsonMeshObject)
		{
			return nullptr;
		}

		JsonMeshObjects.Add(JsonMeshObject.ToSharedRef());
	}

	return LoadStaticMesh_Internal(JsonMeshObjects, StaticMeshConfig);
}

bool FglTFRuntimeParser::LoadStaticMeshIntoProceduralMeshComponent(const int32 MeshIndex, UProceduralMeshComponent* ProceduralMeshComponent, const FglTFRuntimeProceduralMeshConfig& ProceduralMeshConfig)
{
	if (!ProceduralMeshComponent)
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		return false;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, ProceduralMeshConfig.MaterialsConfig))
	{
		return false;
	}

	ProceduralMeshComponent->bUseComplexAsSimpleCollision = ProceduralMeshConfig.bUseComplexAsSimpleCollision;

	int32 SectionIndex = ProceduralMeshComponent->GetNumSections();
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		TArray<FVector2D> UV;
		if (Primitive.UVs.Num() > 0)
		{
			UV = Primitive.UVs[0];
		}
		TArray<int32> Triangles;
		for (uint32 Index : Primitive.Indices)
		{
			Triangles.Add(Index);
		}
		TArray<FLinearColor> Colors;
		for (FVector4 Color : Primitive.Colors)
		{
			Colors.Add(FLinearColor(Color));
		}
		TArray<FProcMeshTangent> Tangents;
		for (FVector Tangent : Primitive.Tangents)
		{
			Tangents.Add(FProcMeshTangent(Tangent, false));
		}
		ProceduralMeshComponent->CreateMeshSection_LinearColor(SectionIndex, Primitive.Positions, Triangles, Primitive.Normals, UV, Colors, Tangents, ProceduralMeshConfig.bBuildSimpleCollision);
		ProceduralMeshComponent->SetMaterial(SectionIndex, Primitive.Material);
		SectionIndex++;
	}

	return true;
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
