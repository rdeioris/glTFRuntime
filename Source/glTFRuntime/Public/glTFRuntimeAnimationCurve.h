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
    FRichCurve QuatCurves[4];

    UPROPERTY()
    FRichCurve RotatorCurves[3];

    UPROPERTY()
    FRichCurve ScaleCurves[3];

    TArray<TPair<float, FQuat>> ConvertedQuaternions;
    bool bIsStepped;

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
    void AddQuatValue(const float InTime, const FQuat InQuat, const ERichCurveInterpMode InterpolationMode);
    void AddRotatorValue(const float InTime, const FRotator InRotator, const ERichCurveInterpMode InterpolationMode);
    void AddScaleValue(const float InTime, const FVector InScale, const ERichCurveInterpMode InterpolationMode);
    void SetDefaultValues(const FVector Location, const FQuat Quat, const FRotator Rotator, const FVector Scale);
    void AddConvertedQuaternion(const float InTime, const FQuat InQuat, const bool bStep);
};
