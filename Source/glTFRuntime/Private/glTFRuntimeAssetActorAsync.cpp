// Copyright 2021-2023, Roberto De Ioris.


#include "glTFRuntimeAssetActorAsync.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Engine/StaticMeshSocket.h"

// Sets default values
AglTFRuntimeAssetActorAsync::AglTFRuntimeAssetActorAsync()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;

	bShowWhileLoading = true;
	bStaticMeshesAsSkeletal = false;

	bAllowLights = true;
}

// Called when the game starts or when spawned
void AglTFRuntimeAssetActorAsync::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset)
	{
		return;
	}

	LoadingStartTime = FPlatformTime::Seconds();

	TArray<FglTFRuntimeScene> Scenes = Asset->GetScenes();
	for (FglTFRuntimeScene& Scene : Scenes)
	{
		USceneComponent* SceneComponent = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Scene %d"), Scene.Index));
		SceneComponent->SetupAttachment(RootComponent);
		SceneComponent->RegisterComponent();
		AddInstanceComponent(SceneComponent);
		for (int32 NodeIndex : Scene.RootNodesIndices)
		{
			FglTFRuntimeNode Node;
			if (!Asset->GetNode(NodeIndex, Node))
			{
				return;
			}
			ProcessNode(SceneComponent, NAME_None, Node);
		}
	}

	LoadNextMeshAsync();
}

void AglTFRuntimeAssetActorAsync::ProcessNode(USceneComponent* NodeParentComponent, const FName SocketName, FglTFRuntimeNode& Node)
{
	// skip bones/joints
	if (Asset->NodeIsBone(Node.Index))
	{
		for (int32 ChildIndex : Node.ChildrenIndices)
		{
			FglTFRuntimeNode Child;
			if (!Asset->GetNode(ChildIndex, Child))
			{
				return;
			}
			ProcessNode(NodeParentComponent, *Child.Name, Child);
		}
		return;
	}

	USceneComponent* NewComponent = nullptr;
	if (Node.MeshIndex < 0)
	{
		NewComponent = NewObject<USceneComponent>(this, GetSafeNodeName<USceneComponent>(Node));
		NewComponent->SetupAttachment(NodeParentComponent);
		NewComponent->RegisterComponent();
		NewComponent->SetRelativeTransform(Node.Transform);
		AddInstanceComponent(NewComponent);
	}
	else
	{
		if (Node.SkinIndex < 0 && !bStaticMeshesAsSkeletal)
		{
			UStaticMeshComponent* StaticMeshComponent = NewObject<UStaticMeshComponent>(this, GetSafeNodeName<UStaticMeshComponent>(Node));
			StaticMeshComponent->SetupAttachment(NodeParentComponent);
			StaticMeshComponent->RegisterComponent();
			StaticMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(StaticMeshComponent);
			MeshesToLoad.Add(StaticMeshComponent, Node);
			NewComponent = StaticMeshComponent;
		}
		else
		{
			USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, GetSafeNodeName<USkeletalMeshComponent>(Node));
			SkeletalMeshComponent->SetupAttachment(NodeParentComponent);
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(SkeletalMeshComponent);
			MeshesToLoad.Add(SkeletalMeshComponent, Node);
			NewComponent = SkeletalMeshComponent;
		}
	}

	if (bAllowLights)
	{
		int32 LightIndex;
		if (Asset->GetNodeExtensionIndex(Node.Index, "KHR_lights_punctual", "light", LightIndex))
		{
			ULightComponent* LightComponent = Asset->LoadPunctualLight(LightIndex, this, LightConfig);
			if (LightComponent)
			{
				LightComponent->SetupAttachment(NewComponent);
				LightComponent->RegisterComponent();
				LightComponent->SetRelativeTransform(FTransform::Identity);
				AddInstanceComponent(LightComponent);
			}
		}
	}

	ReceiveOnNodeProcessed(Node.Index, NewComponent);

	if (!NewComponent)
	{
		return;
	}
	else
	{
		NewComponent->ComponentTags.Add(*FString::Printf(TEXT("glTFRuntime:NodeName:%s"), *Node.Name));
		NewComponent->ComponentTags.Add(*FString::Printf(TEXT("glTFRuntime:NodeIndex:%d"), Node.Index));

		if (SocketName != NAME_None)
		{
			SocketMapping.Add(NewComponent, SocketName);
		}
	}

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		FglTFRuntimeNode Child;
		if (!Asset->GetNode(ChildIndex, Child))
		{
			return;
		}
		ProcessNode(NewComponent, NAME_None, Child);
	}
}

void AglTFRuntimeAssetActorAsync::LoadNextMeshAsync()
{
	if (MeshesToLoad.Num() == 0)
	{
		return;
	}

	auto It = MeshesToLoad.CreateIterator();
	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(It->Key))
	{
		CurrentPrimitiveComponent = StaticMeshComponent;
		if (StaticMeshConfig.Outer == nullptr)
		{
			StaticMeshConfig.Outer = StaticMeshComponent;
		}
		FglTFRuntimeStaticMeshAsync Delegate;
		Delegate.BindDynamic(this, &AglTFRuntimeAssetActorAsync::LoadStaticMeshAsync);
		Asset->LoadStaticMeshAsync(It->Value.MeshIndex, Delegate, OverrideStaticMeshConfig(It->Value.Index, StaticMeshComponent));
	}
	else if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(It->Key))
	{
		CurrentPrimitiveComponent = SkeletalMeshComponent;
		FglTFRuntimeSkeletalMeshAsync Delegate;
		Delegate.BindDynamic(this, &AglTFRuntimeAssetActorAsync::LoadSkeletalMeshAsync);
		Asset->LoadSkeletalMeshAsync(It->Value.MeshIndex, It->Value.SkinIndex, Delegate, SkeletalMeshConfig);
	}
}

void AglTFRuntimeAssetActorAsync::LoadStaticMeshAsync(UStaticMesh* StaticMesh)
{
	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(CurrentPrimitiveComponent))
	{
		DiscoveredStaticMeshComponents.Add(StaticMeshComponent, StaticMesh);
		if (bShowWhileLoading)
		{
			StaticMeshComponent->SetStaticMesh(StaticMesh);
		}

		if (StaticMesh && !StaticMeshConfig.ExportOriginalPivotToSocket.IsEmpty())
		{
			UStaticMeshSocket* DeltaSocket = StaticMesh->FindSocket(FName(StaticMeshConfig.ExportOriginalPivotToSocket));
			if (DeltaSocket)
			{
				FTransform NewTransform = StaticMeshComponent->GetRelativeTransform();
				FVector DeltaLocation = -DeltaSocket->RelativeLocation * NewTransform.GetScale3D();
				DeltaLocation = NewTransform.GetRotation().RotateVector(DeltaLocation);
				NewTransform.AddToTranslation(DeltaLocation);
				StaticMeshComponent->SetRelativeTransform(NewTransform);
			}
		}

	}

	MeshesToLoad.Remove(CurrentPrimitiveComponent);
	if (MeshesToLoad.Num() > 0)
	{
		LoadNextMeshAsync();
	}
	// trigger event
	else
	{
		ScenesLoaded();
	}
}

void AglTFRuntimeAssetActorAsync::LoadSkeletalMeshAsync(USkeletalMesh* SkeletalMesh)
{
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(CurrentPrimitiveComponent))
	{
		DiscoveredSkeletalMeshComponents.Add(SkeletalMeshComponent, SkeletalMesh);
		if (bShowWhileLoading)
		{
			SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
		}
	}

	MeshesToLoad.Remove(CurrentPrimitiveComponent);
	if (MeshesToLoad.Num() > 0)
	{
		LoadNextMeshAsync();
	}
	// trigger event
	else
	{
		ScenesLoaded();
	}
}

void AglTFRuntimeAssetActorAsync::ScenesLoaded()
{
	if (!bShowWhileLoading)
	{
		for (const TPair<UStaticMeshComponent*, UStaticMesh*>& Pair : DiscoveredStaticMeshComponents)
		{
			Pair.Key->SetStaticMesh(Pair.Value);
		}

		for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
		{
			Pair.Key->SetSkeletalMesh(Pair.Value);
		}
	}

	for (TPair<USceneComponent*, FName>& Pair : SocketMapping)
	{
		for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& MeshPair : DiscoveredSkeletalMeshComponents)
		{
			if (MeshPair.Key->DoesSocketExist(Pair.Value))
			{
				Pair.Key->AttachToComponent(MeshPair.Key, FAttachmentTransformRules::KeepRelativeTransform, Pair.Value);
				Pair.Key->SetRelativeTransform(FTransform::Identity);
				break;
			}
		}
	}

	UE_LOG(LogGLTFRuntime, Log, TEXT("Asset loaded asynchronously in %f seconds"), FPlatformTime::Seconds() - LoadingStartTime);
	ReceiveOnScenesLoaded();
}

void AglTFRuntimeAssetActorAsync::ReceiveOnScenesLoaded_Implementation()
{

}

void AglTFRuntimeAssetActorAsync::PostUnregisterAllComponents()
{
	if (Asset)
	{
		Asset->ClearCache();
		Asset = nullptr;
	}
	Super::PostUnregisterAllComponents();
}

void AglTFRuntimeAssetActorAsync::ReceiveOnNodeProcessed_Implementation(const int32 NodeIndex, USceneComponent* NodeSceneComponent)
{

}

FglTFRuntimeStaticMeshConfig AglTFRuntimeAssetActorAsync::OverrideStaticMeshConfig_Implementation(const int32 NodeIndex, UStaticMeshComponent* NodeStaticMeshComponent)
{
	return StaticMeshConfig;
}