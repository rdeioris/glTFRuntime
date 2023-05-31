// Copyright 2020-2022, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "Runtime/Launch/Resources/Version.h"
#if ENGINE_MAJOR_VERSION > 4
#include "Animation/AnimData/AnimDataModel.h"
#include "Animation/AnimData/IAnimationDataController.h"
#if ENGINE_MINOR_VERSION > 1
#include "Animation/AnimData/IAnimationDataModel.h"
#endif
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
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#endif
#include "Engine/SkeletalMeshSocket.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
#include "Engine/SkinnedAssetCommon.h"
#else
#include "Engine/SkeletalMesh.h"
#endif
#include "glTFAnimBoneCompressionCodec.h"
#include "glTFAnimCurveCompressionCodec.h"
#include "Model.h"
#include "Animation/MorphTarget.h"
#include "Async/Async.h"
#include "Animation/AnimCurveTypes.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/PhysicsConstraintTemplate.h"
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
#include "UObject/SavePackage.h"
#endif

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#define BONE_INFLUENCE_TYPE uint16
#define MAX_BONE_INFLUENCE_WEIGHT 0xffff
#else
#define BONE_INFLUENCE_TYPE uint8
#define MAX_BONE_INFLUENCE_WEIGHT 0xff
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

void FglTFRuntimeParser::AddSkeletonDeltaTranforms(FReferenceSkeleton& RefSkeleton, const TMap<FString, FTransform>& Transforms)
{
	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);
	const TArray<FTransform>& BonesTransforms = Modifier.GetReferenceSkeleton().GetRefBonePose();

	for (const TPair<FString, FTransform>& Pair : Transforms)
	{
		const int32 BoneIndex = RefSkeleton.FindBoneIndex(*Pair.Key);
		if (BoneIndex <= INDEX_NONE)
		{
			continue;
		}
		FTransform Transform = BonesTransforms[BoneIndex];
		Transform.Accumulate(Pair.Value);
		Modifier.UpdateRefPoseTransform(BoneIndex, Transform);
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
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
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

	if (SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.BonesDeltaTransformMap.Num() > 0)
	{
		AddSkeletonDeltaTranforms(RefSkeleton, SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.BonesDeltaTransformMap);
	}

	if (SkeletalMeshContext->SkeletalMeshConfig.Skeleton)
	{
		if (SkeletalMeshContext->SkeletalMeshConfig.bOverwriteRefSkeleton)
		{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			SkeletalMeshContext->SkeletalMesh->SetRefSkeleton(SkeletalMeshContext->SkeletalMeshConfig.Skeleton->GetReferenceSkeleton());
#else
			SkeletalMeshContext->SkeletalMesh->RefSkeleton = SkeletalMeshContext->SkeletalMeshConfig.Skeleton->GetReferenceSkeleton();
#endif
		}
		else if (SkeletalMeshContext->SkeletalMeshConfig.bAddVirtualBones)
		{
			RefSkeleton.RebuildRefSkeleton(SkeletalMeshContext->SkeletalMeshConfig.Skeleton, false);
		}
	}


	TMap<int32, int32> MainBonesCache;
	int32 MatIndex = 0;

	SkeletalMeshContext->SkeletalMesh->NeverStream = true;

	SkeletalMeshContext->SkeletalMesh->ResetLODInfo();

	const float TangentsDirection = SkeletalMeshContext->SkeletalMeshConfig.bReverseTangents ? -1 : 1;

#if WITH_EDITOR

	FSkeletalMeshModel* ImportedResource = SkeletalMeshContext->SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();

	const bool bForceNormalsGeneration = SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Always;

	for (FglTFRuntimeSkeletalMeshLOD& LOD : SkeletalMeshContext->LODs)
	{

		// we initially set bHasTangents and bHasNormals as true: if a primitive misses them we set reset them as false
		LOD.bHasTangents = true;
		LOD.bHasNormals = true;

		TArray<SkeletalMeshImportData::FVertex> Wedges;
		TArray<SkeletalMeshImportData::FTriangle> Triangles;
		TArray<SkeletalMeshImportData::FRawBoneInfluence> Influences;

#if ENGINE_MAJOR_VERSION > 4
		TArray<FVector3f> Points;
#else
		TArray<FVector> Points;
#endif

		for (FglTFRuntimePrimitive& Primitive : LOD.RuntimeLOD->Primitives)
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
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
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

					if (Primitive.Normals.Num() > 0 && !bForceNormalsGeneration)
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

					}
					else if (SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::IfMissing || bForceNormalsGeneration)
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
					}
					else
					{
						LOD.bHasNormals = false;
					}

					float TangentXW[3] = { 1,1,1 };
					if (Primitive.Tangents.Num() > 0)
					{
						TangentXW[0] = Primitive.Tangents[Primitive.Indices[i - 2]].W;
						TangentXW[1] = Primitive.Tangents[Primitive.Indices[i - 1]].W;
						TangentXW[2] = Primitive.Tangents[Primitive.Indices[i]].W;

#if ENGINE_MAJOR_VERSION > 4
						Triangle.TangentX[0] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i - 2]]));
						Triangle.TangentX[1] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i - 1]]));
						Triangle.TangentX[2] = FVector3f(FVector(Primitive.Tangents[Primitive.Indices[i]]));
#else
						Triangle.TangentX[0] = Primitive.Tangents[Primitive.Indices[i - 2]];
						Triangle.TangentX[1] = Primitive.Tangents[Primitive.Indices[i - 1]];
						Triangle.TangentX[2] = Primitive.Tangents[Primitive.Indices[i]];
#endif
					}
					else
					{
						LOD.bHasTangents = false;
					}

#if ENGINE_MAJOR_VERSION > 4
					Triangle.TangentY[0] = FVector3f(ComputeTangentYWithW(FVector(Triangle.TangentZ[0]), FVector(Triangle.TangentX[0]), TangentXW[0] * TangentsDirection));
					Triangle.TangentY[1] = FVector3f(ComputeTangentYWithW(FVector(Triangle.TangentZ[1]), FVector(Triangle.TangentX[1]), TangentXW[1] * TangentsDirection));
					Triangle.TangentY[2] = FVector3f(ComputeTangentYWithW(FVector(Triangle.TangentZ[2]), FVector(Triangle.TangentX[2]), TangentXW[2] * TangentsDirection));
#else
					Triangle.TangentY[0] = ComputeTangentYWithW(Triangle.TangentZ[0], Triangle.TangentX[0], TangentXW[0] * TangentsDirection);
					Triangle.TangentY[1] = ComputeTangentYWithW(Triangle.TangentZ[1], Triangle.TangentX[1], TangentXW[1] * TangentsDirection);
					Triangle.TangentY[2] = ComputeTangentYWithW(Triangle.TangentZ[2], Triangle.TangentX[2], TangentXW[2] * TangentsDirection);
#endif
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

			for (FglTFRuntimePrimitive& Primitive : LOD.RuntimeLOD->Primitives)
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

	for (FglTFRuntimeSkeletalMeshLOD& LOD : SkeletalMeshContext->LODs)
	{
		LOD.bHasTangents = true;
		LOD.bHasNormals = true;

		FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
		int32 LODIndex = SkeletalMeshContext->SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);

		LodRenderData->RenderSections.SetNumUninitialized(LOD.RuntimeLOD->Primitives.Num());

		int32 NumIndices = 0;
		for (int32 PrimitiveIndex = 0; PrimitiveIndex < LOD.RuntimeLOD->Primitives.Num(); PrimitiveIndex++)
		{
			NumIndices += LOD.RuntimeLOD->Primitives[PrimitiveIndex].Indices.Num();
		}

		LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(NumIndices);
		LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetUseFullPrecisionUVs(SkeletalMeshContext->SkeletalMeshConfig.bUseHighPrecisionUVs);
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
		int32 MaxBoneInfluences = 4;

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < LOD.RuntimeLOD->Primitives.Num(); PrimitiveIndex++)
		{
			FglTFRuntimePrimitive& Primitive = LOD.RuntimeLOD->Primitives[PrimitiveIndex];

			new(&LodRenderData->RenderSections[PrimitiveIndex]) FSkelMeshRenderSection();
			FSkelMeshRenderSection& MeshSection = LodRenderData->RenderSections[PrimitiveIndex];

			MeshSection.MaterialIndex = PrimitiveIndex;
			MeshSection.BaseIndex = TotalVertexIndex;
			MeshSection.NumTriangles = Primitive.Indices.Num() / 3;
			MeshSection.BaseVertexIndex = Base;
			MeshSection.MaxBoneInfluences = FMath::Min(Primitive.Joints.Num() * 4, MAX_TOTAL_INFLUENCES);

			if (MeshSection.MaxBoneInfluences > MaxBoneInfluences)
			{
				MaxBoneInfluences = MeshSection.MaxBoneInfluences;
			}

			MeshSection.NumVertices = Primitive.Indices.Num();

			Base += MeshSection.NumVertices;

			TMap<int32, TArray<int32>> OverlappingVertices;
			MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);

			for (int32 VertexIndex = 0; VertexIndex < Primitive.Indices.Num(); VertexIndex++)
			{
				int32 Index = Primitive.Indices[VertexIndex];
				FModelVertex ModelVertex;

				float TangentXW = 1;

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
				}
				else
				{
					LOD.bHasNormals = false;
				}

				if (Index < Primitive.Tangents.Num())
				{
#if ENGINE_MAJOR_VERSION > 4
					TangentXW = Primitive.Tangents[Index].W;
					ModelVertex.TangentX = FVector4f(Primitive.Tangents[Index]);
#else
					ModelVertex.TangentX = Primitive.Tangents[Index];
#endif

				}
				else
				{
					LOD.bHasTangents = false;
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
					LOD.bHasUV = false;
				}

#if ENGINE_MAJOR_VERSION > 4
				FVector3f TangentY = FVector3f(ComputeTangentYWithW(FVector(ModelVertex.TangentZ), FVector(ModelVertex.TangentX), TangentXW * TangentsDirection));
#else
				FVector TangentY = ComputeTangentYWithW(ModelVertex.TangentZ, ModelVertex.TangentX, TangentXW * TangentsDirection);
#endif

				LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TotalVertexIndex) = ModelVertex.Position;
				LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(TotalVertexIndex, ModelVertex.TangentX, TangentY, ModelVertex.TangentZ);
				LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(TotalVertexIndex, 0, ModelVertex.TexCoord);

				TMap<int32, FName>& BoneMapInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.OverrideBoneMap : MainBoneMap;
				TMap<int32, int32>& BonesCacheInUse = Primitive.OverrideBoneMap.Num() > 0 ? Primitive.BonesCache : MainBonesCache;

				if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreSkin && SkeletalMeshContext->SkinIndex > INDEX_NONE)
				{
					uint32 TotalWeight = 0;
					const int32 JoitsNum = FMath::Min(Primitive.Joints.Num(), MeshSection.MaxBoneInfluences / 4);
					for (int32 JointsIndex = 0; JointsIndex < JoitsNum; JointsIndex++)
					{
						FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][Index];
						FVector4 Weights = Primitive.Weights[JointsIndex][Index];
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

								BONE_INFLUENCE_TYPE QuantizedWeight = FMath::Clamp((BONE_INFLUENCE_TYPE)(Weights[j] * ((double)MAX_BONE_INFLUENCE_WEIGHT)), (BONE_INFLUENCE_TYPE)0x00, (BONE_INFLUENCE_TYPE)MAX_BONE_INFLUENCE_WEIGHT);

								if (QuantizedWeight + TotalWeight > MAX_BONE_INFLUENCE_WEIGHT)
								{
									QuantizedWeight = MAX_BONE_INFLUENCE_WEIGHT - TotalWeight;
								}

								InWeights[TotalVertexIndex].InfluenceWeights[JointsIndex * 4 + j] = QuantizedWeight;
								InWeights[TotalVertexIndex].InfluenceBones[JointsIndex * 4 + j] = BoneIndex;

								TotalWeight += QuantizedWeight;
							}
							else if (!SkeletalMeshContext->SkeletalMeshConfig.bIgnoreMissingBones)
							{
								AddError("LoadSkeletalMesh_Internal()", FString::Printf(TEXT("Unable to find map for bone %u"), Joints[j]));
								return nullptr;
							}
						}
					}

					// fix weight
					if (TotalWeight < MAX_BONE_INFLUENCE_WEIGHT)
					{
						InWeights[TotalVertexIndex].InfluenceWeights[0] += MAX_BONE_INFLUENCE_WEIGHT - TotalWeight;
					}
				}
				else
				{
					// reset it to be meaningful
					MeshSection.MaxBoneInfluences = 1;
					for (int32 j = 0; j < MeshSection.MaxBoneInfluences; j++)
					{
						InWeights[TotalVertexIndex].InfluenceWeights[j] = j == 0 ? MAX_BONE_INFLUENCE_WEIGHT : 0;
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

		if (SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Always)
		{
			LOD.bHasNormals = false;
		}
		else if (SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Never)
		{
			LOD.bHasNormals = true;
		}

		if (SkeletalMeshContext->SkeletalMeshConfig.TangentsGenerationStrategy == EglTFRuntimeTangentsGenerationStrategy::Always)
		{
			LOD.bHasTangents = false;
		}
		else if (SkeletalMeshContext->SkeletalMeshConfig.TangentsGenerationStrategy == EglTFRuntimeTangentsGenerationStrategy::Never)
		{
			LOD.bHasTangents = true;
		}

		if ((!LOD.bHasTangents || !LOD.bHasNormals) && TotalVertexIndex % 3 == 0)
		{

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
					TangentX0.Normalize();

					FVector TangentX1 = TriangleTangentX - (TangentZ1 * FVector::DotProduct(TangentZ1, TriangleTangentX));
					TangentX1.Normalize();

					FVector TangentX2 = TriangleTangentX - (TangentZ2 * FVector::DotProduct(TangentZ2, TriangleTangentX));
					TangentX2.Normalize();

#if PLATFORM_ANDROID
					FixVectorIfNan(TangentX0, 0);
					FixVectorIfNan(TangentX1, 0);
					FixVectorIfNan(TangentX2, 0);
#endif

					FVector TangentY0 = ComputeTangentY(TangentZ0, TangentX0) * TangentsDirection;
					FVector TangentY1 = ComputeTangentY(TangentZ1, TangentX1) * TangentsDirection;
					FVector TangentY2 = ComputeTangentY(TangentZ2, TangentX2) * TangentsDirection;
#if PLATFORM_ANDROID
					FixVectorIfNan(TangentY0, 1);
					FixVectorIfNan(TangentY1, 1);
					FixVectorIfNan(TangentY2, 1);
#endif


#if ENGINE_MAJOR_VERSION > 4
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, FVector3f(TangentX0), FVector3f(TangentY0), FVector3f(FVector(TangentZ0)));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, FVector3f(TangentX1), FVector3f(TangentY1), FVector3f(FVector(TangentZ1)));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, FVector3f(TangentX2), FVector3f(TangentY2), FVector3f(FVector(TangentZ2)));
#else
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, TangentX0, TangentY0, TangentZ0);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, TangentX1, TangentY1, TangentZ1);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, TangentX2, TangentY2, TangentZ2);
#endif
				}
				else if (!LOD.bHasNormals) // if we are here we need to reapply normals
				{
#if ENGINE_MAJOR_VERSION > 4
					FVector4f TangentX0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
					FVector4f TangentX1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 1);
					FVector4f TangentX2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 2);
					FVector3f TangentY0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex);
					FVector3f TangentY1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 1);
					FVector3f TangentY2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 2);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, TangentX0, TangentY0, FVector4f(TangentZ0));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, TangentX1, TangentY1, FVector4f(TangentZ1));
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, TangentX2, TangentY2, FVector4f(TangentZ2));
#else
					FVector4 TangentX0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex);
					FVector4 TangentX1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 1);
					FVector4 TangentX2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentX(VertexIndex + 2);
					FVector TangentY0 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex);
					FVector TangentY1 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 1);
					FVector TangentY2 = LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.VertexTangentY(VertexIndex + 2);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex, TangentX0, TangentY0, TangentZ0);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 1, TangentX1, TangentY1, TangentZ1);
					LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(VertexIndex + 2, TangentX2, TangentY2, TangentZ2);
#endif
				}
			}
		}

		LodRenderData->SkinWeightVertexBuffer.SetNeedsCPUAccess(SkeletalMeshContext->SkeletalMeshConfig.bPerPolyCollision);
		LodRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(MaxBoneInfluences);
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
		if (SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Always)
		{
			SkeletalMeshContext->LODs[LODIndex].bHasNormals = false;
		}
		else if (SkeletalMeshContext->SkeletalMeshConfig.NormalsGenerationStrategy == EglTFRuntimeNormalsGenerationStrategy::Never)
		{
			SkeletalMeshContext->LODs[LODIndex].bHasNormals = true;
		}

		if (SkeletalMeshContext->SkeletalMeshConfig.TangentsGenerationStrategy == EglTFRuntimeTangentsGenerationStrategy::Always)
		{
			SkeletalMeshContext->LODs[LODIndex].bHasTangents = false;
		}
		else if (SkeletalMeshContext->SkeletalMeshConfig.TangentsGenerationStrategy == EglTFRuntimeTangentsGenerationStrategy::Never)
		{
			SkeletalMeshContext->LODs[LODIndex].bHasTangents = true;
		}

		FSkeletalMeshLODInfo& LODInfo = SkeletalMeshContext->SkeletalMesh->AddLODInfo();
		LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
		LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
		LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
		LODInfo.BuildSettings.bRecomputeNormals = !SkeletalMeshContext->LODs[LODIndex].bHasNormals;
		LODInfo.BuildSettings.bRecomputeTangents = !SkeletalMeshContext->LODs[LODIndex].bHasTangents;
		LODInfo.BuildSettings.bUseFullPrecisionUVs = SkeletalMeshContext->SkeletalMeshConfig.bUseHighPrecisionUVs;
		LODInfo.LODHysteresis = 0.02f;

		if (SkeletalMeshContext->SkeletalMeshConfig.LODScreenSize.Contains(LODIndex))
		{
			LODInfo.ScreenSize = SkeletalMeshContext->SkeletalMeshConfig.LODScreenSize[LODIndex];
		}

#if !WITH_EDITOR
		TMap<FString, UMorphTarget*> MorphTargetNamesHistory;
		TMap<FString, int32> MorphTargetNamesDuplicateCounter;

		int32 BaseIndex = 0;

		for (int32 PrimitiveIndex = 0; PrimitiveIndex < SkeletalMeshContext->LODs[LODIndex].RuntimeLOD->Primitives.Num(); PrimitiveIndex++)
		{

			FglTFRuntimePrimitive& Primitive = SkeletalMeshContext->LODs[LODIndex].RuntimeLOD->Primitives[PrimitiveIndex];

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
#if ENGINE_MAJOR_VERSION > 4
					MorphTargetLODModel.NumVertices = MorphTargetLODModel.Vertices.Num();
#endif
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
#if ENGINE_MAJOR_VERSION > 4
						CurrentMorphTarget->GetMorphLODModels()[0].NumBaseMeshVerts += MorphTargetLODModel.NumBaseMeshVerts;
						CurrentMorphTarget->GetMorphLODModels()[0].SectionIndices.Append(MorphTargetLODModel.SectionIndices);
						CurrentMorphTarget->GetMorphLODModels()[0].Vertices.Append(MorphTargetLODModel.Vertices);
						CurrentMorphTarget->GetMorphLODModels()[0].NumVertices = CurrentMorphTarget->GetMorphLODModels()[0].Vertices.Num();
#else
						CurrentMorphTarget->MorphLODModels[0].NumBaseMeshVerts += MorphTargetLODModel.NumBaseMeshVerts;
						CurrentMorphTarget->MorphLODModels[0].SectionIndices.Append(MorphTargetLODModel.SectionIndices);
						CurrentMorphTarget->MorphLODModels[0].Vertices.Append(MorphTargetLODModel.Vertices);
#endif
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
#if ENGINE_MAJOR_VERSION > 4
					MorphTarget->GetMorphLODModels().Add(MorphTargetLODModel);
#else
					MorphTarget->MorphLODModels.Add(MorphTargetLODModel);
#endif
					SkeletalMeshContext->SkeletalMesh->RegisterMorphTarget(MorphTarget, false);
					MorphTargetNamesHistory.Add(MorphTargetName, MorphTarget);
					bHasMorphTargets = true;
				}

				MorphTargetIndex++;
			}
			BaseIndex += Primitive.Indices.Num();
		}

#endif

		for (int32 MatIndex = 0; MatIndex < SkeletalMeshContext->LODs[LODIndex].RuntimeLOD->Primitives.Num(); MatIndex++)
		{
			LODInfo.LODMaterialMap.Add(MatIndex);

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION >= 27
			TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMeshContext->SkeletalMesh->GetMaterials();
#else
			TArray<FSkeletalMaterial>& SkeletalMaterials = SkeletalMeshContext->SkeletalMesh->Materials;
#endif
			int32 NewMatIndex = SkeletalMaterials.Add(SkeletalMeshContext->LODs[LODIndex].RuntimeLOD->Primitives[MatIndex].Material);


			SkeletalMaterials[NewMatIndex].UVChannelData.bInitialized = true;

			SkeletalMaterials[NewMatIndex].MaterialSlotName = FName(FString::Printf(TEXT("LOD_%d_Section_%d_%s"), LODIndex, MatIndex, *(SkeletalMeshContext->LODs[LODIndex].RuntimeLOD->Primitives[MatIndex].MaterialName)));
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
#endif
	SkeletalMeshContext->SkeletalMesh->CalculateInvRefMatrices();

	if (SkeletalMeshContext->SkeletalMeshConfig.bShiftBoundsByRootBone)
	{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		FVector RootBone = SkeletalMeshContext->SkeletalMesh->GetRefSkeleton().GetRefBonePose()[0].GetLocation();
#else
		FVector RootBone = SkeletalMeshContext->SkeletalMesh->RefSkeleton.GetRefBonePose()[0].GetLocation();
#endif
		SkeletalMeshContext->BoundingBox = SkeletalMeshContext->BoundingBox.ShiftBy(RootBone);
	}

	SkeletalMeshContext->BoundingBox = SkeletalMeshContext->BoundingBox.ShiftBy(SkeletalMeshContext->SkeletalMeshConfig.ShiftBounds);

	SkeletalMeshContext->SkeletalMesh->SetImportedBounds(FBoxSphereBounds(SkeletalMeshContext->BoundingBox));

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	SkeletalMeshContext->SkeletalMesh->SetHasVertexColors(false);
#else
	SkeletalMeshContext->SkeletalMesh->bHasVertexColors = false;
#endif

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	SkeletalMeshContext->SkeletalMesh->SetVertexColorGuid(SkeletalMeshContext->SkeletalMesh->GetHasVertexColors() ? FGuid::NewGuid() : FGuid());
#else
	SkeletalMeshContext->SkeletalMesh->VertexColorGuid = SkeletalMeshContext->SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();
#endif
#endif

	if (SkeletalMeshContext->SkeletalMeshConfig.Skeleton)
	{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		SkeletalMeshContext->SkeletalMesh->SetSkeleton(SkeletalMeshContext->SkeletalMeshConfig.Skeleton);
#else
		SkeletalMeshContext->SkeletalMesh->Skeleton = SkeletalMeshContext->SkeletalMeshConfig.Skeleton;
#endif
		if (SkeletalMeshContext->SkeletalMeshConfig.bMergeAllBonesToBoneTree)
		{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
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
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			SkeletalMeshContext->SkeletalMesh->SetSkeleton(SkeletonsCache[SkeletalMeshContext->SkinIndex]);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton = SkeletonsCache[SkeletalMeshContext->SkinIndex];
#endif
		}
		else
		{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			SkeletalMeshContext->SkeletalMesh->SetSkeleton(NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public));
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);
			SkeletalMeshContext->SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMeshContext->SkeletalMesh);
#endif

			if (CanWriteToCache(SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.CacheMode))
			{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
				SkeletonsCache.Add(SkeletalMeshContext->SkinIndex, SkeletalMeshContext->SkeletalMesh->GetSkeleton());
#else
				SkeletonsCache.Add(SkeletalMeshContext->SkinIndex, SkeletalMeshContext->SkeletalMesh->Skeleton);
#endif
			}
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->SetPreviewMesh(SkeletalMeshContext->SkeletalMesh);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton->SetPreviewMesh(SkeletalMeshContext->SkeletalMesh);
#endif
		}

		for (const TPair<FString, FglTFRuntimeSocket>& Pair : SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig.Sockets)
		{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMeshContext->SkeletalMesh->GetSkeleton());
#else
			USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMeshContext->SkeletalMesh->Skeleton);
#endif
			SkeletalSocket->SocketName = FName(Pair.Key);
			SkeletalSocket->BoneName = FName(Pair.Value.BoneName);
			SkeletalSocket->RelativeLocation = Pair.Value.Transform.GetLocation();
			SkeletalSocket->RelativeRotation = Pair.Value.Transform.GetRotation().Rotator();
			SkeletalSocket->RelativeScale = Pair.Value.Transform.GetScale3D();
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
			SkeletalMeshContext->SkeletalMesh->GetSkeleton()->Sockets.Add(SkeletalSocket);
#else
			SkeletalMeshContext->SkeletalMesh->Skeleton->Sockets.Add(SkeletalSocket);
#endif
		}
	}

#if !WITH_EDITOR
	if (bHasMorphTargets)
	{
		SkeletalMeshContext->SkeletalMesh->InitMorphTargets();
	}
#endif

	if (SkeletalMeshContext->SkeletalMeshConfig.PhysicsBodies.Num() > 0 || SkeletalMeshContext->SkeletalMeshConfig.PhysicsAssetTemplate)
	{
		UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(SkeletalMeshContext->SkeletalMesh, NAME_None, RF_Public);
		if (PhysicsAsset)
		{
			if (SkeletalMeshContext->SkeletalMeshConfig.PhysicsAssetTemplate)
			{
				UPhysicsAsset* PhysicsAssetTemplate = SkeletalMeshContext->SkeletalMeshConfig.PhysicsAssetTemplate;
				for (USkeletalBodySetup* SourceBodySetup : PhysicsAssetTemplate->SkeletalBodySetups)
				{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
					if (SkeletalMeshContext->SkeletalMesh->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(SourceBodySetup->BoneName) != INDEX_NONE)
#else
					if (SkeletalMeshContext->SkeletalMesh->Skeleton->GetReferenceSkeleton().FindBoneIndex(SourceBodySetup->BoneName) != INDEX_NONE)
#endif
					{
						USkeletalBodySetup* NewBodySetup = NewObject<USkeletalBodySetup>(PhysicsAsset, NAME_None, RF_Public);
						NewBodySetup->CollisionTraceFlag = SourceBodySetup->CollisionTraceFlag;
						NewBodySetup->PhysicsType = SourceBodySetup->PhysicsType;
						NewBodySetup->BoneName = SourceBodySetup->BoneName;
						NewBodySetup->bConsiderForBounds = SourceBodySetup->bConsiderForBounds;
						NewBodySetup->AggGeom = SourceBodySetup->AggGeom;
						PhysicsAsset->SkeletalBodySetups.Add(NewBodySetup);
					}
				}
				for (UPhysicsConstraintTemplate* ConstraintTemplate : PhysicsAssetTemplate->ConstraintSetup)
				{
					UPhysicsConstraintTemplate* NewConstraint = NewObject<UPhysicsConstraintTemplate>(PhysicsAsset, NAME_None, RF_Public);
					NewConstraint->DefaultInstance = ConstraintTemplate->DefaultInstance;
					NewConstraint->ProfileHandles = ConstraintTemplate->ProfileHandles;
					PhysicsAsset->ConstraintSetup.Add(NewConstraint);
				}
			}
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
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
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

	FglTFRuntimeMeshLOD* LOD = nullptr;
	if (!LoadMeshIntoMeshLOD(JsonMeshObject.ToSharedRef(), LOD, SkeletalMeshConfig.MaterialsConfig))
	{
		return nullptr;
	}

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;
	SkeletalMeshContext->LODs.Add(LOD);

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

	FglTFRuntimeMeshLOD* LOD = nullptr;
	if (!LoadMeshIntoMeshLOD(JsonMeshObject.ToSharedRef(), LOD, SkeletalMeshContext->SkeletalMeshConfig.MaterialsConfig))
	{
		return;
	}

	SkeletalMeshContext->LODs.Add(LOD);

	SkeletalMeshContext->SkeletalMesh = CreateSkeletalMeshFromLODs(SkeletalMeshContext);
		});
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMeshLODs(const TArray<int32>&MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;

	for (const int32 MeshIndex : MeshIndices)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
		if (!JsonMeshObject)
		{
			AddError("LoadSkeletalMesh()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
			return nullptr;
		}

		FglTFRuntimeMeshLOD* LOD = nullptr;
		if (!LoadMeshIntoMeshLOD(JsonMeshObject.ToSharedRef(), LOD, SkeletalMeshConfig.MaterialsConfig))
		{
			return nullptr;
		}

		SkeletalMeshContext->LODs.Add(LOD);
	}

	if (CreateSkeletalMeshFromLODs(SkeletalMeshContext))
	{
		return FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
	}

	return nullptr;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMeshRecursive(const FString & NodeName, const int32 SkinIndex, const TArray<FString>&ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{

	FglTFRuntimeMeshLOD CombinedLOD;
	int32 NewSkinIndex = SkinIndex;
	if (!LoadSkinnedMeshRecursiveAsRuntimeLOD(NodeName, NewSkinIndex, ExcludeNodes, CombinedLOD, SkeletalMeshConfig.MaterialsConfig, SkeletalMeshConfig.SkeletonConfig))
	{
		return nullptr;
	}

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = NewSkinIndex;
	SkeletalMeshContext->LODs.Add(&CombinedLOD);

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
	// ensure to cache it as the finalizer requires LOD access
	FglTFRuntimeMeshLOD& CombinedLOD = SkeletalMeshContext->CachedRuntimeMeshLODs.AddDefaulted_GetRef();
	int32 NewSkinIndex = SkinIndex;
	if (!LoadSkinnedMeshRecursiveAsRuntimeLOD(NodeName, NewSkinIndex, ExcludeNodes, CombinedLOD, SkeletalMeshContext->SkeletalMeshConfig.MaterialsConfig, SkeletalMeshContext->SkeletalMeshConfig.SkeletonConfig))
	{
		return;
	}

	SkeletalMeshContext->SkinIndex = NewSkinIndex;
	SkeletalMeshContext->LODs.Add(&CombinedLOD);

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

	TArray<int32> Joints;

	// this could be a static mesh read as a skeletal one...
	if (Node.SkinIndex > INDEX_NONE)
	{
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

		for (TSharedPtr<FJsonValue> JsonJoint : (*JsonJoints))
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
			{
				return nullptr;
			}
			Joints.Add(JointIndex);
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return nullptr;
	}

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
		{
			return nullptr;
		}
		float Duration;
		TMap<FString, FRawAnimSequenceTrack> Tracks;
		TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;
		bool bAnimationFound = false;
		if (!LoadSkeletalAnimation_Internal(JsonAnimationObject.ToSharedRef(), Tracks, MorphTargetCurves, Duration, SkeletalAnimationConfig, [&Joints, &bAnimationFound, NodeIndex](const FglTFRuntimeNode& Node) -> bool
			{
				if (!bAnimationFound)
				{
					bAnimationFound = (Node.Index == NodeIndex) || Joints.Contains(Node.Index);
				}
		return true;
			}))
		{
			return nullptr;
		}
			if (bAnimationFound || MorphTargetCurves.Num() > 0)
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

	int32 NumFrames = FMath::Max<int32>(Duration * SkeletalAnimationConfig.FramesPerSecond, 1);
	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	AnimSequence->SetSkeleton(SkeletalMesh->GetSkeleton());
#else
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
#endif
	AnimSequence->SetPreviewMesh(SkeletalMesh);
#if ENGINE_MAJOR_VERSION > 4
#if WITH_EDITOR
	FFrameRate FrameRate(SkeletalAnimationConfig.FramesPerSecond, 1);

#if ENGINE_MINOR_VERSION < 2
	FIntProperty* IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfFrames")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("PlayLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), Duration);
	IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfKeys")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);


	FStructProperty* StructProperty = CastField<FStructProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("FrameRate")));
	FFrameRate* FrameRatePtr = StructProperty->ContainerPtrToValuePtr<FFrameRate>(AnimSequence->GetDataModel());
	*FrameRatePtr = FrameRate;
#endif
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if ENGINE_MINOR_VERSION >= 2
		FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimSequence::StaticClass()->FindPropertyByName(TEXT("SequenceLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence, Duration);
#else
		AnimSequence->SequenceLength = Duration;
#endif
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
#else
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = Duration;
#endif
	AnimSequence->bEnableRootMotion = SkeletalAnimationConfig.bRootMotion;
	AnimSequence->RootMotionRootLock = SkeletalAnimationConfig.RootMotionRootLock;
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
#else
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().OpenBracket(FText::FromString("glTFRuntime"), false);
	AnimSequence->GetController().InitializeModel();
#endif
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

		FTransform BoneTransform = BonesPoses[BoneIndex];

		// sanitize curves

		// positions
		if (Pair.Value.PosKeys.Num() == 0)
		{
			for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
			{
#if ENGINE_MAJOR_VERSION > 4
				Pair.Value.PosKeys.Add(FVector3f(BoneTransform.GetLocation()));
#else
				Pair.Value.PosKeys.Add(BoneTransform.GetLocation());
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
				Pair.Value.RotKeys.Add(FQuat4f(BoneTransform.GetRotation()));
#else
				Pair.Value.RotKeys.Add(BoneTransform.GetRotation());
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
				Pair.Value.ScaleKeys.Add(FVector3f(BoneTransform.GetScale3D()));
#else
				Pair.Value.ScaleKeys.Add(BoneTransform.GetScale3D());
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
#if ENGINE_MAJOR_VERSION >= 5
#if ENGINE_MINOR_VERSION >= 2
		AnimSequence->GetController().AddBoneCurve(BoneName, false);
		AnimSequence->GetController().SetBoneTrackKeys(BoneName, Pair.Value.PosKeys, Pair.Value.RotKeys, Pair.Value.ScaleKeys, false);
#else
		TArray<FBoneAnimationTrack>& BoneTracks = const_cast<TArray<FBoneAnimationTrack>&>(AnimSequence->GetDataModel()->GetBoneAnimationTracks());
		FBoneAnimationTrack BoneTrack;
		BoneTrack.Name = BoneName;
		BoneTrack.BoneTreeIndex = BoneIndex;
		BoneTrack.InternalTrackData = Pair.Value;
		BoneTracks.Add(BoneTrack);
#endif
#else
		AnimSequence->AddNewRawTrack(BoneName, &Pair.Value);
#endif
#else
		CompressionCodec->Tracks[BoneIndex] = Pair.Value;
#endif
		bHasTracks = true;
}

	if (SkeletalAnimationConfig.bFillAllCurves)
	{
		for (int32 BoneIndex = 0; BoneIndex < BonesPoses.Num(); BoneIndex++)
		{
			const FString BoneName = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetBoneName(BoneIndex).ToString();
			if (!Tracks.Contains(BoneName))
			{
				FRawAnimSequenceTrack NewTrack;
				FglTFRuntimeNode BoneNode;
				if (LoadNodeByName(BoneName, BoneNode))
				{
					FTransform BoneTransform = BoneNode.Transform;

					for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
					{
#if ENGINE_MAJOR_VERSION > 4
						NewTrack.PosKeys.Add(FVector3f(BoneTransform.GetLocation()));
						NewTrack.RotKeys.Add(FQuat4f(BoneTransform.GetRotation()));
						NewTrack.ScaleKeys.Add(FVector3f(BoneTransform.GetScale3D()));
#else
						NewTrack.PosKeys.Add(BoneTransform.GetLocation());
						NewTrack.RotKeys.Add(BoneTransform.GetRotation());
						NewTrack.ScaleKeys.Add(BoneTransform.GetScale3D());
#endif
					}
#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4
#if ENGINE_MINOR_VERSION >= 2
					AnimSequence->GetController().AddBoneCurve(*BoneName, false);
					AnimSequence->GetController().SetBoneTrackKeys(*BoneName, NewTrack.PosKeys, NewTrack.RotKeys, NewTrack.ScaleKeys, false);
#else
					TArray<FBoneAnimationTrack>& BoneTracks = const_cast<TArray<FBoneAnimationTrack>&>(AnimSequence->GetDataModel()->GetBoneAnimationTracks());
					FBoneAnimationTrack BoneTrack;
					BoneTrack.Name = *BoneName;
					BoneTrack.BoneTreeIndex = BoneIndex;
					BoneTrack.InternalTrackData = NewTrack;
					BoneTracks.Add(BoneTrack);
#endif
#else
					AnimSequence->AddNewRawTrack(*BoneName, &NewTrack);
#endif
#else
					CompressionCodec->Tracks[BoneIndex] = NewTrack;
#endif
				}
			}
		}
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
#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
		FAnimationCurveIdentifier CurveId(SmartName, ERawCurveTrackTypes::RCT_Float);
		AnimSequence->GetController().AddCurve(CurveId);
		FRichCurve RichCurve;
#else
		FAnimationCurveData& RawCurveData = const_cast<FAnimationCurveData&>(AnimSequence->GetDataModel()->GetCurveData());
		int32 NewCurveIndex = RawCurveData.FloatCurves.Add(FFloatCurve(SmartName, 0));
		FFloatCurve* NewCurve = &RawCurveData.FloatCurves[NewCurveIndex];
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif
#else
		FRawCurveTracks& CurveTracks = const_cast<FRawCurveTracks&>(AnimSequence->GetCurveData());
		int32 NewCurveIndex = CurveTracks.FloatCurves.Add(FFloatCurve(SmartName, 0));
		FFloatCurve* NewCurve = &CurveTracks.FloatCurves[NewCurveIndex];
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif
#else
		AnimSequence->RawCurveData.AddCurveData(SmartName);
		FFloatCurve* NewCurve = (FFloatCurve*)AnimSequence->RawCurveData.GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif

		for (TPair<float, float>& CurvePair : Pair.Value)
		{
			FKeyHandle NewKeyHandle = RichCurve.AddKey(CurvePair.Key, CurvePair.Value, false);

			ERichCurveInterpMode NewInterpMode = RCIM_Linear;
			ERichCurveTangentMode NewTangentMode = RCTM_Auto;
			ERichCurveTangentWeightMode NewTangentWeightMode = RCTWM_WeightedNone;

			float LeaveTangent = 0.f;
			float ArriveTangent = 0.f;
			float LeaveTangentWeight = 0.f;
			float ArriveTangentWeight = 0.f;

			RichCurve.SetKeyInterpMode(NewKeyHandle, NewInterpMode);
			RichCurve.SetKeyTangentMode(NewKeyHandle, NewTangentMode);
			RichCurve.SetKeyTangentWeightMode(NewKeyHandle, NewTangentWeightMode);
		}

		AnimSequence->GetSkeleton()->AccumulateCurveMetaData(Pair.Key, false, true);

#if !WITH_EDITOR
		AnimSequence->CompressedData.CompressedCurveNames.Add(SmartName);
		const_cast<FCurveMetaData*>(AnimSequence->GetSkeleton()->GetCurveMetaData(SmartName.UID))->Type.bMorphtarget = true;
#else
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
		AnimSequence->GetController().SetCurveKeys(CurveId, RichCurve.GetConstRefOfKeys());
#endif
#endif

		bHasTracks = true;
		}

	if (!bHasTracks)
	{
		AddError("LoadSkeletalAnimation()", "No Bone or MorphTarget Tracks found in animation");
		return nullptr;
	}

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5
#if ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().SetFrameRate(FrameRate, false);
	AnimSequence->GetController().SetNumberOfFrames(NumFrames);
	AnimSequence->GetController().NotifyPopulated();
	AnimSequence->GetController().CloseBracket(false);
#else
	// hack for calling GenerateTransientData()
	AnimSequence->GetDataModel()->PostDuplicate(false);
#endif
#else
	AnimSequence->PostProcessSequence();
#endif
#else
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
#if ENGINE_MAJOR_VERSION > 4
	AnimSequence->CompressedData.CompressedDataStructure->CompressedNumberOfKeys = NumFrames;
#endif
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	UglTFAnimCurveCompressionCodec* AnimCurveCompressionCodec = NewObject<UglTFAnimCurveCompressionCodec>();
	AnimCurveCompressionCodec->AnimSequence = AnimSequence;
	AnimSequence->CompressedData.CurveCompressionCodec = AnimCurveCompressionCodec;
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
	}

UAnimSequence* FglTFRuntimeParser::CreateAnimationFromPose(USkeletalMesh * SkeletalMesh, const int32 SkinIndex, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	constexpr int32 NumFrames = 1;
	const float Duration = NumFrames / SkeletalAnimationConfig.FramesPerSecond;

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	AnimSequence->SetSkeleton(SkeletalMesh->GetSkeleton());
#else
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
#endif
	AnimSequence->SetPreviewMesh(SkeletalMesh);
#if ENGINE_MAJOR_VERSION > 4
#if WITH_EDITOR
	FFrameRate FrameRate(SkeletalAnimationConfig.FramesPerSecond, 1);
#if ENGINE_MINOR_VERSION < 2
	FIntProperty* IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfFrames")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("PlayLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), Duration);
	IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfKeys")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FStructProperty* StructProperty = CastField<FStructProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("FrameRate")));
	FFrameRate* FrameRatePtr = StructProperty->ContainerPtrToValuePtr<FFrameRate>(AnimSequence->GetDataModel());
	*FrameRatePtr = FrameRate;
#endif
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if ENGINE_MINOR_VERSION >= 2
		FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimSequence::StaticClass()->FindPropertyByName(TEXT("SequenceLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence, Duration);
#else
		AnimSequence->SequenceLength = Duration;
#endif
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
#else
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = Duration;
#endif
	AnimSequence->bEnableRootMotion = SkeletalAnimationConfig.bRootMotion;
	AnimSequence->RootMotionRootLock = SkeletalAnimationConfig.RootMotionRootLock;

	const TArray<FTransform> BonesPoses = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetRefBonePose();
	const TArray<FMeshBoneInfo>& MeshBoneInfos = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetRefBoneInfo();

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
#else
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().OpenBracket(FText::FromString("glTFRuntime"), false);
	AnimSequence->GetController().InitializeModel();
#endif
#endif

	int64 RootBoneIndex = INDEX_NONE;
	TArray<int32> Joints;
	if (SkinIndex > INDEX_NONE)
	{
		TSharedPtr<FJsonObject> SkinObject = GetJsonObjectFromRootIndex("skins", SkinIndex);
		if (!SkinObject)
		{
			return nullptr;
		}
		if (!GetRootBoneIndex(SkinObject.ToSharedRef(), RootBoneIndex, Joints, FglTFRuntimeSkeletonConfig()))
		{
			return nullptr;
		}
	}

	TMap<FString, FRawAnimSequenceTrack> Tracks;
	for (int32 BoneIndex = 0; BoneIndex < BonesPoses.Num(); BoneIndex++)
	{
		FglTFRuntimeNode Node;
		const FString TrackName = MeshBoneInfos[BoneIndex].Name.ToString();

		if (RootBoneIndex > INDEX_NONE)
		{
			if (!LoadJointByName(RootBoneIndex, TrackName, Node))
			{
				continue;
			}
		}
		else
		{
			if (!LoadNodeByName(TrackName, Node))
			{
				continue;
			}
		}

		FTransform Transform = Node.Transform;

		if (BoneIndex == 0)
		{
			FglTFRuntimeNode ParentNode = Node;
			while (ParentNode.ParentIndex != INDEX_NONE)
			{
				if (!LoadNode(ParentNode.ParentIndex, ParentNode))
				{
					break; // overengineering... (useless)
				}
				Transform *= ParentNode.Transform;
			}
		}

		FRawAnimSequenceTrack Track;
#if ENGINE_MAJOR_VERSION > 4
		Track.PosKeys.Add(FVector3f(Transform.GetLocation()));
#else
		Track.PosKeys.Add(Transform.GetLocation());
#endif


#if ENGINE_MAJOR_VERSION > 4
		Track.RotKeys.Add(FQuat4f(Transform.GetRotation()));
#else
		Track.RotKeys.Add(Transform.GetRotation());
#endif


#if ENGINE_MAJOR_VERSION > 4
		Track.ScaleKeys.Add(FVector3f(Transform.GetScale3D()));
#else
		Track.ScaleKeys.Add(Transform.GetScale3D());
#endif

		Tracks.Add(TrackName, Track);
		}

	OnCreatedPoseTracks.Broadcast(AsShared(), Tracks);

	for (const TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		const int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(*Pair.Key);
#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION > 4
#if ENGINE_MINOR_VERSION >= 2
		AnimSequence->GetController().AddBoneCurve(*(Pair.Key), false);
		AnimSequence->GetController().SetBoneTrackKeys(*(Pair.Key), Pair.Value.PosKeys, Pair.Value.RotKeys, Pair.Value.ScaleKeys, false);
#else
		TArray<FBoneAnimationTrack>& BoneTracks = const_cast<TArray<FBoneAnimationTrack>&>(AnimSequence->GetDataModel()->GetBoneAnimationTracks());
		FBoneAnimationTrack BoneTrack;
		BoneTrack.Name = *Pair.Key;
		BoneTrack.BoneTreeIndex = BoneIndex;
		BoneTrack.InternalTrackData = Pair.Value;
		BoneTracks.Add(BoneTrack);
#endif
#else
		AnimSequence->AddNewRawTrack(*Pair.Key, const_cast<FRawAnimSequenceTrack*>(&Pair.Value));
#endif
#else
		CompressionCodec->Tracks[BoneIndex] = Pair.Value;
#endif
	}

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5
#if ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().SetFrameRate(FrameRate, false);
	AnimSequence->GetController().SetNumberOfFrames(NumFrames);
	AnimSequence->GetController().NotifyPopulated();
	AnimSequence->GetController().CloseBracket(false);
#else
	// hack for calling GenerateTransientData()
	AnimSequence->GetDataModel()->PostDuplicate(false);
#endif
#else
	AnimSequence->PostProcessSequence();
#endif
#else
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
#if ENGINE_MAJOR_VERSION > 4
	AnimSequence->CompressedData.CompressedDataStructure->CompressedNumberOfKeys = NumFrames;
#endif
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	AnimSequence->CompressedData.CurveCompressionCodec = NewObject<UglTFAnimCurveCompressionCodec>();
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
	}

UAnimSequence* FglTFRuntimeParser::CreateSkeletalAnimationFromPath(USkeletalMesh * SkeletalMesh, const TArray<FglTFRuntimePathItem>&BonesPath, const TArray<FglTFRuntimePathItem>&MorphTargetsPath, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	TSharedPtr<FJsonValue> JsonObject = GetJSONObjectFromPath(MorphTargetsPath);
	if (!JsonObject)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!JsonObject->TryGetArray(JsonArray))
	{
		AddError("CreateSkeletalAnimationFromPath()", "Expected a JSON array.");
		return nullptr;
	}

	const int32 NumFrames = JsonArray->Num();
	const float Duration = NumFrames / SkeletalAnimationConfig.FramesPerSecond;

	TMap<FName, TArray<TPair<float, float>>> MorphTargetCurves;

	// build the curves list
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
	{
		TSharedPtr<FJsonValue> JsonFrame = (*JsonArray)[FrameIndex];
		const TSharedPtr<FJsonObject>* JsonFrameObject = nullptr;
		if (JsonFrame->TryGetObject(JsonFrameObject))
		{
			for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : (*JsonFrameObject)->Values)
			{
				if (!MorphTargetCurves.Contains(*Pair.Key))
				{
					MorphTargetCurves.Add(*Pair.Key);
				}
			}
		}
		else
		{
			AddError("CreateSkeletalAnimationFromPath()", "Expected a JSON object for each frame.");
			return nullptr;
		}
	}

	TArray<FName> MorphTargetKeys;
	MorphTargetCurves.GetKeys(MorphTargetKeys);

	TMap<FName, float> CurrentFrameValues;

	for (const FName& Name : MorphTargetKeys)
	{
		CurrentFrameValues.Add(Name, 0);
	}

	const float FrameDuration = 1.0f / SkeletalAnimationConfig.FramesPerSecond;
	//fill curves
	for (int32 FrameIndex = 0; FrameIndex < NumFrames; FrameIndex++)
	{
		TSharedPtr<FJsonValue> JsonFrame = (*JsonArray)[FrameIndex];
		TSharedPtr<FJsonObject> JsonFrameObject = JsonFrame->AsObject(); // no need to check for errors

		const float Time = FrameDuration * FrameIndex;

		for (const FName& KeyName : MorphTargetKeys)
		{
			if (JsonFrameObject->Values.Contains(KeyName.ToString()))
			{
				double Value = 0;
				if (!JsonFrameObject->Values[KeyName.ToString()]->TryGetNumber(Value))
				{
					Value = 0;
				}
				MorphTargetCurves[KeyName].Add(TPair<float, float>{Time, Value});
				CurrentFrameValues[KeyName] = Value;
			}
			else
			{
				MorphTargetCurves[KeyName].Add(TPair<float, float>{Time, CurrentFrameValues[KeyName]});
			}
		}
	}


	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	AnimSequence->SetSkeleton(SkeletalMesh->GetSkeleton());
#else
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
#endif
	AnimSequence->SetPreviewMesh(SkeletalMesh);
#if ENGINE_MAJOR_VERSION > 4
#if WITH_EDITOR
	FFrameRate FrameRate(SkeletalAnimationConfig.FramesPerSecond, 1);
#if ENGINE_MINOR_VERSION < 2
	FIntProperty* IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfFrames")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);
	FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("PlayLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), Duration);
	IntProperty = CastField<FIntProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("NumberOfKeys")));
	IntProperty->SetPropertyValue_InContainer(AnimSequence->GetDataModel(), NumFrames);

	FStructProperty* StructProperty = CastField<FStructProperty>(UAnimDataModel::StaticClass()->FindPropertyByName(TEXT("FrameRate")));
	FFrameRate* FrameRatePtr = StructProperty->ContainerPtrToValuePtr<FFrameRate>(AnimSequence->GetDataModel());
	*FrameRatePtr = FrameRate;
#endif
#else
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
#if ENGINE_MINOR_VERSION >= 2
		FFloatProperty* FloatProperty = CastField<FFloatProperty>(UAnimSequence::StaticClass()->FindPropertyByName(TEXT("SequenceLength")));
	FloatProperty->SetPropertyValue_InContainer(AnimSequence, Duration);
#else
		AnimSequence->SequenceLength = Duration;
#endif
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
#endif
#else
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = Duration;
#endif
	AnimSequence->bEnableRootMotion = SkeletalAnimationConfig.bRootMotion;
	AnimSequence->RootMotionRootLock = SkeletalAnimationConfig.RootMotionRootLock;


#if WITH_EDITOR && ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().OpenBracket(FText::FromString("glTFRuntime"), false);
	AnimSequence->GetController().InitializeModel();
#endif

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
#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
		FAnimationCurveIdentifier CurveId(SmartName, ERawCurveTrackTypes::RCT_Float);
		AnimSequence->GetController().AddCurve(CurveId);
		FRichCurve RichCurve;
#else
		FAnimationCurveData& RawCurveData = const_cast<FAnimationCurveData&>(AnimSequence->GetDataModel()->GetCurveData());
		int32 NewCurveIndex = RawCurveData.FloatCurves.Add(FFloatCurve(SmartName, 0));
		FFloatCurve* NewCurve = &RawCurveData.FloatCurves[NewCurveIndex];
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif
#else
		FRawCurveTracks& CurveTracks = const_cast<FRawCurveTracks&>(AnimSequence->GetCurveData());
		int32 NewCurveIndex = CurveTracks.FloatCurves.Add(FFloatCurve(SmartName, 0));
		FFloatCurve* NewCurve = &CurveTracks.FloatCurves[NewCurveIndex];
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif
#else
		AnimSequence->RawCurveData.AddCurveData(SmartName);
		FFloatCurve* NewCurve = (FFloatCurve*)AnimSequence->RawCurveData.GetCurveData(SmartName.UID, ERawCurveTrackTypes::RCT_Float);
		FRichCurve& RichCurve = NewCurve->FloatCurve;
#endif

		for (TPair<float, float>& CurvePair : Pair.Value)
		{
			FKeyHandle NewKeyHandle = RichCurve.AddKey(CurvePair.Key, CurvePair.Value, false);

			ERichCurveInterpMode NewInterpMode = RCIM_Linear;
			ERichCurveTangentMode NewTangentMode = RCTM_Auto;
			ERichCurveTangentWeightMode NewTangentWeightMode = RCTWM_WeightedNone;

			float LeaveTangent = 0.f;
			float ArriveTangent = 0.f;
			float LeaveTangentWeight = 0.f;
			float ArriveTangentWeight = 0.f;

			RichCurve.SetKeyInterpMode(NewKeyHandle, NewInterpMode);
			RichCurve.SetKeyTangentMode(NewKeyHandle, NewTangentMode);
			RichCurve.SetKeyTangentWeightMode(NewKeyHandle, NewTangentWeightMode);
		}

		AnimSequence->GetSkeleton()->AccumulateCurveMetaData(Pair.Key, false, true);

#if !WITH_EDITOR
		AnimSequence->CompressedData.CompressedCurveNames.Add(SmartName);
		const_cast<FCurveMetaData*>(AnimSequence->GetSkeleton()->GetCurveMetaData(SmartName.UID))->Type.bMorphtarget = true;
#else
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
		AnimSequence->GetController().SetCurveKeys(CurveId, RichCurve.GetConstRefOfKeys());
#endif
#endif
	}

#if WITH_EDITOR
#if ENGINE_MAJOR_VERSION >= 5
#if ENGINE_MINOR_VERSION >= 2
	AnimSequence->GetController().SetFrameRate(FrameRate, false);
	AnimSequence->GetController().SetNumberOfFrames(NumFrames);
	AnimSequence->GetController().NotifyPopulated();
	AnimSequence->GetController().CloseBracket(false);
#else
	// hack for calling GenerateTransientData()
	AnimSequence->GetDataModel()->PostDuplicate(false);
#endif
#else
	AnimSequence->PostProcessSequence();
#endif
#else
	UglTFAnimBoneCompressionCodec* CompressionCodec = NewObject<UglTFAnimBoneCompressionCodec>();
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
#if ENGINE_MAJOR_VERSION > 4
	AnimSequence->CompressedData.CompressedDataStructure->CompressedNumberOfKeys = NumFrames;
#endif
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	UglTFAnimCurveCompressionCodec* AnimCurveCompressionCodec = NewObject<UglTFAnimCurveCompressionCodec>();
	AnimCurveCompressionCodec->AnimSequence = AnimSequence;
	AnimSequence->CompressedData.CurveCompressionCodec = AnimCurveCompressionCodec;
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
		}

FVector4 FglTFRuntimeParser::CubicSpline(const float TC, const float T0, const float T1, const FVector4 Value0, const FVector4 OutTangent, const FVector4 Value1, const FVector4 InTangent)
{
	float TD = T1 - T0;
	float T = (TC - T0) / TD;
	float TT = T * T;
	float TTT = TT * T;

	float S2 = -2 * TTT + 3 * TT;
	float S3 = TTT - TT;
	float S0 = 1 - S2;
	float S1 = S3 - TT + T;

	FVector4 CubicValue = S0 * Value0;
	CubicValue += S1 * OutTangent * TD;
	CubicValue += S2 * Value1;
	CubicValue += S3 * InTangent * TD;

	return CubicValue;
}

bool FglTFRuntimeParser::LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>&Tracks, TMap<FName, TArray<TPair<float, float>>>&MorphTargetCurves, float& Duration, const FglTFRuntimeSkeletalAnimationConfig & SkeletalAnimationConfig, TFunctionRef<bool(const FglTFRuntimeNode& Node)> Filter)
{
	TArray<FTransform> AnimWorldTransforms;
	TArray<FTransform> RetargetWorldTransforms;
	FReferenceSkeleton AnimRefSkeleton;
	FReferenceSkeleton RetargetRefSkeleton;

	// build retargeting structures
	if (SkeletalAnimationConfig.RetargetTo || SkeletalAnimationConfig.RetargetToSkeletalMesh)
	{
		if (SkeletalAnimationConfig.RetargetSkinIndex > INDEX_NONE)
		{
			TSharedPtr<FJsonObject>	JsonSkinObject = GetJsonObjectFromRootIndex("skins", SkeletalAnimationConfig.RetargetSkinIndex);
			if (!JsonSkinObject)
			{
				AddError("LoadSkeletalAnimation_Internal()", "Unable to find retarget skin.");
				return false;
			}


			TMap<int32, FName> AnimBoneMap;

			if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), AnimRefSkeleton, AnimBoneMap, FglTFRuntimeSkeletonConfig()))
			{
				AddError("LoadSkeletalAnimation_Internal()", "Unable to fill retarget RefSkeleton.");
				return false;
			}

			const TArray<FTransform>& BonesTransforms = AnimRefSkeleton.GetRefBonePose();
			for (int32 BoneIndex = 0; BoneIndex < AnimRefSkeleton.GetNum(); BoneIndex++)
			{
				AnimWorldTransforms.Add(BonesTransforms[BoneIndex]);
				int32 AnimParentIndex = AnimRefSkeleton.GetParentIndex(BoneIndex);
				while (AnimParentIndex > INDEX_NONE)
				{
					AnimWorldTransforms[BoneIndex] *= BonesTransforms[AnimParentIndex];
					AnimParentIndex = AnimRefSkeleton.GetParentIndex(AnimParentIndex);
				}
			}
		}

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
		RetargetRefSkeleton = SkeletalAnimationConfig.RetargetTo ? SkeletalAnimationConfig.RetargetTo->GetReferenceSkeleton() : SkeletalAnimationConfig.RetargetToSkeletalMesh->GetRefSkeleton();
#else
		RetargetRefSkeleton = SkeletalAnimationConfig.RetargetTo ? SkeletalAnimationConfig.RetargetTo->GetReferenceSkeleton() : SkeletalAnimationConfig.RetargetToSkeletalMesh->RefSkeleton;
#endif
		const TArray<FTransform>& BonesTransforms = RetargetRefSkeleton.GetRefBonePose();
		for (int32 BoneIndex = 0; BoneIndex < RetargetRefSkeleton.GetNum(); BoneIndex++)
		{
			RetargetWorldTransforms.Add(BonesTransforms[BoneIndex]);
			int32 RetargetParentIndex = RetargetRefSkeleton.GetParentIndex(BoneIndex);
			while (RetargetParentIndex > INDEX_NONE)
			{
				RetargetWorldTransforms[BoneIndex] *= BonesTransforms[RetargetParentIndex];
				RetargetParentIndex = RetargetRefSkeleton.GetParentIndex(RetargetParentIndex);
			}
		}
			}

	auto RetargetQuat = [&](const FQuat LocalAnimQuat, const FQuat WorldPoseQuat, const FQuat WorldParentPoseQuat, const FQuat WorldRetargetPoseQuat, const FQuat WorldRetargetParentPoseQuat) -> FQuat
	{

		FQuat WorldPoseToRetarget = WorldPoseQuat.Inverse() * WorldRetargetPoseQuat;

		FQuat WorldAnimQuat = WorldParentPoseQuat * LocalAnimQuat;

		// check for singularity
		if ((WorldAnimQuat | WorldPoseQuat) < 0)
		{
			WorldAnimQuat *= WorldPoseToRetarget * -1;
		}
		else
		{
			WorldAnimQuat *= WorldPoseToRetarget;
		}

		return WorldRetargetParentPoseQuat.Inverse() * WorldAnimQuat;
	};

	auto RetargetTransform = [&](const FTransform& LocalAnimTransform, const FTransform& WorldPoseTransform, const FTransform& WorldParentPoseTransform, const FTransform& WorldRetargetPoseTransform, const FTransform& WorldRetargetParentPoseTransform) -> FTransform
	{

		FMatrix WorldAnimMatrix = LocalAnimTransform.ToMatrixWithScale() * WorldParentPoseTransform.ToMatrixWithScale();

		FMatrix DeltaMatrix = WorldAnimMatrix * WorldPoseTransform.ToMatrixWithScale().Inverse();

		FMatrix WorldRetargetMatrix = DeltaMatrix * WorldRetargetPoseTransform.ToMatrixWithScale();

		return FTransform(WorldRetargetMatrix * WorldRetargetParentPoseTransform.ToMatrixWithScale().Inverse());
	};

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const FglTFRuntimeAnimationCurve& Curve)
	{
		FString TrackName = Node.Name;

		if (SkeletalAnimationConfig.CurvesNameMap.Contains(TrackName))
		{
			TrackName = SkeletalAnimationConfig.CurvesNameMap[TrackName];
		}

		if (SkeletalAnimationConfig.CurveRemapper.Remapper.IsBound())
		{
			TrackName = SkeletalAnimationConfig.CurveRemapper.Remapper.Execute(Node.Index, TrackName, Path, SkeletalAnimationConfig.CurveRemapper.Context);
		}

		if (SkeletalAnimationConfig.RemoveTracks.Contains(TrackName))
		{
			return;
		}

		int32 NumFrames = FMath::Max<int32>(Duration * SkeletalAnimationConfig.FramesPerSecond, 1);

		float FrameDelta = 1.f / SkeletalAnimationConfig.FramesPerSecond;

		if (Path == "rotation" && !SkeletalAnimationConfig.bRemoveRotations)
		{
			if (Curve.Timeline.Num() != Curve.Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
				return;
			}

			if (!Tracks.Contains(TrackName))
			{
				Tracks.Add(TrackName, FRawAnimSequenceTrack());
			}

			FRawAnimSequenceTrack& Track = Tracks[TrackName];

			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				const float FrameBase = FrameDelta * Frame;
				FQuat AnimQuat;
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Curve.Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 FirstQuatV = Curve.Values[FirstIndex];
				FVector4 SecondQuatV = Curve.Values[SecondIndex];
				FQuat FirstQuat = FQuat(FirstQuatV.X, FirstQuatV.Y, FirstQuatV.Z, FirstQuatV.W).GetNormalized();
				FQuat SecondQuat = FQuat(SecondQuatV.X, SecondQuatV.Y, SecondQuatV.Z, SecondQuatV.W).GetNormalized();

				// cubic spline ?
				if (FirstIndex != SecondIndex && Curve.Values.Num() == Curve.InTangents.Num() && Curve.InTangents.Num() == Curve.OutTangents.Num())
				{
					FVector4 CubicValue = CubicSpline(FrameBase, Curve.Timeline[FirstIndex], Curve.Timeline[SecondIndex], FirstQuatV, Curve.OutTangents[FirstIndex], SecondQuatV, Curve.InTangents[SecondIndex]);

					AnimQuat = { CubicValue.X, CubicValue.Y, CubicValue.Z, CubicValue.W };

					FMatrix RotationMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(AnimQuat.GetNormalized()) * SceneBasis;

					AnimQuat = RotationMatrix.ToQuat();
				}
				else if (FirstIndex == SecondIndex)
				{
					FMatrix RotationMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(FirstQuat) * SceneBasis;

					AnimQuat = RotationMatrix.ToQuat();
				}
				else
				{

					FMatrix FirstMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(FirstQuat) * SceneBasis;
					FMatrix SecondMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(SecondQuat) * SceneBasis;
					FirstQuat = FirstMatrix.ToQuat();
					SecondQuat = SecondMatrix.ToQuat();
					AnimQuat = FQuat::Slerp(FirstQuat, SecondQuat, Alpha);
				}

				if (SkeletalAnimationConfig.RetargetTo || SkeletalAnimationConfig.RetargetToSkeletalMesh)
				{
					const int32 RetargetBoneIndex = RetargetRefSkeleton.FindBoneIndex(*TrackName);
					if (RetargetBoneIndex > INDEX_NONE)
					{
						const int32 RetargetParentBoneIndex = RetargetRefSkeleton.GetParentIndex(RetargetBoneIndex);

						if (AnimWorldTransforms.Num() > 0)
						{
							const int32 AnimBoneIndex = AnimRefSkeleton.FindBoneIndex(*Node.Name);
							if (AnimBoneIndex > INDEX_NONE)
							{
								const int32 AnimParentBoneIndex = AnimRefSkeleton.GetParentIndex(AnimBoneIndex);

								AnimQuat = RetargetQuat(AnimQuat,
									AnimWorldTransforms[AnimBoneIndex].GetRotation(),
									AnimParentBoneIndex > INDEX_NONE ? AnimWorldTransforms[AnimParentBoneIndex].GetRotation() : FQuat::Identity,
									RetargetWorldTransforms[RetargetBoneIndex].GetRotation(),
									RetargetParentBoneIndex > INDEX_NONE ? RetargetWorldTransforms[RetargetParentBoneIndex].GetRotation() : FQuat::Identity
								).GetNormalized();
							}
						}
						else
						{
							AnimQuat = RetargetQuat(AnimQuat,
								GetNodeWorldTransform(Node).GetRotation(),
								GetParentNodeWorldTransform(Node).GetRotation(),
								RetargetWorldTransforms[RetargetBoneIndex].GetRotation(),
								RetargetParentBoneIndex > INDEX_NONE ? RetargetWorldTransforms[RetargetParentBoneIndex].GetRotation() : FQuat::Identity
							).GetNormalized();
						}
					}
				}

				if (SkeletalAnimationConfig.TransformPose.Contains(TrackName))
				{
					AnimQuat = SkeletalAnimationConfig.TransformPose[TrackName].TransformRotation(AnimQuat);
				}

				if (SkeletalAnimationConfig.FrameRotationRemapper.Remapper.IsBound())
				{
					AnimQuat = SkeletalAnimationConfig.FrameRotationRemapper.Remapper.Execute(TrackName, Frame, AnimQuat.Rotator(), SkeletalAnimationConfig.FrameRotationRemapper.Context).Quaternion();
				}

#if ENGINE_MAJOR_VERSION > 4
				Track.RotKeys.Add(FQuat4f(AnimQuat));
#else
				Track.RotKeys.Add(AnimQuat);
#endif
				}
			}
		else if (Path == "translation" && !SkeletalAnimationConfig.bRemoveTranslations)
		{
			if (Curve.Timeline.Num() != Curve.Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
				return;
			}

			if (!Tracks.Contains(TrackName))
			{
				Tracks.Add(TrackName, FRawAnimSequenceTrack());
			}

			FRawAnimSequenceTrack& Track = Tracks[TrackName];

			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				const float FrameBase = FrameDelta * Frame;
				FVector AnimLocation;
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Curve.Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Curve.Values[FirstIndex];
				FVector4 Second = Curve.Values[SecondIndex];

				// cubic spline ?
				if (FirstIndex != SecondIndex && Curve.Values.Num() == Curve.InTangents.Num() && Curve.InTangents.Num() == Curve.OutTangents.Num())
				{
					FVector4 CubicValue = CubicSpline(FrameBase, Curve.Timeline[FirstIndex], Curve.Timeline[SecondIndex], First, Curve.OutTangents[FirstIndex], Second, Curve.InTangents[SecondIndex]);

					AnimLocation = SceneBasis.TransformPosition(CubicValue) * SceneScale;
				}
				else
				{
					AnimLocation = SceneBasis.TransformPosition(FMath::Lerp(First, Second, Alpha)) * SceneScale;
				}

				if (SkeletalAnimationConfig.RetargetTo || SkeletalAnimationConfig.RetargetToSkeletalMesh)
				{
					const int32 RetargetBoneIndex = RetargetRefSkeleton.FindBoneIndex(*TrackName);
					if (RetargetBoneIndex > INDEX_NONE)
					{
						const int32 RetargetParentBoneIndex = RetargetRefSkeleton.GetParentIndex(RetargetBoneIndex);

						if (AnimWorldTransforms.Num() > 0)
						{
							const int32 AnimBoneIndex = AnimRefSkeleton.FindBoneIndex(*Node.Name);
							if (AnimBoneIndex > INDEX_NONE)
							{
								const int32 AnimParentBoneIndex = AnimRefSkeleton.GetParentIndex(AnimBoneIndex);

								FTransform LocalAnimTransform = AnimRefSkeleton.GetRefBonePose()[AnimBoneIndex];
								LocalAnimTransform.SetLocation(AnimLocation);

								AnimLocation = RetargetTransform(LocalAnimTransform,
									AnimWorldTransforms[AnimBoneIndex],
									AnimParentBoneIndex > INDEX_NONE ? AnimWorldTransforms[AnimParentBoneIndex] : FTransform::Identity,
									RetargetWorldTransforms[RetargetBoneIndex],
									RetargetParentBoneIndex > INDEX_NONE ? RetargetWorldTransforms[RetargetParentBoneIndex] : FTransform::Identity
								).GetLocation();
							}
						}
						else
						{
							FTransform LocalAnimTransform = Node.Transform;
							LocalAnimTransform.SetLocation(AnimLocation);

							AnimLocation = RetargetTransform(LocalAnimTransform,
								GetNodeWorldTransform(Node),
								GetParentNodeWorldTransform(Node),
								RetargetWorldTransforms[RetargetBoneIndex],
								RetargetParentBoneIndex > INDEX_NONE ? RetargetWorldTransforms[RetargetParentBoneIndex] : FTransform::Identity
							).GetLocation();
						}
					}
				}

				if (SkeletalAnimationConfig.TransformPose.Contains(TrackName))
				{
					AnimLocation = SkeletalAnimationConfig.TransformPose[TrackName].TransformPosition(AnimLocation);
				}

				if (SkeletalAnimationConfig.FrameTranslationRemapper.Remapper.IsBound())
				{
					AnimLocation = SkeletalAnimationConfig.FrameTranslationRemapper.Remapper.Execute(TrackName, Frame, AnimLocation, SkeletalAnimationConfig.FrameRotationRemapper.Context);
				}

#if ENGINE_MAJOR_VERSION > 4
				Track.PosKeys.Add(FVector3f(AnimLocation));
#else
				Track.PosKeys.Add(AnimLocation);
#endif
				}
			}
		else if (Path == "scale" && !SkeletalAnimationConfig.bRemoveScales)
		{
			if (Curve.Timeline.Num() != Curve.Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Curve.Timeline.Num(), Curve.Values.Num(), Node.Index));
				return;
			}

			if (!Tracks.Contains(TrackName))
			{
				Tracks.Add(TrackName, FRawAnimSequenceTrack());
			}

			FRawAnimSequenceTrack& Track = Tracks[TrackName];

			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				const float FrameBase = FrameDelta * Frame;
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Curve.Timeline, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Curve.Values[FirstIndex];
				FVector4 Second = Curve.Values[SecondIndex];
#if ENGINE_MAJOR_VERSION > 4
				Track.ScaleKeys.Add(FVector3f((SceneBasis.Inverse() * FScaleMatrix(FMath::Lerp(First, Second, Alpha)) * SceneBasis).ExtractScaling()));
#else
				Track.ScaleKeys.Add((SceneBasis.Inverse() * FScaleMatrix(FMath::Lerp(First, Second, Alpha)) * SceneBasis).ExtractScaling());
#endif
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

			if (Curve.Timeline.Num() * MorphTargetNames.Num() != Curve.Values.Num())
			{
				AddError("LoadSkeletalAnimation_Internal()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for weights on node %d"), Curve.Timeline.Num(), Curve.Values.Num() / MorphTargetNames.Num(), Node.Index));
				return;
			}

			for (int32 MorphTargetIndex = 0; MorphTargetIndex < MorphTargetNames.Num(); MorphTargetIndex++)
			{
				FName MorphTargetName = MorphTargetNames[MorphTargetIndex];
				TArray<TPair<float, float>> Curves;

				for (int32 TimelineIndex = 0; TimelineIndex < Curve.Timeline.Num(); TimelineIndex++)
				{
					TPair<float, float> NewCurve = TPair<float, float>(Curve.Timeline[TimelineIndex], Curve.Values[TimelineIndex * MorphTargetNames.Num() + MorphTargetIndex].X);
					Curves.Add(NewCurve);
				}
				MorphTargetCurves.Add(MorphTargetName, Curves);
			}
		}
		};

	FString IgnoredName;
	return LoadAnimation_Internal(JsonAnimationObject, Duration, IgnoredName, Callback, Filter, SkeletalAnimationConfig.OverrideTrackNameFromExtension);
		}


bool FglTFRuntimeParser::LoadSkinnedMeshRecursiveAsRuntimeLOD(const FString & NodeName, int32 & SkinIndex, const TArray<FString>&ExcludeNodes, FglTFRuntimeMeshLOD & RuntimeLOD, const FglTFRuntimeMaterialsConfig & MaterialsConfig, const FglTFRuntimeSkeletonConfig & SkeletonConfig)
{
	FglTFRuntimeNode Node;
	TArray<FglTFRuntimeNode> Nodes;

	if (NodeName.IsEmpty())
	{
		FglTFRuntimeScene Scene;
		if (!LoadScene(0, Scene))
		{
			AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", "No Scene found in asset");
			return false;
		}

		for (int32 NodeIndex : Scene.RootNodesIndices)
		{
			if (!LoadNodesRecursive(NodeIndex, Nodes))
			{
				AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", "Unable to build Node Tree from first Scene");
				return false;
			}
		}
	}
	else
	{
		if (!LoadNodeByName(NodeName, Node))
		{
			AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", FString::Printf(TEXT("Unable to find Node \"%s\""), *NodeName));
			return false;
		}

		if (!LoadNodesRecursive(Node.Index, Nodes))
		{
			AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", FString::Printf(TEXT("Unable to build Node Tree from \"%s\""), *NodeName));
			return false;
		}
	}

	if (SkinIndex <= INDEX_NONE)
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
				SkinIndex = ChildNode.SkinIndex;
				break;
			}
		}

		if (SkinIndex <= INDEX_NONE)
		{
			AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", "Unable to find a valid Skin");
			return false;
		}
	}

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
				AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", FString::Printf(TEXT("Unable to find Mesh with index %d"), ChildNode.MeshIndex));
				return false;
			}

			// keep track of primitives
			int32 PrimitiveFirstIndex = RuntimeLOD.Primitives.Num();

			FglTFRuntimeMeshLOD* LOD = nullptr;
			if (!LoadMeshIntoMeshLOD(JsonMeshObject.ToSharedRef(), LOD, MaterialsConfig))
			{
				return false;
			}

			RuntimeLOD.Primitives.Append(LOD->Primitives);

			// Always build an override map, to have a cache of the bone/index mapping

			TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", ChildNode.SkinIndex);
			if (!JsonSkinObject)
			{
				AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", FString::Printf(TEXT("Unable to fill skin %d"), ChildNode.SkinIndex));
				return false;
			}

			TMap<int32, FName> BoneMap;

			FReferenceSkeleton FakeRefSkeleton;
			if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), FakeRefSkeleton, BoneMap, SkeletonConfig))
			{
				AddError("LoadSkinnedMeshRecursiveAsRuntimeLOD()", "Unable to fill RefSkeleton.");
				return false;
			}

			// apply overrides
			for (int32 PrimitiveIndex = PrimitiveFirstIndex; PrimitiveIndex < RuntimeLOD.Primitives.Num(); PrimitiveIndex++)
			{
				FglTFRuntimePrimitive& Primitive = RuntimeLOD.Primitives[PrimitiveIndex];
				Primitive.OverrideBoneMap = BoneMap;
			}

		}
	}

	return true;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>&RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig & SkeletalMeshConfig)
{
	if (RuntimeLODs.Num() < 1)
	{
		return nullptr;
	}

	if (RuntimeLODs[0].Primitives.Num() < 1)
	{
		return nullptr;
	}

	if (RuntimeLODs[0].Primitives[0].OverrideBoneMap.Num() < 1)
	{
		AddError("LoadSkeletalMeshFromRuntimeLODs()", "Empty Primitive OverrideBoneMap");
		return nullptr;
	}

	const TMap<int32, FName>& BaseBoneMap = RuntimeLODs[0].Primitives[0].OverrideBoneMap;

	TSharedRef<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe> SkeletalMeshContext = MakeShared<FglTFRuntimeSkeletalMeshContext, ESPMode::ThreadSafe>(AsShared(), SkeletalMeshConfig);
	SkeletalMeshContext->SkinIndex = SkinIndex;

	SkeletalMeshContext->LODs.Add(const_cast<FglTFRuntimeMeshLOD*>(&RuntimeLODs[0]));

	auto ContainsBone = [BaseBoneMap](FName BoneName) -> bool
	{
		for (const TPair<int32, FName>& Pair : BaseBoneMap)
		{
			if (Pair.Value == BoneName)
			{
				return true;
			}
		}
		return false;
	};

	for (int32 LODIndex = 1; LODIndex < RuntimeLODs.Num(); LODIndex++)
	{
		if (RuntimeLODs[LODIndex].Primitives.Num() < 1)
		{
			AddError("LoadSkeletalMeshFromRuntimeLODs()", "Invalid RuntimeLOD, no Primitives defined");
			return nullptr;
		}

		for (const FglTFRuntimePrimitive& Primitive : RuntimeLODs[LODIndex].Primitives)
		{
			if (Primitive.OverrideBoneMap.Num() < 1)
			{
				AddError("LoadSkeletalMeshFromRuntimeLODs()", "Empty Primitive OverrideBoneMap");
				return nullptr;
			}

			FglTFRuntimePrimitive& NonConstPrimitive = const_cast<FglTFRuntimePrimitive&>(Primitive);

			for (TPair<int32, FName>& Pair : NonConstPrimitive.OverrideBoneMap)
			{
				if (!ContainsBone(Pair.Value))
				{
					AddError("LoadSkeletalMeshFromRuntimeLODs()", FString::Printf(TEXT("Unknown bone %s"), *Pair.Value.ToString()));
					return nullptr;
				}
			}
		}

		SkeletalMeshContext->LODs.Add(const_cast<FglTFRuntimeMeshLOD*>(&RuntimeLODs[LODIndex]));
	}

	if (!CreateSkeletalMeshFromLODs(SkeletalMeshContext))
	{
		AddError("LoadSkeletalMesh()", "Unable to load SkeletalMesh.");
		return nullptr;
	}

	return FinalizeSkeletalMeshWithLODs(SkeletalMeshContext);
}
