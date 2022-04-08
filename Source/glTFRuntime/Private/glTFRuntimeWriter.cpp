// Copyright 2020-2022, Roberto De Ioris 
// Copyright 2022, Avatus LLC


#include "glTFRuntimeWriter.h"
#include "Animation/MorphTarget.h"
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

	FVector PositionMin;
	FVector PositionMax;
	for (uint32 PositionIndex = 0; PositionIndex < LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); PositionIndex++)
	{
		FVector Position = LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex);
		Position = SceneBasisMatrix.TransformPosition(Position) * SceneScale;

		if (PositionIndex == 0)
		{
			PositionMin = Position;
			PositionMax = Position;
		}
		else
		{
			if (Position.X < PositionMin.X)
			{
				PositionMin.X = Position.X;
			}
			if (Position.Y < PositionMin.Y)
			{
				PositionMin.Y = Position.Y;
			}
			if (Position.Z < PositionMin.Z)
			{
				PositionMin.Z = Position.Z;
			}
			if (Position.X > PositionMax.X)
			{
				PositionMax.X = Position.X;
			}
			if (Position.Y > PositionMax.Y)
			{
				PositionMax.Y = Position.Y;
			}
			if (Position.Z > PositionMax.Z)
			{
				PositionMax.Z = Position.Z;
			}
		}
		Positions.Add(Position);
	}

	int64 PositionOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Positions.GetData()), Positions.Num() * sizeof(FVector));

	FglTFRuntimeAccessor PositionAccessor("VEC3", 5126, Positions.Num(), PositionOffset, Positions.Num() * sizeof(FVector), false);
	PositionAccessor.Min.Add(MakeShared<FJsonValueNumber>(PositionMin.X));
	PositionAccessor.Min.Add(MakeShared<FJsonValueNumber>(PositionMin.Y));
	PositionAccessor.Min.Add(MakeShared<FJsonValueNumber>(PositionMin.Z));
	PositionAccessor.Max.Add(MakeShared<FJsonValueNumber>(PositionMax.X));
	PositionAccessor.Max.Add(MakeShared<FJsonValueNumber>(PositionMax.Y));
	PositionAccessor.Max.Add(MakeShared<FJsonValueNumber>(PositionMax.Z));

	int32 PositionAccessorIndex = Accessors.Add(PositionAccessor);

	TArray<FVector> Normals;
	for (uint32 VertexIndex = 0; VertexIndex < LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		FVector Normal = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
		Normal = SceneBasisMatrix.TransformVector(Normal);
		Normals.Add(Normal.GetSafeNormal());
	}
	int64 NormalOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Normals.GetData()), Normals.Num() * sizeof(FVector));

	FglTFRuntimeAccessor NormalAccessor("VEC3", 5126, Normals.Num(), NormalOffset, Normals.Num() * sizeof(FVector), false);
	int32 NormalAccessorIndex = Accessors.Add(NormalAccessor);

	TArray<TSharedPtr<FJsonValue>> JsonPrimitives;

	TArray<TPair<FString, TArray<FVector>>> MorphTargetsValues;
	TMap<FString, TPair<FVector, FVector>> MorphTargetsMinMaxValues;
	TArray<TPair<FString, int32>> MorphTargetsAccessors;
	TArray<TSharedPtr<FJsonValue>> JsonMorphTargetsNames;

	TArray<UMorphTarget*> MorphTargets = SkeletalMesh->GetMorphTargets();
	for (UMorphTarget* MorphTarget : MorphTargets)
	{
		if (MorphTarget->MorphLODModels.IsValidIndex(LOD))
		{
			const FMorphTargetLODModel& MorphTargetLodModel = MorphTarget->MorphLODModels[LOD];

			if (MorphTargetLodModel.Vertices.Num() > 0)
			{
				TArray<FVector> Values;
				Values.AddZeroed(Positions.Num());
				MorphTargetsMinMaxValues.Add(MorphTarget->GetName(), TPair<FVector, FVector>(FVector::ZeroVector, FVector::ZeroVector));
				for (const FMorphTargetDelta& Delta : MorphTargetLodModel.Vertices)
				{
					//uint32 PositionIndex = Indices[Delta.SourceIdx];
					Values[Delta.SourceIdx] = SceneBasisMatrix.TransformPosition(Delta.PositionDelta) * SceneScale;

					const FVector Position = Values[Delta.SourceIdx];
					TPair<FVector, FVector>& Pair = MorphTargetsMinMaxValues[MorphTarget->GetName()];
					if (Position.X < Pair.Key.X)
					{
						Pair.Key.X = Position.X;
					}
					if (Position.Y < Pair.Key.Y)
					{
						Pair.Key.Y = Position.Y;
					}
					if (Position.Z < Pair.Key.Z)
					{
						Pair.Key.Z = Position.Z;
					}
					if (Position.X > Pair.Value.X)
					{
						Pair.Value.X = Position.X;
					}
					if (Position.Y > Pair.Value.Y)
					{
						Pair.Value.Y = Position.Y;
					}
					if (Position.Z > Pair.Value.Z)
					{
						Pair.Value.Z = Position.Z;
					}
				}
				MorphTargetsValues.Add(TPair<FString, TArray<FVector>>(MorphTarget->GetName(), Values));

				JsonMorphTargetsNames.Add(MakeShared<FJsonValueString>(MorphTarget->GetName()));
			}
		}
	}

	for (const TPair<FString, TArray<FVector>>& Pair : MorphTargetsValues)
	{
		int64 MorphTargetOffset = BinaryData.Num();
		BinaryData.Append(reinterpret_cast<const uint8*>(Pair.Value.GetData()), Pair.Value.Num() * sizeof(FVector));

		FglTFRuntimeAccessor MorphTargetAccessor("VEC3", 5126, Pair.Value.Num(), MorphTargetOffset, Pair.Value.Num() * sizeof(FVector), false);
		MorphTargetAccessor.Min.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Key.X));
		MorphTargetAccessor.Min.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Key.Y));
		MorphTargetAccessor.Min.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Key.Z));
		MorphTargetAccessor.Max.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Value.X));
		MorphTargetAccessor.Max.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Value.Y));
		MorphTargetAccessor.Max.Add(MakeShared<FJsonValueNumber>(MorphTargetsMinMaxValues[Pair.Key].Value.Z));
		MorphTargetsAccessors.Add(TPair<FString, int32>(Pair.Key, Accessors.Add(MorphTargetAccessor)));
	}

	for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
	{
		FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];

		TSharedRef<FJsonObject> JsonPrimitive = MakeShared<FJsonObject>();

		FglTFRuntimeAccessor IndicesAccessor("SCALAR", 5125, Section.NumTriangles * 3, Section.BaseIndex * sizeof(uint32), (Section.NumTriangles * 3) * sizeof(uint32), false);
		int32 IndicesAccessorIndex = Accessors.Add(IndicesAccessor);

		JsonPrimitive->SetNumberField("indices", IndicesAccessorIndex);

		TSharedRef<FJsonObject> JsonPrimitiveAttributes = MakeShared<FJsonObject>();

		JsonPrimitiveAttributes->SetNumberField("POSITION", PositionAccessorIndex);
		JsonPrimitiveAttributes->SetNumberField("NORMAL", NormalAccessorIndex);

		JsonPrimitive->SetObjectField("attributes", JsonPrimitiveAttributes);

		if (MorphTargetsAccessors.Num() > 0)
		{
			TArray<TSharedPtr<FJsonValue>> JsonMorphTargets;
			for (const TPair<FString, int32>& Pair : MorphTargetsAccessors)
			{
				TSharedRef<FJsonObject> JsonMorphTarget = MakeShared<FJsonObject>();
				JsonMorphTarget->SetNumberField("POSITION", Pair.Value);
				JsonMorphTargets.Add(MakeShared<FJsonValueObject>(JsonMorphTarget));
			}
			JsonPrimitive->SetArrayField("targets", JsonMorphTargets);
		}

		JsonPrimitives.Add(MakeShared<FJsonValueObject>(JsonPrimitive));
	}

	JsonMesh->SetArrayField("primitives", JsonPrimitives);

	if (MorphTargetsAccessors.Num() > 0)
	{
		TSharedRef<FJsonObject> JsonExtras = MakeShared<FJsonObject>();
		JsonExtras->SetArrayField("targetNames", JsonMorphTargetsNames);

		JsonMesh->SetObjectField("extras", JsonExtras);
	}

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

		if (Accessor.Min.Num() > 0)
		{
			JsonAccessor->SetArrayField("min", Accessor.Min);
		}

		if (Accessor.Max.Num() > 0)
		{
			JsonAccessor->SetArrayField("max", Accessor.Max);
		}

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