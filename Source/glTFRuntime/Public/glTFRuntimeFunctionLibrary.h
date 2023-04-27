// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeFunctionLibrary.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeHttpResponse, UglTFRuntimeAsset*, Asset);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FglTFRuntimeHttpProgress, const FglTFRuntimeConfig&, LoaderConfig, int32, BytesProcessed, int32, TotalBytes);

/**
 * 
 */
UCLASS()
class GLTFRUNTIME_API UglTFRuntimeFunctionLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintCallable, meta=(DisplayName="glTF Load Asset from Filename", AutoCreateRefTerm = "LoaderConfig"), Category="glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromFilename(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from String", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Url", AutoCreateRefTerm = "LoaderConfig, Headers"), Category = "glTFRuntime")
	static void glTFLoadAssetFromUrl(const FString& Url, const TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Url with Progress", AutoCreateRefTerm = "LoaderConfig, Headers"), Category = "glTFRuntime")
	static void glTFLoadAssetFromUrlWithProgress(const FString& Url, const TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Data", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromData(const TArray<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Clipboard", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static bool glTFLoadAssetFromClipboard(FglTFRuntimeHttpResponse Completed, FString& ClipboardContent, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Filename Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromFilenameAsync(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (DisplayName = "Make glTFRuntime PathItem Array from JSONPath String"), Category = "glTFRuntime")
	static TArray<FglTFRuntimePathItem> glTFRuntimePathItemArrayFromJSONPath(const FString& JSONPath);
};
