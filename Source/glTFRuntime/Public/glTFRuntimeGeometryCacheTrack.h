// Copyright 2020-2025, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeParser.h"
#include "GeometryCacheTrack.h"
#include "glTFRuntimeGeometryCacheTrack.generated.h"

/**
 *
 */
UCLASS(BlueprintType)
class GLTFRUNTIME_API UglTFRuntimeGeometryCacheTrack : public UGeometryCacheTrack
{
	GENERATED_BODY()

public:
	virtual const bool UpdateBoundsData(const float Time, const bool bLooping, const bool bIsPlayingBackward, int32& InOutBoundsSampleIndex, FBox& OutBounds) override;
	virtual const FGeometryCacheTrackSampleInfo& GetSampleInfo(float Time, const bool bLooping) override;
	virtual const bool UpdateMeshData(const float Time, const bool bLooping, int32& InOutMeshSampleIndex, FGeometryCacheMeshData*& OutMeshData) override;

protected:
	FGeometryCacheTrackSampleInfo SampleInfo;
};
