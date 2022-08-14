// Copyright 2020-2022, Roberto De Ioris.

#include "glTFRuntimeEditor.h"

#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "glTFRuntime/Public/glTFRuntimeFunctionLibrary.h"
#include "glTFRuntime/Public/glTFRuntimeAssetActor.h"
#include "IDesktopPlatform.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FglTFRuntimeEditorModule"

static const FText LoadGLTFText = FText::FromString("Load GLTF Asset from File");

void FglTFRuntimeEditorModule::SpawnglTFRuntimeActor()
{
	IDesktopPlatform* DesktopPlatform = FDesktopPlatformModule::Get();
	if (DesktopPlatform)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		TArray<FString> OutFilenames;
		if (DesktopPlatform->OpenFileDialog(FSlateApplication::Get().FindBestParentWindowHandleForDialogs(nullptr),
			LoadGLTFText.ToString(),
			"",
			"",
			"GLTF Files|*.gltf;*.glb;*.zip;*.gz|",
			EFileDialogFlags::Type::None,
			OutFilenames) && OutFilenames.Num() > 0)
		{
			FglTFRuntimeConfig LoaderConfig;
			LoaderConfig.bAllowExternalFiles = true;
			UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(OutFilenames[0], false, LoaderConfig);
			if (Asset)
			{
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
		}
	}
}

void FglTFRuntimeEditorModule::BuildglTFRuntimeMenu(FMenuBuilder& Builder)
{
	Builder.BeginSection("glTFRuntime", FText::FromString("glTFRuntime"));
#if ENGINE_MAJOR_VERSION > 4
	Builder.AddMenuEntry(LoadGLTFText, LoadGLTFText, FSlateIcon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Default"), FExecuteAction::CreateRaw(this, &FglTFRuntimeEditorModule::SpawnglTFRuntimeActor));
#else
	Builder.AddMenuEntry(LoadGLTFText, LoadGLTFText, FSlateIcon(FEditorStyle::GetStyleSetName(), "ClassIcon.Default"), FExecuteAction::CreateRaw(this, &FglTFRuntimeEditorModule::SpawnglTFRuntimeActor));
#endif
	Builder.EndSection();
}


void FglTFRuntimeEditorModule::StartupModule()
{
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	LevelEditorModule.GetAllLevelViewportContextMenuExtenders().Add(FLevelEditorModule::FLevelViewportMenuExtender_SelectedActors::CreateLambda([&](const TSharedRef<FUICommandList> UICommandList, const TArray<AActor*> SelectedActors)
		{
			TSharedPtr<FExtender> Extender = MakeShared<FExtender>();
			Extender->AddMenuExtension("ActorPreview", EExtensionHook::After, UICommandList, FMenuExtensionDelegate::CreateRaw(this, &FglTFRuntimeEditorModule::BuildglTFRuntimeMenu));
			return Extender.ToSharedRef();
		}));
}

void FglTFRuntimeEditorModule::ShutdownModule()
{
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FglTFRuntimeEditorModule, glTFRuntimeEditor)
