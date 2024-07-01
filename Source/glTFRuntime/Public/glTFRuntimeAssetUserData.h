// Copyright 2024, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Engine/AssetUserData.h"
#include "glTFRuntimeParser.h"
#include "glTFRuntimeAssetUserData.generated.h"

/**
 * 
 */
UCLASS(Abstract, Blueprintable, HideDropdown)
class GLTFRUNTIME_API UglTFRuntimeAssetUserData : public UAssetUserData
{
	GENERATED_BODY()

public:

	void SetParser(TSharedRef<FglTFRuntimeParser> InParser);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	FString GetStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	int32 GetIntegerFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	float GetFloatFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	bool GetBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	int32 GetArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (AutoCreateRefTerm = "Path"), Category = "glTFRuntime")
	FString GetJsonFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const;

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "Fill AssetUserData"))
	void ReceiveFillAssetUserData(const int32 Index);

protected:
	TSharedPtr<FglTFRuntimeParser> Parser;
};
