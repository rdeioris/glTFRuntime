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
		NewActor->SetFlags(RF_Transient);
		NewActor->Asset = Asset;
		NewActor->bAllowSkeletalAnimations = false;
		NewActor->bAllowNodeAnimations = false;
		NewActor->StaticMeshConfig.bGenerateStaticMeshDescription = true;
		NewActor->FinishSpawning(Transform);
		NewActor->DispatchBeginPlay();

		GEditor->SelectNone(true, true, true);
		GEditor->SelectActor(NewActor, true, true, false, true);
	}
}