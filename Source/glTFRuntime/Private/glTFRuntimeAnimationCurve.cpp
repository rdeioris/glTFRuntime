// Copyright 2020-2024, Roberto De Ioris.


#include "glTFRuntimeAnimationCurve.h"

UglTFRuntimeAnimationCurve::UglTFRuntimeAnimationCurve()
{
	glTFCurveAnimationIndex = INDEX_NONE;
	glTFCurveAnimationDuration = 0;
}

FTransform UglTFRuntimeAnimationCurve::GetTransformValue(float InTime) const
{
	FVector Location;
	Location.X = LocationCurves[0].Eval(InTime);
	Location.Y = LocationCurves[1].Eval(InTime);
	Location.Z = LocationCurves[2].Eval(InTime);

	FVector Scale;
	Scale.X = ScaleCurves[0].Eval(InTime);
	Scale.Y = ScaleCurves[1].Eval(InTime);
	Scale.Z = ScaleCurves[2].Eval(InTime);

	FMatrix Matrix = FScaleMatrix(Scale) * FTranslationMatrix(Location);
	FTransform Transform = FTransform(BasisMatrix.Inverse() * Matrix * BasisMatrix);

	if (ConvertedQuaternions.Num() > 0)
	{
		if (bIsStepped)
		{
			for (int32 Index = 0; Index < ConvertedQuaternions.Num(); Index++)
			{
				if (ConvertedQuaternions[Index].Key >= InTime)
				{
					Transform.SetRotation(ConvertedQuaternions[Index].Value);
					break;
				}
			}
			if (InTime <= ConvertedQuaternions[0].Key)
			{
				Transform.SetRotation(ConvertedQuaternions[0].Value);
			}
			else
			{
				Transform.SetRotation(ConvertedQuaternions.Last().Value);
			}
		}
		else
		{
			// TODO refactor it to reuse the skeletal animations one
			auto FindBestFrames = [&](const float WantedTime, int32& FirstIndex, int32& SecondIndex) -> float
				{
					SecondIndex = INDEX_NONE;
					// first search for second (higher value)
					for (int32 Index = 0; Index < ConvertedQuaternions.Num(); Index++)
					{
						float TimeValue = ConvertedQuaternions[Index].Key - ConvertedQuaternions[0].Key;
						if (FMath::IsNearlyEqual(TimeValue, WantedTime))
						{
							FirstIndex = Index;
							SecondIndex = Index;
							return 0;
						}
						else if (TimeValue > WantedTime)
						{
							SecondIndex = Index;
							break;
						}
					}

					// not found ? use the last value
					if (SecondIndex == INDEX_NONE)
					{
						SecondIndex = ConvertedQuaternions.Num() - 1;
					}

					if (SecondIndex == 0)
					{
						FirstIndex = 0;
						return 1.f;
					}

					FirstIndex = SecondIndex - 1;

					return ((WantedTime + ConvertedQuaternions[0].Key) - ConvertedQuaternions[FirstIndex].Key) / (ConvertedQuaternions[SecondIndex].Key - ConvertedQuaternions[FirstIndex].Key);
				};

			if (InTime <= ConvertedQuaternions[0].Key)
			{
				Transform.SetRotation(ConvertedQuaternions[0].Value);
			}
			else if (InTime >= ConvertedQuaternions.Last().Key)
			{
				Transform.SetRotation(ConvertedQuaternions.Last().Value);
			}
			else
			{
				int32 FirstIndex;
				int32 SecondIndex;
				const float Alpha = FindBestFrames(InTime, FirstIndex, SecondIndex);

				Transform.SetRotation(FQuat::Slerp(ConvertedQuaternions[FirstIndex].Value, ConvertedQuaternions[SecondIndex].Value, Alpha));
			}
		}

	}

	return Transform;
}

void UglTFRuntimeAnimationCurve::SetDefaultValues(const FVector Location, const FQuat Quat, const FRotator Rotator, const FVector Scale)
{
	LocationCurves[0].DefaultValue = Location.X;
	LocationCurves[1].DefaultValue = Location.Y;
	LocationCurves[2].DefaultValue = Location.Z;

	QuatCurves[0].DefaultValue = Quat.X;
	QuatCurves[1].DefaultValue = Quat.Y;
	QuatCurves[2].DefaultValue = Quat.Z;
	QuatCurves[3].DefaultValue = Quat.W;

	RotatorCurves[0].DefaultValue = Rotator.Roll;
	RotatorCurves[1].DefaultValue = Rotator.Pitch;
	RotatorCurves[2].DefaultValue = Rotator.Yaw;

	ScaleCurves[0].DefaultValue = Scale.X;
	ScaleCurves[1].DefaultValue = Scale.Y;
	ScaleCurves[2].DefaultValue = Scale.Z;
}

static const FName LocationXCurveName(TEXT("Location X"));
static const FName LocationYCurveName(TEXT("Location Y"));
static const FName LocationZCurveName(TEXT("Location Z"));
static const FName QuatXCurveName(TEXT("Quat X"));
static const FName QuatYCurveName(TEXT("Quat Y"));
static const FName QuatZCurveName(TEXT("Quat Z"));
static const FName QuatWCurveName(TEXT("Quat W"));
static const FName RotatorXCurveName(TEXT("Rotation X"));
static const FName RotatorYCurveName(TEXT("Rotation Y"));
static const FName RotatorZCurveName(TEXT("Rotation Z"));
static const FName ScaleXCurveName(TEXT("Scale X"));
static const FName ScaleYCurveName(TEXT("Scale Y"));
static const FName ScaleZCurveName(TEXT("Scale Z"));

TArray<FRichCurveEditInfoConst> UglTFRuntimeAnimationCurve::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[0], LocationXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[1], LocationYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[2], LocationZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&QuatCurves[0], QuatXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&QuatCurves[1], QuatYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&QuatCurves[2], QuatZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&QuatCurves[3], QuatWCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotatorCurves[0], RotatorXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotatorCurves[1], RotatorYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotatorCurves[2], RotatorZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&ScaleCurves[0], ScaleXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&ScaleCurves[1], ScaleYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&ScaleCurves[2], ScaleZCurveName));
	return Curves;
}

TArray<FRichCurveEditInfo> UglTFRuntimeAnimationCurve::GetCurves()
{
	TArray<FRichCurveEditInfo> Curves;
	Curves.Add(FRichCurveEditInfo(&LocationCurves[0], LocationXCurveName));
	Curves.Add(FRichCurveEditInfo(&LocationCurves[1], LocationYCurveName));
	Curves.Add(FRichCurveEditInfo(&LocationCurves[2], LocationZCurveName));
	Curves.Add(FRichCurveEditInfo(&QuatCurves[0], QuatXCurveName));
	Curves.Add(FRichCurveEditInfo(&QuatCurves[1], QuatYCurveName));
	Curves.Add(FRichCurveEditInfo(&QuatCurves[2], QuatZCurveName));
	Curves.Add(FRichCurveEditInfo(&QuatCurves[3], QuatWCurveName));
	Curves.Add(FRichCurveEditInfo(&RotatorCurves[0], RotatorXCurveName));
	Curves.Add(FRichCurveEditInfo(&RotatorCurves[1], RotatorYCurveName));
	Curves.Add(FRichCurveEditInfo(&RotatorCurves[2], RotatorZCurveName));
	Curves.Add(FRichCurveEditInfo(&ScaleCurves[0], ScaleXCurveName));
	Curves.Add(FRichCurveEditInfo(&ScaleCurves[1], ScaleYCurveName));
	Curves.Add(FRichCurveEditInfo(&ScaleCurves[2], ScaleZCurveName));
	return Curves;
}

bool UglTFRuntimeAnimationCurve::operator==(const UglTFRuntimeAnimationCurve& Curve) const
{
	return (LocationCurves[0] == Curve.LocationCurves[0]) &&
		(LocationCurves[1] == Curve.LocationCurves[1]) &&
		(LocationCurves[2] == Curve.LocationCurves[2]) &&
		(QuatCurves[0] == Curve.QuatCurves[0]) &&
		(QuatCurves[1] == Curve.QuatCurves[1]) &&
		(QuatCurves[2] == Curve.QuatCurves[2]) &&
		(QuatCurves[3] == Curve.QuatCurves[3]) &&
		(RotatorCurves[0] == Curve.RotatorCurves[0]) &&
		(RotatorCurves[1] == Curve.RotatorCurves[1]) &&
		(RotatorCurves[2] == Curve.RotatorCurves[2]) &&
		(ScaleCurves[0] == Curve.ScaleCurves[0]) &&
		(ScaleCurves[1] == Curve.ScaleCurves[1]) &&
		(ScaleCurves[2] == Curve.ScaleCurves[2]);
}

bool UglTFRuntimeAnimationCurve::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	return CurveInfo.CurveToEdit == &LocationCurves[0] ||
		CurveInfo.CurveToEdit == &LocationCurves[1] ||
		CurveInfo.CurveToEdit == &LocationCurves[2] ||
		CurveInfo.CurveToEdit == &QuatCurves[0] ||
		CurveInfo.CurveToEdit == &QuatCurves[1] ||
		CurveInfo.CurveToEdit == &QuatCurves[2] ||
		CurveInfo.CurveToEdit == &QuatCurves[3] ||
		CurveInfo.CurveToEdit == &RotatorCurves[0] ||
		CurveInfo.CurveToEdit == &RotatorCurves[1] ||
		CurveInfo.CurveToEdit == &RotatorCurves[2] ||
		CurveInfo.CurveToEdit == &ScaleCurves[1] ||
		CurveInfo.CurveToEdit == &ScaleCurves[2] ||
		CurveInfo.CurveToEdit == &ScaleCurves[3];
}

void UglTFRuntimeAnimationCurve::AddLocationValue(const float InTime, const FVector InLocation, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle LocationKey0 = LocationCurves[0].AddKey(InTime, InLocation.X);
	LocationCurves[0].SetKeyInterpMode(LocationKey0, InterpolationMode);
	FKeyHandle LocationKey1 = LocationCurves[1].AddKey(InTime, InLocation.Y);
	LocationCurves[1].SetKeyInterpMode(LocationKey1, InterpolationMode);
	FKeyHandle LocationKey2 = LocationCurves[2].AddKey(InTime, InLocation.Z);
	LocationCurves[2].SetKeyInterpMode(LocationKey2, InterpolationMode);
}

void UglTFRuntimeAnimationCurve::AddQuatValue(const float InTime, const FQuat InQuat, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle RotationKey0 = QuatCurves[0].AddKey(InTime, InQuat.X);
	QuatCurves[0].SetKeyInterpMode(RotationKey0, InterpolationMode);
	FKeyHandle RotationKey1 = QuatCurves[1].AddKey(InTime, InQuat.Y);
	QuatCurves[1].SetKeyInterpMode(RotationKey1, InterpolationMode);
	FKeyHandle RotationKey2 = QuatCurves[2].AddKey(InTime, InQuat.Z);
	QuatCurves[2].SetKeyInterpMode(RotationKey2, InterpolationMode);
	FKeyHandle RotationKey3 = QuatCurves[3].AddKey(InTime, InQuat.W);
	QuatCurves[3].SetKeyInterpMode(RotationKey3, InterpolationMode);
}

void UglTFRuntimeAnimationCurve::AddConvertedQuaternion(const float InTime, const FQuat InQuat, const bool bStep)
{
	int32 Index = 0;
	if (ConvertedQuaternions.Num() == 0 || ConvertedQuaternions.Last().Key < InTime)
	{
		Index = ConvertedQuaternions.Num();
	}
	else
	{
		for (; Index < ConvertedQuaternions.Num() && ConvertedQuaternions[Index].Key < InTime; ++Index);
	}

	ConvertedQuaternions.Insert(TPair<float, FQuat>{ InTime, InQuat }, Index);

	bIsStepped = bStep;
}

void UglTFRuntimeAnimationCurve::AddRotatorValue(const float InTime, const FRotator InRotator, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle RotationKey0 = RotatorCurves[0].AddKey(InTime, InRotator.Roll, true);
	RotatorCurves[0].SetKeyInterpMode(RotationKey0, InterpolationMode);
	FKeyHandle RotationKey1 = RotatorCurves[1].AddKey(InTime, InRotator.Pitch, true);
	RotatorCurves[1].SetKeyInterpMode(RotationKey1, InterpolationMode);
	FKeyHandle RotationKey2 = RotatorCurves[2].AddKey(InTime, InRotator.Yaw, true);
	RotatorCurves[2].SetKeyInterpMode(RotationKey2, InterpolationMode);
}

void UglTFRuntimeAnimationCurve::AddScaleValue(const float InTime, const FVector InScale, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle ScaleKey0 = ScaleCurves[0].AddKey(InTime, InScale.X);
	ScaleCurves[0].SetKeyInterpMode(ScaleKey0, InterpolationMode);
	FKeyHandle ScaleKey1 = ScaleCurves[1].AddKey(InTime, InScale.Y);
	ScaleCurves[1].SetKeyInterpMode(ScaleKey1, InterpolationMode);
	FKeyHandle ScaleKey2 = ScaleCurves[2].AddKey(InTime, InScale.Z);
	ScaleCurves[2].SetKeyInterpMode(ScaleKey2, InterpolationMode);
}
