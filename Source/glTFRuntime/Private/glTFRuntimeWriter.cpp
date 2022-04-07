// Copyright 2020-2022, Roberto De Ioris.


#include "glTFRuntimeWriter.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/ArrayWriter.h"
#include "Serialization/JsonSerializer.h"

FglTFRuntimeWriter::FglTFRuntimeWriter()
{
	JsonRoot = MakeShared<FJsonObject>();
}

FglTFRuntimeWriter::~FglTFRuntimeWriter()
{
}

bool FglTFRuntimeWriter::AddMesh(USkeletalMesh* SkeletalMesh, const int32 LOD)
{
	if (LOD < 0)
	{
		return false;
	}

	uint32 MeshIndex = JsonMeshes.Num();

	FSkeletalMeshRenderData* RenderData = SkeletalMesh->GetResourceForRendering();
	if (!RenderData)
	{
		SkeletalMesh->InitResources();
		RenderData = SkeletalMesh->GetResourceForRendering();
	}

	int32 NumLODs = RenderData->LODRenderData.Num();

	if (LOD >= NumLODs)
	{
		return false;
	}

	FMatrix SceneBasisMatrix = FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector).Inverse();
	float SceneScale = 1.f / 100.f;

	FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[LOD];

	TSharedRef<FJsonObject> JsonMesh = MakeShared<FJsonObject>();
	JsonMesh->SetStringField("name", SkeletalMesh->GetPathName());

	int64 IndicesOffset = BinaryData.Num();
	TArray<uint32> Indices;
	LODRenderData.MultiSizeIndexContainer.GetIndexBuffer(Indices);
	BinaryData.Append(reinterpret_cast<uint8*>(Indices.GetData()), Indices.Num() * sizeof(uint32));

	TArray<FVector> Positions;
	for (uint32 PositionIndex = 0; PositionIndex < LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); PositionIndex++)
	{
		const FVector& Position = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex);
		Positions.Add(SceneBasisMatrix.TransformPosition(Position) * SceneScale);
	}

	int64 PositionOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Positions.GetData()), Positions.Num() * sizeof(FVector));

	FglTFRuntimeAccessor PositionAccessor("VEC3", 5126, Positions.Num(), PositionOffset, Positions.Num() * sizeof(FVector), false);
	int32 PositionAccessorIndex = Accessors.Add(PositionAccessor);

	TArray<TSharedPtr<FJsonValue>> JsonPrimitives;

	for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
	{
		FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];

		TSharedRef<FJsonObject> JsonPrimitive = MakeShared<FJsonObject>();

		FglTFRuntimeAccessor IndicesAccessor("SCALAR", 5125, Section.NumTriangles * 3, Section.BaseIndex * sizeof(uint32), (Section.NumTriangles * 3) * sizeof(uint32), false);
		int32 IndicesAccessorIndex = Accessors.Add(IndicesAccessor);

		JsonPrimitive->SetNumberField("indices", IndicesAccessorIndex);

		TSharedRef<FJsonObject> JsonPrimitiveAttributes = MakeShared<FJsonObject>();

		JsonPrimitiveAttributes->SetNumberField("POSITION", PositionAccessorIndex);

		JsonPrimitive->SetObjectField("attributes", JsonPrimitiveAttributes);

		JsonPrimitives.Add(MakeShared<FJsonValueObject>(JsonPrimitive));
	}

	JsonMesh->SetArrayField("primitives", JsonPrimitives);

	JsonMeshes.Add(MakeShared<FJsonValueObject>(JsonMesh));

	return true;
}

bool FglTFRuntimeWriter::WriteToFile(const FString& Filename)
{
	TArray<TSharedPtr<FJsonValue>> JsonScenes;
	TArray<TSharedPtr<FJsonValue>> JsonNodes;
	TArray<TSharedPtr<FJsonValue>> JsonAccessors;
	TArray<TSharedPtr<FJsonValue>> JsonBufferViews;
	TArray<TSharedPtr<FJsonValue>> JsonBuffers;

	TSharedRef<FJsonObject> JsonAsset = MakeShared<FJsonObject>();

	JsonAsset->SetStringField("generator", "Unreal Engine glTFRuntime Plugin");
	JsonAsset->SetStringField("version", "2.0");

	JsonRoot->SetObjectField("asset", JsonAsset);

	TSharedRef<FJsonObject> JsonBuffer = MakeShared<FJsonObject>();
	JsonBuffer->SetNumberField("byteLength", BinaryData.Num());

	JsonBuffers.Add(MakeShared<FJsonValueObject>(JsonBuffer));

	for (const FglTFRuntimeAccessor& Accessor : Accessors)
	{
		TSharedRef<FJsonObject> JsonBufferView = MakeShared<FJsonObject>();
		JsonBufferView->SetNumberField("buffer", 0);
		JsonBufferView->SetNumberField("byteLength", Accessor.ByteLength);
		JsonBufferView->SetNumberField("byteOffset", Accessor.ByteOffset);

		int32 BufferViewIndex = JsonBufferViews.Add(MakeShared<FJsonValueObject>(JsonBufferView));

		TSharedRef<FJsonObject> JsonAccessor = MakeShared<FJsonObject>();
		JsonAccessor->SetNumberField("bufferView", BufferViewIndex);
		JsonAccessor->SetNumberField("componentType", Accessor.ComponentType);
		JsonAccessor->SetNumberField("count", Accessor.Count);
		JsonAccessor->SetStringField("type", Accessor.Type);
		JsonAccessor->SetBoolField("normalized", Accessor.bNormalized);

		JsonAccessors.Add(MakeShared<FJsonValueObject>(JsonAccessor));
	}

	TSharedRef<FJsonObject> JsonNode = MakeShared<FJsonObject>();
	JsonNode->SetStringField("name", "Test");
	JsonNode->SetNumberField("mesh", 0);

	int32 JsonNodeIndex = JsonNodes.Add(MakeShared<FJsonValueObject>(JsonNode));

	TSharedRef<FJsonObject> JsonScene = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> JsonSceneNodes;
	JsonSceneNodes.Add(MakeShared<FJsonValueNumber>(JsonNodeIndex));
	JsonScene->SetArrayField("nodes", JsonSceneNodes);

	JsonScenes.Add(MakeShared<FJsonValueObject>(JsonScene));

	JsonRoot->SetArrayField("scenes", JsonScenes);
	JsonRoot->SetArrayField("nodes", JsonNodes);
	JsonRoot->SetArrayField("accessors", JsonAccessors);
	JsonRoot->SetArrayField("bufferViews", JsonBufferViews);
	JsonRoot->SetArrayField("buffers", JsonBuffers);
	JsonRoot->SetArrayField("meshes", JsonMeshes);

	FArrayWriter Json;
	TSharedRef<TJsonWriter<UTF8CHAR>> JsonWriter = TJsonWriterFactory<UTF8CHAR>::Create(&Json);
	FJsonSerializer::Serialize(MakeShared<FJsonValueObject>(JsonRoot), "", JsonWriter);

	// Align sizes to 4 bytes
	if (Json.Num() % 4)
	{
		int32 Padding = (4 - (Json.Num() % 4));
		for (int32 Pad = 0; Pad < Padding; Pad++)
		{
			Json.Add(0x20);
		}
	}
	if (BinaryData.Num() % 4)
	{
		BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
	}

	FArrayWriter Writer;
	uint32 Magic = 0x46546C67;
	uint32 Version = 2;
	uint32 Length = 12 + 8 + Json.Num() + 8 + BinaryData.Num();

	Writer << Magic;
	Writer << Version;
	Writer << Length;

	uint32 JsonChunkLength = Json.Num();
	uint32 JsonChunkType = 0x4E4F534A;

	Writer << JsonChunkLength;
	Writer << JsonChunkType;

	Writer.Serialize(Json.GetData(), Json.Num());

	uint32 BinaryChunkLength = BinaryData.Num();
	uint32 BinaryChunkType = 0x004E4942;

	Writer << BinaryChunkLength;
	Writer << BinaryChunkType;

	Writer.Serialize(BinaryData.GetData(), BinaryData.Num());

	return FFileHelper::SaveArrayToFile(Writer, *Filename);
}