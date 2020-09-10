// Copyright 2020, Roberto De Ioris.


#include "glTFRuntimeFunctionLibrary.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(const FString& Filename, const bool bPathRelativeToContent)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;

	FString TruePath = Filename;
	if (bPathRelativeToContent)
	{
		TruePath = FPaths::Combine(FPaths::ProjectContentDir(), Filename);
	}

	if (!Asset->LoadFromFilename(TruePath))
		return nullptr;

	return Asset;
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(const FString& JsonData)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromString(JsonData))
		return nullptr;

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(const FString& Url, TMap<FString, FString>& Headers, FglTFRuntimeHttpResponse Completed)
{
	TSharedRef<IHttpRequest> HttpRequest = FHttpModule::Get().CreateRequest();
	HttpRequest->SetURL(Url);
	for (TPair<FString, FString> Header : Headers)
	{
		HttpRequest->AppendToHeader(Header.Key, Header.Value);
	}

	HttpRequest->OnProcessRequestComplete().BindLambda([](FHttpRequestPtr RequestPtr, FHttpResponsePtr ResponsePtr, bool bSuccess, FglTFRuntimeHttpResponse Completed)
	{
		UglTFRuntimeAsset* Asset = nullptr;
		if (bSuccess)
		{
			Asset = glTFLoadAssetFromData(ResponsePtr->GetContent());
		}
		Completed.ExecuteIfBound(Asset);
	}, Completed);

	HttpRequest->ProcessRequest();
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(const TArray<uint8>& Data)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromData(Data.GetData(), Data.Num()))
		return nullptr;

	return Asset;
}