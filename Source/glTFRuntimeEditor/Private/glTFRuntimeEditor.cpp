// Copyright 2020-2022, Roberto De Ioris.

#include "glTFRuntimeEditor.h"

#include "Developer/DesktopPlatform/Public/IDesktopPlatform.h"
#include "Developer/DesktopPlatform/Public/DesktopPlatformModule.h"
#include "glTFRuntimeEditorDelegates.h"
#include "glTFRuntime/Public/glTFRuntimeFunctionLibrary.h"
#include "glTFRuntime/Public/glTFRuntimeAssetActor.h"
#include "IDesktopPlatform.h"
#include "LevelEditor.h"

#define LOCTEXT_NAMESPACE "FglTFRuntimeEditorModule"

static const FText LoadGLTFText = FText::FromString("Load GLTF Asset from File");
static const FText LoadGLTFTextFromClipboard = FText::FromString("Load GLTF Asset from Clipboard");

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
		}
	}
}

void FglTFRuntimeEditorModule::SpawnglTFRuntimeActorFromClipboard()
{
	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAllowExternalFiles = true;
	glTFRuntimeEditorDelegates = TStrongObjectPtr<UglTFRuntimeEditorDelegates>(NewObject<UglTFRuntimeEditorDelegates>());

	FglTFRuntimeHttpResponse HttpResponse;
	HttpResponse.BindUFunction(glTFRuntimeEditorDelegates.Get(), GET_FUNCTION_NAME_CHECKED(UglTFRuntimeEditorDelegates, SpawnFromClipboard));

	FString ClipboardContent;

	if (!UglTFRuntimeFunctionLibrary::glTFLoadAssetFromClipboard(HttpResponse, ClipboardContent, LoaderConfig))
	{
		UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to load asset from clipboard"));
	}
}

void FglTFRuntimeEditorModule::BuildglTFRuntimeMenu(FMenuBuilder& Builder)
{
#if ENGINE_MAJOR_VERSION > 4
	FSlateIcon Icon(FAppStyle::GetAppStyleSetName(), "ClassIcon.Default");
#else
	FSlateIcon Icon(FEditorStyle::GetStyleSetName(), "ClassIcon.Default");
#endif
	Builder.BeginSection("glTFRuntime", FText::FromString("glTFRuntime"));
	Builder.AddMenuEntry(LoadGLTFText, LoadGLTFText, Icon, FExecuteAction::CreateRaw(this, &FglTFRuntimeEditorModule::SpawnglTFRuntimeActor));
	Builder.AddMenuEntry(LoadGLTFTextFromClipboard, LoadGLTFTextFromClipboard, Icon, FExecuteAction::CreateRaw(this, &FglTFRuntimeEditorModule::SpawnglTFRuntimeActorFromClipboard));
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
