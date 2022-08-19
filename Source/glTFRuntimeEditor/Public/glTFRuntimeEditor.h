// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"
#include "UObject/StrongObjectPtr.h"

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
