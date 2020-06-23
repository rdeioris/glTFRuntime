// Copyright 2020, Roberto De Ioris


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

	FQuat Rotation;
	Rotation.X = RotationCurves[0].Eval(InTime);
	Rotation.Y = RotationCurves[1].Eval(InTime);
	Rotation.Z = RotationCurves[2].Eval(InTime);
	Rotation.W = RotationCurves[3].Eval(InTime);

	FVector Scale;
	Scale.X = ScaleCurves[0].Eval(InTime);
	Scale.Y = ScaleCurves[1].Eval(InTime);
	Scale.Z = ScaleCurves[2].Eval(InTime);

	return FTransform(Rotation, Location, Scale);
}

void UglTFRuntimeAnimationCurve::SetDefaultValues(const FVector Location, const FQuat Rotation, const FVector Scale)
{
	LocationCurves[0].DefaultValue = Location.X;
	LocationCurves[1].DefaultValue = Location.Y;
	LocationCurves[2].DefaultValue = Location.Z;

	RotationCurves[0].DefaultValue = Rotation.X;
	RotationCurves[1].DefaultValue = Rotation.Y;
	RotationCurves[2].DefaultValue = Rotation.Z;
	RotationCurves[3].DefaultValue = Rotation.W;

	ScaleCurves[0].DefaultValue = Scale.X;
	ScaleCurves[1].DefaultValue = Scale.Y;
	ScaleCurves[2].DefaultValue = Scale.Z;
}

static const FName LocationXCurveName(TEXT("LocationX"));
static const FName LocationYCurveName(TEXT("LocationY"));
static const FName LocationZCurveName(TEXT("LocationZ"));
static const FName RotationXCurveName(TEXT("RotationX"));
static const FName RotationYCurveName(TEXT("RotationY"));
static const FName RotationZCurveName(TEXT("RotationZ"));
static const FName RotationWCurveName(TEXT("RotationW"));
static const FName ScaleXCurveName(TEXT("ScaleX"));
static const FName ScaleYCurveName(TEXT("ScaleY"));
static const FName ScaleZCurveName(TEXT("ScaleZ"));

TArray<FRichCurveEditInfoConst> UglTFRuntimeAnimationCurve::GetCurves() const
{
	TArray<FRichCurveEditInfoConst> Curves;
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[0], LocationXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[1], LocationYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&LocationCurves[2], LocationZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[0], RotationXCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[1], RotationYCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[2], RotationZCurveName));
	Curves.Add(FRichCurveEditInfoConst(&RotationCurves[3], RotationWCurveName));
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
	Curves.Add(FRichCurveEditInfo(&RotationCurves[0], RotationXCurveName));
	Curves.Add(FRichCurveEditInfo(&RotationCurves[1], RotationYCurveName));
	Curves.Add(FRichCurveEditInfo(&RotationCurves[2], RotationZCurveName));
	Curves.Add(FRichCurveEditInfo(&RotationCurves[3], RotationWCurveName));
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
		(RotationCurves[3] == Curve.RotationCurves[3]) &&
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
		CurveInfo.CurveToEdit == &RotationCurves[3] ||
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

void UglTFRuntimeAnimationCurve::AddRotationValue(const float InTime, const FQuat InRotation, const ERichCurveInterpMode InterpolationMode)
{
	FKeyHandle RotationKey0 = RotationCurves[0].AddKey(InTime, InRotation.X);
	RotationCurves[0].SetKeyInterpMode(RotationKey0, InterpolationMode);
	FKeyHandle RotationKey1 = RotationCurves[1].AddKey(InTime, InRotation.Y);
	RotationCurves[1].SetKeyInterpMode(RotationKey1, InterpolationMode);
	FKeyHandle RotationKey2 = RotationCurves[2].AddKey(InTime, InRotation.Z);
	RotationCurves[2].SetKeyInterpMode(RotationKey2, InterpolationMode);
	FKeyHandle RotationKey3 = RotationCurves[3].AddKey(InTime, InRotation.W);
	RotationCurves[3].SetKeyInterpMode(RotationKey3, InterpolationMode);
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
