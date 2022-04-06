// Copyright 2020, Roberto De Ioris.

#include "SkeletalMeshExporterGLTF.h"
#include "Rendering/SkeletalMeshRenderData.h"

USkeletalMeshExporterGLTF::USkeletalMeshExporterGLTF(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USkeletalMesh::StaticClass();
	FormatExtension.Add(TEXT("gltf"));
	PreferredFormatIndex = 0;
	FormatDescription.Add(TEXT("glTF Embedded file"));
	bText = true;
}

void FglTFExportContextSkeletalMesh::GenerateSkeletalMesh(USkeletalMesh* SkeletalMesh)
{
#if ENGINE_MAJOR_VERSION > 4
	if (!SkeletalMesh->GetSkeleton())
	{
		return;
	}

	GenerateSkeleton(SkeletalMesh->GetSkeleton());
#else
	if (!SkeletalMesh->Skeleton)
	{
		return;
	}

	GenerateSkeleton(SkeletalMesh->Skeleton);
#endif

	TSharedRef<FJsonObject> JsonScene = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> JsonSceneNodes;
	TArray<TSharedPtr<FJsonValue>> JsonMeshes;

	FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData)
	{
		SkeletalMesh->InitResources();
		RenderData = SkeletalMesh->GetResourceForRendering();
	}

	int32 NumLods = RenderData->LODRenderData.Num();

	FMatrix SceneBasisMatrix = FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector).Inverse();
	float SceneScale = 1.f / 100.f;

	FBoxSphereBounds Bounds = SkeletalMesh->GetBounds();

	for (int32 LodIndex = 0; LodIndex < NumLods; LodIndex++)
	{
		TSharedRef<FJsonObject> JsonMesh = MakeShared<FJsonObject>();
		JsonMesh->SetStringField("name", FString::Printf(TEXT("Mesh_LOD_%d"), LodIndex));

		TArray<FSkelMeshRenderSection>& RenderSections = RenderData->LODRenderData[LodIndex].RenderSections;
		int32 NumSections = RenderSections.Num();
		TArray<TSharedPtr<FJsonValue>> JsonPrimitives;

		uint32 NumPositions = RenderData->LODRenderData[LodIndex].StaticVertexBuffers.PositionVertexBuffer.GetNumVertices();
		TArray<FVector> SectionPositions;

		for (uint32 PositionIndex = 0; PositionIndex < NumPositions; PositionIndex++)
		{
#if ENGINE_MAJOR_VERSION > 4
			FVector Position = FVector(RenderData->LODRenderData[LodIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex));
#else
			FVector Position = RenderData->LODRenderData[LodIndex].StaticVertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex);
#endif
			SectionPositions.Add(SceneBasisMatrix.TransformPosition(Position) * SceneScale);
		}

		int32 PositionsAccessor = AppendAccessor(5126, NumPositions, "VEC3",
			(uint8*)SectionPositions.GetData(), NumPositions * sizeof(float) * 3,
			true, Bounds.GetBox().Min, Bounds.GetBox().Max);

		for (int32 SectionIndex = 0; SectionIndex < NumSections; SectionIndex++)
		{
			TSharedRef<FJsonObject> JsonPrimitive = MakeShared<FJsonObject>();

			FSkelMeshRenderSection& RenderSection = RenderSections[SectionIndex];
			int32 IndexAccessor = -1;

			// fix winding
			if (RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetDataTypeSize() == 2)
			{
				TArray<uint16> SectionIndices;
				for (uint32 IBIndex = 0; IBIndex < RenderSection.NumTriangles * 3; IBIndex += 3)
				{
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex));
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex + 2));
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex + 1));
				}
				IndexAccessor = AppendAccessor(5123, RenderSection.NumTriangles * 3, "SCALAR", (uint8*)SectionIndices.GetData(), (RenderSection.NumTriangles * 3) * sizeof(uint16));
			}
			else
			{
				TArray<uint32> SectionIndices;
				for (uint32 IBIndex = 0; IBIndex < RenderSection.NumTriangles * 3; IBIndex += 3)
				{
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex));
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex + 2));
					SectionIndices.Add(RenderData->LODRenderData[LodIndex].MultiSizeIndexContainer.GetIndexBuffer()->Get(RenderSection.BaseVertexIndex + IBIndex + 1));
				}
				IndexAccessor = AppendAccessor(5125, RenderSection.NumTriangles * 3, "SCALAR", (uint8*)SectionIndices.GetData(), (RenderSection.NumTriangles * 3) * sizeof(uint32));
			}

			JsonPrimitive->SetNumberField("indices", IndexAccessor);

			TSharedRef<FJsonObject> JsonPrimitiveAttributes = MakeShared<FJsonObject>();
			JsonPrimitiveAttributes->SetNumberField("POSITION", PositionsAccessor);

			JsonPrimitive->SetObjectField("attributes", JsonPrimitiveAttributes);

			JsonPrimitives.Add(MakeShared<FJsonValueObject>(JsonPrimitive));
		}

		JsonMesh->SetArrayField("primitives", JsonPrimitives);
		int32 MeshIndex = JsonMeshes.Add(MakeShared<FJsonValueObject>(JsonMesh));

		TSharedRef<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		JsonNode->SetStringField("name", FString::Printf(TEXT("LOD_%d"), LodIndex));
		JsonNode->SetNumberField("mesh", MeshIndex);

		int32 JsonNodeIndex = JsonNodes.Add(MakeShared<FJsonValueObject>(JsonNode));

		JsonSceneNodes.Add(MakeShared<FJsonValueNumber>(JsonNodeIndex));
	}

	JsonScene->SetArrayField("nodes", JsonSceneNodes);

	JsonScenes.Add(MakeShared<FJsonValueObject>(JsonScene));

	JsonRoot->SetArrayField("meshes", JsonMeshes);
}

bool USkeletalMeshExporterGLTF::ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags)
{
	USkeletalMesh* SkeletalMesh = CastChecked<USkeletalMesh>(Object);
	TSharedRef<FglTFExportContextSkeletalMesh> ExporterContext = MakeShared<FglTFExportContextSkeletalMesh>();

	ExporterContext->GenerateSkeletalMesh(SkeletalMesh);

	Ar.Log(ExporterContext->GenerateJson());

	return true;
}
