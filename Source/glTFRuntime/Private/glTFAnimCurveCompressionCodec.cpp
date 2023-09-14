// Copyright 2023, Roberto De Ioris.


#include "glTFAnimCurveCompressionCodec.h"

#include "Animation/AnimSequence.h"

void UglTFAnimCurveCompressionCodec::DecompressCurves(const FCompressedAnimSequence& AnimSeq, FBlendedCurve& Curves, float CurrentTime) const
{
	if (!AnimSequence)
	{
		return;
	}
#if !WITH_EDITOR
	AnimSequence->GetCurveData().EvaluateCurveData(Curves, CurrentTime);
#endif
}

#if ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION >= 3
float UglTFAnimCurveCompressionCodec::DecompressCurve(const FCompressedAnimSequence& AnimSeq, FName CurveName, float CurrentTime) const
#else
float UglTFAnimCurveCompressionCodec::DecompressCurve(const FCompressedAnimSequence& AnimSeq, SmartName::UID_Type CurveUID, float CurrentTime) const
#endif
{
	if (!AnimSequence)
	{
		return 0.0f;
	}
#if !WITH_EDITOR
#if ENGINE_MAJOR_VERSION >=5 && ENGINE_MINOR_VERSION >= 3
	return static_cast<const FFloatCurve*>(AnimSequence->GetCurveData().GetCurveData(CurveName))->Evaluate(CurrentTime);
#else
	return static_cast<const FFloatCurve*>(AnimSequence->GetCurveData().GetCurveData(CurveUID))->Evaluate(CurrentTime);
#endif
#else
	return 0.0f;
#endif
}