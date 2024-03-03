// Copyright 2023, Roberto De Ioris.

#include "glTFRuntimeSkeletalMeshComponent.h"
#include "Engine/SkeletalMesh.h"
#include "Rendering/SkeletalMeshRenderData.h"

bool UglTFRuntimeSkeletalMeshComponent::ContainsPhysicsTriMeshData(bool InUseAllTriData) const
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
	USkeletalMesh* CurrentSkeletalMeshAsset = GetSkeletalMeshAsset();
#else
	USkeletalMesh* CurrentSkeletalMeshAsset = SkeletalMesh;
#endif
	if (!CurrentSkeletalMeshAsset)
	{
		return false;
	}

#if ENGINE_MAJOR_VERSION >= 5 || (ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION > 26)
	return bEnablePerPolyCollision && CurrentSkeletalMeshAsset->GetEnablePerPolyCollision();
#else
	return bEnablePerPolyCollision && CurrentSkeletalMeshAsset->bEnablePerPolyCollision;
#endif
}

bool UglTFRuntimeSkeletalMeshComponent::GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData)
{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
	USkeletalMesh* CurrentSkeletalMeshAsset = GetSkeletalMeshAsset();
#else
	USkeletalMesh* CurrentSkeletalMeshAsset = SkeletalMesh;
#endif
	if (!CurrentSkeletalMeshAsset)
	{
		return false;
	}

	FSkeletalMeshRenderData* SkeletalMeshRenderData = CurrentSkeletalMeshAsset->GetResourceForRendering();
	if (!SkeletalMeshRenderData)
	{
		return false;
	}

	if (SkeletalMeshRenderData->LODRenderData.Num() < 1)
	{
		return false;
	}

	FRawStaticIndexBuffer16or32Interface* IndexBuffer = SkeletalMeshRenderData->LODRenderData[0].MultiSizeIndexContainer.GetIndexBuffer();
	if (!IndexBuffer)
	{
		return false;
	}

	if (IndexBuffer->Num() % 3 != 0)
	{
		return false;
	}

	CollisionData->Vertices.AddUninitialized(SkeletalMeshRenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices());
	for (uint32 Index = 0; Index < SkeletalMeshRenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); Index++)
	{
		CollisionData->Vertices[Index] = SkeletalMeshRenderData->LODRenderData[0].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(Index);
	}

	int32 NumTriangles = IndexBuffer->Num() / 3;

	CollisionData->Indices.AddUninitialized(NumTriangles);
	CollisionData->MaterialIndices.AddUninitialized(NumTriangles);

	for (const FSkelMeshRenderSection& Section : SkeletalMeshRenderData->LODRenderData[0].RenderSections)
	{
		for (uint32 Index = 0; Index < Section.NumTriangles; Index++)
		{
			const int32 TriangleIndex = Section.BaseIndex + Index;
			const uint32 VertexIndex = Index * 3;
			CollisionData->Indices[TriangleIndex].v0 = IndexBuffer->Get(VertexIndex);
			CollisionData->Indices[TriangleIndex].v1 = IndexBuffer->Get(VertexIndex + 1);
			CollisionData->Indices[TriangleIndex].v2 = IndexBuffer->Get(VertexIndex + 2);
			CollisionData->MaterialIndices[TriangleIndex] = Section.MaterialIndex;
		}
	}

	CollisionData->bFlipNormals = true;
	CollisionData->bDeformableMesh = true;

	return true;
}

