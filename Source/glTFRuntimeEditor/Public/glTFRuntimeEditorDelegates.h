// Copyright 2022, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "glTFRuntimeAssetActor.h"
#include "glTFRuntimeEditorDelegates.generated.h"

/**
 * 
 */
UCLASS()
class UglTFRuntimeEditorDelegates : public UObject
{
	GENERATED_BODY()
public:
	UFUNCTION()
	void SpawnFromClipboard(UglTFRuntimeAsset* Asset);
};
