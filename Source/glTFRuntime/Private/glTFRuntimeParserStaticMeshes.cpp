// Copyright 2020-2021, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "StaticMeshOperations.h"
#include "Engine/StaticMeshSocket.h"
#if WITH_EDITOR
#include "Editor/EditorEngine.h"
#endif
#include "PhysicsEngine/BodySetup.h"

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TArray<TSharedRef<FJsonObject>> JsonMeshObjects, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig, const TMap<TSharedRef<FJsonObject>, TArray<FglTFRuntimePrimitive>>& PrimitivesCache)
{

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(StaticMeshConfig.Outer ? StaticMeshConfig.Outer : GetTransientPackage(), NAME_None, RF_Public);
#if PLATFORM_ANDROID || PLATFORM_IOS
	StaticMesh->bAllowCPUAccess = false;
#else
	StaticMesh->bAllowCPUAccess = StaticMeshConfig.bAllowCPUAccess;
#endif

#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MINOR_VERSION > 26)
	StaticMesh->SetIsBuiltAtRuntime(true);
#endif
	StaticMesh->NeverStream = true;

	bool bHasVertexColors = false;
	FVector LOD0PivotDelta = FVector::ZeroVector;

	FStaticMeshRenderData* RenderData = nullptr;

#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MINOR_VERSION > 26)
	if (StaticMesh->GetRenderData())
	{
		StaticMesh->GetRenderData()->ReleaseResources();
	}
	StaticMesh->SetRenderData(MakeUnique<FStaticMeshRenderData>());
	RenderData = StaticMesh->GetRenderData();
#else
	if (StaticMesh->RenderData.IsValid())
	{
		StaticMesh->RenderData->ReleaseResources();
	}
	StaticMesh->RenderData = MakeUnique<FStaticMeshRenderData>();
	RenderData = StaticMesh->RenderData.Get();
#endif

	
	RenderData->AllocateLODResources(JsonMeshObjects.Num());

	int32 LODIndex = 0;

	FBoxSphereBounds BoundingBoxAndSphere;

	for (TSharedRef<FJsonObject> JsonMeshObject : JsonMeshObjects)
	{
		const int32 CurrentLODIndex = LODIndex++;
		FStaticMeshLODResources& LODResources = RenderData->LODResources[CurrentLODIndex];

#if ENGINE_MAJOR_VERSION > 4
		FStaticMeshSectionArray& Sections = LODResources.Sections;
#else
		FStaticMeshLODResources::FStaticMeshSectionArray& Sections = LODResources.Sections;
#endif

		TArray<FglTFRuntimePrimitive> Primitives;
		TArray<uint32> LODIndices;


		if (PrimitivesCache.Contains(JsonMeshObject))
		{
			Primitives = PrimitivesCache[JsonMeshObject];
		}
		else
		{
			if (!LoadPrimitives(JsonMeshObject, Primitives, StaticMeshConfig.MaterialsConfig))
			{
				return nullptr;
			}
		}

		TArray<FStaticMaterial> StaticMaterials;

		int32 NumUVs = 1;
		FVector PivotDelta = FVector::ZeroVector;

		int32 NumVertexInstancesPerLOD = 0;

		for (FglTFRuntimePrimitive& Primitive : Primitives)
		{
			if (Primitive.UVs.Num() > NumUVs)
			{
				NumUVs = Primitive.UVs.Num();
			}

			if (Primitive.Colors.Num() > 0)
			{
				bHasVertexColors = true;
			}

			NumVertexInstancesPerLOD += Primitive.Indices.Num();
		}

		TArray<FStaticMeshBuildVertex> StaticMeshBuildVertices;
		StaticMeshBuildVertices.SetNum(NumVertexInstancesPerLOD);

		FBox BoundingBox;
		BoundingBox.Init();

		int32 VertexInstanceBaseIndex = 0;

		for (FglTFRuntimePrimitive& Primitive : Primitives)
		{
			FName MaterialName = FName(FString::Printf(TEXT("LOD_%d_Section_%d_%s"), CurrentLODIndex, StaticMaterials.Num(), *Primitive.MaterialName));
			FStaticMaterial StaticMaterial(Primitive.Material, MaterialName);
			StaticMaterial.UVChannelData.bInitialized = true;
			int32 MaterialIndex = StaticMaterials.Add(StaticMaterial);

			FStaticMeshSection& Section = Sections.AddDefaulted_GetRef();
			int32 NumVertexInstancesPerSection = Primitive.Indices.Num();

			Section.NumTriangles = NumVertexInstancesPerSection / 3;
			Section.FirstIndex = VertexInstanceBaseIndex;
			Section.bEnableCollision = true;
			Section.bCastShadow = true;
			Section.MaterialIndex = MaterialIndex;

			bool bMissingNormals = false;
			bool bMissingTangents = false;
			bool bMissingIgnore = false;

			for (int32 VertexInstanceSectionIndex = 0; VertexInstanceSectionIndex < NumVertexInstancesPerSection; VertexInstanceSectionIndex++)
			{
				uint32 VertexIndex = Primitive.Indices[VertexInstanceSectionIndex];
				LODIndices.Add(VertexInstanceBaseIndex + VertexInstanceSectionIndex);

				FStaticMeshBuildVertex& StaticMeshVertex = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex];

				StaticMeshVertex.Position = GetSafeValue(Primitive.Positions, VertexIndex, FVector::ZeroVector, bMissingIgnore);
				BoundingBox += StaticMeshVertex.Position;
				StaticMeshVertex.TangentX = GetSafeValue(Primitive.Tangents, VertexIndex, FVector4(0, 0, 0, 0), bMissingTangents);
				StaticMeshVertex.TangentZ = GetSafeValue(Primitive.Normals, VertexIndex, FVector::ZeroVector, bMissingNormals);
				StaticMeshVertex.TangentY = ComputeTangentY(StaticMeshVertex.TangentZ, StaticMeshVertex.TangentX);

				for (int32 UVIndex = 0; UVIndex < NumUVs; UVIndex++)
				{
					if (UVIndex < Primitive.UVs.Num())
					{
						StaticMeshVertex.UVs[UVIndex] = GetSafeValue(Primitive.UVs[UVIndex], VertexIndex, FVector2D::ZeroVector, bMissingIgnore);
					}
				}

				if (bHasVertexColors)
				{
					if (VertexIndex < static_cast<uint32>(Primitive.Colors.Num()))
					{
						StaticMeshVertex.Color = FLinearColor(Primitive.Colors[VertexIndex]).ToFColor(true);
					}
					else
					{
						StaticMeshVertex.Color = FColor::White;
					}
				}
			}

			const bool bCanGenerateNormals = (bMissingNormals && StaticMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::IfMissing) ||
				StaticMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Always;
			if (bCanGenerateNormals && (NumVertexInstancesPerSection % 3) == 0)
			{
				for (int32 VertexInstanceSectionIndex = 0; VertexInstanceSectionIndex < NumVertexInstancesPerSection; VertexInstanceSectionIndex += 3)
				{
					FStaticMeshBuildVertex& StaticMeshVertex0 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex];
					FStaticMeshBuildVertex& StaticMeshVertex1 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex + 1];
					FStaticMeshBuildVertex& StaticMeshVertex2 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex + 2];

					FVector SideA = StaticMeshVertex1.Position - StaticMeshVertex0.Position;
					FVector SideB = StaticMeshVertex2.Position - StaticMeshVertex0.Position;

					FVector NormalFromCross = FVector::CrossProduct(SideB, SideA).GetSafeNormal();

					StaticMeshVertex0.TangentZ = NormalFromCross;
					StaticMeshVertex1.TangentZ = NormalFromCross;
					StaticMeshVertex2.TangentZ = NormalFromCross;
				}
				bMissingNormals = false;
			}

			// recompute tangents if required (need normals and uvs)
			if (bMissingTangents && !bMissingNormals && Primitive.UVs.Num() > 0 && (NumVertexInstancesPerSection % 3) == 0)
			{
				for (int32 VertexInstanceSectionIndex = 0; VertexInstanceSectionIndex < NumVertexInstancesPerSection; VertexInstanceSectionIndex += 3)
				{
					FStaticMeshBuildVertex& StaticMeshVertex0 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex];
					FStaticMeshBuildVertex& StaticMeshVertex1 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex + 1];
					FStaticMeshBuildVertex& StaticMeshVertex2 = StaticMeshBuildVertices[VertexInstanceBaseIndex + VertexInstanceSectionIndex + 2];

					FVector Position0 = StaticMeshVertex0.Position;
					FVector4 TangentZ0 = StaticMeshVertex0.TangentZ;
					FVector2D UV0 = StaticMeshVertex0.UVs[0];

					FVector Position1 = StaticMeshVertex1.Position;
					FVector4 TangentZ1 = StaticMeshVertex1.TangentZ;
					FVector2D UV1 = StaticMeshVertex1.UVs[0];

					FVector Position2 = StaticMeshVertex2.Position;
					FVector4 TangentZ2 = StaticMeshVertex2.TangentZ;
					FVector2D UV2 = StaticMeshVertex2.UVs[0];

					FVector DeltaPosition0 = Position1 - Position0;
					FVector DeltaPosition1 = Position2 - Position0;

					FVector2D DeltaUV0 = UV1 - UV0;
					FVector2D DeltaUV1 = UV2 - UV0;

					float Factor = 1.0f / (DeltaUV0.X * DeltaUV1.Y - DeltaUV0.Y * DeltaUV1.X);

					FVector TriangleTangentX = ((DeltaPosition0 * DeltaUV1.Y) - (DeltaPosition1 * DeltaUV0.Y)) * Factor;
					FVector TriangleTangentY = ((DeltaPosition0 * DeltaUV1.X) - (DeltaPosition1 * DeltaUV0.X)) * Factor;

					FVector TangentX0 = TriangleTangentX - (TangentZ0 * FVector::DotProduct(TangentZ0, TriangleTangentX));
					FVector CrossX0 = FVector::CrossProduct(TangentZ0, TangentX0);
					TangentX0 *= (FVector::DotProduct(CrossX0, TriangleTangentY) < 0) ? -1.0f : 1.0f;
					TangentX0.Normalize();

					FVector TangentX1 = TriangleTangentX - (TangentZ1 * FVector::DotProduct(TangentZ1, TriangleTangentX));
					FVector CrossX1 = FVector::CrossProduct(TangentZ1, TangentX1);
					TangentX1 *= (FVector::DotProduct(CrossX1, TriangleTangentY) < 0) ? -1.0f : 1.0f;
					TangentX1.Normalize();

					FVector TangentX2 = TriangleTangentX - (TangentZ2 * FVector::DotProduct(TangentZ2, TriangleTangentX));
					FVector CrossX2 = FVector::CrossProduct(TangentZ2, TangentX2);
					TangentX2 *= (FVector::DotProduct(CrossX2, TriangleTangentY) < 0) ? -1.0f : 1.0f;
					TangentX2.Normalize();

					StaticMeshVertex0.TangentX = TangentX0;
					StaticMeshVertex0.TangentY = ComputeTangentY(StaticMeshVertex0.TangentZ, StaticMeshVertex0.TangentX);

					StaticMeshVertex1.TangentX = TangentX1;
					StaticMeshVertex1.TangentY = ComputeTangentY(StaticMeshVertex1.TangentZ, StaticMeshVertex1.TangentX);

					StaticMeshVertex2.TangentX = TangentX2;
					StaticMeshVertex2.TangentY = ComputeTangentY(StaticMeshVertex2.TangentZ, StaticMeshVertex2.TangentX);

				}
			}

			VertexInstanceBaseIndex += NumVertexInstancesPerSection;
		}

		// check for pivot repositioning
		if (StaticMeshConfig.PivotPosition != EglTFRuntimePivotPosition::Asset)
		{
			if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Center)
			{
				PivotDelta = BoundingBox.GetCenter();
			}
			else if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Top)
			{
				PivotDelta = BoundingBox.GetCenter() + FVector(0, 0, BoundingBox.GetExtent().Z);
			}
			else if (StaticMeshConfig.PivotPosition == EglTFRuntimePivotPosition::Bottom)
			{
				PivotDelta = BoundingBox.GetCenter() - FVector(0, 0, BoundingBox.GetExtent().Z);
			}

			for (FStaticMeshBuildVertex& StaticMeshVertex : StaticMeshBuildVertices)
			{
				StaticMeshVertex.Position -= PivotDelta;
			}

			if (CurrentLODIndex == 0)
			{
				LOD0PivotDelta = PivotDelta;
			}
		}

		if (CurrentLODIndex == 0)
		{
			BoundingBox.GetCenterAndExtents(BoundingBoxAndSphere.Origin, BoundingBoxAndSphere.BoxExtent);
			BoundingBoxAndSphere.SphereRadius = 0.0f;
			for (const FStaticMeshBuildVertex& StaticMeshVertex : StaticMeshBuildVertices)
			{
				BoundingBoxAndSphere.SphereRadius = FMath::Max((StaticMeshVertex.Position - BoundingBoxAndSphere.Origin).Size(), BoundingBoxAndSphere.SphereRadius);
			}
		}

		LODResources.VertexBuffers.PositionVertexBuffer.Init(StaticMeshBuildVertices, StaticMesh->bAllowCPUAccess);
		LODResources.VertexBuffers.StaticMeshVertexBuffer.Init(StaticMeshBuildVertices, NumUVs, StaticMesh->bAllowCPUAccess);
		if (bHasVertexColors)
		{
			LODResources.VertexBuffers.ColorVertexBuffer.Init(StaticMeshBuildVertices, StaticMesh->bAllowCPUAccess);
		}
		LODResources.bHasColorVertexData = bHasVertexColors;
		LODResources.IndexBuffer.SetIndices(LODIndices, EIndexBufferStride::Force32Bit);


#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MINOR_VERSION > 26)
		TArray<FStaticMaterial> CurrentStaticMaterials = StaticMesh->GetStaticMaterials();
		CurrentStaticMaterials.Append(StaticMaterials);
		StaticMesh->SetStaticMaterials(CurrentStaticMaterials);
#else
		StaticMesh->StaticMaterials.Append(StaticMaterials);
#endif
	}

	
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MINOR_VERSION > 26)
	UBodySetup* BodySetup = StaticMesh->GetBodySetup();
#else
	UBodySetup* BodySetup = StaticMesh->BodySetup;
#endif

	// Override LODs ScreenSize
	for (const TPair<int32, float>& Pair : StaticMeshConfig.LODScreenSize)
	{
		int32 CurrentLODIndex = Pair.Key;
		if (RenderData && CurrentLODIndex >= 0 && CurrentLODIndex < RenderData->LODResources.Num())
		{
			RenderData->ScreenSize[CurrentLODIndex].Default = Pair.Value;
		}
	}

	StaticMesh->InitResources();

	RenderData->Bounds = BoundingBoxAndSphere;
	StaticMesh->CalculateExtendedBounds();

	if (!BodySetup)
	{
		StaticMesh->CreateBodySetup();
#if ENGINE_MAJOR_VERSION > 4 || (ENGINE_MINOR_VERSION > 26)
		BodySetup = StaticMesh->GetBodySetup();
#else
		BodySetup = StaticMesh->BodySetup;
#endif
	}

	if (!StaticMesh->bAllowCPUAccess)
	{
		BodySetup->bNeverNeedsCookedCollisionData = true;
	}

	BodySetup->bMeshCollideAll = false;
	BodySetup->CollisionTraceFlag = StaticMeshConfig.CollisionComplexity;

	BodySetup->InvalidatePhysicsData();

	if (StaticMeshConfig.bBuildSimpleCollision)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = RenderData->Bounds.Origin;
		BoxElem.X = RenderData->Bounds.BoxExtent.X * 2.0f;
		BoxElem.Y = RenderData->Bounds.BoxExtent.Y * 2.0f;
		BoxElem.Z = RenderData->Bounds.BoxExtent.Z * 2.0f;
		BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FBox& Box : StaticMeshConfig.BoxCollisions)
	{
		FKBoxElem BoxElem;
		BoxElem.Center = Box.GetCenter();
		FVector BoxSize = Box.GetSize();
		BoxElem.X = BoxSize.X;
		BoxElem.Y = BoxSize.Y;
		BoxElem.Z = BoxSize.Z;
		BodySetup->AggGeom.BoxElems.Add(BoxElem);
	}

	for (const FVector4 Sphere : StaticMeshConfig.SphereCollisions)
	{
		FKSphereElem SphereElem;
		SphereElem.Center = Sphere;
		SphereElem.Radius = Sphere.W;
		BodySetup->AggGeom.SphereElems.Add(SphereElem);
	}

	BodySetup->CreatePhysicsMeshes();

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

	TMap<TSharedRef<FJsonObject>, TArray<FglTFRuntimePrimitive>> PrimitivesCache;
	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObjects, StaticMeshConfig, PrimitivesCache);
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

TArray<UStaticMesh*> FglTFRuntimeParser::LoadStaticMeshesFromPrimitives(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	TArray<UStaticMesh*> StaticMeshes;

	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		return StaticMeshes;
	}

	TArray<TSharedRef<FJsonObject>> JsonMeshObjects;
	JsonMeshObjects.Add(JsonMeshObject.ToSharedRef());

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, StaticMeshConfig.MaterialsConfig))
	{
		return StaticMeshes;
	}

	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		TMap<TSharedRef<FJsonObject>, TArray<FglTFRuntimePrimitive>> PrimitivesCache;
		TArray<FglTFRuntimePrimitive> SinglePrimitive;
		SinglePrimitive.Add(Primitive);
		PrimitivesCache.Add(JsonMeshObject.ToSharedRef(), SinglePrimitive);

		UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObjects, StaticMeshConfig, PrimitivesCache);
		if (!StaticMesh)
		{
			break;
		}

		StaticMeshes.Add(StaticMesh);
	}

	return StaticMeshes;
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

	TMap<TSharedRef<FJsonObject>, TArray<FglTFRuntimePrimitive>> PrimitivesCache;
	return LoadStaticMesh_Internal(JsonMeshObjects, StaticMeshConfig, PrimitivesCache);
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
