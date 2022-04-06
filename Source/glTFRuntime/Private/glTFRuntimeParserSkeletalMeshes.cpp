// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#if ENGINE_MAJOR_VERSION > 4
#include "Animation/AnimData/AnimDataModel.h"
#endif
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#if WITH_EDITOR
#include "IMeshBuilderModule.h"
#include "LODUtilities.h"
#include "MeshUtilities.h"
#include "AssetRegistryModule.h"
#endif
#include "Engine/SkeletalMeshSocket.h"
#include "glTFAnimBoneCompressionCodec.h"
#include "Animation/AnimCurveCompressionCodec_CompressedRichCurve.h"
#include "Model.h"
#include "Animation/MorphTarget.h"
#include "Async/Async.h"
#include "Animation/AnimCurveTypes.h"
#include "PhysicsEngine/PhysicsAsset.h"
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
#include "UObject/SavePackage.h"
#endif

struct FglTFRuntimeSkeletalMeshContextFinalizer
{
	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext;
	FglTFRuntimeSkeletalMeshAsync AsyncCallback;

	FglTFRuntimeSkeletalMeshContextFinalizer(TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> InSkeletalMeshContext, FglTFRuntimeSkeletalMeshAsync InAsyncCallback) :
		SkeletalMeshContext(InSkeletalMeshContext),
		AsyncCallback(InAsyncCallback)
	{
	}

	~FglTFRuntimeSkeletalMeshContextFinalizer()
	{
		FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this]()
			{
				if (SkeletalMeshContext->SkeletalMesh)
				{
					SkeletalMeshContext->SkeletalMesh = SkeletalMeshContext->Parser->FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
				}
				AsyncCallback.ExecuteIfBound(SkeletalMeshContext->SkeletalMesh);
			}, TStatId(), nullptr, ENamedThreads::GameThread);
		FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
	}
};

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

void FglTFRuntimeParser::ClearSkeletonRotations(FReferenceSkeleton& RefSkeleton)
{
	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);
	ApplySkeletonBoneRotation(Modifier, 0, FQuat::Identity);
}

void FglTFRuntimeParser::ApplySkeletonBoneRotation(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FQuat ParentRotation)
{
	TArray<FTransform> BonesTransforms = Modifier.GetReferenceSkeleton().GetRefBonePose();

	FTransform NewTransform = BonesTransforms[BoneIndex];
	NewTransform.SetLocation(ParentRotation * NewTransform.GetLocation());

	ParentRotation *= NewTransform.GetRotation();
	NewTransform.SetRotation(FQuat::Identity);

	Modifier.UpdateRefPoseTransform(BoneIndex, NewTransform);

	TArray<FMeshBoneInfo> MeshBoneInfos = Modifier.GetRefBoneInfo();
	for (int32 MeshBoneIndex = 0; MeshBoneIndex < MeshBoneInfos.Num(); MeshBoneIndex++)
	{
		FMeshBoneInfo& MeshBoneInfo = MeshBoneInfos[MeshBoneIndex];
		if (MeshBoneInfo.ParentIndex == BoneIndex)
		{
			ApplySkeletonBoneRotation(Modifier, MeshBoneIndex, ParentRotation);
		}
	}
}

void FglTFRuntimeParser::CopySkeletonRotationsFrom(FReferenceSkeleton& RefSkeleton, const FReferenceSkeleton& SrcRefSkeleton)
{
	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	const TArray<FTransform>& BonesTransforms = Modifier.GetReferenceSkeleton().GetRefBonePose();
	const TArray<FTransform>& SrcBonesTransforms = SrcRefSkeleton.GetRefBonePose();

	for (int32 BoneIndex = 0; BoneIndex < RefSkeleton.GetNum(); BoneIndex++)
	{
		FName BoneName = RefSkeleton.GetBoneName(BoneIndex);
		FTransform NewTransform = BonesTransforms[BoneIndex];

		int32 SrcBoneIndex = SrcRefSkeleton.FindBoneIndex(BoneName);
		// no bone found, find the first available parent
		if (SrcBoneIndex <= INDEX_NONE)
		{
			int32 ParentIndex = RefSkeleton.GetParentIndex(BoneIndex);
			if (ParentIndex > INDEX_NONE)
			{
				FName ParentBoneName = RefSkeleton.GetBoneName(ParentIndex);
				SrcBoneIndex = SrcRefSkeleton.FindBoneIndex(ParentBoneName);
				while (SrcBoneIndex <= INDEX_NONE)
				{
					ParentIndex = RefSkeleton.GetParentIndex(ParentIndex);
					if (ParentIndex > INDEX_NONE)
					{
						ParentBoneName = RefSkeleton.GetBoneName(ParentIndex);
						SrcBoneIndex = SrcRefSkeleton.FindBoneIndex(ParentBoneName);
					}
					else
					{
						break;
					}
				}
			}
		}

		if (SrcBoneIndex > INDEX_NONE)
		{
			NewTransform.SetRotation(SrcBonesTransforms[SrcBoneIndex].GetRotation());
			int32 SrcParentIndex = SrcRefSkeleton.GetParentIndex(SrcBoneIndex);
			FQuat AllRotations = FQuat::Identity;
			while (SrcParentIndex > INDEX_NONE)
			{
				AllRotations = SrcBonesTransforms[SrcParentIndex].GetRotation() * AllRotations;
				SrcParentIndex = SrcRefSkeleton.GetParentIndex(SrcParentIndex);
			}
			NewTransform.SetLocation(AllRotations.Inverse() * NewTransform.GetLocation());
			Modifier.UpdateRefPoseTransform(BoneIndex, NewTransform);
		}
	}
}

USkeletalMesh* FglTFRuntimeParser::CreateSkeletalMeshFromLODs(TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext)
{
#if ENGINE_MAJOR_VERSION > 4
	SkeletalMeshContext->SkeletalMesh->SetEnablePerPolyCollision(SkeletalMeshContext->SkeletalMeshConfig.bPerPolyCollision);
#else
	SkeletalMeshContext->SkeletalMesh->bEnablePerPolyCollision = SkeletalMeshContext->SkeletalMeshConfig.bPerPolyCollision;
#endif

	if (SkeletalMeshContext->SkeletalMeshConfig.OverrideSkinIndex > INDEX_NONE)
	{
		SkeletalMeshContext->SkinIndex = SkeletalMeshContext->SkeletalMeshConfig.OverrideSkinIndex;
	}

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	FReferenceSkeleton& RefSkeleton = SkeletalMeshContext->SkeletalMesh->GetRefSkeleton();
#else
	FReferenceSkeleton& RefSkeleton = SkeletalMeshContext->SkeletalMesh->RefSkeleton;
#endif

	TMap<int32, FName> MainBoneMap;
	if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreSkin && SkeletalMeshContext->SkinIndex > INDEX_NONE)
	{
		TSharedPtr<FJsonObject>	JsonSkinObject = GetJsonObjectFromRootIndex("skins", SkeletalMeshContext->SkinIndex);
		if (!JsonSkinObject)
		{
			AddError("CreateSkeletalMeshFromLODs()", "Unable to fill RefSkeleton.");
			return nullptr;
		}

		if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), RefSkeleton, MainBoneMap, SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig))
		{
			AddError("CreateSkeletalMeshFromLODs()", "Unable to fill RefSkeleton.");
			return nullptr;
		}
	}
	else
	{
		if (!FillFakeSkeleton(RefSkeleton, MainBoneMap, SkeletalMeshContext->SkeletalMeshConfig))
		{
			AddError("CreateSkeletalMeshFromLODs()", "Unable to fill fake RefSkeleton.");
			return nullptr;
		}
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.bNormalizeSkeletonScale)
	{
		NormalizeSkeletonScale(RefSkeleton);
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.bClearRotations || SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CopyRotationsFrom)
	{
		ClearSkeletonRotations(RefSkeleton);
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CopyRotationsFrom)
	{
		CopySkeletonRotationsFrom(RefSkeleton, SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CopyRotationsFrom->GetReferenceSkeleton());
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.Skeleton && SkeletalMeshContext->SkeletalMeshConfig.bOverwriteRefSkeleton)
	{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		SkeletalMeshContext->SkeletalMesh->SetRefSkeleton(SkeletalMeshContext->SkeletalMeshConfig.Skeleton->GetReferenceSkeleton());
#else
		SkeletalMeshContext->SkeletalMesh->RefSkeleton = SkeletalMeshContext->SkeletalMeshConfig.Skeleton->GetReferenceSkeleton();
#endif
	}

	TMap<int32, int32> MainBonesCache;
	int32 MatIndex = 0;

	SkeletalMeshContext->SkeletalMesh->ResetLODInfo();

#if WITH_EDITOR

	FSkeletalMeshModel* ImportedResource = SkeletalMeshContext->SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();

	for (FglTFRuntimeLOD& LOD : SkeletalMeshContext->LODs)
	{

		TArray<SkeletalMeshImportData::FVertex> Wedges;
		TArray<SkeletalMeshImportData::FTriangle> Triangles;
		TArray<SkeletalMeshImportData::FRawBoneInfluence> Influences;

#if ENGINE_MAJOR_VERSION > 4
		TArray<FVector3f> Points;
#else
		TArray<FVector> Points;
#endif

		for (FglTFRuntimePrimitive& Primitive : LOD.Primitives)
		{
			int32 Base = Points.Num();
			Points.Append(Primitive.Positions);

			int32 TriangleIndex = 0;
			TSet<TPair<int32, int32>> InfluencesMap;

			TMap<int32, FName>& BoneMapInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.OverrideBoneMap : MainBoneMap;
			TMap<int32, int32>& BonesCacheInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.BonesCache : MainBonesCache;

			for (int32 i = 0; i < Primitive.Indices.Num(); i++)
			{
				int32 PrimitiveIndex = Primitive.Indices[i];

				SkeletalMeshImportData::FVertex Wedge;
				Wedge.VertexIndex = Base + PrimitiveIndex;

				for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
				{
#if ENGINE_MAJOR_VERSION > 4
					Wedge.UVs[UVIndex] = FVector2f(Primitive.UVs[UVIndex][PrimitiveIndex]);
#else
					Wedge.UVs[UVIndex] = Primitive.UVs[UVIndex][PrimitiveIndex];
#endif
				}

				int32 WedgeIndex = Wedges.Add(Wedge);

				if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreSkin && SkeletalMeshContext->SkinIndex > INDEX_NONE)
				{
					for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
					{
						FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][PrimitiveIndex];
						FVector4 Weights = Primitive.Weights[JointsIndex][PrimitiveIndex];
						// 4 bones for each joints list
						for (int32 JointPartIndex = 0; JointPartIndex < 4; JointPartIndex++)
						{
							if (BoneMapInUse.Contains(Joints[JointPartIndex]))
							{
								SkeletalMeshImportData::FRawBoneInfluence Influence;
								Influence.VertexIndex = Wedge.VertexIndex;
								int32 BoneIndex = INDEX_NONE;
								if (BonesCacheInUse.Contains(Joints[JointPartIndex]))
								{
									BoneIndex = BonesCacheInUse[Joints[JointPartIndex]];
								}
								else
								{
#if ENGINE_MAJOR_VERSION > 4
									BoneIndex = SkeletalMeshContext->SkeletalMesh->GetRefSkeleton().FindBoneIndex(BoneMapInUse[Joints[JointPartIndex]]);
#else
									BoneIndex = SkeletalMeshContext->SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMapInUse[Joints[JointPartIndex]]);
#endif
									BonesCacheInUse.Add(Joints[JointPartIndex], BoneIndex);
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
							else if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreMissingBones)
							{
								AddError("LoadSkeletalMesh_Internal()", FString::Printf(TEXT("Unable to find map for bone %u"), Joints[JointPartIndex]));
								return nullptr;
							}
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
#if ENGINE_MAJOR_VERSION > 4
						Triangle.TangentZ[0] = FVector3f(Primitive.Normals[Primitive.Indices[i - 2]]);
						Triangle.TangentZ[1] = FVector3f(Primitive.Normals[Primitive.Indices[i - 1]]);
						Triangle.TangentZ[2] = FVector3f(Primitive.Normals[Primitive.Indices[i]]);
#else
						Triangle.TangentZ[0] = Primitive.Normals[Primitive.Indices[i - 2]];
						Triangle.TangentZ[1] = Primitive.Normals[Primitive.Indices[i - 1]];
						Triangle.TangentZ[2] = Primitive.Normals[Primitive.Indices[i]];
#endif
						LOD.bHasNormals = true;
					}
					else
					{
						FVector Position0 = Primitive.Positions[Primitive.Indices[i - 2]];
						FVector Position1 = Primitive.Positions[Primitive.Indices[i - 1]];
						FVector Position2 = Primitive.Positions[Primitive.Indices[i]];
						FVector SideA = Position1 - Position0;
						FVector SideB = Position2 - Position0;

						FVector NormalFromCross = FVector::CrossProduct(SideB, SideA).GetSafeNormal();
#if ENGINE_MAJOR_VERSION > 4
						Triangle.TangentZ[0] = FVector3f(NormalFromCross);
						Triangle.TangentZ[1] = FVector3f(NormalFromCross);
						Triangle.TangentZ[2] = FVector3f(NormalFromCross);
#else
						Triangle.TangentZ[0] = NormalFromCross;
						Triangle.TangentZ[1] = NormalFromCross;
						Triangle.TangentZ[2] = NormalFromCross;
#endif
						LOD.bHasNormals = true;
					}

					if (Primitive.Tangents.Num() > 0)
					{
#if ENGINE_MAJOR_VERSION > 4
						Triangle.TangentX[0] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i - 2]]));
						Triangle.TangentX[1] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i - 1]]));
						Triangle.TangentX[2] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i]]));
#else
						Triangle.TangentX[0] = Primitive.Tangents[Primitive.Indices[i - 2]];
						Triangle.TangentX[1] = Primitive.Tangents[Primitive.Indices[i - 1]];
						Triangle.TangentX[2] = Primitive.Tangents[Primitive.Indices[i]];
#endif
						LOD.bHasTangents = true;
					}

					Triangle.MatIndex = MatIndex;

					Triangles.Add(Triangle);
					TriangleIndex = 0;
				}
			}

			MatIndex++;
		}

		TArray<int32> PointToRawMap;
		for (int32 PointIndex = 0; PointIndex < Points.Num(); PointIndex++)
		{
			// update boundingbox
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->BoundingBox += FVector(Points[PointIndex]) * SkeletalMeshContext->SkeletalMeshConfig.BoundsScale;
#else
			SkeletalMeshContext->BoundingBox += Points[PointIndex] * SkeletalMeshContext->SkeletalMeshConfig.BoundsScale;
#endif
			PointToRawMap.Add(PointIndex);
		}

		if (SkeletalMeshContext->SkeletalMeshConfig.bIgnoreSkin || SkeletalMeshContext->SkinIndex <= INDEX_NONE)
		{
			Influences.Empty();
			TSet<int32> VertexIndexHistory;
			for (int32 WedgeIndex = 0; WedgeIndex < Wedges.Num(); WedgeIndex++)
			{
				if (VertexIndexHistory.Contains(Wedges[WedgeIndex].VertexIndex))
				{
					continue;
				}
				SkeletalMeshImportData::FRawBoneInfluence Influence;
				Influence.VertexIndex = Wedges[WedgeIndex].VertexIndex;
				Influence.BoneIndex = 0;
				Influence.Weight = 1;
				Influences.Add(Influence);
				VertexIndexHistory.Add(Influence.VertexIndex);
			}
		}

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
		FLODUtilities::ProcessImportMeshInfluences(Wedges.Num(), Influences, FString::Printf(TEXT("LOD_%d"), ImportedResource->LODModels.Num()));
#else
		FLODUtilities::ProcessImportMeshInfluences(Wedges.Num(), Influences);
#endif

		LOD.ImportData.bHasNormals = LOD.bHasNormals;
		LOD.ImportData.bHasVertexColors = false;
		LOD.ImportData.bHasTangents = LOD.bHasTangents;
		LOD.ImportData.Faces = Triangles;
		LOD.ImportData.Points = Points;
		LOD.ImportData.PointToRawMap = PointToRawMap;
		LOD.ImportData.NumTexCoords = 1;
		LOD.ImportData.Wedges = Wedges;
		LOD.ImportData.Influences = Influences;

		if (!SkeletalMeshContext->SkeletalMeshConfig.bDisableMorphTargets)
		{

			TArray<TSet<uint32>> MorphTargetModifiedPoints;
			TArray<FSkeletalMeshImportData> MorphTargetsData;
			TArray<FString> MorphTargetNames;

			int32 MorphTargetIndex = 0;
			int32 PointsBase = 0;
			TMap<FString, int32> MorphTargetNamesHistory;
			TMap<FString, int32> MorphTargetNamesDuplicateCounter;

			for (FglTFRuntimePrimitive& Primitive : LOD.Primitives)
			{
				for (FglTFRuntimeMorphTarget& MorphTarget : Primitive.MorphTargets)
				{
					TSet<uint32> MorphTargetPoints;
#if ENGINE_MAJOR_VERSION > 4
					TArray<FVector3f> MorphTargetPositions;
#else
					TArray<FVector> MorphTargetPositions;
#endif
					bool bSkip = true;
					for (uint32 PointIndex = 0; PointIndex < (uint32)Primitive.Positions.Num(); PointIndex++)
					{
						MorphTargetPoints.Add(PointsBase + PointIndex);
						if (!MorphTarget.Positions[PointIndex].IsNearlyZero())
						{
							bSkip = false;
						}
#if ENGINE_MAJOR_VERSION > 4
						MorphTargetPositions.Add(FVector3f(Primitive.Positions[PointIndex] + MorphTarget.Positions[PointIndex]));
#else
						MorphTargetPositions.Add(Primitive.Positions[PointIndex] + MorphTarget.Positions[PointIndex]);
#endif
					}

					if (SkeletalMeshContext->SkeletalMeshConfig.bIgnoreEmptyMorphTargets && bSkip)
					{
						continue;
					}

					FString MorphTargetName = MorphTarget.Name;
					if (MorphTargetName.IsEmpty())
					{
						MorphTargetName = FString::Printf(TEXT("MorphTarget_%d"), MorphTargetIndex);
					}

					bool bAddMorphTarget = false;
					if (MorphTargetNamesHistory.Contains(MorphTargetName))
					{
						int32 Index = MorphTargetNamesHistory[MorphTargetName];
						EglTFRuntimeMorphTargetsDuplicateStrategy DuplicateStrategy = SkeletalMeshContext->SkeletalMeshConfig.MorphTargetsDuplicateStrategy;
						if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::Ignore)
						{
							// NOP
						}
						else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::Merge)
						{
							MorphTargetModifiedPoints[Index].Append(MorphTargetPoints);
							MorphTargetsData[Index].Points.Append(MorphTargetPositions);
						}
						else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::AppendDuplicateCounter)
						{
							if (MorphTargetNamesDuplicateCounter.Contains(MorphTargetName))
							{
								MorphTargetName = FString::Printf(TEXT("%s_%d"), *MorphTargetName, MorphTargetNamesDuplicateCounter[MorphTargetName] + 1);
								MorphTargetNamesDuplicateCounter[MorphTargetName] += 1;
							}
							else
							{
								MorphTargetName = FString::Printf(TEXT("%s_1"), *MorphTargetName);
								MorphTargetNamesDuplicateCounter.Add(MorphTargetName, 1);
							}
							bAddMorphTarget = true;
						}
						else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::AppendMorphIndex)
						{
							MorphTargetName = FString::Printf(TEXT("%s_%d"), *MorphTargetName, MorphTargetIndex);
							bAddMorphTarget = true;
						}
					}
					else
					{
						bAddMorphTarget = true;
					}

					if (bAddMorphTarget)
					{
						MorphTargetModifiedPoints.Add(MorphTargetPoints);

						FSkeletalMeshImportData MorphTargetImportData;
						MorphTargetImportData.PointToRawMap = LOD.ImportData.PointToRawMap;
						MorphTargetImportData.bDiffPose = LOD.ImportData.bDiffPose;
						MorphTargetImportData.bUseT0AsRefPose = LOD.ImportData.bUseT0AsRefPose;
						MorphTargetImportData.Points = MorphTargetPositions;

						MorphTargetsData.Add(MorphTargetImportData);

						MorphTargetNamesHistory.Add(MorphTargetName, MorphTargetNames.Add(MorphTargetName));
					}

					MorphTargetIndex++;
				}
				PointsBase += Primitive.Positions.Num();
			}

			LOD.ImportData.MorphTargetModifiedPoints = MorphTargetModifiedPoints;
			LOD.ImportData.MorphTargets = MorphTargetsData;
			LOD.ImportData.MorphTargetNames = MorphTargetNames;
		}

		ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
#else

	SkeletalMeshContext->SkeletalMesh->AllocateResourceForRendering();

	for (FglTFRuntimeLOD& LOD : SkeletalMeshContext->LODs)
	{
		FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
		int32 LODIndex = SkeletalMeshContext->SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);

		LodRenderData->RenderSections.SetNumUninitialized(LOD.Primitives.Num());

		int32 NumIndices = 0;
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < LOD.Primitives.Num(); PrimitiveIndex++)
		{
			NumIndices += LOD.Primitives[PrimitiveIndex].Indices.Num();
		}

		LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(NumIndices);
		LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(NumIndices, 1);

		int32 NumBones = RefSkeleton.GetNum();

		for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
		{
			LodRenderData->RequiredBones.Add(BoneIndex);
			LodRenderData->ActiveBoneIndices.Add(BoneIndex);
		}

		TArray<FSkinWeightInfo> InWeights;
		InWeights.AddUninitialized(NumIndices);

		int32 TotalVertexIndex = 0;
		int32 Base = 0;

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < LOD.Primitives.Num(); PrimitiveIndex++)
		{
			FglTFRuntimePrimitive& Primitive = LOD.Primitives[PrimitiveIndex];

			new(&LodRenderData->RenderSections[PrimitiveIndex]) FSkelMeshRenderSection();
			FSkelMeshRenderSection& MeshSection = LodRenderData->RenderSections[PrimitiveIndex];

			MeshSection.MaterialIndex = PrimitiveIndex;
			MeshSection.BaseIndex = TotalVertexIndex;
			MeshSection.NumTriangles = Primitive.Indices.Num() / 3;
			MeshSection.BaseVertexIndex = Base;
			MeshSection.MaxBoneInfluences = 4;

			MeshSection.NumVertices = Primitive.Indices.Num();

			Base += MeshSection.NumVertices;

			TMap<int32, TArray<int32>> OverlappingVertices;
			MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);

			for (int32 VertexIndex = 0; VertexIndex < Primitive.Indices.Num(); VertexIndex++)
			{
				int32 Index = Primitive.Indices[VertexIndex];
				FModelVertex ModelVertex;

#if ENGINE_MAJOR_VERSION > 4
				ModelVertex.Position = FVector3f(Primitive.Positions[Index]);
				SkeletalMeshContext->BoundingBox += FVector(ModelVertex.Position) * SkeletalMeshContext->SkeletalMeshConfig.BoundsScale;
				ModelVertex.TangentX = FVector3f::ZeroVector;
				ModelVertex.TangentZ = FVector3f::ZeroVector;
#else
				ModelVertex.Position = Primitive.Positions[Index];
				SkeletalMeshContext->BoundingBox += ModelVertex.Position * SkeletalMeshContext->SkeletalMeshConfig.BoundsScale;
				ModelVertex.TangentX = FVector::ZeroVector;
				ModelVertex.TangentZ = FVector::ZeroVector;
#endif
				if (Index < Primitive.Normals.Num())
				{
#if ENGINE_MAJOR_VERSION > 4
					ModelVertex.TangentZ = FVector3f(FVector(Primitive.Normals[Index]));
#else
					ModelVertex.TangentZ = Primitive.Normals[Index];
#endif
					LOD.bHasNormals = true;
				}
				if (Index < Primitive.Tangents.Num())
				{
#if ENGINE_MAJOR_VERSION > 4
					ModelVertex.TangentX = FVector4f(Primitive.Tangents[Index]);
#else
					ModelVertex.TangentX = Primitive.Tangents[Index];
#endif
					LOD.bHasTangents = true;
				}
				if (Primitive.UVs.Num() > 0 && Index < Primitive.UVs[0].Num())
				{

#if ENGINE_MAJOR_VERSION > 4
					ModelVertex.TexCoord = FVector2f(Primitive.UVs[0][Index]);
#else
					ModelVertex.TexCoord = Primitive.UVs[0][Index];
#endif
					LOD.bHasUV = true;
				}
				else
				{
#if ENGINE_MAJOR_VERSION > 4
					ModelVertex.TexCoord = FVector2f::ZeroVector;
#else
					ModelVertex.TexCoord = FVector2D::ZeroVector;
#endif
				}

				LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TotalVertexIndex) = ModelVertex.Position;
				LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(TotalVertexIndex, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
				LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(TotalVertexIndex, 0, ModelVertex.TexCoord);

				TMap<int32, FName>& BoneMapInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.OverrideBoneMap : MainBoneMap;
				TMap<int32, int32>& BonesCacheInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.BonesCache : MainBonesCache;

				if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreSkin && SkeletalMeshContext->SkinIndex > INDEX_NONE)
				{
					for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
					{
						FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][Index];
						FVector4 Weights = Primitive.Weights[JointsIndex][Index];
						uint32 TotalWeight = 0;
						for (int32 j = 0; j < 4; j++)
						{
							if (BoneMapInUse.Contains(Joints[j]))
							{
								int32 BoneIndex = INDEX_NONE;
								if (BonesCacheInUse.Contains(Joints[j]))
								{
									BoneIndex = BonesCacheInUse[Joints[j]];
								}
								else
								{
									BoneIndex = RefSkeleton.FindBoneIndex(BoneMapInUse[Joints[j]]);
									BonesCacheInUse.Add(Joints[j], BoneIndex);
								}

								uint8 QuantizedWeight = FMath::Clamp((uint8)(Weights[j] * ((double)0xFF)), (uint8)0x00, (uint8)0xFF);

								if (QuantizedWeight + TotalWeight > 255)
								{
									QuantizedWeight = 255 - TotalWeight;
								}

								InWeights[TotalVertexIndex].InfluenceWeights[j] = QuantizedWeight;
								InWeights[TotalVertexIndex].InfluenceBones[j] = BoneIndex;

								TotalWeight += QuantizedWeight;
							}
							else if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreMissingBones)
							{
								AddError("LoadSkeletalMesh_Internal()", FString::Printf(TEXT("Unable to find map for bone %u"), Joints[j]));
								return nullptr;
							}
						}

						// fix weight
						if (TotalWeight < 255)
						{
							InWeights[TotalVertexIndex].InfluenceWeights[0] += 255 - TotalWeight;
						}

					}
				}
				else
				{
					for (int32 j = 0; j < 4; j++)
					{
						InWeights[TotalVertexIndex].InfluenceWeights[j] = j == 0 ? 0xFF : 0;
						InWeights[TotalVertexIndex].InfluenceBones[j] = 0;
					}
				}

				TotalVertexIndex++;
			}

			for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
			{
				MeshSection.BoneMap.Add(BoneIndex);
			}
		}

		if ((!LOD.bHasTangents || !LOD.bHasNormals) && TotalVertexIndex % 3 == 0)
		{
			auto GetTangentY = [](FVector4 Normal, FVector TangentX)
			{
				FVector TanX = TangentX;
				FVector TanZ = Normal;

				return (TanZ ^ TanX) * Normal.W;
			};

			//normals with NaNs are incorrectly handled on Android
			auto FixVectorIfNan = [](FVector& Tangent, int32 Index)
			{
				if (Tangent.ContainsNaN() && Index >= 0 && Index < 3)
				{
					Tangent.Set(0.0, 0.0, 0.0);
					Tangent[Index] = 1.0;
				}
			};

			for (int32 VertexIndex = 0; VertexIndex < TotalVertexIndex; VertexIndex += 3)
			{

#if ENGINE_MAJOR_VERSION > 4
				FVector Position0 = FVector(LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex));
				FVector4 TangentZ0 = FVector4(LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex));
#else
				FVector Position0 = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex);
				FVector4 TangentZ0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex);
#endif


#if ENGINE_MAJOR_VERSION > 4
				FVector Position1 = FVector(LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex + 1));
				FVector4 TangentZ1 = FVector4(LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex + 1));
#else
				FVector Position1 = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex + 1);
				FVector4 TangentZ1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex + 1);
#endif


#if ENGINE_MAJOR_VERSION > 4
				FVector Position2 = FVector(LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex + 2));
				FVector4 TangentZ2 = FVector4(LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex + 2));
#else
				FVector Position2 = LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(VertexIndex + 2);
				FVector4 TangentZ2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentZ(VertexIndex + 2);
#endif

				if (!LOD.bHasNormals)
				{
					const FVector SideA = Position1 - Position0;
					const FVector SideB = Position2 - Position0;

					const FVector NormalFromCross = FVector::CrossProduct(SideB, SideA).GetSafeNormal();

					TangentZ0 = NormalFromCross;
					TangentZ1 = NormalFromCross;
					TangentZ2 = NormalFromCross;
				}

				// if we do not have tangents but we have normals and a UV channel, we can compute them
				if (!LOD.bHasTangents && LOD.bHasUV)
				{
					FVector DeltaPosition0 = Position1 - Position0;
					FVector DeltaPosition1 = Position2 - Position0;


#if ENGINE_MAJOR_VERSION > 4
					const FVector2f& UV0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, 0);
					const FVector2f& UV1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex + 1, 0);
					const FVector2f& UV2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex + 2, 0);
					FVector2f DeltaUV0 = UV1 - UV0;
					FVector2f DeltaUV1 = UV2 - UV0;
#else
					const FVector2D& UV0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex, 0);
					const FVector2D& UV1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex + 1, 0);
					const FVector2D& UV2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.GetVertexUV(VertexIndex + 2, 0);
					FVector2D DeltaUV0 = UV1 - UV0;
					FVector2D DeltaUV1 = UV2 - UV0;
#endif

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

#if PLATFORM_ANDROID
					FixVectorIfNan(TangentX0, 0);
					FixVectorIfNan(TangentX1, 0);
					FixVectorIfNan(TangentX2, 0);
#endif

					FVector TangentY0 = GetTangentY(TangentZ0, TangentX0);
					FVector TangentY1 = GetTangentY(TangentZ1, TangentX1);
					FVector TangentY2 = GetTangentY(TangentZ2, TangentX2);
#if PLATFORM_ANDROID
					FixVectorIfNan(TangentY0, 1);
					FixVectorIfNan(TangentY1, 1);
					FixVectorIfNan(TangentY2, 1);
#endif


#if ENGINE_MAJOR_VERSION > 4
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, FVector3f(TangentX0), FVector3f(GetTangentY(TangentZ0, TangentX0)), FVector3f(FVector(TangentZ0)));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, FVector3f(TangentX1), FVector3f(GetTangentY(TangentZ1, TangentX1)), FVector3f(FVector(TangentZ1)));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, FVector3f(TangentX2), FVector3f(GetTangentY(TangentZ2, TangentX2)), FVector3f(FVector(TangentZ2)));
#else
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, TangentX0, GetTangentY(TangentZ0, TangentX0), TangentZ0);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, TangentX1, GetTangentY(TangentZ1, TangentX1), TangentZ1);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, TangentX2, GetTangentY(TangentZ2, TangentX2), TangentZ2);
#endif
				}
				else if (!LOD.bHasNormals) // if we are here we need to reapply normals
				{
					FVector4f TangentX0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
					FVector4f TangentX1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 1);
					FVector4f TangentX2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 2);
					FVector3f TangentY0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex);
					FVector3f TangentY1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 1);
					FVector3f TangentY2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 2);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, TangentX0, TangentY0, FVector4f(TangentZ0));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, TangentX1, TangentY1, FVector4f(TangentZ1));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, TangentX2, TangentY2, FVector4f(TangentZ2));
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
	}

	return SkeletalMeshContext->SkeletalMesh;
}

USkeletalMesh* FglTFRuntimeParser::FinalizeSkeletalMeshWithLODs(TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext)
{

#if !WITH_EDITOR
	bool bHasMorphTargets = false;
	int32 MorphTargetIndex = 0;
#endif

	for (int32 LODIndex = 0; LODIndex < SkeletalMeshContext->LODs.Num(); LODIndex++)
	{
#if WITH_EDITOR
		SkeletalMeshContext->SkeletalMesh->SaveLODImportedData(LODIndex, SkeletalMeshContext->LODs[LODIndex].ImportData);
#endif
		// LOD tuning

		FSkeletalMeshLODInfo& LODInfo = SkeletalMeshContext->SkeletalMesh->AddLODInfo();
		LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
		LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
		LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
		LODInfo.BuildSettings.bRecomputeNormals = false; // do not force normals regeneration to avoid inconsistencies between editor and runtime
		LODInfo.BuildSettings.bRecomputeTangents = !SkeletalMeshContext->LODs[LODIndex].bHasTangents;
		LODInfo.LODHysteresis = 0.02f;

		if (SkeletalMeshContext->SkeletalMeshConfig.LODScreenSize.Contains(LODIndex))
		{
			LODInfo.ScreenSize = SkeletalMeshContext->SkeletalMeshConfig.LODScreenSize[LODIndex];
		}

#if !WITH_EDITOR
		int32 BaseIndex = 0;
		TMap<FString, UMorphTarget*> MorphTargetNamesHistory;
		TMap<FString, int32> MorphTargetNamesDuplicateCounter;

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < SkeletalMeshContext->LODs[LODIndex].Primitives.Num(); PrimitiveIndex++)
		{
			FglTFRuntimePrimitive& Primitive = SkeletalMeshContext->LODs[LODIndex].Primitives[PrimitiveIndex];

			for (FglTFRuntimeMorphTarget& MorphTargetData : Primitive.MorphTargets)
			{
				bool bSkip = true;
				FMorphTargetLODModel MorphTargetLODModel;
				MorphTargetLODModel.NumBaseMeshVerts = Primitive.Indices.Num();
				MorphTargetLODModel.SectionIndices.Add(PrimitiveIndex);

				for (int32 Index = 0; Index < Primitive.Indices.Num(); Index++)
				{
					FMorphTargetDelta Delta;
					int32 VertexIndex = Primitive.Indices[Index];
					if (VertexIndex < MorphTargetData.Positions.Num())
					{
#if ENGINE_MAJOR_VERSION > 4
						Delta.PositionDelta = FVector3f(MorphTargetData.Positions[VertexIndex]);
#else
						Delta.PositionDelta = MorphTargetData.Positions[VertexIndex];
#endif
					}
					else
					{
#if ENGINE_MAJOR_VERSION > 4
						Delta.PositionDelta = FVector3f::ZeroVector;
#else
						Delta.PositionDelta = FVector::ZeroVector;
#endif
					}

					if (!Delta.PositionDelta.IsNearlyZero())
					{
						bSkip = false;
					}

					Delta.SourceIdx = BaseIndex + Index;
#if ENGINE_MAJOR_VERSION > 4
					Delta.TangentZDelta = FVector3f::ZeroVector;
#else
					Delta.TangentZDelta = FVector::ZeroVector;
#endif
					MorphTargetLODModel.Vertices.Add(Delta);
				}

				if (SkeletalMeshContext->SkeletalMeshConfig.bIgnoreEmptyMorphTargets && bSkip)
				{
					continue;
				}

				FString MorphTargetName = MorphTargetData.Name;
				if (MorphTargetName.IsEmpty())
				{
					MorphTargetName = FString::Printf(TEXT("MorphTarget_%d"), MorphTargetIndex);
				}

				bool bAddMorphTarget = false;
				if (MorphTargetNamesHistory.Contains(MorphTargetName))
				{
					UMorphTarget* CurrentMorphTarget = MorphTargetNamesHistory[MorphTargetName];
					EglTFRuntimeMorphTargetsDuplicateStrategy DuplicateStrategy = SkeletalMeshContext->SkeletalMeshConfig.MorphTargetsDuplicateStrategy;
					if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::Ignore)
					{
						// NOP
					}
					else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::Merge)
					{
						CurrentMorphTarget->GetMorphLODModels()[0].NumBaseMeshVerts += MorphTargetLODModel.NumBaseMeshVerts;
						CurrentMorphTarget->GetMorphLODModels()[0].SectionIndices.Append(MorphTargetLODModel.SectionIndices);
						CurrentMorphTarget->GetMorphLODModels()[0].Vertices.Append(MorphTargetLODModel.Vertices);
					}
					else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::AppendDuplicateCounter)
					{
						if (MorphTargetNamesDuplicateCounter.Contains(MorphTargetName))
						{
							MorphTargetName = FString::Printf(TEXT("%s_%d"), *MorphTargetName, MorphTargetNamesDuplicateCounter[MorphTargetName] + 1);
							MorphTargetNamesDuplicateCounter[MorphTargetName] += 1;
						}
						else
						{
							MorphTargetName = FString::Printf(TEXT("%s_1"), *MorphTargetName);
							MorphTargetNamesDuplicateCounter.Add(MorphTargetName, 1);
						}
						bAddMorphTarget = true;
					}
					else if (DuplicateStrategy == EglTFRuntimeMorphTargetsDuplicateStrategy::AppendMorphIndex)
					{
						MorphTargetName = FString::Printf(TEXT("%s_%d"), *MorphTargetName, MorphTargetIndex);
						bAddMorphTarget = true;
					}
				}
				else
				{
					bAddMorphTarget = true;
				}

				if (bAddMorphTarget)
				{
					UMorphTarget* MorphTarget = NewObject<UMorphTarget>(SkeletalMeshContext->SkeletalMesh, *MorphTargetName, RF_Public);
					MorphTarget->GetMorphLODModels().Add(MorphTargetLODModel);
					SkeletalMeshContext->SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
					MorphTargetNamesHistory.Add(MorphTargetName, MorphTarget);
					bHasMorphTargets = true;
				}

				MorphTargetIndex++;
			}
			BaseIndex += Primitive.Indices.Num();
		}

#endif

		for (int32 MatIndex = 0; MatIndex < SkeletalMeshContext->LODs[LODIndex].Primitives.Num(); MatIndex++)
		{
			LODInfo.LODMaterialMap.Add(MatIndex);

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27
			TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMeshContext->SkeletalMesh->GetMaterials();
#else
			TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMeshContext->SkeletalMesh->Materials;
#endif
			int32 NewMatIndex = SkeletalMaterials.Add(SkeletalMeshContext->LODs[LODIndex].Primitives[MatIndex].Material);
			SkeletalMaterials[NewMatIndex].UVChannelData.bInitialized = true;
			SkeletalMaterials[NewMatIndex].MaterialSlotName = FName(FString::Printf(TEXT("LOD_%d_Section_%d_%s"), LODIndex, MatIndex, *(SkeletalMeshContext->LODs[LODIndex].Primitives[MatIndex].MaterialName)));
		}
#if WITH_EDITOR
		IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27
		FSkeletalMeshBuildParameters SkeletalMeshBuildParameters(SkeletalMeshContext->SkeletalMesh, GetTargetPlatformManagerRef().GetRunningTargetPlatform(), LODIndex, false);
		if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMeshBuildParameters))
#else
		if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMeshContext->SkeletalMesh, LODIndex, false))
#endif
		{
			return nullptr;
		}
#endif
	}

#if WITH_EDITOR
	SkeletalMeshContext->SkeletalMesh->Build();
#else
	if (bHasMorphTargets)
	{
		SkeletalMeshContext->SkeletalMesh->InitMorphTargets();
	}
#endif

	SkeletalMeshContext->SkeletalMesh->CalculateInvRefMatrices();

	if (SkeletalMeshContext->SkeletalMeshConfig.bShiftBoundsByRootBone)
	{
#if ENGINE_MAJOR_VERSION > 4
		FVector RootBone = SkeletalMeshContext->SkeletalMesh->GetRefSkeleton().GetRefBonePose()[0].GetLocation();
#else
		FVector RootBone = SkeletalMeshContext->SkeletalMesh->RefSkeleton.GetRefBonePose()[0].GetLocation();
#endif
		SkeletalMeshContext->BoundingBox = SkeletalMeshContext->BoundingBox.ShiftBy(RootBone);
	}

	SkeletalMeshContext->BoundingBox = SkeletalMeshContext->BoundingBox.ShiftBy(SkeletalMeshContext->SkeletalMeshConfig.ShiftBounds);

	SkeletalMeshContext->SkeletalMesh->SetImportedBounds(FBoxSphereBounds(SkeletalMeshContext->BoundingBox));

#if ENGINE_MAJOR_VERSION > 4
	SkeletalMeshContext->SkeletalMesh->SetHasVertexColors(false);
#else
	SkeletalMeshContext->SkeletalMesh->bHasVertexColors = false;
#endif

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4
	SkeletalMeshContext->SkeletalMesh->SetVertexColorGuid(SkeletalMeshContext->SkeletalMesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
#else
	SkeletalMeshContext->SkeletalMesh->VertexColorGuid = SkeletalMeshContext->SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();
#endif
#endif

	if (SkeletalMeshContext->SkeletalMeshConfig.Skeleton)
	{
#if ENGINE_MAJOR_VERSION > 4
		SkeletalMeshContext->SkeletalMesh->SetSkeleton(SkeletalMeshContext->SkeletalMeshConfig.Skeleton);
#else
		SkeletalMeshContext->SkeletalMesh->Skeleton = SkeletalMeshContext->SkeletalMeshConfig.Skeleton;
#endif
		if (SkeletalMeshContext->SkeletalMeshConfig.bMergeAllBonesToBoneTree)
		{
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#endif
		}
	}
	else
	{
		if (CanReadFromCache(SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CacheMode) && SkeletonsCache.Contains(SkeletalMeshContext->SkinIndex))
		{
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->SetSkeleton(SkeletonsCache[SkeletalMeshContext->SkinIndex]);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton = SkeletonsCache[SkeletalMeshContext->SkinIndex];
#endif
		}
		else
		{
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->SetSkeleton(NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public));
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);
			SkeletalMeshContext->SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#endif

			if (CanWriteToCache(SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CacheMode))
			{
#if ENGINE_MAJOR_VERSION > 4
				SkeletonsCache.Add(SkeletalMeshContext->SkinIndex, SkeletalMeshContext->SkeletalMesh->GetSkeleton());
#else
				SkeletonsCache.Add(SkeletalMeshContext->SkinIndex, SkeletalMeshContext->SkeletalMesh->Skeleton);
#endif
			}
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->SetPreviewMesh(SkeletalMeshContext->SkeletalMesh);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton->SetPreviewMesh(SkeletalMeshContext->SkeletalMesh);
#endif
		}

		for (const TPair<FString, FglTFRuntimeSocket>& Pair : SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.Sockets)
		{
#if ENGINE_MAJOR_VERSION > 4
			USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMeshContext->SkeletalMesh->GetSkeleton());
#else
			USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMeshContext->SkeletalMesh->Skeleton);
#endif
			SkeletalSocket->SocketName = FName(Pair.Key);
			SkeletalSocket->BoneName = FName(Pair.Value.BoneName);
			SkeletalSocket->RelativeLocation = Pair.Value.Transform.GetLocation();
			SkeletalSocket->RelativeRotation = Pair.Value.Transform.GetRotation().Rotator();
			SkeletalSocket->RelativeScale = Pair.Value.Transform.GetScale3D();
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->Sockets.Add(SkeletalSocket);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton->Sockets.Add(SkeletalSocket);
#endif
		}
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.PhysicsBodies.Num() > 0)
	{
		UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(SkeletalMeshContext->SkeletalMesh, NAME_None, RF_Public);
		if (PhysicsAsset)
		{
			for (const TPair<FString, FglTFRuntimePhysicsBody>& PhysicsBody : SkeletalMeshContext->SkeletalMeshConfig.PhysicsBodies)
			{
				if (PhysicsBody.Key.IsEmpty())
				{
					continue;
				}
				USkeletalBodySetup* NewBodySetup = NewObject<USkeletalBodySetup>(PhysicsAsset, NAME_None, RF_Public);
				NewBodySetup->CollisionTraceFlag = PhysicsBody.Value.CollisionTraceFlag;
				NewBodySetup->PhysicsType = PhysicsBody.Value.PhysicsType;
				NewBodySetup->BoneName = FName(PhysicsBody.Key);
				NewBodySetup->bConsiderForBounds = PhysicsBody.Value.bConsiderForBounds;

				for (const FglTFRuntimeCapsule& CapsuleCollision : PhysicsBody.Value.CapsuleCollisions)
				{
					FKSphylElem Capsule;
					Capsule.Length = CapsuleCollision.Length;
					Capsule.Center = CapsuleCollision.Center;
					Capsule.Radius = CapsuleCollision.Radius;
					Capsule.Rotation = CapsuleCollision.Rotation;
					NewBodySetup->AggGeom.SphylElems.Add(Capsule);
				}

				PhysicsAsset->SkeletalBodySetups.Add(NewBodySetup);
			}

			PhysicsAsset->UpdateBodySetupIndexMap();
			PhysicsAsset->UpdateBoundsBodiesArray();
#if WITH_EDITOR
			PhysicsAsset->PreviewSkeletalMesh = SkeletalMeshContext->SkeletalMesh;
#endif
#if ENGINE_MAJOR_VERSION > 4
			SkeletalMeshContext->SkeletalMesh->SetPhysicsAsset(PhysicsAsset);
#else
			SkeletalMeshContext->SkeletalMesh->PhysicsAsset = PhysicsAsset;
#endif
		}
	}

#if !WITH_EDITOR
	SkeletalMeshContext->SkeletalMesh->PostLoad();
#endif

	if (OnSkeletalMeshCreated.IsBound())
	{
		OnSkeletalMeshCreated.Broadcast(SkeletalMeshContext->SkeletalMesh);
	}

#if WITH_EDITOR
	if (!SkeletalMeshContext->SkeletalMeshConfig.SaveToPackage.IsEmpty())
	{
		UPackage* Package = Cast<UPackage>(SkeletalMeshContext->SkeletalMesh->GetOuter());
		if (Package && Package != GetTransientPackage())
		{
			const FString Filename = FPackageName::LongPackageNameToFilename(SkeletalMeshContext->SkeletalMeshConfig.SaveToPackage, FPackageName::GetAssetPackageExtension());
#if ENGINE_MAJOR_VERSION > 4
			FSavePackageArgs SavePackageArgs;
			SavePackageArgs.TopLevelFlags = EObjectFlags::RF_Public | EObjectFlags::RF_Standalone;
			if (UPackage::SavePackage(Package, nullptr, *Filename, SavePackageArgs))
#else
			if (UPackage::SavePackage(Package, nullptr, EObjectFlags::RF_Public | EObjectFlags::RF_Standalone, *Filename))
#endif
			{
				FAssetRegistryModule::AssetCreated(SkeletalMeshContext->SkeletalMesh);
			}
		}
	}
#endif

	return SkeletalMeshContext->SkeletalMesh;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{

	// first check cache
	if (CanReadFromCache(SkeletalMeshConfig.CacheMode) && SkeletalMeshesCache.Contains(MeshIndex))
	{
		return SkeletalMeshesCache[MeshIndex];
	}

	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		AddError("LoadSkeletalMesh()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, SkeletalMeshConfig.MaterialsConfig))
	{
		return nullptr;
	}

	TArray<FglTFRuntimeLOD> LODs;
	FglTFRuntimeLOD LOD0;
	LOD0.Primitives = Primitives;
	LODs.Add(LOD0);

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;
	SkeletalMeshContext->LODs = LODs;

	if (!CreateSkeletalMeshFromLODs(SkeletalMeshContext))
	{
		AddError("LoadSkeletalMesh()", "Unable to load SkeletalMesh.");
		return nullptr;
	}

	USkeletalMesh* SkeletalMesh = FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
	if (!SkeletalMesh)
	{
		AddError("LoadSkeletalMesh()", "Unable to finalize SkeletalMesh.");
		return nullptr;
	}

	if (CanWriteToCache(SkeletalMeshConfig.CacheMode))
	{
		SkeletalMeshesCache.Add(MeshIndex, SkeletalMesh);
	}

	return SkeletalMesh;
}

void FglTFRuntimeParser::LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;

	Async(EAsyncExecution::Thread, [this, SkeletalMeshContext, MeshIndex, AsyncCallback]()
		{
			FglTFRuntimeSkeletalMeshContextFinalizer AsyncFinalizer(SkeletalMeshContext, AsyncCallback);

			TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
			if (!JsonMeshObject)
			{
				AddError("LoadSkeletalMeshAsync()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
				return;
			}

			TArray<FglTFRuntimePrimitive> Primitives;
			if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, SkeletalMeshContext->SkeletalMeshConfig.MaterialsConfig))
			{
				return;
			}

			TArray<FglTFRuntimeLOD> LODs;
			FglTFRuntimeLOD LOD0;
			LOD0.Primitives = Primitives;
			LODs.Add(LOD0);

			SkeletalMeshContext->LODs = LODs;

			SkeletalMeshContext->SkeletalMesh = CreateSkeletalMeshFromLODs(SkeletalMeshContext);
		});
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMeshLODs(const TArray<int32> MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	TArray<FglTFRuntimeLOD> LODs;

	for (const int32 MeshIndex : MeshIndices)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
		if (!JsonMeshObject)
		{
			AddError("LoadSkeletalMesh()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
			return nullptr;
		}

		TArray<FglTFRuntimePrimitive> Primitives;
		if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, SkeletalMeshConfig.MaterialsConfig))
		{
			return nullptr;
		}

		FglTFRuntimeLOD LOD;
		LOD.Primitives = Primitives;
		LODs.Add(LOD);
	}

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;
	SkeletalMeshContext->LODs = LODs;

	if (CreateSkeletalMeshFromLODs(SkeletalMeshContext))
	{
		return FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
	}

	return nullptr;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMeshRecursive(const FString & NodeName, const int32 SkinIndex, const TArray<FString>&ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	FglTFRuntimeNode Node;
	TArray<FglTFRuntimeNode> Nodes;

	if (NodeName.IsEmpty())
	{
		FglTFRuntimeScene Scene;
		if (!LoadScene(0, Scene))
		{
			AddError("LoadSkeletalMeshRecursive()", "No Scene found in asset");
			return nullptr;
		}

		for (int32 NodeIndex : Scene.RootNodesIndices)
		{
			if (!LoadNodesRecursive(NodeIndex, Nodes))
			{
				AddError("LoadSkeletalMeshRecursive()", "Unable to build Node Tree from first Scene");
				return nullptr;
			}
		}
	}
	else
	{
		if (!LoadNodeByName(NodeName, Node))
		{
			AddError("LoadSkeletalMeshRecursive()", FString::Printf(TEXT("Unable to find Node \"%s\""), *NodeName));
			return nullptr;
		}

		if (!LoadNodesRecursive(Node.Index, Nodes))
		{
			AddError("LoadSkeletalMeshRecursive()", FString::Printf(TEXT("Unable to build Node Tree from \"%s\""), *NodeName));
			return nullptr;
		}
	}

	int32 NewSkinIndex = SkinIndex;

	if (NewSkinIndex <= INDEX_NONE)
	{
		// first search for skinning
		for (FglTFRuntimeNode& ChildNode : Nodes)
		{
			if (ExcludeNodes.Contains(ChildNode.Name))
			{
				continue;
			}
			if (ChildNode.SkinIndex > INDEX_NONE)
			{
				NewSkinIndex = ChildNode.SkinIndex;
				break;
			}
		}

		if (NewSkinIndex <= INDEX_NONE)
		{
			AddError("LoadSkeletalMeshRecursive()", "Unable to find a valid Skin");
			return nullptr;
		}
	}

	TArray<FglTFRuntimePrimitive> Primitives;

	// now search for all meshes (will be all merged in the same primitives list)
	for (FglTFRuntimeNode& ChildNode : Nodes)
	{
		if (ExcludeNodes.Contains(ChildNode.Name))
		{
			continue;
		}
		if (ChildNode.MeshIndex != INDEX_NONE)
		{
			TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", ChildNode.MeshIndex);
			if (!JsonMeshObject)
			{
				AddError("LoadSkeletalMeshRecursive()", FString::Printf(TEXT("Unable to find Mesh with index %d"), ChildNode.MeshIndex));
				return nullptr;
			}

			// keep track of primitives
			int32 PrimitiveFirstIndex = Primitives.Num();

			if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, SkeletalMeshConfig.MaterialsConfig))
			{
				return nullptr;
			}

			// if the SkinIndex is different from the selected one,
			// build an override bone map
			if (ChildNode.SkinIndex > INDEX_NONE && ChildNode.SkinIndex != NewSkinIndex)
			{
				TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", ChildNode.SkinIndex);
				if (!JsonSkinObject)
				{
					AddError("LoadSkeletalMeshRecursive()", FString::Printf(TEXT("Unable to fill skin %d"), ChildNode.SkinIndex));
					return nullptr;
				}

				TMap<int32, FName> BoneMap;

				FReferenceSkeleton FakeRefSkeleton;
				if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), FakeRefSkeleton, BoneMap, SkeletalMeshConfig.SkeletonConfig))
				{
					AddError("LoadSkeletalMeshRecursive()", "Unable to fill RefSkeleton.");
					return nullptr;
				}

				// apply overrides
				for (int32 PrimitiveIndex = PrimitiveFirstIndex; PrimitiveIndex < Primitives.Num(); PrimitiveIndex++)
				{
					FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveIndex];
					Primitive.OverrideBoneMap = BoneMap;
				}
			}
		}
	}

	TArray<FglTFRuntimeLOD> LODs;
	FglTFRuntimeLOD LOD0;
	LOD0.Primitives = Primitives;
	LODs.Add(LOD0);

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = NewSkinIndex;
	SkeletalMeshContext->LODs = LODs;

	if (CreateSkeletalMeshFromLODs(SkeletalMeshContext))
	{
		return FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
	}

	return nullptr;
}

void FglTFRuntimeParser::LoadSkeletalMeshRecursiveAsync(const FString & NodeName, const int32 SkinIndex, const TArray<FString>&ExcludeNodes, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);

	Async(EAsyncExecution::Thread, [this, SkeletalMeshContext, ExcludeNodes, NodeName, SkinIndex, AsyncCallback]()
		{
			FglTFRuntimeSkeletalMeshContextFinalizer AsyncFinalizer(SkeletalMeshContext, AsyncCallback);

			FglTFRuntimeNode Node;
			TArray<FglTFRuntimeNode> Nodes;

			if (NodeName.IsEmpty())
			{
				FglTFRuntimeScene Scene;
				if (!LoadScene(0, Scene))
				{
					AddError("LoadSkeletalMeshRecursiveAsync()", "No Scene found in asset");
					return;
				}

				for (int32 NodeIndex : Scene.RootNodesIndices)
				{
					if (!LoadNodesRecursive(NodeIndex, Nodes))
					{
						AddError("LoadSkeletalMeshRecursiveAsync()", "Unable to build Node Tree from first Scene");
						return;
					}
				}
			}
			else
			{
				if (!LoadNodeByName(NodeName, Node))
				{
					AddError("LoadSkeletalMeshRecursiveAsync()", FString::Printf(TEXT("Unable to find Node \"%s\""), *NodeName));
					return;
				}

				if (!LoadNodesRecursive(Node.Index, Nodes))
				{
					AddError("LoadSkeletalMeshRecursiveAsync()", FString::Printf(TEXT("Unable to build Node Tree from \"%s\""), *NodeName));
					return;
				}
			}

			int32 NewSkinIndex = SkinIndex;

			if (NewSkinIndex <= INDEX_NONE)
			{
				// first search for skinning
				for (FglTFRuntimeNode& ChildNode : Nodes)
				{
					if (ExcludeNodes.Contains(ChildNode.Name))
					{
						continue;
					}
					if (ChildNode.SkinIndex > INDEX_NONE)
					{
						NewSkinIndex = ChildNode.SkinIndex;
						break;
					}
				}

				if (NewSkinIndex <= INDEX_NONE)
				{
					AddError("LoadSkeletalMeshRecursiveAsync()", "Unable to find a valid Skin");
					return;
				}
			}

			TArray<FglTFRuntimePrimitive> Primitives;

			// now search for all meshes (will be all merged in the same primitives list)
			for (FglTFRuntimeNode& ChildNode : Nodes)
			{
				if (ExcludeNodes.Contains(ChildNode.Name))
				{
					continue;
				}
				if (ChildNode.MeshIndex != INDEX_NONE)
				{
					TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", ChildNode.MeshIndex);
					if (!JsonMeshObject)
					{
						AddError("LoadSkeletalMeshRecursiveAsync()", FString::Printf(TEXT("Unable to find Mesh with index %d"), ChildNode.MeshIndex));
						return;
					}

					// keep track of primitives
					int32 PrimitiveFirstIndex = Primitives.Num();

					if (!LoadPrimitives(JsonMeshObject.ToSharedRef(), Primitives, SkeletalMeshContext->SkeletalMeshConfig.MaterialsConfig))
					{
						return;
					}

					// if the SkinIndex is different from the selected one,
					// build an override bone map
					if (ChildNode.SkinIndex > INDEX_NONE && ChildNode.SkinIndex != NewSkinIndex)
					{
						TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", ChildNode.SkinIndex);
						if (!JsonSkinObject)
						{
							AddError("LoadSkeletalMeshRecursiveAsync()", FString::Printf(TEXT("Unable to fill skin %d"), ChildNode.SkinIndex));
							return;
						}

						TMap<int32, FName> BoneMap;

						FReferenceSkeleton FakeRefSkeleton;
						if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), FakeRefSkeleton, BoneMap, SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig))
						{
							AddError("LoadSkeletalMeshRecursiveAsync()", "Unable to fill RefSkeleton.");
							return;
						}

						// apply overrides
						for (int32 PrimitiveIndex = PrimitiveFirstIndex; PrimitiveIndex < Primitives.Num(); PrimitiveIndex++)
						{
							FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveIndex];
							Primitive.OverrideBoneMap = BoneMap;
						}
					}
				}
			}

			TArray<FglTFRuntimeLOD> LODs;
			FglTFRuntimeLOD LOD0;
			LOD0.Primitives = Primitives;
			LODs.Add(LOD0);

			SkeletalMeshContext->SkinIndex = NewSkinIndex;
			SkeletalMeshContext->LODs = LODs;

			SkeletalMeshContext->SkeletalMesh = CreateSkeletalMeshFromLODs(SkeletalMeshContext);
		});
}

UAnimSequence* FglTFRuntimeParser::LoadSkeletalAnimationByName(USkeletalMesh * SkeletalMesh, const FString AnimationName, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		AddError("LoadSkeletalAnimationByName()", "No animations defined in the asset.");
		return nullptr;
	}

	for (int32 AnimationIndex = 0; AnimationIndex < JsonAnimations->Num(); AnimationIndex++)
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
				return LoadSkeletalAnimation(SkeletalMesh, AnimationIndex, SkeletalAnimationConfig);
			}
		}
	}

	return nullptr;
}

UAnimSequence* FglTFRuntimeParser::LoadNodeSkeletalAnimation(USkeletalMesh * SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig)
{

	if (!SkeletalMesh)
	{
		return nullptr;
	}

	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		return nullptr;
	}

	if (Node.SkinIndex <= INDEX_NONE)
	{
		AddError("LoadNodeSkeletalAnimation()", FString::Printf(TEXT("No skin defined for node %d"), NodeIndex));
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", Node.SkinIndex);
	if (!JsonSkinObject)
	{
		AddError("LoadNodeSkeletalAnimation()", "No skins defined in the asset");
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
	if (!JsonSkinObject->TryGetArrayField("joints", JsonJoints))
	{
		AddError("LoadNodeSkeletalAnimation()", "No joints defined in the skin");
		return nullptr;
	}

	TArray<int32> Joints;
	for (TSharedPtr<FJsonValue> JsonJoint : (*JsonJoints))
	{
		int64 JointIndex;
		if (!JsonJoint->TryGetNumber(JointIndex))
		{
			return nullptr;
		}
		Joints.Add(JointIndex);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		AddError("LoadNodeSkeletalAnimation()", "No animations defined in the asset");
		return nullptr;
	}

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
			return nullptr;
		float Duration;
		TMap<FString, FRawAnimSequenceTrack> Tracks;
		TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;
		bool bAnimationFound = false;
		if (!LoadSkeletalAnimation_Internal(JsonAnimationObject.ToSharedRef(), Tracks, MorphTargetCurves, Duration, SkeletalAnimationConfig, [&Joints, &bAnimationFound](const FglTFRuntimeNode& Node) -> bool
			{
				if (!bAnimationFound)
				{
					bAnimationFound = Joints.Contains(Node.Index);
				}
				return true;
			}))
		{
			return nullptr;
		}

			if (bAnimationFound)
			{
				// this is very inefficient as we parse the tracks twice
				// TODO: refactor it
				return LoadSkeletalAnimation(SkeletalMesh, JsonAnimationIndex, SkeletalAnimationConfig);
			}
	}

	return nullptr;
}


UAnimSequence* FglTFRuntimeParser::LoadSkeletalAnimation(USkeletalMesh * SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonAnimationObject = GetJsonObjectFromRootIndex("animations", AnimationIndex);
	if (!JsonAnimationObject)
	{
		AddError("LoadNodeSkeletalAnimation()", FString::Printf(TEXT("Unable to find animation %d"), AnimationIndex));
		return nullptr;
	}

	float Duration;
	TMap<FString, FRawAnimSequenceTrack> Tracks;
	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;
	if (!LoadSkeletalAnimation_Internal(JsonAnimationObject.ToSharedRef(), Tracks, MorphTargetCurves, Duration, SkeletalAnimationConfig, [](const FglTFRuntimeNode& Node) -> bool { return true; }))
	{
		return nullptr;
	}

	int32 NumFrames = Duration * 30;
	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
#if ENGINE_MAJOR_VERSION > 4
	AnimSequence->SetSkeleton(SkeletalMesh->GetSkeleton());
#else
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
#endif
	AnimSequence->SetPreviewMesh(SkeletalMesh);
#if ENGINE_MAJOR_VERSION > 4
#if WITH_EDITOR
	FIntProperty* IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfFrames")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("PlayLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), Duration);
	IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfKeys")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FFrameRate FrameRate(NumFrames, Duration);
	FStructProperty* StructProperty = CastField<FStructProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("FrameRate")));
	FFrameRate* FrameRatePtr = StructProperty->ContainerPtrToValuePtr<FFrameRate>(AnimSequence->GetDataModel());
	*FrameRatePtr = FrameRate;
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	AnimSequence->SequenceLength = Duration;
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
#else
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = Duration;
#endif
	AnimSequence->bEnableRootMotion = SkeletalAnimationConfig.bRootMotion;

	const TArray<FTransform> BonesPoses = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetRefBonePose();

#if !WITH_EDITOR
	UglTFAnimBoneCompressionCodec* CompressionCodec = NewObject<UglTFAnimBoneCompressionCodec>();
	CompressionCodec->Tracks.AddDefaulted(BonesPoses.Num());
	AnimSequence->CompressedData.CompressedTrackToSkeletonMapTable.AddDefaulted(BonesPoses.Num());
	for (int32 BoneIndex = 0; BoneIndex < BonesPoses.Num(); BoneIndex++)
	{
		AnimSequence->CompressedData.CompressedTrackToSkeletonMapTable[BoneIndex] = BoneIndex;
		for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
		{
#if ENGINE_MAJOR_VERSION > 4
			CompressionCodec->Tracks[BoneIndex].PosKeys.Add(FVector3f(BonesPoses[BoneIndex].GetLocation()));
			CompressionCodec->Tracks[BoneIndex].RotKeys.Add(FQuat4f(BonesPoses[BoneIndex].GetRotation()));
			CompressionCodec->Tracks[BoneIndex].ScaleKeys.Add(FVector3f(BonesPoses[BoneIndex].GetScale3D()));
#else
			CompressionCodec->Tracks[BoneIndex].PosKeys.Add(BonesPoses[BoneIndex].GetLocation());
			CompressionCodec->Tracks[BoneIndex].RotKeys.Add(BonesPoses[BoneIndex].GetRotation());
			CompressionCodec->Tracks[BoneIndex].ScaleKeys.Add(BonesPoses[BoneIndex].GetScale3D());
#endif

		}
}
#endif

	bool bHasTracks = false;
	for (TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		FName BoneName = FName(Pair.Key);
		int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			AddError("LoadSkeletalAnimation()", FString::Printf(TEXT("Unable to find bone %s"), *Pair.Key));
			continue;
		}

		// sanitize curves

		// positions
		if (Pair.Value.PosKeys.Num() == 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
#if ENGINE_MAJOR_VERSION > 4
				Pair.Value.PosKeys.Add(FVector3f(BonesPoses[BoneIndex].GetLocation()));
#else
				Pair.Value.PosKeys.Add(BonesPoses[BoneIndex].GetLocation());
#endif
			}
		}
		else if (Pair.Value.PosKeys.Num() < NumFrames)
		{
#if ENGINE_MAJOR_VERSION > 4
			FVector3f LastValidPosition = Pair.Value.PosKeys.Last();
#else
			FVector LastValidPosition = Pair.Value.PosKeys.Last();
#endif
			int32 FirstNewFrame = Pair.Value.PosKeys.Num();
			for (int32 FrameIndex = FirstNewFrame; FrameIndex < NumFrames; FrameIndex++)
			{
				Pair.Value.PosKeys.Add(LastValidPosition);
			}
		}
		else
		{
			Pair.Value.PosKeys.RemoveAt(NumFrames, Pair.Value.PosKeys.Num() - NumFrames, true);
		}

		// rotations
		if (Pair.Value.RotKeys.Num() == 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
#if ENGINE_MAJOR_VERSION > 4
				Pair.Value.RotKeys.Add(FQuat4f(BonesPoses[BoneIndex].GetRotation()));
#else
				Pair.Value.RotKeys.Add(BonesPoses[BoneIndex].GetRotation());
#endif
			}
		}
		else if (Pair.Value.RotKeys.Num() < NumFrames)
		{
#if ENGINE_MAJOR_VERSION > 4
			FQuat4f LastValidRotation = Pair.Value.RotKeys.Last();
#else
			FQuat LastValidRotation = Pair.Value.RotKeys.Last();
#endif
			int32 FirstNewFrame = Pair.Value.RotKeys.Num();
			for (int32 FrameIndex = FirstNewFrame; FrameIndex < NumFrames; FrameIndex++)
			{
				Pair.Value.RotKeys.Add(LastValidRotation);
			}
		}
		else
		{
			Pair.Value.RotKeys.RemoveAt(NumFrames, Pair.Value.RotKeys.Num() - NumFrames, true);
		}

		if (Pair.Value.ScaleKeys.Num() == 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
#if ENGINE_MAJOR_VERSION > 4
				Pair.Value.ScaleKeys.Add(FVector3f(BonesPoses[BoneIndex].GetScale3D()));
#else
				Pair.Value.ScaleKeys.Add(BonesPoses[BoneIndex].GetScale3D());
#endif
			}
		}
		else if (Pair.Value.ScaleKeys.Num() < NumFrames)
		{
#if ENGINE_MAJOR_VERSION > 4
			FVector3f LastValidScale = Pair.Value.ScaleKeys.Last();
#else
			FVector LastValidScale = Pair.Value.ScaleKeys.Last();
#endif
			int32 FirstNewFrame = Pair.Value.ScaleKeys.Num();
			for (int32 FrameIndex = FirstNewFrame; FrameIndex < NumFrames; FrameIndex++)
			{
				Pair.Value.ScaleKeys.Add(LastValidScale);
			}
		}
		else
		{
			Pair.Value.ScaleKeys.RemoveAt(NumFrames, Pair.Value.ScaleKeys.Num() - NumFrames, true);
		}


		if (BoneIndex == 0)
		{
			if (SkeletalAnimationConfig.RootNodeIndex > INDEX_NONE)
			{
				FglTFRuntimeNode AnimRootNode;
				if (!LoadNode(SkeletalAnimationConfig.RootNodeIndex, AnimRootNode))
				{
					return nullptr;
				}

				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
#if ENGINE_MAJOR_VERSION > 4
					FVector3f Pos = Pair.Value.PosKeys[FrameIndex];
					FQuat4d Quat = FQuat4d(Pair.Value.RotKeys[FrameIndex]);
					FVector3f Scale = Pair.Value.ScaleKeys[FrameIndex];
					FTransform FrameTransform = FTransform(FQuat(Quat), FVector(Pos), FVector(Scale)) * AnimRootNode.Transform;
#else
					FVector Pos = Pair.Value.PosKeys[FrameIndex];
					FQuat Quat = Pair.Value.RotKeys[FrameIndex];
					FVector Scale = Pair.Value.ScaleKeys[FrameIndex];
					FTransform FrameTransform = FTransform(Quat, Pos, Scale) * AnimRootNode.Transform;
#endif

#if ENGINE_MAJOR_VERSION > 4
					Pair.Value.PosKeys[FrameIndex] = FVector3f(FrameTransform.GetLocation());
					Pair.Value.RotKeys[FrameIndex] = FQuat4f(FrameTransform.GetRotation());
#else
					Pair.Value.PosKeys[FrameIndex] = FrameTransform.GetLocation();
					Pair.Value.RotKeys[FrameIndex] = FrameTransform.GetRotation();
#endif

#if ENGINE_MAJOR_VERSION > 4
					Pair.Value.ScaleKeys[FrameIndex] = FVector3f(FrameTransform.GetScale3D());
#else
					Pair.Value.ScaleKeys[FrameIndex] = FrameTransform.GetScale3D();
#endif
				}
			}

			if (SkeletalAnimationConfig.bRemoveRootMotion)
			{
				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
					Pair.Value.PosKeys[FrameIndex] = Pair.Value.PosKeys[0];
				}
			}
		}

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4
		TArray<FBoneAnimationTrack>& BoneTracks = const_cast<TArray<FBoneAnimationTrack>&>(AnimSequence->GetDataModel()->GetBoneAnimationTracks());
		FBoneAnimationTrack BoneTrack;
		BoneTrack.Name = BoneName;
		BoneTrack.BoneTreeIndex = BoneIndex;
		BoneTrack.InternalTrackData = Pair.Value;
		BoneTracks.Add(BoneTrack);
#else
		AnimSequence->AddNewRawTrack(BoneName, &Pair.Value);
#endif
#else
		CompressionCodec->Tracks[BoneIndex] = Pair.Value;
#endif
		bHasTracks = true;
	}

	// add MorphTarget curves
	for (TPair<FName, TArray<TPair<float, float>>>& Pair : MorphTargetCurves)
	{
		FSmartName SmartName;
		if (!AnimSequence->GetSkeleton()->GetSmartNameByName(USkeleton::AnimCurveMappingName, Pair.Key, SmartName))
		{
			SmartName.DisplayName = Pair.Key;
			AnimSequence->GetSkeleton()->VerifySmartName(USkeleton::AnimCurveMappingName, SmartName);
		}

#if ENGINE_MAJOR_VERSION > 4
		FRawCurveTracks& RawCurveData = const_cast<FRawCurveTracks&>(AnimSequence->GetCurveData());
		RawCurveData.AddCurveData(SmartName);
		FFloatCurve* NewCurve = (FFloatCurve*)RawCurveData.GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);
#else
		AnimSequence->RawCurveData.AddCurveData(SmartName);
		FFloatCurve* NewCurve = (FFloatCurve*)AnimSequence->RawCurveData.GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);

#endif

		for (TPair<float, float>& CurvePair : Pair.Value)
		{
			FKeyHandle NewKeyHandle = NewCurve->FloatCurve.AddKey(CurvePair.Key, CurvePair.Value, false);

			ERichCurveInterpMode NewInterpMode = RCIM_Linear;
			ERichCurveTangentMode NewTangentMode = RCTM_Auto;
			ERichCurveTangentWeightMode NewTangentWeightMode = RCTWM_WeightedNone;

			float LeaveTangent = 0.f;
			float ArriveTangent = 0.f;
			float LeaveTangentWeight = 0.f;
			float ArriveTangentWeight = 0.f;

			NewCurve->FloatCurve.SetKeyInterpMode(NewKeyHandle, NewInterpMode);
			NewCurve->FloatCurve.SetKeyTangentMode(NewKeyHandle, NewTangentMode);
			NewCurve->FloatCurve.SetKeyTangentWeightMode(NewKeyHandle, NewTangentWeightMode);
		}

		AnimSequence->GetSkeleton()->AccumulateCurveMetaData(Pair.Key, false, true);

		bHasTracks = true;
	}

	if (!bHasTracks)
	{
		AddError("LoadSkeletalAnimation()", "No Bone or MorphTarget Tracks found in animation");
		return nullptr;
	}

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4
	// hack for calling GenerateTransientData()
	AnimSequence->GetDataModel()->PostDuplicate(false);
#else
	AnimSequence->PostProcessSequence();
#endif
#else
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
#if ENGINE_MAJOR_VERSION > 4
	AnimSequence->CompressedData.CompressedDataStructure->CompressedNumberOfKeys = NumFrames;
#endif
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	AnimSequence->CompressedData.CurveCompressionCodec = NewObject<UAnimCurveCompressionCodec_CompressedRichCurve>();
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
}

bool FglTFRuntimeParser::LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>&Tracks, TMap<FName, TArray<TPair<float, float>>>&MorphTargetCurves, float& Duration, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig, TFunctionRef<bool(const FglTFRuntimeNode& Node)> Filter)
{

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)
	{
		int32 NumFrames = Duration * 30;

		float FrameDelta = 1.f / 30;

		if (Path == "rotation" && !SkeletalAnimationConfig.bRemoveRotations)
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}

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
				FMatrix FirstMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(FirstQuat) * SceneBasis;
				FMatrix SecondMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(SecondQuat) * SceneBasis;
				FirstQuat = FirstMatrix.ToQuat();
				SecondQuat = SecondMatrix.ToQuat();
				FQuat AnimQuat = FQuat::Slerp(FirstQuat, SecondQuat, Alpha);
#if ENGINE_MAJOR_VERSION > 4
				Track.RotKeys.Add(FQuat4f(AnimQuat));
#else
				Track.RotKeys.Add(AnimQuat);
#endif
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "translation" && !SkeletalAnimationConfig.bRemoveTranslations)
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}

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
				FVector4 First = Values[FirstIndex];
				FVector4 Second = Values[SecondIndex];
				FVector AnimLocation = SceneBasis.TransformPosition(FMath::Lerp(First, Second, Alpha)) * SceneScale;
#if ENGINE_MAJOR_VERSION > 4
				Track.PosKeys.Add(FVector3f(AnimLocation));
#else
				Track.PosKeys.Add(AnimLocation);
#endif
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "scale" && !SkeletalAnimationConfig.bRemoveScales)
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}

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
				FVector4 First = Values[FirstIndex];
				FVector4 Second = Values[SecondIndex];
#if ENGINE_MAJOR_VERSION > 4
				Track.ScaleKeys.Add(FVector3f((SceneBasis.Inverse() * FScaleMatrix(FMath::Lerp(First, Second, Alpha)) * SceneBasis).ExtractScaling()));
#else
				Track.ScaleKeys.Add((SceneBasis.Inverse() * FScaleMatrix(FMath::Lerp(First, Second, Alpha)) * SceneBasis).ExtractScaling());
#endif
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "weights" && !SkeletalAnimationConfig.bRemoveMorphTargets)
		{
			TArray<FName> MorphTargetNames;
			if (!GetMorphTargetNames(Node.MeshIndex, MorphTargetNames))
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Mesh %d has no MorphTargets"), Node.Index));
				return;
			}

			if (Timeline.Num() != Values.Num() / MorphTargetNames.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for weights on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}

			for (int32 MorphTargetIndex = 0; MorphTargetIndex < MorphTargetNames.Num(); MorphTargetIndex++)
			{
				FName MorphTargetName = MorphTargetNames[MorphTargetIndex];
				TArray<TPair<float, float>> Curves;
				for (int32 TimelineIndex = 0; TimelineIndex < Timeline.Num(); TimelineIndex++)
				{
					TPair<float, float> Curve = TPair<float, float>(Timeline[TimelineIndex], Values[TimelineIndex * MorphTargetNames.Num() + MorphTargetIndex].X);
					Curves.Add(Curve);
				}
				MorphTargetCurves.Add(MorphTargetName, Curves);
			}
		}
	};

	FString IgnoredName;
	return LoadAnimation_Internal(JsonAnimationObject, Duration, IgnoredName, Callback, Filter);
}
