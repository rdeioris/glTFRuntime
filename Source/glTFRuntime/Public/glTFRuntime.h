// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "UObject/GCObject.h"
#include "Modules/ModuleManager.h"

class UglTFDataAsset;

class FglTFRuntimeModule
	: public IModuleInterface
	, public FGCObject
{
public:

	static const FglTFRuntimeModule& Get();
	
	UglTFDataAsset* GetGltfDataAsset() const;
	
	/** IModuleInterface implementation */
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	virtual void AddReferencedObjects(
		FReferenceCollector& Collector) override;

	virtual FString GetReferencerName() const override { return TEXT("GltfRuntimeModule"); }
	
private:

	UglTFDataAsset* GltfDataAsset = nullptr;
	
};
