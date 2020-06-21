// Copyright 2020, Roberto De Ioris


#include "glTFRuntimeFunctionLibrary.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"
#include "HttpModule.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(const FString Filename)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromFilename(Filename))
		return nullptr;

	return Asset;
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(const FString JsonData)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromString(JsonData))
		return nullptr;

	return Asset;
}

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFileDialog(const FString Title)
{
	TArray<FString> Filenames;
	if (GEngine && GEngine->GameViewport)
	{

		void* ParentWindowHandle = GEngine->GameViewport->GetWindow()->GetNativeWindow()->GetOSWindowHandle();
		IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
		if (DesktopPlatform)
		{
			DesktopPlatform->OpenFileDialog(ParentWindowHandle, Title, "", "", "", EFileDialogFlags::None, Filenames);
		}
	}

	if (Filenames.Num() != 1)
	{
		return nullptr;
	}

	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
	{
		return nullptr;
	}

	if (!Asset->LoadFromFilename(Filenames[0]))
	{
		return nullptr;
	}

	return Asset;
}

void UglTFRuntimeFunctionLibrary::glTFLoadAssetFromUrl(const FString Url, TMap<FString, FString> Headers, FglTFRuntimeHttpResponse Completed)
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
			Asset = glTFLoadAssetFromString(ResponsePtr->GetContentAsString());
		}
		Completed.ExecuteIfBound(Asset);
	}, Completed);

	HttpRequest->ProcessRequest();
}