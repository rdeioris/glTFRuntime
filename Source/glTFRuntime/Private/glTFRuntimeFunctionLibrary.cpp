// Copyright 2020-2024, Roberto De Ioris.


#include "glTFRuntimeFunctionLibrary.h"
#include "Animation/AnimSequence.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "GenericPlatform/GenericPlatformProcess.h"
#include "Runtime/Launch/Resources/Version.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	// Annoying copy, but we do not want to remove the const
	FglTFRuntimeConfig OverrideConfig = LoaderConfig;

	if (bPathRelativeToContent)
	{
		OverrideConfig.bSearchContentDir = true;
	}

	if (!Asset->LoadFromFilename(Filename, OverrideConfig))
	{
		return nullptr;
	}

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilenameAsync(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr);
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	// Annoying copy, but we do not want to remove the const
	FglTFRuntimeConfig OverrideConfig = LoaderConfig;

	if (bPathRelativeToContent)
	{
		OverrideConfig.bSearchContentDir = true;
	}

	Async(EAsyncExecution::Thread, [Filename, Asset, Completed, OverrideConfig]()
		{
			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromFilename(Filename, OverrideConfig);

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset);
					}
					else
					{
						Completed.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	if (!Asset->LoadFromString(JsonData, LoaderConfig))
	{
		return nullptr;
	}

	return Asset;
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromBase64(const FString& Base64, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	TArray<uint8> BytesBase64;

	if (!FBase64::Decode(Base64, BytesBase64))
	{
		return nullptr;
	}

	if (!Asset->LoadFromData(BytesBase64.GetData(), BytesBase64.Num(), LoaderConfig))
	{
		return nullptr;
	}

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromBase64Async(const FString& Base64, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr);
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	Async(EAsyncExecution::Thread, [Base64, Asset, LoaderConfig, Completed]()
		{
			TArray<uint8> BytesBase64;

			if (!FBase64::Decode(Base64, BytesBase64))
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Completed]()
					{
						Completed.ExecuteIfBound(nullptr);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromData(BytesBase64, LoaderConfig);

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset);
					}
					else
					{
						Completed.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUTF8String(const FString& String, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

#if ENGINE_MAJOR_VERSION >= 5
	auto UTF8String = StringCast<UTF8CHAR>(*String);
#else
	FTCHARToUTF8 UTF8String(*String);
#endif

	if (!Asset->LoadFromData(reinterpret_cast<const uint8*>(UTF8String.Get()), UTF8String.Length(), LoaderConfig))
	{
		return nullptr;
	}

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUTF8StringAsync(const FString& String, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr);
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	Async(EAsyncExecution::Thread, [String, Asset, LoaderConfig, Completed]()
		{
#if ENGINE_MAJOR_VERSION >= 5
			auto UTF8String = StringCast<UTF8CHAR>(*String);
#else
			FTCHARToUTF8 UTF8String(*String);
#endif

			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromData(reinterpret_cast<const uint8*>(UTF8String.Get()), UTF8String.Length(), LoaderConfig);

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset);
					}
					else
					{
						Completed.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromStringAsync(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr);
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	Async(EAsyncExecution::Thread, [JsonData, Asset, LoaderConfig, Completed]()
		{
			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromString(JsonData, LoaderConfig);

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset);
					}
					else
					{
						Completed.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFileMap(const TMap<FString, FString>& FileMap, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	TMap<FString, TArray64<uint8>> Map;

	for (const TPair<FString, FString>& Pair : FileMap)
	{
		TArray64<uint8> Data;
		if (FFileHelper::LoadFileToArray(Data, *Pair.Value))
		{
			Map.Add(Pair.Key, MoveTemp(Data));
		}
	}

	TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromMap(Map, LoaderConfig);

	if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
	{
		return Asset;
	}

	return nullptr;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFileMapAsync(const TMap<FString, FString>& FileMap, const FglTFRuntimeConfig& LoaderConfig, const FglTFRuntimeHttpResponse& Completed)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr);
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	Async(EAsyncExecution::Thread, [FileMap, Asset, LoaderConfig, Completed]()
		{
			TMap<FString, TArray64<uint8>> Map;

			for (const TPair<FString, FString>& Pair : FileMap)
			{
				TArray64<uint8> Data;
				if (FFileHelper::LoadFileToArray(Data, *Pair.Value))
				{
					Map.Add(Pair.Key, MoveTemp(Data));
				}
			}

			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromMap(Map, LoaderConfig);

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset);
					}
					else
					{
						Completed.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(const FString& Url, const TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
#else
	TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
#endif
	HttpRequest->SetURL(Url);
	for (TPair<FString, FString> Header : Headers)
	{
		HttpRequest->AppendToHeader(Header.Key, Header.Value);
	}

	float StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
		{
			UglTFRuntimeAsset* Asset = nullptr;
			if (bSuccess && !IsGarbageCollecting())
			{
				Asset = glTFLoadAssetFromData(ResponsePtr->GetContent(), LoaderConfig);
				if (Asset)
				{
					Asset->GetParser()->SetDownloadTime(FPlatformTime::Seconds() - StartTime);
				}
			}
			Completed.ExecuteIfBound(Asset);
		}, Completed, LoaderConfig);

	HttpRequest->ProcessRequest();
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrlWithCache(const FString& Url, const FString& CacheFilename, const TMap<FString, FString>& Headers, const bool bUseCacheOnError, const FglTFRuntimeHttpResponse& Completed, const FglTFRuntimeConfig& LoaderConfig)
{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
#else
	TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
#endif

	HttpRequest->SetURL(Url);

	bool bCacheFileValid = false;

	if (!CacheFilename.IsEmpty() && FPaths::FileExists(CacheFilename))
	{
		const FDateTime ModificationTime = IFileManager::Get().GetTimeStamp(*CacheFilename);
		HttpRequest->AppendToHeader("If-Modified-Since", ModificationTime.ToHttpDate());
		bCacheFileValid = true;
	}

	for (TPair<FString, FString> Header : Headers)
	{
		HttpRequest->AppendToHeader(Header.Key, Header.Value);
	}

	float StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime, bCacheFileValid, bUseCacheOnError](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig, const FString& CacheFilename)
		{
			UglTFRuntimeAsset* Asset = nullptr;
			if (!IsGarbageCollecting())
			{
				if (bSuccess)
				{
					if (ResponsePtr->GetResponseCode() == 304 && bCacheFileValid)
					{
						Asset = glTFLoadAssetFromFilename(CacheFilename, false, LoaderConfig);
					}
					else
					{
						if (!CacheFilename.IsEmpty())
						{
							FFileHelper::SaveArrayToFile(ResponsePtr->GetContent(), *CacheFilename);
						}
						Asset = glTFLoadAssetFromData(ResponsePtr->GetContent(), LoaderConfig);
					}
				}
				else if (bCacheFileValid && bUseCacheOnError)
				{
					Asset = glTFLoadAssetFromFilename(CacheFilename, false, LoaderConfig);
				}

				if (Asset)
				{
					Asset->GetParser()->SetDownloadTime(FPlatformTime::Seconds() - StartTime);
				}
			}
			Completed.ExecuteIfBound(Asset);
		}, Completed, LoaderConfig, CacheFilename);

	HttpRequest->ProcessRequest();
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrlWithProgress(const FString& Url, const TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig)
{
#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 25
	TSharedRef<IHttpRequest, ESPMode::ThreadSafe> HttpRequest = FHttpModule::Get().CreateRequest();
#else
	TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
#endif
	HttpRequest->SetURL(Url);
	for (TPair<FString, FString> Header : Headers)
	{
		HttpRequest->AppendToHeader(Header.Key, Header.Value);
	}

	float StartTime = FPlatformTime::Seconds();

	HttpRequest->OnProcessRequestComplete().BindLambda([StartTime](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
		{
			UglTFRuntimeAsset* Asset = nullptr;
			if (bSuccess && !IsGarbageCollecting())
			{
				Asset = glTFLoadAssetFromData(ResponsePtr->GetContent(), LoaderConfig);
				if (Asset)
				{
					Asset->GetParser()->SetDownloadTime(FPlatformTime::Seconds() - StartTime);
				}
			}
			Completed.ExecuteIfBound(Asset);
		}, Completed, LoaderConfig);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 4
	HttpRequest->OnRequestProgress64().BindLambda([](FHttpRequestPtr RequestPtr, uint64 BytesSent, uint64 BytesReceived, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig)
#else
	HttpRequest->OnRequestProgress().BindLambda([](FHttpRequestPtr RequestPtr, int32 BytesSent, int32 BytesReceived, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig)
#endif
		{
			int32 ContentLength = 0;
			if (RequestPtr->GetResponse().IsValid())
			{
				ContentLength = RequestPtr->GetResponse()->GetContentLength();
			}
			Progress.ExecuteIfBound(LoaderConfig, BytesReceived, ContentLength);
		}, Progress, LoaderConfig);

	HttpRequest->ProcessRequest();
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(const TArray<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	if (!Asset->LoadFromData(Data.GetData(), Data.Num(), LoaderConfig))
	{
		return nullptr;
	}

	return Asset;
}

bool UglTFRuntimeFunctionLibrary::glTFLoadAssetFromClipboard(FglTFRuntimeHttpResponse Completed, FString& ClipboardContent, const FglTFRuntimeConfig& LoaderConfig)
{

	FString Url;
	FPlatformApplicationMisc::ClipboardPaste(Url);

	if (Url.IsEmpty())
	{
		return false;
	}

	// escaped?
	if (Url.StartsWith("\"") && Url.EndsWith("\""))
	{
		Url = Url.RightChop(1).LeftChop(1);
	}

	ClipboardContent = Url;

	if (Url.Contains("://"))
	{
		glTFLoadAssetFromUrl(Url, {}, Completed, LoaderConfig);
		return true;
	}

	UglTFRuntimeAsset* Asset = glTFLoadAssetFromFilename(Url, false, LoaderConfig);
	Completed.ExecuteIfBound(Asset);

	return Asset != nullptr;
}

TArray<FglTFRuntimePathItem> UglTFRuntimeFunctionLibrary::glTFRuntimePathItemArrayFromJSONPath(const FString& JSONPath)
{
	TArray<FglTFRuntimePathItem> Paths;
	TArray<FString> Keys;
	JSONPath.ParseIntoArray(Keys, TEXT("."));

	for (const FString& Key : Keys)
	{
		FString PathKey = Key;
		int32 PathIndex = -1;

		int32 SquareBracketStart = 0;
		int32 SquareBracketEnd = 0;
		if (Key.FindChar('[', SquareBracketStart))
		{
			if (Key.FindChar(']', SquareBracketEnd))
			{
				if (SquareBracketEnd > SquareBracketStart)
				{
					const FString KeyIndex = Key.Mid(SquareBracketStart + 1, SquareBracketEnd - SquareBracketStart);
					PathIndex = FCString::Atoi(*KeyIndex);
					PathKey = Key.Left(SquareBracketStart);
				}
			}
		}

		FglTFRuntimePathItem PathItem;
		PathItem.Path = PathKey;
		PathItem.Index = PathIndex;
		Paths.Add(PathItem);
	}

	return Paths;
}

bool UglTFRuntimeFunctionLibrary::GetIndicesAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes)
{
	if (!RuntimeLOD.Primitives.IsValidIndex(PrimitiveIndex))
	{
		return false;
	}

	const FglTFRuntimePrimitive& Primitive = RuntimeLOD.Primitives[PrimitiveIndex];

	Bytes.AddUninitialized(Primitive.Indices.Num() * sizeof(uint32));
	FMemory::Memcpy(Bytes.GetData(), Primitive.Indices.GetData(), Primitive.Indices.Num() * sizeof(uint32));
	return true;
}

bool UglTFRuntimeFunctionLibrary::GetPositionsAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes)
{
	if (!RuntimeLOD.Primitives.IsValidIndex(PrimitiveIndex))
	{
		return false;
	}

	const FglTFRuntimePrimitive& Primitive = RuntimeLOD.Primitives[PrimitiveIndex];
	Bytes.Reserve(Primitive.Positions.Num() * sizeof(float) * 3);
	for (const FVector& Position : Primitive.Positions)
	{
		float X = static_cast<float>(Position.X);
		float Y = static_cast<float>(Position.Y);
		float Z = static_cast<float>(Position.Z);
		Bytes.Append(reinterpret_cast<const uint8*>(&X), sizeof(float));
		Bytes.Append(reinterpret_cast<const uint8*>(&Y), sizeof(float));
		Bytes.Append(reinterpret_cast<const uint8*>(&Z), sizeof(float));
	}
	return true;
}

bool UglTFRuntimeFunctionLibrary::GetNormalsAsBytesFromglTFRuntimeLODPrimitive(const FglTFRuntimeMeshLOD& RuntimeLOD, const int32 PrimitiveIndex, TArray<uint8>& Bytes)
{
	if (!RuntimeLOD.Primitives.IsValidIndex(PrimitiveIndex))
	{
		return false;
	}

	const FglTFRuntimePrimitive& Primitive = RuntimeLOD.Primitives[PrimitiveIndex];
	Bytes.Reserve(Primitive.Positions.Num() * sizeof(float) * 3);
	for (const FVector& Normal : Primitive.Normals)
	{
		float X = static_cast<float>(Normal.X);
		float Y = static_cast<float>(Normal.Y);
		float Z = static_cast<float>(Normal.Z);
		Bytes.Append(reinterpret_cast<const uint8*>(&X), sizeof(float));
		Bytes.Append(reinterpret_cast<const uint8*>(&Y), sizeof(float));
		Bytes.Append(reinterpret_cast<const uint8*>(&Z), sizeof(float));
	}
	return true;
}

FglTFRuntimeMeshLOD UglTFRuntimeFunctionLibrary::glTFMergeRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs)
{
	FglTFRuntimeMeshLOD NewRuntimeLOD;

	for (const FglTFRuntimeMeshLOD& RuntimeLOD : RuntimeLODs)
	{
		NewRuntimeLOD.Primitives.Append(RuntimeLOD.Primitives);
		NewRuntimeLOD.AdditionalTransforms.Append(RuntimeLOD.AdditionalTransforms);
		if (NewRuntimeLOD.Skeleton.Num() == 0)
		{
			NewRuntimeLOD.Skeleton = RuntimeLOD.Skeleton;
		}
		if (!NewRuntimeLOD.bHasNormals)
		{
			NewRuntimeLOD.bHasNormals = RuntimeLOD.bHasNormals;
		}
		if (NewRuntimeLOD.bHasTangents)
		{
			NewRuntimeLOD.bHasTangents = RuntimeLOD.bHasTangents;
		}
		if (!NewRuntimeLOD.bHasUV)
		{
			NewRuntimeLOD.bHasUV = RuntimeLOD.bHasUV;
		}
		if (!NewRuntimeLOD.bHasVertexColors)
		{
			NewRuntimeLOD.bHasVertexColors = RuntimeLOD.bHasVertexColors;
		}
	}

	return NewRuntimeLOD;
}

FglTFRuntimeMeshLOD UglTFRuntimeFunctionLibrary::glTFMergeRuntimeLODsWithSkeleton(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FString& RootBoneName)
{
	FglTFRuntimeMeshLOD NewRuntimeLOD;

	FglTFRuntimeBone RootBone;
	RootBone.BoneName = RootBoneName;
	RootBone.ParentIndex = INDEX_NONE;
	RootBone.Transform = FTransform::Identity;

	NewRuntimeLOD.Skeleton.Add(RootBone);

	TSet<FString> BoneNames;
	BoneNames.Add(RootBoneName);

	// build the skeleton
	for (const FglTFRuntimeMeshLOD& RuntimeLOD : RuntimeLODs)
	{
		NewRuntimeLOD.AdditionalTransforms.Append(RuntimeLOD.AdditionalTransforms);

		if (!NewRuntimeLOD.bHasNormals)
		{
			NewRuntimeLOD.bHasNormals = RuntimeLOD.bHasNormals;
		}
		if (NewRuntimeLOD.bHasTangents)
		{
			NewRuntimeLOD.bHasTangents = RuntimeLOD.bHasTangents;
		}
		if (!NewRuntimeLOD.bHasUV)
		{
			NewRuntimeLOD.bHasUV = RuntimeLOD.bHasUV;
		}
		if (!NewRuntimeLOD.bHasVertexColors)
		{
			NewRuntimeLOD.bHasVertexColors = RuntimeLOD.bHasVertexColors;
		}

		// we have a skeleton to merge
		if (RuntimeLOD.Skeleton.Num() > 0)
		{

		}
		// check for overrides
		else
		{
			for (int32 PrimitiveIndex = 0; PrimitiveIndex < RuntimeLOD.Primitives.Num(); PrimitiveIndex++)
			{
				const FglTFRuntimePrimitive& Primitive = RuntimeLOD.Primitives[PrimitiveIndex];
				// case for static meshes recursively merged as skinned
				if (Primitive.OverrideBoneMap.Num() == 1 && Primitive.OverrideBoneMap.Contains(0) && Primitive.Joints.Num() == 0 && Primitive.Weights.Num() == 0)
				{
					FglTFRuntimePrimitive NewPrimitive = Primitive;
					NewPrimitive.OverrideBoneMap.Empty();

					// let's add the bone to the skeleton
					FglTFRuntimeBone NewBone;
					NewBone.BoneName = Primitive.OverrideBoneMap[0].ToString();
					// name collision ?
					if (BoneNames.Contains(NewBone.BoneName))
					{
						NewBone.BoneName += FString("_") + FGuid::NewGuid().ToString();
					}
					NewBone.ParentIndex = 0;
					NewBone.Transform = FTransform::Identity;
					if (RuntimeLOD.AdditionalTransforms.IsValidIndex(PrimitiveIndex))
					{
						NewBone.Transform = RuntimeLOD.AdditionalTransforms[PrimitiveIndex];
					}

					BoneNames.Add(NewBone.BoneName);

					const int32 NewBoneIndex = NewRuntimeLOD.Skeleton.Add(NewBone);
					// fix joints and weights
					NewPrimitive.Joints.AddDefaulted();
					NewPrimitive.Weights.AddDefaulted();
					NewPrimitive.Joints[0].AddUninitialized(NewPrimitive.Positions.Num());
					NewPrimitive.Weights[0].AddUninitialized(NewPrimitive.Positions.Num());
					for (int32 VertexIndex = 0; VertexIndex < NewPrimitive.Joints[0].Num(); VertexIndex++)
					{
						NewPrimitive.Joints[0][VertexIndex].X = NewBoneIndex;
						NewPrimitive.Joints[0][VertexIndex].Y = 0;
						NewPrimitive.Joints[0][VertexIndex].Z = 0;
						NewPrimitive.Joints[0][VertexIndex].W = 0;
						NewPrimitive.Weights[0][VertexIndex].X = 1;
						NewPrimitive.Weights[0][VertexIndex].Y = 0;
						NewPrimitive.Weights[0][VertexIndex].Z = 0;
						NewPrimitive.Weights[0][VertexIndex].W = 0;
					}

					NewRuntimeLOD.Primitives.Add(MoveTemp(NewPrimitive));
				}
			}
		}
	}

	return NewRuntimeLOD;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromCommand(const FString& Command, const FString& Arguments, const FString& WorkingDirectory, const FglTFRuntimeCommandResponse& Completed, const FglTFRuntimeConfig& LoaderConfig, const int32 ExpectedExitCode)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		Completed.ExecuteIfBound(nullptr, -1, "");
		return;
	}

	Asset->RuntimeContextObject = LoaderConfig.RuntimeContextObject;
	Asset->RuntimeContextString = LoaderConfig.RuntimeContextString;

	Async(EAsyncExecution::Thread, [Command, Arguments, WorkingDirectory, Asset, LoaderConfig, Completed, ExpectedExitCode]()
		{
			TArray<uint8> Bytes;

			void* ReadPipe = nullptr;
			void* WritePipe = nullptr;

			if (!FPlatformProcess::CreatePipe(ReadPipe, WritePipe))
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Completed]()
					{
						Completed.ExecuteIfBound(nullptr, -1, "Unable to create process pipe");
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

			FProcHandle ProcHandle = FPlatformProcess::CreateProc(
				*Command,
				*Arguments,
				true,
				true,
				true,
				nullptr,
				0,
				WorkingDirectory.IsEmpty() ? nullptr : *WorkingDirectory,
				WritePipe,
				ReadPipe);

			if (!ProcHandle.IsValid())
			{
				FPlatformProcess::ClosePipe(ReadPipe, WritePipe);
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Completed]()
					{
						Completed.ExecuteIfBound(nullptr, -1, "Unable to launch process");
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

			while (FPlatformProcess::IsProcRunning(ProcHandle))
			{
				TArray<uint8> PipeChunk;
				if (FPlatformProcess::ReadPipeToArray(ReadPipe, PipeChunk))
				{
					Bytes.Append(PipeChunk);
				}
			}

			int32 ReturnCode = 0;
			FPlatformProcess::GetProcReturnCode(ProcHandle, &ReturnCode);

			FPlatformProcess::CloseProc(ProcHandle);
			FPlatformProcess::ClosePipe(ReadPipe, WritePipe);

			if (ReturnCode != ExpectedExitCode)
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Completed, ReturnCode, &Bytes]()
					{
						FString StdErr;
						FFileHelper::BufferToString(StdErr, Bytes.GetData(), Bytes.Num());
						Completed.ExecuteIfBound(nullptr, ReturnCode, StdErr);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

			TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromData(Bytes, LoaderConfig);
			if (!WorkingDirectory.IsEmpty())
			{
				Parser->SetBaseDirectory(WorkingDirectory);
			}

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([Parser, Asset, Completed, ReturnCode]()
				{
					if (Parser.IsValid() && Asset->SetParser(Parser.ToSharedRef()))
					{
						Completed.ExecuteIfBound(Asset, ReturnCode, "");
					}
					else
					{
						Completed.ExecuteIfBound(nullptr, ReturnCode, "Unable to parse command output");
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		});

}

UBlendSpace1D* UglTFRuntimeFunctionLibrary::CreateRuntimeBlendSpace1D(const FString& ParameterName, const float Min, const float Max, const TArray<FglTFRuntimeBlendSpaceSample>& Samples)
{
#if ENGINE_MAJOR_VERSION >= 5
	if (Samples.Num() < 1)
	{
		return nullptr;
	}

	USkeleton* CurrentSkeleton = nullptr;
	for (const FglTFRuntimeBlendSpaceSample& BlendSpaceSample : Samples)
	{
		if (!BlendSpaceSample.Animation)
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("BlendSpace Animation Sample cannot be NULL"));
			return nullptr;
		}

		if (!CurrentSkeleton)
		{
			CurrentSkeleton = BlendSpaceSample.Animation->GetSkeleton();
		}
		else if (BlendSpaceSample.Animation->GetSkeleton() != CurrentSkeleton)
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("BlendSpace Animation Skeleton mismatch"));
			return nullptr;
		}
	}

	UBlendSpace1D* BlendSpace = NewObject<UBlendSpace1D>();

	BlendSpace->SetSkeleton(CurrentSkeleton);

	FStructProperty* BlendParametersStructProperty = CastField<FStructProperty>(UBlendSpace1D::StaticClass()->FindPropertyByName("BlendParameters"));
	if (!BlendParametersStructProperty)
	{
		return nullptr;
	}

	FBlendParameter* BlendParameterX = BlendParametersStructProperty->ContainerPtrToValuePtr<FBlendParameter>(BlendSpace, 0);
	if (!BlendParameterX)
	{
		return nullptr;
	}

	BlendParameterX->DisplayName = ParameterName;
	BlendParameterX->Min = Min;
	BlendParameterX->Max = Max;
	BlendParameterX->GridNum = Samples.Num();

	FStructProperty* BlendSpaceDataStructProperty = CastField<FStructProperty>(UBlendSpace1D::StaticClass()->FindPropertyByName("BlendSpaceData"));
	if (!BlendSpaceDataStructProperty)
	{
		return nullptr;
	}

	FBlendSpaceData* BlendSpaceData = BlendSpaceDataStructProperty->ContainerPtrToValuePtr<FBlendSpaceData>(BlendSpace);
	if (!BlendSpaceData)
	{
		return nullptr;
	}

	FArrayProperty* DimensionIndicesArrayProperty = CastField<FArrayProperty>(UBlendSpace1D::StaticClass()->FindPropertyByName("DimensionIndices"));
	if (!DimensionIndicesArrayProperty)
	{
		return nullptr;
	}

	FScriptArrayHelper_InContainer DimensionArrayHelper(DimensionIndicesArrayProperty, BlendSpace);
	DimensionArrayHelper.Resize(1);
	int32* DimensionIndices0Value = DimensionIndicesArrayProperty->Inner->ContainerPtrToValuePtr<int32>(DimensionArrayHelper.GetRawPtr(0));
	*DimensionIndices0Value = 0;

	FArrayProperty* SampleDataArrayProperty = CastField<FArrayProperty>(UBlendSpace1D::StaticClass()->FindPropertyByName("SampleData"));
	if (!SampleDataArrayProperty)
	{
		return nullptr;
	}

	TArray<FglTFRuntimeBlendSpaceSample> SamplesSorted = Samples;
	SamplesSorted.Sort([](const FglTFRuntimeBlendSpaceSample& A, const FglTFRuntimeBlendSpaceSample& B) { return A.Value < B.Value; });

	BlendSpaceData->Empty();

	FScriptArrayHelper_InContainer ArrayHelper(SampleDataArrayProperty, BlendSpace);
	ArrayHelper.Resize(SamplesSorted.Num());

	for (int32 SampleIndex = 0; SampleIndex < SamplesSorted.Num(); SampleIndex++)
	{
		FBlendSample* BlendSample = SampleDataArrayProperty->Inner->ContainerPtrToValuePtr<FBlendSample>(ArrayHelper.GetRawPtr(SampleIndex));
		BlendSample->Animation = SamplesSorted[SampleIndex].Animation;
		BlendSample->SampleValue = FVector(SamplesSorted[SampleIndex].Value, 0, 0);
		if (BlendSample->Animation->GetPlayLength() > BlendSpace->AnimLength)
		{
			BlendSpace->AnimLength = BlendSample->Animation->GetPlayLength();
		}
#if WITH_EDITOR
		BlendSample->bIsValid = 1;
		BlendSample->CachedMarkerDataUpdateCounter = BlendSample->Animation->GetMarkerUpdateCounter();
#endif
		FBlendSpaceSegment BlendSpaceSegment;
		if (SamplesSorted.Num() == 1)
		{
			BlendSpaceSegment.SampleIndices[0] = 0;
			BlendSpaceSegment.SampleIndices[1] = 0;
			BlendSpaceSegment.Vertices[0] = 0.0;
			BlendSpaceSegment.Vertices[1] = 1.0;
			BlendSpaceData->Segments.Add(BlendSpaceSegment);
		}
		else if (SampleIndex < SamplesSorted.Num() - 1)
		{
			BlendSpaceSegment.SampleIndices[0] = SampleIndex;
			BlendSpaceSegment.SampleIndices[1] = SampleIndex + 1;
			BlendSpaceSegment.Vertices[0] = (SamplesSorted[SampleIndex].Value - Min) / (Max - Min);
			BlendSpaceSegment.Vertices[1] = (SamplesSorted[SampleIndex + 1].Value - Min) / (Max - Min);
			BlendSpaceData->Segments.Add(BlendSpaceSegment);
		}
	}

	return BlendSpace;
#else
	return nullptr;
#endif
}