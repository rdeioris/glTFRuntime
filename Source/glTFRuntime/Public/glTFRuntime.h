// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

class FglTFRuntimeModule : public IModuleInterface
{
public:

	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;
};
