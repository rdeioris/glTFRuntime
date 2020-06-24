// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Exporters/Exporter.h"
#include "SkeletonExporterGLTF.generated.h"

/**
 * 
 */
UCLASS()
class GLTFRUNTIMEEDITOR_API USkeletonExporterGLTF : public UExporter
{
	GENERATED_UCLASS_BODY()
	
public:
	bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags) override;

protected:
	void GetSkeletonBoneChildren(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex, TArray<int32>& BoneChildrenIndices);
	FMatrix BuildBoneFullMatrix(const FReferenceSkeleton& SkeletonRef, const int32 ParentBoneIndex);
};
