// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeAnimSequence.h"

float UglTFRuntimeAnimSequence::GetDuration()
{
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    return SequenceLength;
    PRAGMA_ENABLE_DEPRECATION_WARNINGS
}

void UglTFRuntimeAnimSequence::SetDuration(float Duration)
{
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
    SequenceLength = Duration;
    PRAGMA_DISABLE_DEPRECATION_WARNINGS
}
