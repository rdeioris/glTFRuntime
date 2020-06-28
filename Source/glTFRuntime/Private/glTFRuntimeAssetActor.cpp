// Copyright 2020, Roberto De Ioris.


#include "glTFRuntimeAssetActor.h"

// Sets default values
AglTFRuntimeAssetActor::AglTFRuntimeAssetActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;
}

// Called when the game starts or when spawned
void AglTFRuntimeAssetActor::BeginPlay()
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
}

void AglTFRuntimeAssetActor::ProcessNode(USceneComponent* NodeParentComponent, FglTFRuntimeNode& Node)
{
	// skip bones/joints
	if (Asset->NodeIsBone(Node.Index))
	{
		return;
	}

	USceneComponent* NewComponent = nullptr;
	if (Node.MeshIndex < 0)
	{
		NewComponent = NewObject<USceneComponent>(this, *Node.Name);
		NewComponent->SetupAttachment(NodeParentComponent);
		NewComponent->RegisterComponent();
		NewComponent->SetRelativeTransform(Node.Transform);
		AddInstanceComponent(NewComponent);
	}
	else
	{
		if (Node.SkinIndex < 0)
		{
			UStaticMeshComponent* StaticMeshComponent = NewObject<UStaticMeshComponent>(this, *Node.Name);
			StaticMeshComponent->SetupAttachment(NodeParentComponent);
			StaticMeshComponent->RegisterComponent();
			StaticMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(StaticMeshComponent);
			FglTFRuntimeStaticMeshConfig StaticMeshConfig;
			StaticMeshComponent->SetStaticMesh(Asset->LoadStaticMesh(Node.MeshIndex, StaticMeshConfig));
			NewComponent = StaticMeshComponent;
		}
		else
		{
			USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, *Node.Name);
			SkeletalMeshComponent->SetupAttachment(NodeParentComponent);
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(SkeletalMeshComponent);
			FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;
			SkeletalMeshComponent->SetSkeletalMesh(Asset->LoadSkeletalMesh(Node.MeshIndex, Node.SkinIndex, SkeletalMeshConfig));
			NewComponent = SkeletalMeshComponent;
		}
	}

	if (!NewComponent)
	{
		return;
	}

	// check for animations
	if (!NewComponent->IsA<USkeletalMeshComponent>())
	{
		UglTFRuntimeAnimationCurve* AnimationCurve = Asset->LoadNodeAnimationCurve(Node.Index);
		if (AnimationCurve)
		{
			CurveBasedAnimations.Add(NewComponent, AnimationCurve);
			CurveBasedAnimationsTimeTracker.Add(NewComponent, 0);
		}
	}
	else
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(NewComponent);
		FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig;
		UAnimSequence* SkeletalAnimation = Asset->LoadNodeSkeletalAnimation(SkeletalMeshComponent->SkeletalMesh, Node.Index, SkeletalAnimationConfig);
		if (SkeletalAnimation)
		{
			SkeletalMeshComponent->AnimationData.AnimToPlay = SkeletalAnimation;
			SkeletalMeshComponent->AnimationData.bSavedLooping = true;
			SkeletalMeshComponent->AnimationData.bSavedPlaying = true;
			SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
		}
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

// Called every frame
void AglTFRuntimeAssetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (TPair<USceneComponent*, UglTFRuntimeAnimationCurve*>& Pair : CurveBasedAnimations)
	{
		float MinTime;
		float MaxTime;
		Pair.Value->GetTimeRange(MinTime, MaxTime);

		float CurrentTime = CurveBasedAnimationsTimeTracker[Pair.Key];
		if (CurrentTime > Pair.Value->glTFCurveAnimationDuration)
		{
			CurveBasedAnimationsTimeTracker[Pair.Key] = 0;
			CurrentTime = 0;
		}

		if (CurrentTime >= MinTime)
		{
			FTransform FrameTransform = Pair.Value->GetTransformValue(CurveBasedAnimationsTimeTracker[Pair.Key]);
			Pair.Key->SetRelativeTransform(FrameTransform);
		}
		CurveBasedAnimationsTimeTracker[Pair.Key] += DeltaTime;
	}
}

