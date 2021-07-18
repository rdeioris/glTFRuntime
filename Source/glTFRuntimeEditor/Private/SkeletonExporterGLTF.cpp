// Copyright 2020-2021, Roberto De Ioris.


#include "SkeletonExporterGLTF.h"

USkeletonExporterGLTF::USkeletonExporterGLTF(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SupportedClass = USkeleton::StaticClass();
	FormatExtension.Add(TEXT("gltf"));
	PreferredFormatIndex = 0;
	FormatDescription.Add(TEXT("glTF Embedded file"));
	bText = true;
}

void FglTFExportContextSkeleton::GetSkeletonBoneChildren(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex, TArray<int32>& BoneChildrenIndices)
{
	int32 NumBones = SkeletonRef.GetNum();
	for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		if (SkeletonRef.GetParentIndex(BoneIndex) == ParentBoneIndex)
		{
			BoneChildrenIndices.Add(BoneIndex);
		}
	}
}

FMatrix FglTFExportContextSkeleton::BuildBoneFullMatrix(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex)
{
	TArray<FTransform> BoneTransforms = SkeletonRef.GetRefBonePose();

	FTransform Transform = BoneTransforms[ParentBoneIndex];
	int32 BoneIndex = SkeletonRef.GetParentIndex(ParentBoneIndex);
	while (BoneIndex != INDEX_NONE)
	{
		Transform *= BoneTransforms[BoneIndex];
		BoneIndex = SkeletonRef.GetParentIndex(BoneIndex);
	}

	return Transform.ToMatrixWithScale();
}

void FglTFExportContextSkeleton::GenerateSkeleton(USkeleton* Skeleton)
{
	const FReferenceSkeleton& SkeletonRef = Skeleton->GetReferenceSkeleton();

	int32 NumBones = SkeletonRef.GetNum();

	TArray<FTransform> BoneTransforms = SkeletonRef.GetRefBonePose();

	FMatrix Basis = FBasisVectorMatrix(FVector(0, 1, 0), FVector(0, 0, 1), FVector(-1, 0, 0), FVector::ZeroVector);

	TArray<TSharedPtr<FJsonValue>> JsonJoints;
	TArray<float> MatricesData;

	for (int32 BoneIndex = 0; BoneIndex < NumBones; BoneIndex++)
	{
		TSharedRef<FJsonObject> JsonNode = MakeShared<FJsonObject>();
		JsonNode->SetStringField("name", SkeletonRef.GetBoneName(BoneIndex).ToString());
		TArray<TSharedPtr<FJsonValue>> JsonNodeChildren;
		TArray<int32> BoneChildren;
		GetSkeletonBoneChildren(SkeletonRef, BoneIndex, BoneChildren);
		for (int32 ChildBoneIndex : BoneChildren)
		{
			JsonNodeChildren.Add(MakeShared<FJsonValueNumber>(ChildBoneIndex));
		}

		if (JsonNodeChildren.Num() > 0)
		{
			JsonNode->SetArrayField("children", JsonNodeChildren);
		}

		FMatrix Matrix = Basis.Inverse() * BoneTransforms[BoneIndex].ToMatrixWithScale() * Basis;
		Matrix.ScaleTranslation(FVector::OneVector / 100);

		FMatrix FullMatrix = Basis.Inverse() * BuildBoneFullMatrix(SkeletonRef, BoneIndex) * Basis;
		FullMatrix.ScaleTranslation(FVector::OneVector / 100);
		FullMatrix = FullMatrix.Inverse();

		TArray<TSharedPtr<FJsonValue>> JsonNodeMatrix;
		for (int32 Row = 0; Row < 4; Row++)
		{
			for (int32 Col = 0; Col < 4; Col++)
			{
				JsonNodeMatrix.Add(MakeShared<FJsonValueNumber>(Matrix.M[Row][Col]));
				MatricesData.Add(FullMatrix.M[Row][Col]);
			}
		}
		JsonNode->SetArrayField("matrix", JsonNodeMatrix);

		JsonNodes.Add(MakeShared<FJsonValueObject>(JsonNode));
		JsonJoints.Add(MakeShared<FJsonValueNumber>(BoneIndex));
	}

	TArray<TSharedPtr<FJsonValue>> JsonSkins;
	TSharedRef<FJsonObject> JsonSkin = MakeShared<FJsonObject>();
	JsonSkin->SetStringField("name", Skeleton->GetName());
	JsonSkin->SetNumberField("inverseBindMatrices", 0);

	JsonSkin->SetArrayField("joints", JsonJoints);

	JsonSkins.Add(MakeShared<FJsonValueObject>(JsonSkin));

	JsonRoot->SetArrayField("skins", JsonSkins);

	// build accessors/bufferViews/buffers for bind matrices
	AppendAccessor(5126, NumBones, "MAT4", (uint8*)MatricesData.GetData(), NumBones * 16 * sizeof(float));
}

bool USkeletonExporterGLTF::ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags)
{
	USkeleton* Skeleton = CastChecked<USkeleton>(Object);

	TSharedRef<FglTFExportContextSkeleton> ExporterContext = MakeShared<FglTFExportContextSkeleton>();

	ExporterContext->GenerateSkeleton(Skeleton);

	Ar.Log(ExporterContext->GenerateJson());

	return true;
}

int32 FglTFExportContext::AppendAccessor(const int64 ComponentType, const uint64 Count, const FString& DataType, uint8* Data, uint64 Len, const bool bMinMax, FVector AccessorMin, FVector AccessorMax)
{
	TSharedRef<FJsonObject> JsonBuffer = MakeShared<FJsonObject>();
	JsonBuffer->SetNumberField("byteLength", Len);
	JsonBuffer->SetStringField("uri", "data:application/octet-stream;base64," + FBase64::Encode(Data, Len));
	int32 BufferIndex = JsonBuffers.Add(MakeShared<FJsonValueObject>(JsonBuffer));

	TSharedRef<FJsonObject> JsonBufferView = MakeShared<FJsonObject>();
	JsonBufferView->SetNumberField("buffer", BufferIndex);
	JsonBufferView->SetNumberField("byteLength", Len);
	JsonBufferView->SetNumberField("byteOffset", 0);
	int32 BufferViewIndex = JsonBufferViews.Add(MakeShared<FJsonValueObject>(JsonBufferView));

	TSharedRef<FJsonObject> JsonAccessor = MakeShared<FJsonObject>();
	JsonAccessor->SetNumberField("bufferView", BufferViewIndex);
	JsonAccessor->SetNumberField("componentType", ComponentType);
	JsonAccessor->SetNumberField("count", Count);
	JsonAccessor->SetStringField("type", DataType);

	if (bMinMax)
	{
		TArray<TSharedPtr<FJsonValue>> JsonAccessorMin;
		JsonAccessorMin.Add(MakeShared<FJsonValueNumber>(AccessorMin.X));
		JsonAccessorMin.Add(MakeShared<FJsonValueNumber>(AccessorMin.Y));
		JsonAccessorMin.Add(MakeShared<FJsonValueNumber>(AccessorMin.Z));

		TArray<TSharedPtr<FJsonValue>> JsonAccessorMax;
		JsonAccessorMax.Add(MakeShared<FJsonValueNumber>(AccessorMax.X));
		JsonAccessorMax.Add(MakeShared<FJsonValueNumber>(AccessorMax.Y));
		JsonAccessorMax.Add(MakeShared<FJsonValueNumber>(AccessorMax.Z));

		JsonAccessor->SetArrayField("min", JsonAccessorMin);
		JsonAccessor->SetArrayField("max", JsonAccessorMax);
	}

	return JsonAccessors.Add(MakeShared<FJsonValueObject>(JsonAccessor));
}

FglTFExportContext::FglTFExportContext()
{
	JsonRoot = MakeShared<FJsonObject>();

	TSharedRef<FJsonObject> JsonAsset = MakeShared<FJsonObject>();

	JsonAsset->SetStringField("generator", "Unreal Engine glTFRuntime Plugin");
	JsonAsset->SetStringField("version", "2.0");

	JsonRoot->SetObjectField("asset", JsonAsset);
}

FString FglTFExportContext::GenerateJson()
{
	JsonRoot->SetArrayField("scenes", JsonScenes);
	JsonRoot->SetArrayField("nodes", JsonNodes);
	JsonRoot->SetArrayField("accessors", JsonAccessors);
	JsonRoot->SetArrayField("bufferViews", JsonBufferViews);
	JsonRoot->SetArrayField("buffers", JsonBuffers);

	FString Json;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(MakeShared<FJsonValueObject>(JsonRoot), "", JsonWriter);

	return Json;
}
