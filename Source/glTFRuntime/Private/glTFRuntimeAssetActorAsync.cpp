// Copyright 2021, Roberto De Ioris.


#include "glTFRuntimeAssetActorAsync.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshSocket.h"

// Sets default values
AglTFRuntimeAssetActorAsync::AglTFRuntimeAssetActorAsync()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;
}

// Called when the game starts or when spawned
void AglTFRuntimeAssetActorAsync::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset)
	{
		return;
	}

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
			ProcessNode(SceneComponent, Node);
		}
	}

	LoadNextMeshAsync();
}

void AglTFRuntimeAssetActorAsync::ProcessNode(USceneComponent* NodeParentComponent, FglTFRuntimeNode& Node)
{
	// skip bones/joints
	if (Asset->NodeIsBone(Node.Index))
	{
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
		if (Node.SkinIndex < 0)
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

	if (!NewComponent)
	{
		return;
	}

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		FglTFRuntimeNode Child;
		if (!Asset->GetNode(ChildIndex, Child))
		{
			return;
		}
		ProcessNode(NewComponent, Child);
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
		Asset->LoadStaticMeshAsync(It->Value.MeshIndex, Delegate, StaticMeshConfig);
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
		StaticMeshComponent->SetStaticMesh(StaticMesh);

		if (StaticMeshConfig.Outer == nullptr)
		{
			StaticMeshConfig.Outer = StaticMeshComponent;
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
		ReceiveOnScenesLoaded();
	}
}

void AglTFRuntimeAssetActorAsync::LoadSkeletalMeshAsync(USkeletalMesh* SkeletalMesh)
{
	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(CurrentPrimitiveComponent))
	{
		SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
	}

	MeshesToLoad.Remove(CurrentPrimitiveComponent);
	if (MeshesToLoad.Num() > 0)
	{
		LoadNextMeshAsync();
	}
	// trigger event
	else
	{
		ReceiveOnScenesLoaded();
	}
}

void AglTFRuntimeAssetActorAsync::ReceiveOnScenesLoaded_Implementation()
{

}
