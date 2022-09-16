// Copyright 2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimCurveCompressionCodec.h"
#include "glTFAnimCurveCompressionCodec.generated.h"

/**
 *
 */
UCLASS()
class GLTFRUNTIME_API UglTFAnimCurveCompressionCodec : public UAnimCurveCompressionCodec
{
	GENERATED_BODY()

public:

	virtual void DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const override;
	virtual float DecompressCurve(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, float CurrentTime) const override;

	UPROPERTY()
	UAnimSequence* AnimSequence = nullptr;
};
