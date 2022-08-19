// Copyright 2022, Roberto De Ioris.

#include "glTFRuntimeEditorDelegates.h"

#include "LevelEditor.h"

void UglTFRuntimeEditorDelegates::SpawnFromClipboard(UglTFRuntimeAsset* Asset)
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	FTransform Transform = FTransform(GEditor->ClickLocation);
	AglTFRuntimeAssetActor* NewActor = LevelEditorModule.GetFirstLevelEditor()->GetWorld()->SpawnActorDeferred<AglTFRuntimeAssetActor>(AglTFRuntimeAssetActor::StaticClass(), Transform);
	if (NewActor)
	{
		NewActor->SetFlags(EObjectFlags::RF_DuplicateTransient);
		NewActor->Asset = Asset;
		NewActor->bAllowSkeletalAnimations = false;
		NewActor->bAllowNodeAnimations = false;
		NewActor->FinishSpawning(Transform);
		NewActor->DispatchBeginPlay();
	}
}