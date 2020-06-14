// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFAnimBoneCompressionCodec.h"

void UglTFAnimBoneCompressionCodec::DecompressBone(FAnimSequenceDecompressionContext& DecompContext, int32 TrackIndex, FTransform& OutAtom) const
{
	UE_LOG(LogTemp, Error, TEXT("Decompress!!! %d Bone %f %f"), TrackIndex, DecompContext.Time, DecompContext.SequenceLength);

	OutAtom = FTransform(FRotator(0, 90, 0));
}

void UglTFAnimBoneCompressionCodec::DecompressPose(FAnimSequenceDecompressionContext& DecompContext, const BoneTrackArray& RotationPairs, const BoneTrackArray& TranslationPairs, const BoneTrackArray& ScalePairs, TArrayView<FTransform>& OutAtoms) const
{
	UE_LOG(LogTemp, Error, TEXT("Decompress!!! Pose %d"), OutAtoms.Num());

	TArray<FRotator> Rotations = { {0, 110, 0}, { 0, -90, 0} };

	//for (int32 i = 0; i < OutAtoms.Num(); i++)
	//{
		//AEF.GetPoseRotations(OutAtoms, RotationPairs, DecompContext);
	int32 A = 0;
	int32 B = 0;
	float Alpha = TimeToIndex(DecompContext.SequenceLength, DecompContext.RelativePos, Rotations.Num(), DecompContext.Interpolation, A, B);
	OutAtoms[1] = FTransform(FMath::Lerp(Rotations[A], Rotations[B], Alpha));

	//}
}

float UglTFAnimBoneCompressionCodec::TimeToIndex(
	float SequenceLength,
	float RelativePos,
	int32 NumKeys,
	EAnimInterpolationType Interpolation,
	int32& PosIndex0Out,
	int32& PosIndex1Out) const
{
	float Alpha;

	if (NumKeys < 2)
	{
		checkSlow(NumKeys == 1); // check if data is empty for some reason.
		PosIndex0Out = 0;
		PosIndex1Out = 0;
		return 0.0f;
	}
	// Check for before-first-frame case.
	if (RelativePos <= 0.f)
	{
		PosIndex0Out = 0;
		PosIndex1Out = 0;
		Alpha = 0.0f;
	}
	else
	{
		NumKeys -= 1; // never used without the minus one in this case
		// Check for after-last-frame case.
		if (RelativePos >= 1.0f)
		{
			// If we're not looping, key n-1 is the final key.
			PosIndex0Out = NumKeys;
			PosIndex1Out = NumKeys;
			Alpha = 0.0f;
		}
		else
		{
			// For non-looping animation, the last frame is the ending frame, and has no duration.
			const float KeyPos = RelativePos * float(NumKeys);
			checkSlow(KeyPos >= 0.0f);
			const float KeyPosFloor = floorf(KeyPos);
			PosIndex0Out = FMath::Min(FMath::TruncToInt(KeyPosFloor), NumKeys);
			Alpha = (Interpolation == EAnimInterpolationType::Step) ? 0.0f : KeyPos - KeyPosFloor;
			PosIndex1Out = FMath::Min(PosIndex0Out + 1, NumKeys);
		}
	}
	return Alpha;
}