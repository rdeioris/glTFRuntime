// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "SkeletonExporterGLTF.h"
#include "SkeletalMeshExporterGLTF.generated.h"

class FglTFExportContextSkeletalMesh : public FglTFExportContextSkeleton
{
public:
	void GenerateSkeletalMesh(USkeletalMesh* SkeletalMesh);
};

/**
 * 
 */
UCLASS()
class GLTFRUNTIMEEDITOR_API USkeletalMeshExporterGLTF : public USkeletonExporterGLTF
{
	GENERATED_UCLASS_BODY()
	
public:
	virtual bool ExportText(const FExportObjectInnerContext* Context, UObject* Object, const TCHAR* Type, FOutputDevice& Ar, FFeedbackContext* Warn, uint32 PortFlags) override;
};
