// Copyright 2022, Roberto De Ioris.


#include "glTFAnimCurveCompressionCodec.h"

void UglTFAnimCurveCompressionCodec::DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const
{
	AnimSequence->GetCurveData().EvaluateCurveData(Curves, CurrentTime);
}

float UglTFAnimCurveCompressionCodec::DecompressCurve(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, float CurrentTime) const
{
	return reinterpret_cast<const FFloatCurve*>(AnimSequence->GetCurveData().GetCurveData(CurveUID))->Evaluate(CurrentTime);
}