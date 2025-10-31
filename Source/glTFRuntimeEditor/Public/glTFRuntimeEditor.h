// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

namespace glTFRuntime
{
	namespace Tests
	{
		struct FFixture
		{
			FFixture(const FString& Filename);

			TArray64<uint8> Blob;
		};

		struct FFixture32
		{
			FFixture32(const FString& Filename);

			TArray<uint8> Blob;
		};

		struct FFixturePath
		{
			FFixturePath(const FString& Filename);

			FString Path;
		};
	}
};

class FglTFRuntimeEditorModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	void BuildglTFRuntimeMenu(class FMenuBuilder& Builder);
	void SpawnglTFRuntimeActor();
	void SpawnglTFRuntimeActorFromClipboard();

	TStrongObjectPtr<class UglTFRuntimeEditorDelegates> glTFRuntimeEditorDelegates;
};
