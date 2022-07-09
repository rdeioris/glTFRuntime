// Copyright 2020-2022, Roberto De Ioris 
// Copyright 2022, Avatus LLC

#include "glTFRuntimeWriter.h"
#include "Animation/MorphTarget.h"
#include "Engine/Canvas.h"
#include "glTFRuntimeMaterialBaker.h"
#include "GroomAsset.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Kismet/KismetRenderingLibrary.h"
#include "Misc/FileHelper.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Serialization/ArrayWriter.h"
#include "Serialization/JsonSerializer.h"

struct FglTFRuntimeInfluence
{
	uint16 Bones[4];

	FglTFRuntimeInfluence(const uint16 InBones[4], const int32 Delta = 0)
	{
		Bones[0] = InBones[0] - Delta;
		Bones[1] = InBones[1] - Delta;
		Bones[2] = InBones[2] - Delta;
		Bones[3] = InBones[3] - Delta;
	}
};

struct FglTFRuntimeWeight
{
	uint8 Weights[4];

	FglTFRuntimeWeight(const uint8 InWeights[4])
	{
		Weights[0] = InWeights[0];
		Weights[1] = InWeights[1];
		Weights[2] = InWeights[2];
		Weights[3] = InWeights[3];
	}
};

FglTFRuntimeWriter::FglTFRuntimeWriter(const FglTFRuntimeWriterConfig& InConfig)
{
	Config = InConfig;
	JsonRoot = MakeShared<FJsonObject>();
}

FglTFRuntimeWriter::~FglTFRuntimeWriter()
{
}

bool FglTFRuntimeWriter::AddStaticMesh(UWorld* World, UStaticMesh* StaticMesh, const int32 LOD, UStaticMeshComponent* StaticMeshComponent, class UGroomAsset* Groom, const float OrthographicScale)
{
	if (LOD < 0)
	{
		return false;
	}

	if (!StaticMesh && StaticMeshComponent)
	{
		StaticMesh = StaticMeshComponent->GetStaticMesh();
	}

	if (!StaticMesh)
	{
		return false;
	}

	uint32 MeshIndex = JsonMeshes.Num();

	FStaticMeshRenderData* RenderData = StaticMesh->GetRenderData();
	if (!RenderData)
	{
		StaticMesh->InitResources();
		RenderData = StaticMesh->GetRenderData();
	}

	int32 NumLODs = RenderData->LODResources.Num();

	if (LOD >= NumLODs)
	{
		return false;
	}

	FMatrix SceneBasisMatrix = FBasisVectorMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector).Inverse();
	float SceneScale = 1.f / 100.f;

	FStaticMeshLODResources& LODRenderData = RenderData->LODResources[LOD];

	TSharedRef<FJsonObject> JsonMesh = MakeShared<FJsonObject>();
	JsonMesh->SetStringField("name", StaticMesh->GetPathName());

	int64 IndicesOffset = BinaryData.Num();
	TArray<uint32> Indices;
	LODRenderData.IndexBuffer.GetCopy(Indices);
	BinaryData.Append(reinterpret_cast<uint8*>(Indices.GetData()), Indices.Num() * sizeof(uint32));

	TArray<FVector> Positions;
	FVector PivotDelta = Config.PivotDelta;

	FVector PositionMin;
	FVector PositionMax;
	for (uint32 PositionIndex = 0; PositionIndex < LODRenderData.VertexBuffers.PositionVertexBuffer.GetNumVertices(); PositionIndex++)
	{
		FVector Position = LODRenderData.VertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex);

		Position = SceneBasisMatrix.TransformPosition(Position) * SceneScale;

		Position -= PivotDelta;

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

	TArray<FVector> Normals;
	TArray<FVector4> Tangents;
	TArray<FVector2D> TexCoords;

	for (uint32 VertexIndex = 0; VertexIndex < LODRenderData.VertexBuffers.StaticMeshVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		FVector Normal = LODRenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
		Normal = SceneBasisMatrix.TransformVector(Normal);
		Normals.Add(Normal.GetSafeNormal());
		FVector4 Tangent = LODRenderData.VertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
		Tangent = SceneBasisMatrix.TransformFVector4(Tangent).GetSafeNormal();
		Tangent.W = -1; // left handed
		Tangents.Add(Tangent);
		FVector2D UV = LODRenderData.VertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, 0);
		TexCoords.Add(UV);
	}

	TArray<TSharedPtr<FJsonValue>> JsonPrimitives;

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

	int64 NormalOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Normals.GetData()), Normals.Num() * sizeof(FVector));
	FglTFRuntimeAccessor NormalAccessor("VEC3", 5126, Normals.Num(), NormalOffset, Normals.Num() * sizeof(FVector), false);
	int32 NormalAccessorIndex = Accessors.Add(NormalAccessor);

	int64 TangentOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Tangents.GetData()), Tangents.Num() * sizeof(FVector4));
	FglTFRuntimeAccessor TangentAccessor("VEC4", 5126, Tangents.Num(), TangentOffset, Tangents.Num() * sizeof(FVector4), false);
	int32 TangentAccessorIndex = Accessors.Add(TangentAccessor);

	int64 TexCoordOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(TexCoords.GetData()), TexCoords.Num() * sizeof(FVector2D));
	FglTFRuntimeAccessor TexCoordAccessor("VEC2", 5126, TexCoords.Num(), TexCoordOffset, TexCoords.Num() * sizeof(FVector2D), false);
	int32 TexCoordAccessorIndex = Accessors.Add(TexCoordAccessor);

	int32 TextureIndex = 0;
	for (int32 SectionIndex = 0; SectionIndex < LODRenderData.Sections.Num(); SectionIndex++)
	{
		FStaticMeshSection& Section = LODRenderData.Sections[SectionIndex];

		TSharedRef<FJsonObject> JsonPrimitive = MakeShared<FJsonObject>();

		FglTFRuntimeAccessor IndicesAccessor("SCALAR", 5125, Section.NumTriangles * 3, IndicesOffset + (Section.FirstIndex * sizeof(uint32)), (Section.NumTriangles * 3) * sizeof(uint32), false);
		int32 IndicesAccessorIndex = Accessors.Add(IndicesAccessor);

		JsonPrimitive->SetNumberField("indices", IndicesAccessorIndex);

		TSharedRef<FJsonObject> JsonPrimitiveAttributes = MakeShared<FJsonObject>();

		JsonPrimitiveAttributes->SetNumberField("POSITION", PositionAccessorIndex);

		if (Config.bExportNormals)
		{
			JsonPrimitiveAttributes->SetNumberField("NORMAL", NormalAccessorIndex);
		}

		if (Config.bExportTangents)
		{
			JsonPrimitiveAttributes->SetNumberField("TANGENT", TangentAccessorIndex);
		}

		if (Config.bExportUVs)
		{
			JsonPrimitiveAttributes->SetNumberField("TEXCOORD_0", TexCoordAccessorIndex);
		}

		JsonPrimitive->SetObjectField("attributes", JsonPrimitiveAttributes);

		UMaterialInterface* StaticMeshMaterial = UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);

		if (Section.MaterialIndex >= 0)
		{
			const uint16 MaterialIndex = Section.MaterialIndex;

			StaticMeshMaterial = StaticMeshComponent ? StaticMeshComponent->GetMaterial(MaterialIndex) : StaticMesh->GetStaticMaterials()[MaterialIndex].MaterialInterface;
		}

		if (Groom)
		{
			StaticMeshMaterial = Groom->HairGroupsMaterials[0].Material;
		}

		AglTFRuntimeMaterialBaker* MaterialBaker = World->SpawnActor<AglTFRuntimeMaterialBaker>();

		TArray<uint8> PNGBaseColor;
		TArray<uint8> PNGNormalMap;
		TArray<uint8> PNGMetallicRoughness;

		bool bMaterialBaked = false;
		if (Groom)
		{
			bMaterialBaked = MaterialBaker->BakeGroomToPng(Groom, PNGBaseColor, PNGNormalMap, PNGMetallicRoughness, OrthographicScale);
			StaticMeshMaterial = nullptr;
			for (int32 Index = 0; Index < Groom->HairGroupsMaterials.Num(); Index++)
			{
				if (Groom->HairGroupsMaterials[Index].SlotName == Groom->HairGroupsCards[0].MaterialSlotName)
				{
					StaticMeshMaterial = Groom->HairGroupsMaterials[Index].Material;
					break;
				}
			}

			if (!StaticMeshMaterial)
			{
				return false;
			}
		}
		else
		{
			bMaterialBaked = MaterialBaker->BakeMaterialToPng(StaticMeshMaterial, PNGBaseColor, PNGNormalMap, PNGMetallicRoughness);
		}

		if (bMaterialBaked)
		{

			FString AlphaMode = "OPAQUE";

			if (StaticMeshMaterial->GetBlendMode() == EBlendMode::BLEND_Translucent)
			{
				AlphaMode = "BLEND";
			}
			else if (StaticMeshMaterial->GetBlendMode() == EBlendMode::BLEND_Masked)
			{
				AlphaMode = "MASK";
			}

			int64 ImageBufferViewBaseColorOffset = BinaryData.Num();
			BinaryData.Append(PNGBaseColor.GetData(), PNGBaseColor.Num());
			if (BinaryData.Num() % 4)
			{
				BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
			}
			ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewBaseColorOffset, PNGBaseColor.Num()));

			TSharedRef<FJsonObject> JsonMaterial = MakeShared<FJsonObject>();
			JsonMaterial->SetStringField("name", StaticMeshMaterial->GetPathName());

			TSharedRef<FJsonObject> JsonPBRMaterial = MakeShared<FJsonObject>();

			TSharedRef<FJsonObject> JsonBaseColorTexture = MakeShared<FJsonObject>();
			JsonBaseColorTexture->SetNumberField("index", TextureIndex++);

			JsonPBRMaterial->SetObjectField("baseColorTexture", JsonBaseColorTexture);

			if (AlphaMode != "BLEND")
			{
				int64 ImageBufferViewNormalMapOffset = BinaryData.Num();
				BinaryData.Append(PNGNormalMap.GetData(), PNGNormalMap.Num());
				if (BinaryData.Num() % 4)
				{
					BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
				}
				ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewNormalMapOffset, PNGNormalMap.Num()));

				int64 ImageBufferViewMetallicRoughnessOffset = BinaryData.Num();
				BinaryData.Append(PNGMetallicRoughness.GetData(), PNGMetallicRoughness.Num());
				if (BinaryData.Num() % 4)
				{
					BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
				}
				ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewMetallicRoughnessOffset, PNGMetallicRoughness.Num()));

				TSharedRef<FJsonObject> JsonNormalTexture = MakeShared<FJsonObject>();
				JsonNormalTexture->SetNumberField("index", TextureIndex++);
				JsonMaterial->SetObjectField("normalTexture", JsonNormalTexture);

				TSharedRef<FJsonObject> JsonMetallicRoughnessTexture = MakeShared<FJsonObject>();
				JsonMetallicRoughnessTexture->SetNumberField("index", TextureIndex++);
				JsonPBRMaterial->SetObjectField("metallicRoughnessTexture", JsonMetallicRoughnessTexture);
			}

			JsonMaterial->SetObjectField("pbrMetallicRoughness", JsonPBRMaterial);

			JsonMaterial->SetStringField("alphaMode", AlphaMode);

			if (AlphaMode == "MASK")
			{
				JsonMaterial->SetNumberField("alphaCutoff", StaticMeshMaterial->GetOpacityMaskClipValue());
			}

			if (StaticMeshMaterial->IsTwoSided())
			{
				JsonMaterial->SetBoolField("doubleSided", true);
			}

			int32 JsonMaterialIndex = JsonMaterials.Add(MakeShared<FJsonValueObject>(JsonMaterial));
			JsonPrimitive->SetNumberField("material", JsonMaterialIndex);

		}

		//MaterialBaker->Destroy();

		JsonPrimitives.Add(MakeShared<FJsonValueObject>(JsonPrimitive));
	}

	JsonMesh->SetArrayField("primitives", JsonPrimitives);

	JsonMeshes.Add(MakeShared<FJsonValueObject>(JsonMesh));

	return true;
}

bool FglTFRuntimeWriter::AddSkeletalMesh(UWorld* World, USkeletalMesh* SkeletalMesh, const int32 LOD, const TArray<UAnimSequence*>& Animations, USkeletalMeshComponent* SkeletalMeshComponent)
{
	if (LOD < 0)
	{
		return false;
	}

	if (!SkeletalMesh && SkeletalMeshComponent)
	{
		SkeletalMesh = SkeletalMeshComponent->SkeletalMesh;
	}

	if (!SkeletalMesh)
	{
		return false;
	}

	const FReferenceSkeleton& SkeletonRef = SkeletalMesh->GetRefSkeleton();

	int32 NumBones = SkeletonRef.GetNum();

	TArray<FTransform> BoneTransforms = SkeletonRef.GetRefBonePose();

	FMatrix Basis = FBasisVectorMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);

	auto BuildBoneFullMatrix = [](const FReferenceSkeleton& SkeletonRef, const TArray<FTransform>& BoneTransforms, const int32 ParentBoneIndex, const int32 OverrideNoneIndex) -> FMatrix
	{
		FTransform Transform = BoneTransforms[ParentBoneIndex];
		int32 BoneIndex = SkeletonRef.GetParentIndex(ParentBoneIndex);
		while (BoneIndex != OverrideNoneIndex)
		{
			Transform *= BoneTransforms[BoneIndex];
			BoneIndex = SkeletonRef.GetParentIndex(BoneIndex);
		}

		return Transform.ToMatrixWithScale();
	};

	int32 RootBoneIndex = 0;
	int32 OverrideNoneIndex = INDEX_NONE;
	FTransform BoneReferenceTransform = FTransform::Identity;

	if (!Config.ForceRootBone.IsEmpty())
	{
		RootBoneIndex = SkeletonRef.FindBoneIndex(*Config.ForceRootBone);
		if (RootBoneIndex == INDEX_NONE)
		{
			return false;
		}
		OverrideNoneIndex = SkeletonRef.GetParentIndex(RootBoneIndex);
		BoneReferenceTransform = BoneTransforms[RootBoneIndex];
		int32 BoneIndex = SkeletonRef.GetParentIndex(RootBoneIndex);
		while (BoneIndex != INDEX_NONE)
		{
			BoneReferenceTransform *= BoneTransforms[BoneIndex];
			BoneIndex = SkeletonRef.GetParentIndex(BoneIndex);
		}
		BoneReferenceTransform = BoneReferenceTransform.Inverse();
		BoneTransforms[RootBoneIndex] = FTransform::Identity;
	}

	if (Config.bExportSkin)
	{
		TArray<TSharedPtr<FJsonValue>> JsonJoints;
		TArray<float> MatricesData;


		for (int32 BoneIndex = RootBoneIndex; BoneIndex < NumBones; BoneIndex++)
		{
			TSharedRef<FJsonObject> JsonNode = MakeShared<FJsonObject>();
			JsonNode->SetStringField("name", SkeletonRef.GetBoneName(BoneIndex).ToString());
			TArray<TSharedPtr<FJsonValue>> JsonNodeChildren;
			TArray<int32> BoneChildren;
			for (int32 ChildBoneIndex = RootBoneIndex; ChildBoneIndex < NumBones; ChildBoneIndex++)
			{
				if (SkeletonRef.GetParentIndex(ChildBoneIndex) == BoneIndex)
				{
					JsonNodeChildren.Add(MakeShared<FJsonValueNumber>(ChildBoneIndex - RootBoneIndex));
				}
			}

			if (JsonNodeChildren.Num() > 0)
			{
				JsonNode->SetArrayField("children", JsonNodeChildren);
			}

			FMatrix Matrix = Basis.Inverse() * BoneTransforms[BoneIndex].ToMatrixWithScale() * Basis;
			Matrix.ScaleTranslation(FVector::OneVector / 100);

			FMatrix FullMatrix = Basis.Inverse() * BuildBoneFullMatrix(SkeletonRef, BoneTransforms, BoneIndex, OverrideNoneIndex) * Basis;
			FullMatrix.ScaleTranslation(FVector::OneVector / 100);
			FullMatrix = FullMatrix.Inverse();

			//TArray<TSharedPtr<FJsonValue>> JsonNodeMatrix;
			for (int32 Row = 0; Row < 4; Row++)
			{
				for (int32 Col = 0; Col < 4; Col++)
				{
					//JsonNodeMatrix.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Col]));
					MatricesData.Add(FullMatrix.M[Row][Col]);
				}
			}
			//JsonNode->SetArrayField("matrix", JsonNodeMatrix);

			TArray<TSharedPtr<FJsonValue>> JsonNodeTranslation;
			TArray<TSharedPtr<FJsonValue>> JsonNodeRotation;
			TArray<TSharedPtr<FJsonValue>> JsonNodeScale;

			FTransform NodeTransform;
			NodeTransform.SetFromMatrix(Matrix);

			if (Config.OverrideBonesByIndex.Contains(BoneIndex))
			{
				NodeTransform = Config.OverrideBonesByIndex[BoneIndex];
			}

			if (Config.OverrideBonesByName.Contains(SkeletonRef.GetBoneName(BoneIndex)))
			{
				NodeTransform = Config.OverrideBonesByName[SkeletonRef.GetBoneName(BoneIndex)];
			}

			FVector NodeTranslation = NodeTransform.GetLocation();
			FQuat NodeRotation = NodeTransform.GetRotation();
			FVector NodeScale = NodeTransform.GetScale3D();

			JsonNodeTranslation.Add(MakeShared<FJsonValueNumber>(NodeTranslation.X));
			JsonNodeTranslation.Add(MakeShared<FJsonValueNumber>(NodeTranslation.Y));
			JsonNodeTranslation.Add(MakeShared<FJsonValueNumber>(NodeTranslation.Z));

			JsonNodeRotation.Add(MakeShared<FJsonValueNumber>(NodeRotation.X));
			JsonNodeRotation.Add(MakeShared<FJsonValueNumber>(NodeRotation.Y));
			JsonNodeRotation.Add(MakeShared<FJsonValueNumber>(NodeRotation.Z));
			JsonNodeRotation.Add(MakeShared<FJsonValueNumber>(NodeRotation.W));

			JsonNodeScale.Add(MakeShared<FJsonValueNumber>(NodeScale.X));
			JsonNodeScale.Add(MakeShared<FJsonValueNumber>(NodeScale.Y));
			JsonNodeScale.Add(MakeShared<FJsonValueNumber>(NodeScale.Z));

			JsonNode->SetArrayField("translation", JsonNodeTranslation);
			JsonNode->SetArrayField("rotation", JsonNodeRotation);
			JsonNode->SetArrayField("scale", JsonNodeScale);

			int32 JointNode = JsonNodes.Add(MakeShared<FJsonValueObject>(JsonNode));
			check(JointNode == BoneIndex - RootBoneIndex);
			JsonJoints.Add(MakeShared<FJsonValueNumber>(JointNode));
		}


		TArray<TSharedPtr<FJsonValue>> JsonSkins;
		TSharedRef<FJsonObject> JsonSkin = MakeShared<FJsonObject>();
		JsonSkin->SetStringField("name", SkeletalMesh->GetSkeleton()->GetName());

		JsonSkin->SetArrayField("joints", JsonJoints);

		// build accessors/bufferViews/buffers for bind matrices
		int64 SkeletonMatricesOffset = BinaryData.Num();
		BinaryData.Append(reinterpret_cast<uint8*>(MatricesData.GetData()), NumBones * 16 * sizeof(float));

		FglTFRuntimeAccessor SkeletonMatricesAccessor("MAT4", 5126, MatricesData.Num() / 16, SkeletonMatricesOffset, MatricesData.Num() * 16 * sizeof(float), false);
		int32 SkeletonMatricesAccessorIndex = Accessors.Add(SkeletonMatricesAccessor);

		JsonSkin->SetNumberField("inverseBindMatrices", SkeletonMatricesAccessorIndex);

		int32 SkinIndex = JsonSkins.Add(MakeShared<FJsonValueObject>(JsonSkin));

		JsonRoot->SetArrayField("skins", JsonSkins);
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

	FMatrix SceneBasisMatrix = FBasisVectorMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector).Inverse();
	float SceneScale = 1.f / 100.f;

	FSkeletalMeshLODRenderData& LODRenderData = RenderData->LODRenderData[LOD];

	TSharedRef<FJsonObject> JsonMesh = MakeShared<FJsonObject>();
	JsonMesh->SetStringField("name", SkeletalMesh->GetPathName());

	int64 IndicesOffset = BinaryData.Num();
	TArray<uint32> Indices;
	LODRenderData.MultiSizeIndexContainer.GetIndexBuffer(Indices);
	BinaryData.Append(reinterpret_cast<uint8*>(Indices.GetData()), Indices.Num() * sizeof(uint32));

	TArray<FVector> Positions;
	FVector PivotDelta = Config.PivotDelta;
	if (!Config.PivotToBone.IsEmpty())
	{
		int32 PivotBoneIndex = SkeletonRef.FindBoneIndex(*Config.PivotToBone);
		if (PivotBoneIndex < 0)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to find Pivot Bone %s"), *Config.PivotToBone);
			return false;
		}
		FMatrix FullMatrix = Basis.Inverse() * BuildBoneFullMatrix(SkeletonRef, BoneTransforms, PivotBoneIndex, OverrideNoneIndex) * Basis;
		FullMatrix.ScaleTranslation(FVector::OneVector / 100);

		FTransform PivotTransform;
		PivotTransform.SetFromMatrix(FullMatrix);

		PivotDelta = PivotTransform.TransformPosition(PivotDelta);
	}

	FVector PositionMin;
	FVector PositionMax;
	for (uint32 PositionIndex = 0; PositionIndex < LODRenderData.StaticVertexBuffers.PositionVertexBuffer.GetNumVertices(); PositionIndex++)
	{
		FVector Position = BoneReferenceTransform.TransformPosition(LODRenderData.StaticVertexBuffers.PositionVertexBuffer.VertexPosition(PositionIndex));

		Position = SceneBasisMatrix.TransformPosition(Position) * SceneScale;

		Position -= PivotDelta;

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

	TArray<FVector> Normals;
	TArray<FVector4> Tangents;
	TArray<FVector2D> TexCoords;

	for (uint32 VertexIndex = 0; VertexIndex < LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices(); VertexIndex++)
	{
		FVector Normal = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
		Normal = SceneBasisMatrix.TransformVector(Normal);
		Normals.Add(Normal.GetSafeNormal());
		FVector4 Tangent = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
		Tangent = SceneBasisMatrix.TransformFVector4(Tangent).GetSafeNormal();
		Tangent.W = -1; // left handed
		Tangents.Add(Tangent);
		FVector2D UV = LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, 0);
		TexCoords.Add(UV);
	}

	TArray<int32> JointAccessorIndices;
	TArray<int32> WeightAccessorIndices;

	if (Config.bExportSkin)
	{
		TArray<FglTFRuntimeInfluence> SkinInfluences[3];
		TArray<FglTFRuntimeWeight> SkinWeights[3];

		SkinInfluences[0].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		SkinInfluences[1].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		SkinInfluences[2].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		SkinWeights[0].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		SkinWeights[1].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		SkinWeights[2].AddZeroed(LODRenderData.StaticVertexBuffers.StaticMeshVertexBuffer.GetNumVertices());
		TSet<uint32> ProcessedIndices;
		for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
		{
			FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];
			for (uint32 VertexIndex = Section.BaseIndex; VertexIndex < static_cast<uint32>(Section.BaseIndex + (Section.NumTriangles * 3)); VertexIndex++)
			{
				uint32 VertexVertexIndex = Indices[VertexIndex];

				if (ProcessedIndices.Contains(VertexVertexIndex))
				{
					continue;
				}

				ProcessedIndices.Add(VertexVertexIndex);

				const FSkinWeightInfo& SkinWeightInfo = LODRenderData.SkinWeightVertexBuffer.GetVertexSkinWeights(VertexVertexIndex);

				for (int32 JointsGroup = 0; JointsGroup < 3; JointsGroup++)
				{
					uint16 InfluencesData[4];
					uint8 WeightsData[4];
					for (int32 InfluenceIndex = 0; InfluenceIndex < 4; InfluenceIndex++)
					{
						const int32 BoneIndex = SkinWeightInfo.InfluenceBones[(JointsGroup * 4) + InfluenceIndex];
						const uint8 Weight = SkinWeightInfo.InfluenceWeights[(JointsGroup * 4) + InfluenceIndex];
						InfluencesData[InfluenceIndex] = Section.BoneMap[BoneIndex];
						WeightsData[InfluenceIndex] = Weight;
						if (Weight == 0)
						{
							InfluencesData[InfluenceIndex] = 0;
						}
						//UE_LOG(LogTemp, Warning, TEXT("Bone %d to %d"), BoneIndex, InfluencesData[InfluenceIndex]);
					}
					for (int32 InfluenceIndex = 0; InfluenceIndex < 4; InfluenceIndex++)
					{
						SkinInfluences[JointsGroup][VertexVertexIndex] = FglTFRuntimeInfluence(InfluencesData, RootBoneIndex);
					}
					SkinWeights[JointsGroup][VertexVertexIndex] = WeightsData;
				}
			}
		}

		for (int32 JointsGroup = 0; JointsGroup < 3; JointsGroup++)
		{
			int64 JointOffset = BinaryData.Num();
			BinaryData.Append(reinterpret_cast<uint8*>(SkinInfluences[JointsGroup].GetData()), SkinInfluences[JointsGroup].Num() * sizeof(uint16) * 4);
			FglTFRuntimeAccessor JointAccessor("VEC4", 5123, SkinInfluences[JointsGroup].Num(), JointOffset, SkinInfluences[JointsGroup].Num() * sizeof(uint16) * 4, false);
			JointAccessorIndices.Add(Accessors.Add(JointAccessor));

			int64 WeightOffset = BinaryData.Num();
			BinaryData.Append(reinterpret_cast<uint8*>(SkinWeights[JointsGroup].GetData()), SkinWeights[JointsGroup].Num() * 4);
			FglTFRuntimeAccessor WeightAccessor("VEC4", 5121, SkinWeights[JointsGroup].Num(), WeightOffset, SkinWeights[JointsGroup].Num() * 4, true);
			WeightAccessorIndices.Add(Accessors.Add(WeightAccessor));
		}
	}

	TArray<TSharedPtr<FJsonValue>> JsonPrimitives;

	TArray<TPair<FString, TArray<FVector>>> MorphTargetsValues;
	TMap<FString, TPair<FVector, FVector>> MorphTargetsMinMaxValues;
	TArray<TPair<FString, int32>> MorphTargetsAccessors;
	TArray<TSharedPtr<FJsonValue>> JsonMorphTargetsNames;
	TMap<FString, int32> MorphTargetNameMap;

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

				MorphTargetNameMap.Add(MorphTarget->GetName(), JsonMorphTargetsNames.Add(MakeShared<FJsonValueString>(MorphTarget->GetName())));
			}
		}
	}

	if (Config.bBakeMorphTargets && SkeletalMeshComponent && SkeletalMeshComponent->SkeletalMesh == SkeletalMesh)
	{
		for (UMorphTarget* MorphTarget : MorphTargets)
		{
			const FString& MorphTargetName = MorphTarget->GetName();
			if (MorphTargetNameMap.Contains(MorphTargetName))
			{
				float Weight = SkeletalMeshComponent->GetMorphTarget(*MorphTargetName);
				for (int32 PositionIndex = 0; PositionIndex < Positions.Num(); PositionIndex++)
				{
					int32 MorphTargetIndex = MorphTargetNameMap[MorphTargetName];
					FVector& Position = Positions[PositionIndex];
					Position += MorphTargetsValues[MorphTargetIndex].Value[PositionIndex] * Weight;
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
			}
		}
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

	int64 NormalOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Normals.GetData()), Normals.Num() * sizeof(FVector));
	FglTFRuntimeAccessor NormalAccessor("VEC3", 5126, Normals.Num(), NormalOffset, Normals.Num() * sizeof(FVector), false);
	int32 NormalAccessorIndex = Accessors.Add(NormalAccessor);

	int64 TangentOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(Tangents.GetData()), Tangents.Num() * sizeof(FVector4));
	FglTFRuntimeAccessor TangentAccessor("VEC4", 5126, Tangents.Num(), TangentOffset, Tangents.Num() * sizeof(FVector4), false);
	int32 TangentAccessorIndex = Accessors.Add(TangentAccessor);

	int64 TexCoordOffset = BinaryData.Num();
	BinaryData.Append(reinterpret_cast<uint8*>(TexCoords.GetData()), TexCoords.Num() * sizeof(FVector2D));
	FglTFRuntimeAccessor TexCoordAccessor("VEC2", 5126, TexCoords.Num(), TexCoordOffset, TexCoords.Num() * sizeof(FVector2D), false);
	int32 TexCoordAccessorIndex = Accessors.Add(TexCoordAccessor);

	if (Config.bExportMorphTargets)
	{
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
	}

	int32 TextureIndex = 0;
	for (int32 SectionIndex = 0; SectionIndex < LODRenderData.RenderSections.Num(); SectionIndex++)
	{
		FSkelMeshRenderSection& Section = LODRenderData.RenderSections[SectionIndex];

		TSharedRef<FJsonObject> JsonPrimitive = MakeShared<FJsonObject>();

		FglTFRuntimeAccessor IndicesAccessor("SCALAR", 5125, Section.NumTriangles * 3, IndicesOffset + (Section.BaseIndex * sizeof(uint32)), (Section.NumTriangles * 3) * sizeof(uint32), false);
		int32 IndicesAccessorIndex = Accessors.Add(IndicesAccessor);

		JsonPrimitive->SetNumberField("indices", IndicesAccessorIndex);

		TSharedRef<FJsonObject> JsonPrimitiveAttributes = MakeShared<FJsonObject>();

		JsonPrimitiveAttributes->SetNumberField("POSITION", PositionAccessorIndex);

		if (Config.bExportNormals)
		{
			JsonPrimitiveAttributes->SetNumberField("NORMAL", NormalAccessorIndex);
		}

		if (Config.bExportTangents)
		{
			JsonPrimitiveAttributes->SetNumberField("TANGENT", TangentAccessorIndex);
		}

		if (Config.bExportUVs)
		{
			JsonPrimitiveAttributes->SetNumberField("TEXCOORD_0", TexCoordAccessorIndex);
		}

		if (Config.bExportSkin)
		{
			if (Config.MaxBonesInfluences > 1)
			{
				JsonPrimitiveAttributes->SetNumberField("JOINTS_0", JointAccessorIndices[0]);
				JsonPrimitiveAttributes->SetNumberField("WEIGHTS_0", WeightAccessorIndices[0]);
				if (Config.MaxBonesInfluences > 4)
				{
					JsonPrimitiveAttributes->SetNumberField("JOINTS_1", JointAccessorIndices[1]);
					JsonPrimitiveAttributes->SetNumberField("WEIGHTS_1", WeightAccessorIndices[1]);
					if (Config.MaxBonesInfluences > 8)
					{
						JsonPrimitiveAttributes->SetNumberField("JOINTS_2", JointAccessorIndices[2]);
						JsonPrimitiveAttributes->SetNumberField("WEIGHTS_2", WeightAccessorIndices[2]);
					}
				}
			}
		}

		JsonPrimitive->SetObjectField("attributes", JsonPrimitiveAttributes);

		if (Config.bExportMorphTargets && MorphTargetsAccessors.Num() > 0)
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

		const uint16 MaterialIndex = Section.MaterialIndex;

		UMaterialInterface* SkeletalMaterial = SkeletalMeshComponent ? SkeletalMeshComponent->GetMaterial(MaterialIndex) : SkeletalMesh->GetMaterials()[MaterialIndex].MaterialInterface;

		AglTFRuntimeMaterialBaker* MaterialBaker = World->SpawnActor<AglTFRuntimeMaterialBaker>();

		TArray<uint8> PNGBaseColor;
		TArray<uint8> PNGNormalMap;
		TArray<uint8> PNGMetallicRoughness;

		FString AlphaMode = "OPAQUE";

		if (SkeletalMaterial->GetBlendMode() == EBlendMode::BLEND_Translucent)
		{
			AlphaMode = "BLEND";
		}
		else if (SkeletalMaterial->GetBlendMode() == EBlendMode::BLEND_Masked)
		{
			AlphaMode = "MASK";
		}

		if (MaterialBaker->BakeMaterialToPng(SkeletalMaterial, PNGBaseColor, PNGNormalMap, PNGMetallicRoughness))
		{

			int64 ImageBufferViewBaseColorOffset = BinaryData.Num();
			BinaryData.Append(PNGBaseColor.GetData(), PNGBaseColor.Num());
			if (BinaryData.Num() % 4)
			{
				BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
			}
			ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewBaseColorOffset, PNGBaseColor.Num()));

			TSharedRef<FJsonObject> JsonMaterial = MakeShared<FJsonObject>();
			JsonMaterial->SetStringField("name", SkeletalMaterial->GetPathName());

			TSharedRef<FJsonObject> JsonPBRMaterial = MakeShared<FJsonObject>();

			TSharedRef<FJsonObject> JsonBaseColorTexture = MakeShared<FJsonObject>();
			JsonBaseColorTexture->SetNumberField("index", TextureIndex++);

			JsonPBRMaterial->SetObjectField("baseColorTexture", JsonBaseColorTexture);

			if (AlphaMode != "BLEND")
			{
				int64 ImageBufferViewNormalMapOffset = BinaryData.Num();
				BinaryData.Append(PNGNormalMap.GetData(), PNGNormalMap.Num());
				if (BinaryData.Num() % 4)
				{
					BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
				}
				ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewNormalMapOffset, PNGNormalMap.Num()));

				int64 ImageBufferViewMetallicRoughnessOffset = BinaryData.Num();
				BinaryData.Append(PNGMetallicRoughness.GetData(), PNGMetallicRoughness.Num());
				if (BinaryData.Num() % 4)
				{
					BinaryData.AddZeroed(4 - (BinaryData.Num() % 4));
				}
				ImagesBuffers.Add(TPair<int64, int64>(ImageBufferViewMetallicRoughnessOffset, PNGMetallicRoughness.Num()));

				TSharedRef<FJsonObject> JsonNormalTexture = MakeShared<FJsonObject>();
				JsonNormalTexture->SetNumberField("index", TextureIndex++);
				JsonMaterial->SetObjectField("normalTexture", JsonNormalTexture);

				TSharedRef<FJsonObject> JsonMetallicRoughnessTexture = MakeShared<FJsonObject>();
				JsonMetallicRoughnessTexture->SetNumberField("index", TextureIndex++);
				JsonPBRMaterial->SetObjectField("metallicRoughnessTexture", JsonMetallicRoughnessTexture);
			}

			JsonMaterial->SetObjectField("pbrMetallicRoughness", JsonPBRMaterial);

			JsonMaterial->SetStringField("alphaMode", AlphaMode);

			if (AlphaMode == "MASK")
			{
				JsonMaterial->SetNumberField("alphaCutoff", SkeletalMaterial->GetOpacityMaskClipValue());
			}

			if (SkeletalMaterial->IsTwoSided())
			{
				JsonMaterial->SetBoolField("doubleSided", true);
			}

			int32 JsonMaterialIndex = JsonMaterials.Add(MakeShared<FJsonValueObject>(JsonMaterial));
			JsonPrimitive->SetNumberField("material", JsonMaterialIndex);

		}

		MaterialBaker->Destroy();

		JsonPrimitives.Add(MakeShared<FJsonValueObject>(JsonPrimitive));
	}

	JsonMesh->SetArrayField("primitives", JsonPrimitives);

	if (Config.bExportMorphTargets && MorphTargetsAccessors.Num() > 0)
	{
		TSharedRef<FJsonObject> JsonExtras = MakeShared<FJsonObject>();
		JsonExtras->SetArrayField("targetNames", JsonMorphTargetsNames);

		JsonMesh->SetObjectField("extras", JsonExtras);
	}

	JsonMeshes.Add(MakeShared<FJsonValueObject>(JsonMesh));

	if (Config.bExportSkin)
	{
		const FReferenceSkeleton& AnimSkeletonRef = SkeletalMesh->GetSkeleton()->GetReferenceSkeleton();

		TArray<FTransform> AnimBoneTransforms = AnimSkeletonRef.GetRefBonePose();
		TMap<FName, FTransform> AnimBoneRetargetingTransforms;

		auto GetBoneTransform = [](const FReferenceSkeleton& SkeletonRef, const int32 BoneIndex) -> FTransform
		{
			TArray<FTransform> Transforms = SkeletonRef.GetRefBonePose();
			int32 CurrentBoneIndex = BoneIndex;
			FTransform Transform = FTransform::Identity;
			while (CurrentBoneIndex != INDEX_NONE)
			{
				Transform *= Transforms[CurrentBoneIndex];
				CurrentBoneIndex = SkeletonRef.GetParentIndex(CurrentBoneIndex);
			}
			return Transform;
		};

		for (int32 SkelIndex = 0; SkelIndex < AnimBoneTransforms.Num(); SkelIndex++)
		{
			FName BoneName = AnimSkeletonRef.GetBoneName(SkelIndex);
			int32 RefIndex = SkeletonRef.FindBoneIndex(BoneName);
			FTransform SkeletonAnimTransform = AnimBoneTransforms[SkelIndex];
			FTransform SkeletalMeshAnimTransform = BoneTransforms[RefIndex];
			AnimBoneRetargetingTransforms.Add(BoneName, SkeletalMeshAnimTransform * SkeletonAnimTransform.Inverse());
		}

		for (const UAnimSequence* AnimSequence : Animations)
		{
			if (!AnimSequence)
			{
				continue;
			}
			TArray<float> Timeline;
			const float FrameDeltaTime = AnimSequence->SequenceLength / AnimSequence->GetRawNumberOfFrames();
			UE_LOG(LogTemp, Warning, TEXT("Number Of Frames: %d"), AnimSequence->GetRawNumberOfFrames());
			for (int32 FrameIndex = 0; FrameIndex < AnimSequence->GetRawNumberOfFrames(); FrameIndex++)
			{
				Timeline.Add(FrameIndex * FrameDeltaTime);
			}

			int64 TimelineOffset = BinaryData.Num();
			BinaryData.Append(reinterpret_cast<uint8*>(Timeline.GetData()), Timeline.Num() * sizeof(float));
			FglTFRuntimeAccessor InputAccessor("SCALAR", 5126, Timeline.Num(), TimelineOffset, Timeline.Num() * sizeof(float), false);
			InputAccessor.Min.Add(MakeShared<FJsonValueNumber>(0));
			InputAccessor.Max.Add(MakeShared<FJsonValueNumber>(AnimSequence->SequenceLength - FrameDeltaTime));
			int32 InputAccessorIndex = Accessors.Add(InputAccessor);

			TSharedRef<FJsonObject> JsonAnimation = MakeShared<FJsonObject>();
			JsonAnimation->SetStringField("name", AnimSequence->GetFullName());

			TArray<TSharedPtr<FJsonValue>> JsonAnimationChannels;
			TArray<TSharedPtr<FJsonValue>> JsonAnimationSamplers;

			TArray<FName> Tracks = AnimSequence->GetAnimationTrackNames();
			for (int32 TrackIndex = 0; TrackIndex < Tracks.Num(); TrackIndex++)
			{
				// only bones based animations supported for now
				int32 AnimSkelIndex = AnimSkeletonRef.FindBoneIndex(Tracks[TrackIndex]);
				if (AnimSkelIndex == INDEX_NONE)
				{
					continue;
				}

				int32 BoneIndex = SkeletonRef.FindBoneIndex(Tracks[TrackIndex]);
				if (BoneIndex == INDEX_NONE)
				{
					continue;
				}

				const FRawAnimSequenceTrack& Track = AnimSequence->GetRawAnimationTrack(TrackIndex);

				/* Rotation curves */
				/* {
					TSharedRef<FJsonObject> JsonAnimationRotationSampler = MakeShared<FJsonObject>();

					JsonAnimationRotationSampler->SetNumberField("input", InputAccessorIndex);
					JsonAnimationRotationSampler->SetStringField("interpolation", "LINEAR");

					TArray<FQuat> RotKeys;
					RotKeys.AddUninitialized(Timeline.Num());
					for (FQuat& RotKey : RotKeys)
					{
						FMatrix RotationMatrix = SceneBasisMatrix.Inverse() * FQuatRotationMatrix(BoneTransforms[SkeletonRef.FindBoneIndex(Tracks[TrackIndex])].GetRotation()) * SceneBasisMatrix;
						RotKey = RotationMatrix.ToQuat();
					}
					int32 RotIndex = 0;
					UE_LOG(LogTemp, Warning, TEXT("Number Of Rot Frames: %d"), Track.RotKeys.Num());
					for (FQuat Quat : Track.RotKeys)
					{
						FMatrix RotationMatrix = SceneBasisMatrix.Inverse() * FQuatRotationMatrix(Quat) * SceneBasisMatrix;
						RotKeys[RotIndex++] = RotationMatrix.ToQuat();
					}

					int64 AnimRotationOffset = BinaryData.Num();
					BinaryData.Append(reinterpret_cast<uint8*>(RotKeys.GetData()), RotKeys.Num() * sizeof(FQuat));
					FglTFRuntimeAccessor OutputAccessor("VEC4", 5126, RotKeys.Num(), AnimRotationOffset, RotKeys.Num() * sizeof(FQuat), false);
					int32 OutputAccessorIndex = Accessors.Add(OutputAccessor);

					JsonAnimationRotationSampler->SetNumberField("output", OutputAccessorIndex);

					int32 SamplerIndex = JsonAnimationSamplers.Add(MakeShared<FJsonValueObject>(JsonAnimationRotationSampler));

					TSharedRef<FJsonObject> JsonAnimationRotationChannel = MakeShared<FJsonObject>();
					JsonAnimationRotationChannel->SetNumberField("sampler", SamplerIndex);

					TSharedRef<FJsonObject> JsonAnimationRotationChannelTarget = MakeShared<FJsonObject>();
					JsonAnimationRotationChannelTarget->SetNumberField("node", SkeletonRef.FindBoneIndex(Tracks[TrackIndex]));
					JsonAnimationRotationChannelTarget->SetStringField("path", "rotation");

					JsonAnimationRotationChannel->SetObjectField("target", JsonAnimationRotationChannelTarget);

					JsonAnimationChannels.Add(MakeShared<FJsonValueObject>(JsonAnimationRotationChannel));
				}*/

				/* Translation curves */
				{
					TSharedRef<FJsonObject> JsonAnimationTranslationSampler = MakeShared<FJsonObject>();

					JsonAnimationTranslationSampler->SetNumberField("input", InputAccessorIndex);
					JsonAnimationTranslationSampler->SetStringField("interpolation", "LINEAR");

					TArray<FVector> PosKeys;
					PosKeys.AddUninitialized(Timeline.Num());
					for (FVector& PosKey : PosKeys)
					{
						FMatrix TranslationMatrix = SceneBasisMatrix.Inverse() * FTranslationMatrix(BoneTransforms[BoneIndex].GetLocation()) * SceneBasisMatrix;
						PosKey = FTransform(TranslationMatrix).GetLocation() * SceneScale;
					}
					int32 PosIndex = 0;
					for (FVector Pos : Track.PosKeys)
					{
						FMatrix TranslationMatrix = SceneBasisMatrix.Inverse() * FTranslationMatrix(Pos - AnimBoneRetargetingTransforms[Tracks[TrackIndex]].GetLocation()) * SceneBasisMatrix;
						PosKeys[PosIndex++] = FTransform(TranslationMatrix).GetLocation() * SceneScale;
					}

					int64 AnimPositionOffset = BinaryData.Num();
					BinaryData.Append(reinterpret_cast<uint8*>(PosKeys.GetData()), PosKeys.Num() * sizeof(FVector));
					FglTFRuntimeAccessor OutputAccessor("VEC3", 5126, PosKeys.Num(), AnimPositionOffset, PosKeys.Num() * sizeof(FVector), false);
					int32 OutputAccessorIndex = Accessors.Add(OutputAccessor);

					JsonAnimationTranslationSampler->SetNumberField("output", OutputAccessorIndex);

					int32 SamplerIndex = JsonAnimationSamplers.Add(MakeShared<FJsonValueObject>(JsonAnimationTranslationSampler));

					TSharedRef<FJsonObject> JsonAnimationTranslationChannel = MakeShared<FJsonObject>();
					JsonAnimationTranslationChannel->SetNumberField("sampler", SamplerIndex);

					TSharedRef<FJsonObject> JsonAnimationTranslationChannelTarget = MakeShared<FJsonObject>();
					JsonAnimationTranslationChannelTarget->SetNumberField("node", SkeletonRef.FindBoneIndex(Tracks[TrackIndex]));
					JsonAnimationTranslationChannelTarget->SetStringField("path", "translation");

					JsonAnimationTranslationChannel->SetObjectField("target", JsonAnimationTranslationChannelTarget);

					JsonAnimationChannels.Add(MakeShared<FJsonValueObject>(JsonAnimationTranslationChannel));
				}

				/* Scale curves */
				/* {
					TSharedRef<FJsonObject> JsonAnimationScaleSampler = MakeShared<FJsonObject>();

					JsonAnimationScaleSampler->SetNumberField("input", InputAccessorIndex);
					JsonAnimationScaleSampler->SetStringField("interpolation", "LINEAR");

					TArray<FVector> ScaleKeys;
					ScaleKeys.AddUninitialized(Timeline.Num());
					for (FVector& ScaleKey : ScaleKeys)
					{
						FMatrix ScaleMatrix = SceneBasisMatrix.Inverse() * FScaleMatrix(AnimBoneTransforms[AnimSkeletonRef.FindBoneIndex(Tracks[TrackIndex])].GetScale3D()) * SceneBasisMatrix;
						ScaleKey = ScaleMatrix.ExtractScaling();
					}
					int32 ScaleIndex = 0;
					for (FVector Scale : Track.ScaleKeys)
					{
						FMatrix ScaleMatrix = SceneBasisMatrix.Inverse() * FScaleMatrix(Scale) * SceneBasisMatrix;
						ScaleKeys[ScaleIndex++] = FTransform(ScaleMatrix).GetScale3D();
					}

					int64 AnimScaleOffset = BinaryData.Num();
					BinaryData.Append(reinterpret_cast<uint8*>(ScaleKeys.GetData()), ScaleKeys.Num() * sizeof(FVector));
					FglTFRuntimeAccessor OutputAccessor("VEC3", 5126, ScaleKeys.Num(), AnimScaleOffset, ScaleKeys.Num() * sizeof(FVector), false);
					int32 OutputAccessorIndex = Accessors.Add(OutputAccessor);

					JsonAnimationScaleSampler->SetNumberField("output", OutputAccessorIndex);

					int32 SamplerIndex = JsonAnimationSamplers.Add(MakeShared<FJsonValueObject>(JsonAnimationScaleSampler));

					TSharedRef<FJsonObject> JsonAnimationScaleChannel = MakeShared<FJsonObject>();
					JsonAnimationScaleChannel->SetNumberField("sampler", SamplerIndex);

					TSharedRef<FJsonObject> JsonAnimationScaleChannelTarget = MakeShared<FJsonObject>();
					JsonAnimationScaleChannelTarget->SetNumberField("node", SkeletonRef.FindBoneIndex(Tracks[TrackIndex]));
					JsonAnimationScaleChannelTarget->SetStringField("path", "scale");

					JsonAnimationScaleChannel->SetObjectField("target", JsonAnimationScaleChannelTarget);

					JsonAnimationChannels.Add(MakeShared<FJsonValueObject>(JsonAnimationScaleChannel));
				}*/
			}

			for (FSmartName SmartName : AnimSequence->GetCompressedCurveNames())
			{
				UE_LOG(LogTemp, Warning, TEXT("SmartName %s"), *SmartName.DisplayName.ToString());
			}

			JsonAnimation->SetArrayField("channels", JsonAnimationChannels);
			JsonAnimation->SetArrayField("samplers", JsonAnimationSamplers);

			JsonAnimations.Add(MakeShared<FJsonValueObject>(JsonAnimation));
		}
	}

	return true;
}

bool FglTFRuntimeWriter::WriteToFile(const FString& Filename)
{
	TArray<TSharedPtr<FJsonValue>> JsonScenes;
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

	for (const TPair<int64, int64>& Pair : ImagesBuffers)
	{
		TSharedRef<FJsonObject> JsonBufferView = MakeShared<FJsonObject>();
		JsonBufferView->SetNumberField("buffer", 0);
		JsonBufferView->SetNumberField("byteOffset", Pair.Key);
		JsonBufferView->SetNumberField("byteLength", Pair.Value);

		int32 BufferViewIndex = JsonBufferViews.Add(MakeShared<FJsonValueObject>(JsonBufferView));

		TSharedRef<FJsonObject> JsonImage = MakeShared<FJsonObject>();
		JsonImage->SetNumberField("bufferView", BufferViewIndex);
		JsonImage->SetStringField("mimeType", "image/png");

		int32 ImageIndex = JsonImages.Add(MakeShared<FJsonValueObject>(JsonImage));

		TSharedRef<FJsonObject> JsonTexture = MakeShared<FJsonObject>();
		JsonTexture->SetNumberField("source", ImageIndex);

		JsonTextures.Add(MakeShared<FJsonValueObject>(JsonTexture));
	}

	TSharedRef<FJsonObject> JsonNode = MakeShared<FJsonObject>();
	JsonNode->SetStringField("name", "Mesh");
	JsonNode->SetNumberField("mesh", 0);
	if (Config.bExportSkin)
	{
		JsonNode->SetNumberField("skin", 0);
	}

	int32 JsonNodeIndex = JsonNodes.Add(MakeShared<FJsonValueObject>(JsonNode));

	int32 JsonParentNodeIndex = INDEX_NONE;

	if (Config.bAddParentNode)
	{
		TSharedRef<FJsonObject> JsonParentNode = MakeShared<FJsonObject>();

		TArray<TSharedPtr<FJsonValue>> JsonParentNodeTranslation;
		TArray<TSharedPtr<FJsonValue>> JsonParentNodeRotation;
		TArray<TSharedPtr<FJsonValue>> JsonParentNodeScale;

		FMatrix Basis = FBasisVectorMatrix(FVector(1, 0, 0), FVector(0, 0, 1), FVector(0, 1, 0), FVector::ZeroVector);
		FMatrix Matrix = Basis.Inverse() * Config.ParentNodeTransform.ToMatrixWithScale() * Basis;
		Matrix.ScaleTranslation(FVector::OneVector / 100);

		FTransform ParentNodeTransform = FTransform(Matrix);

		FVector ParentNodeTranslation = ParentNodeTransform.GetLocation();
		FQuat ParentNodeRotation = ParentNodeTransform.GetRotation();
		FVector ParentNodeScale = ParentNodeTransform.GetScale3D();

		JsonParentNodeTranslation.Add(MakeShared<FJsonValueNumber>(ParentNodeTranslation.X));
		JsonParentNodeTranslation.Add(MakeShared<FJsonValueNumber>(ParentNodeTranslation.Y));
		JsonParentNodeTranslation.Add(MakeShared<FJsonValueNumber>(ParentNodeTranslation.Z));

		JsonParentNodeRotation.Add(MakeShared<FJsonValueNumber>(ParentNodeRotation.X));
		JsonParentNodeRotation.Add(MakeShared<FJsonValueNumber>(ParentNodeRotation.Y));
		JsonParentNodeRotation.Add(MakeShared<FJsonValueNumber>(ParentNodeRotation.Z));
		JsonParentNodeRotation.Add(MakeShared<FJsonValueNumber>(ParentNodeRotation.W));

		JsonParentNodeScale.Add(MakeShared<FJsonValueNumber>(ParentNodeScale.X));
		JsonParentNodeScale.Add(MakeShared<FJsonValueNumber>(ParentNodeScale.Y));
		JsonParentNodeScale.Add(MakeShared<FJsonValueNumber>(ParentNodeScale.Z));

		JsonParentNode->SetStringField("name", "Parent");

		JsonParentNode->SetArrayField("translation", JsonParentNodeTranslation);
		JsonParentNode->SetArrayField("rotation", JsonParentNodeRotation);
		JsonParentNode->SetArrayField("scale", JsonParentNodeScale);

		TArray<TSharedPtr<FJsonValue>> JsonParentNodeChildren;
		JsonParentNodeChildren.Add(MakeShared<FJsonValueNumber>(JsonNodeIndex));

		JsonParentNode->SetArrayField("children", JsonParentNodeChildren);

		JsonParentNodeIndex = JsonNodes.Add(MakeShared<FJsonValueObject>(JsonParentNode));
	}

	TSharedRef<FJsonObject> JsonScene = MakeShared<FJsonObject>();
	TArray<TSharedPtr<FJsonValue>> JsonSceneNodes;
	if (Config.bExportSkin)
	{
		JsonSceneNodes.Add(MakeShared<FJsonValueNumber>(0));
	}

	JsonSceneNodes.Add(MakeShared<FJsonValueNumber>(JsonParentNodeIndex != INDEX_NONE ? JsonParentNodeIndex : JsonNodeIndex));
	JsonScene->SetArrayField("nodes", JsonSceneNodes);

	JsonScenes.Add(MakeShared<FJsonValueObject>(JsonScene));

	JsonRoot->SetArrayField("scenes", JsonScenes);
	JsonRoot->SetArrayField("nodes", JsonNodes);
	JsonRoot->SetArrayField("accessors", JsonAccessors);
	JsonRoot->SetArrayField("bufferViews", JsonBufferViews);
	JsonRoot->SetArrayField("buffers", JsonBuffers);
	JsonRoot->SetArrayField("meshes", JsonMeshes);
	if (JsonAnimations.Num() > 0)
	{
		JsonRoot->SetArrayField("animations", JsonAnimations);
	}
	JsonRoot->SetArrayField("images", JsonImages);
	JsonRoot->SetArrayField("textures", JsonTextures);
	JsonRoot->SetArrayField("materials", JsonMaterials);

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