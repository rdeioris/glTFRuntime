#include "glTFRuntimeParser.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#if WITH_EDITOR
#include "IMeshBuilderModule.h"
#include "LODUtilities.h"
#include "MeshUtilities.h"
#endif
#include "Engine/SkeletalMeshSocket.h"
#include "glTfAnimBoneCompressionCodec.h"

void FglTFRuntimeParser::NormalizeSkeletonScale(FReferenceSkeleton& RefSkeleton)
{
	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);
	NormalizeSkeletonBoneScale(Modifier, 0, FVector::OneVector);
}

void FglTFRuntimeParser::NormalizeSkeletonBoneScale(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FVector BoneScale)
{
	TArray<FTransform> BonesTransforms = Modifier.GetReferenceSkeleton().GetRefBonePose();

	FTransform BoneTransform = BonesTransforms[BoneIndex];
	FVector ParentScale = BoneTransform.GetScale3D();
	BoneTransform.ScaleTranslation(BoneScale);
	BoneTransform.SetScale3D(FVector::OneVector);

	Modifier.UpdateRefPoseTransform(BoneIndex, BoneTransform);

	TArray<FMeshBoneInfo> MeshBoneInfos = Modifier.GetRefBoneInfo();
	for (int32 MeshBoneIndex = 0; MeshBoneIndex < MeshBoneInfos.Num(); MeshBoneIndex++)
	{
		FMeshBoneInfo& MeshBoneInfo = MeshBoneInfos[MeshBoneIndex];
		if (MeshBoneInfo.ParentIndex == BoneIndex)
		{
			NormalizeSkeletonBoneScale(Modifier, MeshBoneIndex, ParentScale * BoneScale);
		}
	}
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, TSharedRef<FJsonObject> JsonSkinObject, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{

	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives, SkeletalMeshConfig.MaterialsConfig))
		return nullptr;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Public);

	TMap<int32, FName> BoneMap;

	if (!FillReferenceSkeleton(JsonSkinObject, SkeletalMesh->RefSkeleton, BoneMap, SkeletalMeshConfig))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to fill skeleton!"));
		return nullptr;
	}

	//NormalizeSkeletonScale(SkeletalMesh->RefSkeleton);

	TArray<FVector> Points;
	TArray<int32> PointToRawMap;
	int32 MatIndex = 0;
	TMap<int32, int32> BonesCache;

#if WITH_EDITOR
	TArray<SkeletalMeshImportData::FVertex> Wedges;
	TArray<SkeletalMeshImportData::FTriangle> Triangles;
	TArray<SkeletalMeshImportData::FRawBoneInfluence> Influences;


	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		int32 Base = Points.Num();
		Points.Append(Primitive.Positions);

		int32 TriangleIndex = 0;
		TSet<TPair<int32, int32>> InfluencesMap;

		for (int32 i = 0; i < Primitive.Indices.Num(); i++)
		{
			int32 PrimitiveIndex = Primitive.Indices[i];

			SkeletalMeshImportData::FVertex Wedge;
			Wedge.VertexIndex = Base + PrimitiveIndex;

			for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
			{
				Wedge.UVs[UVIndex] = Primitive.UVs[UVIndex][PrimitiveIndex];
			}

			int32 WedgeIndex = Wedges.Add(Wedge);

			for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
			{
				FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][PrimitiveIndex];
				FVector4 Weights = Primitive.Weights[JointsIndex][PrimitiveIndex];
				// 4 bones for each joints list
				for (int32 JointPartIndex = 0; JointPartIndex < 4; JointPartIndex++)
				{
					if (BoneMap.Contains(Joints[JointPartIndex]))
					{
						SkeletalMeshImportData::FRawBoneInfluence Influence;
						Influence.VertexIndex = Wedge.VertexIndex;
						int32 BoneIndex = INDEX_NONE;
						if (BonesCache.Contains(Joints[JointPartIndex]))
						{
							BoneIndex = BonesCache[Joints[JointPartIndex]];
						}
						else
						{
							BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMap[Joints[JointPartIndex]]);
							BonesCache.Add(Joints[JointPartIndex], BoneIndex);
						}
						Influence.BoneIndex = BoneIndex;
						Influence.Weight = Weights[JointPartIndex];
						TPair<int32, int32> InfluenceKey = TPair<int32, int32>(Influence.VertexIndex, Influence.BoneIndex);
						// do not waste cpu time processing zero influences
						if (!FMath::IsNearlyZero(Influence.Weight, KINDA_SMALL_NUMBER) && !InfluencesMap.Contains(InfluenceKey))
						{
							Influences.Add(Influence);
							InfluencesMap.Add(InfluenceKey);
						}
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Unable to find map for bone %u"), Joints[JointPartIndex]);
						return nullptr;
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

				if (Primitive.Tangents.Num() > 0)
				{
					Triangle.TangentX[0] = Primitive.Tangents[Primitive.Indices[i - 2]];
					Triangle.TangentX[1] = Primitive.Tangents[Primitive.Indices[i - 1]];
					Triangle.TangentX[2] = Primitive.Tangents[Primitive.Indices[i]];
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

	FLODUtilities::ProcessImportMeshInfluences(Wedges.Num(), Influences);

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
#else
	FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
	SkeletalMesh->AllocateResourceForRendering();
	SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);

	LodRenderData->RenderSections.SetNumUninitialized(Primitives.Num());

	int32 NumIndices = 0;
	for (int32 i = 0; i < Primitives.Num(); i++)
	{
		NumIndices += Primitives[i].Indices.Num();
	}

	LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(NumIndices);
	LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(NumIndices, 1);

	for (TPair<int32, FName>& Pair : BoneMap)
	{
		int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(Pair.Value);
		if (BoneIndex > INDEX_NONE)
		{
			LodRenderData->RequiredBones.Add(BoneIndex);
			LodRenderData->ActiveBoneIndices.Add(BoneIndex);
		}
	}

	TArray<FSkinWeightInfo> InWeights;
	InWeights.AddUninitialized(NumIndices);

	int32 TotalVertexIndex = 0;

	for (int32 i = 0; i < Primitives.Num(); i++)
	{
		FglTFRuntimePrimitive& Primitive = Primitives[i];

		int32 Base = Points.Num();
		Points.Append(Primitive.Positions);

		new(&LodRenderData->RenderSections[i]) FSkelMeshRenderSection();
		FSkelMeshRenderSection& MeshSection = LodRenderData->RenderSections[i];

		MeshSection.MaterialIndex = i;
		MeshSection.BaseIndex = TotalVertexIndex;
		MeshSection.NumTriangles = Primitive.Indices.Num() / 3;
		MeshSection.BaseVertexIndex = Base;
		MeshSection.MaxBoneInfluences = 4;

		MeshSection.NumVertices = Primitive.Positions.Num();

		TMap<int32, TArray<int32>> OverlappingVertices;
		MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);

		for (int32 VertexIndex = 0; VertexIndex < Primitive.Indices.Num(); VertexIndex++)
		{
			int32 Index = Primitive.Indices[VertexIndex];
			FModelVertex ModelVertex;
			ModelVertex.Position = Primitive.Positions[Index];
			ModelVertex.TangentX = FVector::ZeroVector;
			ModelVertex.TangentZ = FVector::ZeroVector;
			if (Index < Primitive.Normals.Num())
			{
				ModelVertex.TangentZ = Primitive.Normals[Index];
			}
			if (Index < Primitive.Tangents.Num())
			{
				ModelVertex.TangentX = Primitive.Tangents[Index];
			}
			if (Primitive.UVs.Num() > 0 && Index < Primitive.UVs[0].Num())
			{
				ModelVertex.TexCoord = Primitive.UVs[0][Index];
			}
			else
			{
				ModelVertex.TexCoord = FVector2D::ZeroVector;
			}

			LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TotalVertexIndex) = ModelVertex.Position;
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(TotalVertexIndex, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(TotalVertexIndex, 0, ModelVertex.TexCoord);

			for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
			{
				FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][Index];
				FVector4 Weights = Primitive.Weights[JointsIndex][Index];

				for (int32 j = 0; j < 4; j++)
				{
					if (BoneMap.Contains(Joints[j]))
					{
						int32 BoneIndex = INDEX_NONE;
						if (BonesCache.Contains(Joints[j]))
						{
							BoneIndex = BonesCache[Joints[j]];
						}
						else
						{
							BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMap[Joints[j]]);
							BonesCache.Add(Joints[j], BoneIndex);
						}
						InWeights[TotalVertexIndex].InfluenceWeights[j] = Weights[j] * 255;
						InWeights[TotalVertexIndex].InfluenceBones[j] = BoneIndex;
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Unable to find map for bone %u"), Joints[j]);
						return nullptr;
					}
				}
			}

			TotalVertexIndex++;
		}

		for (TPair<int32, FName>& Pair : BoneMap)
		{
			int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(Pair.Value);
			if (BoneIndex > INDEX_NONE)
			{
				MeshSection.BoneMap.Add(BoneIndex);
			}
		}
	}

	LodRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
	LodRenderData->SkinWeightVertexBuffer = InWeights;
	LodRenderData->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint32_t));

	for (int32 Index = 0; Index < NumIndices; Index++)
	{
		LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(Index);
	}
#endif

	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LODInfo.BuildSettings.bRecomputeNormals = false;
	LODInfo.LODHysteresis = 0.02f;

	SkeletalMesh->CalculateInvRefMatrices();

	FBox BoundingBox(Points.GetData(), Points.Num());
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

	SkeletalMesh->bHasVertexColors = false;
#if WITH_EDITOR
	SkeletalMesh->VertexColorGuid = SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();
#endif

	for (MatIndex = 0; MatIndex < Primitives.Num(); MatIndex++)
	{
		LODInfo.LODMaterialMap.Add(MatIndex);
		SkeletalMesh->Materials.Add(Primitives[MatIndex].Material);
		SkeletalMesh->Materials[MatIndex].UVChannelData.bInitialized = true;
	}

#if WITH_EDITOR
	IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMesh, 0, false))
		return nullptr;

	SkeletalMesh->Build();
#endif

	SkeletalMesh->Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	for (const TPair<FString, FglTFRuntimeSocket>& Pair : SkeletalMeshConfig.Sockets)
	{
		USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMesh->Skeleton);
		SkeletalSocket->SocketName = FName(Pair.Key);
		SkeletalSocket->BoneName = FName(Pair.Value.BoneName);
		SkeletalSocket->RelativeLocation = Pair.Value.Transform.GetLocation();
		SkeletalSocket->RelativeRotation = Pair.Value.Transform.GetRotation().Rotator();
		SkeletalSocket->RelativeScale = Pair.Value.Transform.GetScale3D();
		SkeletalMesh->Skeleton->Sockets.Add(SkeletalSocket);
	}



#if !WITH_EDITOR
	SkeletalMesh->PostLoad();
#endif

	return SkeletalMesh;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	if (MeshIndex < 0)
		return nullptr;

	// first check cache
	if (CanReadFromCache(SkeletalMeshConfig.CacheMode) && SkeletalMeshesCache.Contains(MeshIndex))
	{
		return SkeletalMeshesCache[MeshIndex];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	if (MeshIndex >= JsonMeshes->Num())
	{
		UE_LOG(LogTemp, Error, TEXT("unable to find mesh %d"), MeshIndex);
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonSkins;
	// no skins ?
	if (!Root->TryGetArrayField("skins", JsonSkins))
	{
		UE_LOG(LogTemp, Error, TEXT("unable to find skin %d"), MeshIndex);
		return nullptr;
	}

	if (SkinIndex >= JsonSkins->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[MeshIndex]->AsObject();
	if (!JsonMeshObject)
		return nullptr;

	TSharedPtr<FJsonObject> JsonSkinObject = (*JsonSkins)[SkinIndex]->AsObject();
	if (!JsonSkinObject)
		return nullptr;

	USkeletalMesh* SkeletalMesh = LoadSkeletalMesh_Internal(JsonMeshObject.ToSharedRef(), JsonSkinObject.ToSharedRef(), SkeletalMeshConfig);
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load skeletal mesh"));
		return nullptr;
	}

	if (CanWriteToCache(SkeletalMeshConfig.CacheMode))
	{
		SkeletalMeshesCache.Add(MeshIndex, SkeletalMesh);
	}

	return SkeletalMesh;
}

UAnimSequence* FglTFRuntimeParser::LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString AnimationName, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return nullptr;
	}

	for (int32 AnimationIndex = 0; AnimationIndex, JsonAnimations->Num(); AnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[AnimationIndex]->AsObject();
		if (!JsonAnimationObject)
		{
			return nullptr;
		}

		FString JsonAnimationName;
		if (JsonAnimationObject->TryGetStringField("name", JsonAnimationName))
		{
			if (JsonAnimationName == AnimationName)
			{
				return LoadSkeletalAnimation(SkeletalMesh, AnimationIndex, AnimationConfig);
			}
		}
	}

	return nullptr;
}

UAnimSequence* FglTFRuntimeParser::LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return nullptr;
	}

	if (AnimationIndex >= JsonAnimations->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[AnimationIndex]->AsObject();
	if (!JsonAnimationObject)
	{
		return nullptr;
	}

	float Duration;
	TMap<FString, FRawAnimSequenceTrack> Tracks;
	if (!LoadSkeletalAnimation_Internal(JsonAnimationObject.ToSharedRef(), Tracks, Duration))
	{
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
	AnimSequence->SetPreviewMesh(SkeletalMesh);
	AnimSequence->SetRawNumberOfFrame(Duration / 30);
	AnimSequence->SequenceLength = Duration;
	AnimSequence->bEnableRootMotion = AnimationConfig.bRootMotion;

	const TArray<FTransform> BonesPoses = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetRefBonePose();

	/*
	bool bTrueRootFound = false;

	for (TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		FName BoneName = FName(Pair.Key);
		int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex == 1)
		{
			bTrueRootFound = true;
			break;
		}
	}

	if (!bTrueRootFound)
	{
		return nullptr;
	}

	FRawAnimSequenceTrack RootTrack;
	for (int32 FrameIndex = 0; FrameIndex < Tracks["Hips"].RotKeys.Num(); FrameIndex++)
	{
		RootTrack.PosKeys.Add(Tracks["Hips"].PosKeys[FrameIndex]);
		RootTrack.RotKeys.Add(FQuat::Identity);
		RootTrack.ScaleKeys.Add(FVector(1, 1, 1));

		Tracks["Hips"].PosKeys[FrameIndex] = Tracks["Hips"].PosKeys[0];
	}*/
	//Tracks.Add("Root", RootTrack);

#if !WITH_EDITOR
	UglTFAnimBoneCompressionCodec* CompressionCodec = NewObject<UglTFAnimBoneCompressionCodec>();
	CompressionCodec->Tracks.AddDefaulted(Tracks.Num());
#endif

	// tracks here will be already sanitized
	for (TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		FName BoneName = FName(Pair.Key);
		int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to find bone %s"), *Pair.Key);
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("Found %s at %d"), *BoneName.ToString(), BoneIndex);

		if (BoneIndex == 0)
		{
			if (AnimationConfig.RootNodeIndex > INDEX_NONE)
			{
				FglTFRuntimeNode AnimRootNode;
				if (!LoadNode(AnimationConfig.RootNodeIndex, AnimRootNode))
				{
					return nullptr;
				}
				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
					FVector Pos = Pair.Value.PosKeys[FrameIndex];
					FQuat Quat = Pair.Value.RotKeys[FrameIndex];
					FVector Scale = Pair.Value.ScaleKeys[FrameIndex];

					FTransform FrameTransform = FTransform(Quat, Pos, Scale) * AnimRootNode.Transform;

					Pair.Value.PosKeys[FrameIndex] = FrameTransform.GetLocation();
					Pair.Value.RotKeys[FrameIndex] = FrameTransform.GetRotation();
					Pair.Value.ScaleKeys[FrameIndex] = FrameTransform.GetScale3D();
				}
			}

			if (AnimationConfig.bRemoveRootMotion)
			{
				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
					Pair.Value.PosKeys[FrameIndex] = Pair.Value.PosKeys[0];
				}
			}
		}

#if WITH_EDITOR
		AnimSequence->AddNewRawTrack(BoneName, &Pair.Value);
#else
		CompressionCodec->Tracks[BoneIndex] = Pair.Value;
		AnimSequence->CompressedData.CompressedTrackToSkeletonMapTable.Add(FTrackToSkeletonMap(BoneIndex));
#endif

	}

#if WITH_EDITOR
	AnimSequence->PostProcessSequence();
#else
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
}

bool FglTFRuntimeParser::LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>& Tracks, float& Duration)
{

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)
	{
		int32 NumFrames = Duration * 30;

		float FrameDelta = 1.f / 30;

		if (Path == "rotation")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 FirstQuatV = Values[FirstIndex];
				FVector4 SecondQuatV = Values[SecondIndex];
				FQuat FirstQuat = { FirstQuatV.X, FirstQuatV.Y, FirstQuatV.Z, FirstQuatV.W };
				FQuat SecondQuat = { SecondQuatV.X, SecondQuatV.Y, SecondQuatV.Z, SecondQuatV.W };
				FMatrix FirstMatrix = SceneBasis.Inverse() * FRotationMatrix(FirstQuat.Rotator()) * SceneBasis;
				FMatrix SecondMatrix = SceneBasis.Inverse() * FRotationMatrix(SecondQuat.Rotator()) * SceneBasis;
				FirstQuat = FirstMatrix.ToQuat();
				SecondQuat = SecondMatrix.ToQuat();
				FQuat AnimQuat = FMath::Lerp(FirstQuat, SecondQuat, Alpha);
				Track.RotKeys.Add(AnimQuat);
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "translation")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			//UE_LOG(LogTemp, Error, TEXT("Found translation for %s"), *Node.Name);

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Values[FirstIndex];
				FVector4 Second = Values[SecondIndex];
				FVector AnimLocation = SceneBasis.TransformPosition(FMath::Lerp(First, Second, Alpha)) * SceneScale;
				Track.PosKeys.Add(AnimLocation);
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "scale")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			//UE_LOG(LogTemp, Error, TEXT("Found translation for %s"), *Node.Name);

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Values[FirstIndex];
				FVector4 Second = Values[SecondIndex];
				Track.ScaleKeys.Add((SceneBasis.Inverse() * FScaleMatrix(FMath::Lerp(First, Second, Alpha)) * SceneBasis).ExtractScaling());
				FrameBase += FrameDelta;
			}
		}
	};

	FString IgnoredName;
	return LoadAnimation_Internal(JsonAnimationObject, Duration, IgnoredName, Callback, [](const FglTFRuntimeNode& Node) -> bool { return true; });
}

