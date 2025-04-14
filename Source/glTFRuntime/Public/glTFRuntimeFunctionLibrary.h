// Copyright 2020-2024, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Kismet/BlueprintFunctionLibrary.h"
#include "glTFRuntimeAsset.h"
#include "Animation/BlendSpace1D.h"
#include "glTFRuntimeFunctionLibrary.generated.h"

DECLARE_DYNAMIC_DELEGATE_OneParam(FglTFRuntimeHttpResponse, UglTFRuntimeAsset*, Asset);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FglTFRuntimeHttpProgress, const FglTFRuntimeConfig&, LoaderConfig, int32, BytesProcessed, int32, TotalBytes);
DECLARE_DYNAMIC_DELEGATE_ThreeParams(FglTFRuntimeCommandResponse, UglTFRuntimeAsset*, Asset, const int32, ExitCode, const FString&, StdErr);

USTRUCT(BlueprintType)
struct FglTFRuntimeBlendSpaceSample
{
	GENERATED_BODY()

	UPROPERTY(EditAnyWhere, BlueprintReadWrite, Category="glTFRuntime")
	UAnimSequence* Animation = nullptr;

	UPROPERTY(EditAnyWhere, BlueprintReadWrite, Category="glTFRuntime")
	float Value = 0;
};

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

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Url with Cache", AutoCreateRefTerm = "LoaderConfig, Headers"), Category = "glTFRuntime")
	static void glTFLoadAssetFromUrlWithCache(const FString& Url, const FString& CacheFilename, const TMap<FString, FString>& Headers, const bool bUseCacheOnError, const FglTFRuntimeHttpResponse& Completed, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Url with Progress", AutoCreateRefTerm = "LoaderConfig, Headers"), Category = "glTFRuntime")
	static void glTFLoadAssetFromUrlWithProgress(const FString& Url, const TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Data", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromData(const TArray<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Clipboard", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static bool glTFLoadAssetFromClipboard(FglTFRuntimeHttpResponse Completed, FString& ClipboardContent, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Filename Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromFilenameAsync(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from String Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromStringAsync(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, BlueprintPure, meta = (DisplayName = "Make glTFRuntime PathItem Array from JSONPath String"), Category = "glTFRuntime")
	static TArray<FglTFRuntimePathItem> glTFRuntimePathItemArrayFromJSONPath(const FString& JSONPath);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get an array of bytes containing the glTF Runtime LOD indices"), Category = "glTFRuntime")
	static bool GetIndicesAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get an array of bytes containing the glTF Runtime LOD positions"), Category = "glTFRuntime")
	static bool GetPositionsAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Get an array of bytes containing the glTF Runtime LOD normals"), Category = "glTFRuntime")
	static bool GetNormalsAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Base64 String", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromBase64(const FString& Base64, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Base64 String Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromBase64Async(const FString& Base64, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from UTF8 String", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromUTF8String(const FString& String, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from UTF8 String Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromUTF8StringAsync(const FString& String, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Merge multiple glTF Runtime LODs"), Category = "glTFRuntime")
	static FglTFRuntimeMeshLOD glTFMergeRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Merge multiple glTF Runtime LODs with Skeleton"), Category = "glTFRuntime")
	static FglTFRuntimeMeshLOD glTFMergeRuntimeLODsWithSkeleton(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FString& RootBoneName = "root");

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from Command", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromCommand(const FString& Command, const FString& Arguments, const FString& WorkingDirectory, const FglTFRuntimeCommandResponse& Completed, const FglTFRuntimeConfig& LoaderConfig, const int32 ExpectedExitCode = 0);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from FileMap", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static UglTFRuntimeAsset* glTFLoadAssetFromFileMap(const TMap<FString, FString>& FileMap, const FglTFRuntimeConfig& LoaderConfig);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "glTF Load Asset from FileMap Async", AutoCreateRefTerm = "LoaderConfig"), Category = "glTFRuntime")
	static void glTFLoadAssetFromFileMapAsync(const TMap<FString, FString>& FileMap, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed);

	UFUNCTION(BlueprintCallable, meta = (DisplayName = "Create 1D BlendSpace"), Category = "glTFRuntime")
	static UBlendSpace1D* CreateRuntimeBlendSpace1D(const FString& ParameterName, const float Min, const float Max, const TArray<FglTFRuntimeBlendSpaceSample>& Samples);
};
