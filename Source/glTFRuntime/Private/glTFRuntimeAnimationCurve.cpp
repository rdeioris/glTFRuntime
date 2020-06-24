// Copyright 2020, Roberto De Ioris.


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

	FVector EulerRotation;
	EulerRotation.X = RotationCurves[0].Eval(InTime);
	EulerRotation.Y = RotationCurves[1].Eval(InTime);
	EulerRotation.Z = RotationCurves[2].Eval(InTime);

	FVector Scale;
	Scale.X = ScaleCurves[0].Eval(InTime);
	Scale.Y = ScaleCurves[1].Eval(InTime);
	Scale.Z = ScaleCurves[2].Eval(InTime);

	FMatrix Matrix = FScaleMatrix(Scale) * FRotationMatrix(FRotator::MakeFromEuler(EulerRotation)) * FTranslationMatrix(Location);
	return FTransform(BasisMatrix.Inverse() * Matrix * BasisMatrix);
}

void UglTFRuntimeAnimationCurve::SetDefaultValues(const FVector Location, const FVector EulerRotation, const FVector Scale)
{
	LocationCurves[0].DefaultValue = Location.X;
	LocationCurves[1].DefaultValue = Location.Y;
	LocationCurves[2].DefaultValue = Location.Z;

	RotationCurves[0].DefaultValue = EulerRotation.X;
	RotationCurves[1].DefaultValue = EulerRotation.Y;
	RotationCurves[2].DefaultValue = EulerRotation.Z;

	ScaleCurves[0].DefaultValue = Scale.X;
	ScaleCurves[1].DefaultValue = Scale.Y;
	ScaleCurves[2].DefaultValue = Scale.Z;
}

static const FName LocationXCurveName(TEXT("Location X"));
static const FName LocationYCurveName(TEXT("Location Y"));
static const FName LocationZCurveName(TEXT("Location Z"));
static const FName EulerRotationXCurveName(TEXT("EulerRotation X"));
static const FName EulerRotationYCurveName(TEXT("EulerRotation Y"));
static const FName EulerRotationZCurveName(TEXT("EulerRotation Z"));
static const FName ScaleXCurveName(TEXT("Scale X"));
static const FName ScaleYCurveName(TEXT("Scale Y"));
static const FName ScaleZCurveName(TEXT("Scale Z"));

TArray<FRichCurveEditInfoConst> UglTFRuntimeAnimationCurve::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[0], LocationXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[1], LocationYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[2], LocationZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[0], EulerRotationXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[1], EulerRotationYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[2], EulerRotationZCurveName));
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
	Curves.Add(FRichCurveEditInfo(&RotationCurves[0], EulerRotationXCurveName));
	Curves.Add(FRichCurveEditInfo(&RotationCurves[1], EulerRotationYCurveName));
	Curves.Add(FRichCurveEditInfo(&RotationCurves[2], EulerRotationZCurveName));
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
		(RotationCurves[0] == Curve.RotationCurves[0]) &&
		(RotationCurves[1] == Curve.RotationCurves[1]) &&
		(RotationCurves[2] == Curve.RotationCurves[2]) &&
		(ScaleCurves[0] == Curve.ScaleCurves[0]) &&
		(ScaleCurves[1] == Curve.ScaleCurves[1]) &&
		(ScaleCurves[2] == Curve.ScaleCurves[2]);
}

bool UglTFRuntimeAnimationCurve::IsValidCurve(FRichCurveEditInfo CurveInfo)
{
	return CurveInfo.CurveToEdit == &LocationCurves[0] ||
		CurveInfo.CurveToEdit == &LocationCurves[1] ||
		CurveInfo.CurveToEdit == &LocationCurves[2] ||
		CurveInfo.CurveToEdit == &RotationCurves[0] ||
		CurveInfo.CurveToEdit == &RotationCurves[1] ||
		CurveInfo.CurveToEdit == &RotationCurves[2] ||
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

void UglTFRuntimeAnimationCurve::AddRotationValue(const float InTime, const FVector InEulerRotation, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle RotationKey0 = RotationCurves[0].AddKey(InTime, InEulerRotation.X, true);
	RotationCurves[0].SetKeyInterpMode(RotationKey0, InterpolationMode);
	FKeyHandle RotationKey1 = RotationCurves[1].AddKey(InTime, InEulerRotation.Y, true);
	RotationCurves[1].SetKeyInterpMode(RotationKey1, InterpolationMode);
	FKeyHandle RotationKey2 = RotationCurves[2].AddKey(InTime, InEulerRotation.Z, true);
	RotationCurves[2].SetKeyInterpMode(RotationKey2, InterpolationMode);
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
