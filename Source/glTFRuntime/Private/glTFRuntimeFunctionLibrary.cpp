// Copyright 2020, Roberto De Ioris


#include "glTFRuntimeFunctionLibrary.h"
#include "DesktopPlatformModule.h"
#include "IDesktopPlatform.h"

UglTFRuntimeAsset* UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(const FString Filename)
{
	UglTFRuntimeAsset* Asset = NewObject<UglTFRuntimeAsset>();
	if (!Asset)
		return nullptr;
	if (!Asset->LoadFromFilename(Filename))
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