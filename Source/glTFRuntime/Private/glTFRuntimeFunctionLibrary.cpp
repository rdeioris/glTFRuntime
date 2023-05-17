// Copyright 2020-2023, Roberto De Ioris.


#include "glTFRuntimeFunctionLibrary.h"
#include "Async/Async.h"
#include "HttpModule.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
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

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
		{
			UglTFRuntimeAsset* Asset = nullptr;
	if (bSuccess)
	{
		Asset = glTFLoadAssetFromData(ResponsePtr->GetContent(), LoaderConfig);
	}
	Completed.ExecuteIfBound(Asset);
		}, Completed, LoaderConfig);

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

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
		{
			UglTFRuntimeAsset* Asset = nullptr;
	if (bSuccess)
	{
		Asset = glTFLoadAssetFromData(ResponsePtr->GetContent(), LoaderConfig);
	}
	Completed.ExecuteIfBound(Asset);
		}, Completed, LoaderConfig);

	HttpRequest->OnRequestProgress().BindLambda([](FHttpRequestPtr RequestPtr, int32 BytesSent, int32 BytesReceived, FglTFRuntimeHttpProgress Progress, const FglTFRuntimeConfig& LoaderConfig)
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
					const FString KeyIndex = Key.Mid(SquareBracketStart + 1, SquareBracketEnd - SquareBracketEnd);
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