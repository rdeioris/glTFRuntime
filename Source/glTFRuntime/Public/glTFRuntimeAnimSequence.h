// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "glTFRuntimeAnimSequence.generated.h"

UCLASS()
class GLTFRUNTIME_API UglTFRuntimeAnimSequence : public UAnimSequence
{
    GENERATED_BODY()

public:
    float GetDuration();

    void SetDuration(float Duration);
};
