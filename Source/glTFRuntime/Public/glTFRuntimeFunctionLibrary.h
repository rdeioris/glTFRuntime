// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeParser.h"
#include "glTFRuntimeFunctionLibrary.generated.h"

/**
 * 
 */
UCLASS()
class GLTFRUNTIME_API UglTFRuntimeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta=(DisplayName="glTF Load StaticMesh from Filename"))
	static UStaticMesh* glTFLoadStaticMeshFromFilename(FString Filename, int32 Index);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load StaticMeshes from Filename"))
	static TArray<UStaticMesh*> glTFLoadStaticMeshesFromFilename(FString Filename, bool& bSuccess);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Nodes from Filename"))
	static TArray<FglTFRuntimeNode> glTFLoadNodesFromFilename(FString Filename, bool& bSuccess);
	
};
