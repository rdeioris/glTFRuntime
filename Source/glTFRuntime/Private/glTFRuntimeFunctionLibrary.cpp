// Copyright 2020, Roberto De Ioris.


#include "glTFRuntimeFunctionLibrary.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(const FString& Filename, const bool bPathRelativeToContent, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;

	FString TruePath = Filename;
	if (bPathRelativeToContent)
	{
		TruePath = FPaths::Combine(FPaths::ProjectContentDir(), Filename);
	}

	if (!Asset->LoadFromFilename(TruePath, LoaderConfig))
		return nullptr;

	return Asset;
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromString(JsonData, LoaderConfig))
		return nullptr;

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(const FString& Url, TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed, const FglTFRuntimeConfig& LoaderConfig)
{
#if ENGINE_MINOR_VERSION > 25
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

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(const TArray<uint8>& Data, const FglTFRuntimeConfig& LoaderConfig)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromData(Data.GetData(), Data.Num(), LoaderConfig))
		return nullptr;

	return Asset;
}