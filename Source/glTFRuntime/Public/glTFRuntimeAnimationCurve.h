// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveBase.h"
#include "glTFRuntimeAnimationCurve.generated.h"

/**
 * 
 */
UCLASS(BlueprintType)
class GLTFRUNTIME_API UglTFRuntimeAnimationCurve : public UCurveBase
{
	GENERATED_BODY()
	
    UPROPERTY()
    FRichCurve LocationCurves[3];

    UPROPERTY()
    FRichCurve RotationCurves[3];

    UPROPERTY()
    FRichCurve ScaleCurves[3];

    // Begin FCurveOwnerInterface
    virtual TArray<FRichCurveEditInfoConst> GetCurves() const override;
    virtual TArray<FRichCurveEditInfo> GetCurves() override;

    /** Determine if Curve is the same */
    bool operator == (const UglTFRuntimeAnimationCurve& Curve) const;

    virtual bool IsValidCurve(FRichCurveEditInfo CurveInfo) override;

public:
    UglTFRuntimeAnimationCurve();

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "glTFRuntime|Curves")
    FString glTFCurveAnimationName;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "glTFRuntime|Curves")
    int32 glTFCurveAnimationIndex;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "glTFRuntime|Curves")
    float glTFCurveAnimationDuration;

    FMatrix BasisMatrix;

    /** Evaluate this float curve at the specified time */
    UFUNCTION(BlueprintCallable, Category = "glTFRuntime|Curves")
    FTransform GetTransformValue(float InTime) const;

    void AddLocationValue(const float InTime, const FVector InLocation, const ERichCurveInterpMode InterpolationMode);
    void AddRotationValue(const float InTime, const FVector InEulerRotation, const ERichCurveInterpMode InterpolationMode);
    void AddScaleValue(const float InTime, const FVector InScale, const ERichCurveInterpMode InterpolationMode);
    void SetDefaultValues(const FVector Location, const FVector EulerRotation, const FVector Scale);
};
