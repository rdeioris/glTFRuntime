// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/Exporter.h"
#include "SkeletonExporterGLTF.generated.h"

class FglTFExportContext
{
public:
	FglTFExportContext();
	virtual ~FglTFExportContext() {};

	virtual FString GenerateJson();

protected:
	int32 AppendAccessor(const int64 ComponentType, const uint64 Count, const FString& DataType, uint8* Data, uint64 Len, const bool bMinMax = false, FVector AccessorMin = FVector::ZeroVector, FVector AccessorMax = FVector::ZeroVector);

	TSharedPtr<FJsonObject> JsonRoot;
	TArray<TSharedPtr<FJsonValue>> JsonScenes;
	TArray<TSharedPtr<FJsonValue>> JsonNodes;
	TArray<TSharedPtr<FJsonValue>> JsonAccessors;
	TArray<TSharedPtr<FJsonValue>> JsonBufferViews;
	TArray<TSharedPtr<FJsonValue>> JsonBuffers;
};

class FglTFExportContextSkeleton : public FglTFExportContext
{
public:
	void GenerateSkeleton(USkeleton* Skeleton);
protected:
	void GetSkeletonBoneChildren(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex, TArray<int32>& BoneChildrenIndices);
	FMatrix BuildBoneFullMatrix(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex);
};

/**
 * 
 */
UCLASS()
class GLTFRUNTIMEEDITOR_API USkeletonExporterGLTF : public UExporter
{
	GENERATED_UCLASS_BODY()
	
public:
	virtual bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags) override;
};
