// Copyright 2020, Roberto De Ioris.

#include "glTFRuntime.h"
#include "glTFDataAsset.h"

#define LOCTEXT_NAMESPACE "FglTFRuntimeModule"

const FglTFRuntimeModule& FglTFRuntimeModule::Get()
{
	return FModuleManager::LoadModuleChecked<FglTFRuntimeModule>(TEXT("glTFRuntime"));
}

void FglTFRuntimeModule::StartupModule()
{
	GltfDataAsset = LoadObject<UglTFDataAsset>(nullptr, TEXT("/glTFRuntime/DA_glTFRuntime"));
}

void FglTFRuntimeModule::ShutdownModule()
{
	GltfDataAsset = nullptr;
}

void FglTFRuntimeModule::AddReferencedObjects(
	FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(GltfDataAsset);
}

UglTFDataAsset* FglTFRuntimeModule::GetGltfDataAsset() const
{
	ensureAlways(GltfDataAsset);
	return GltfDataAsset;
}

#undef LOCTEXT_NAMESPACE
	
IMPLEMENT_MODULE(FglTFRuntimeModule, glTFRuntime)