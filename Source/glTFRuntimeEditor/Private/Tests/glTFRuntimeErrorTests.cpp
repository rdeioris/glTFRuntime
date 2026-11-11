// Copyright 2026 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeEditor.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Misc/AutomationTest.h"

// The index in the error comes from a 64-bit JSON number: make sure it is reported verbatim
// (a mismatched printf specifier would print garbage or, since UE 5.9, fail to compile).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Errors_ImageMissingBufferView, "glTFRuntime.UnitTests.Errors.ImageMissingBufferView", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Errors_ImageMissingBufferView::RunTest(const FString& Parameters)
{
	const FString Json = TEXT("{\"asset\":{\"version\":\"2.0\"},\"images\":[{\"bufferView\":4294967303,\"mimeType\":\"image/png\"}]}");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Json, LoaderConfig);
	TestNotNull("Asset", Asset);
	if (!Asset)
	{
		return false;
	}

	FglTFRuntimeImagesConfig ImagesConfig;
	UTexture2D* Texture = Asset->LoadImage(0, ImagesConfig);
	TestNull("Texture", Texture);

	const TArray<FString> Errors = Asset->GetErrors();
	TestTrue("Errors contains \"GetJsonObjectBytes(): Unable to get bufferView: 4294967303\"", Errors.Contains("GetJsonObjectBytes(): Unable to get bufferView: 4294967303"));
	TestTrue("Errors contains \"LoadImageBytes(): Unable to load image 0\"", Errors.Contains("LoadImageBytes(): Unable to load image 0"));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Errors_MissingImage, "glTFRuntime.UnitTests.Errors.MissingImage", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Errors_MissingImage::RunTest(const FString& Parameters)
{
	const FString Json = TEXT("{\"asset\":{\"version\":\"2.0\"}}");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromString(Json, LoaderConfig);
	TestNotNull("Asset", Asset);
	if (!Asset)
	{
		return false;
	}

	FglTFRuntimeImagesConfig ImagesConfig;
	UTexture2D* Texture = Asset->LoadImage(3, ImagesConfig);
	TestNull("Texture", Texture);

	TestTrue("Errors contains \"LoadImageBytes(): Unable to load image 3\"", Asset->GetErrors().Contains("LoadImageBytes(): Unable to load image 3"));

	return true;
}

#endif
