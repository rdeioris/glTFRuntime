// Copyright 2020-2023, Roberto De Ioris.


#include "glTFRuntimeAssetActor.h"
#include "Components/InstancedStaticMeshComponent.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Engine/StaticMeshSocket.h"
#include "Animation/AnimSequence.h"
#include "glTFRuntimeSkeletalMeshComponent.h"

// Sets default values
AglTFRuntimeAssetActor::AglTFRuntimeAssetActor()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;
	bAllowNodeAnimations = true;
	bStaticMeshesAsSkeletal = false;
	bAllowSkeletalAnimations = true;
	bAllowPoseAnimations = true;
	bAllowCameras = true;
	bAllowLights = true;
	bForceSkinnedMeshToRoot = false;
	RootNodeIndex = INDEX_NONE;
	bLoadAllSkeletalAnimations = false;
	bAutoPlayAnimations = true;
	bStaticMeshesAsSkeletalOnMorphTargets = true;
}

// Called when the game starts or when spawned
void AglTFRuntimeAssetActor::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset)
	{
		return;
	}

	double LoadingStartTime = FPlatformTime::Seconds();

	if (RootNodeIndex > INDEX_NONE)
	{
		FglTFRuntimeNode Node;
		if (!Asset->GetNode(RootNodeIndex, Node))
		{
			return;
		}
		AssetRoot = nullptr;
		ProcessNode(nullptr, NAME_None, Node);
	}
	else
	{
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
	}

	for (TPair<USceneComponent*, FName>& Pair : SocketMapping)
	{
		for (USkeletalMeshComponent* SkeletalMeshComponent : DiscoveredSkeletalMeshComponents)
		{
			if (SkeletalMeshComponent->DoesSocketExist(Pair.Value))
			{
				Pair.Key->AttachToComponent(SkeletalMeshComponent, FAttachmentTransformRules::KeepRelativeTransform, Pair.Value);
				Pair.Key->SetRelativeTransform(FTransform::Identity);
				CurveBasedAnimations.Remove(Pair.Key);
				break;
			}
		}
	}

	UE_LOG(LogGLTFRuntime, Log, TEXT("Asset loaded in %f seconds"), FPlatformTime::Seconds() - LoadingStartTime);
}

void AglTFRuntimeAssetActor::ProcessNode(USceneComponent* NodeParentComponent, const FName SocketName, FglTFRuntimeNode& Node)
{
	// special case for bones/joints
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
	if (bAllowCameras && Node.CameraIndex != INDEX_NONE)
	{
		UCameraComponent* NewCameraComponent = NewObject<UCameraComponent>(this, GetSafeNodeName<UCameraComponent>(Node));
		if (!NodeParentComponent)
		{
			SetRootComponent(NewCameraComponent);
		}
		else
		{
			NewCameraComponent->SetupAttachment(NodeParentComponent);
		}
		NewCameraComponent->RegisterComponent();
		NewCameraComponent->SetRelativeTransform(Node.Transform);
		AddInstanceComponent(NewCameraComponent);
		Asset->LoadCamera(Node.CameraIndex, NewCameraComponent);
		NewComponent = NewCameraComponent;

	}
	else if (Node.MeshIndex < 0)
	{
		NewComponent = NewObject<USceneComponent>(this, GetSafeNodeName<USceneComponent>(Node));
		if (!NodeParentComponent)
		{
			SetRootComponent(NewComponent);
		}
		else
		{
			NewComponent->SetupAttachment(NodeParentComponent);
		}
		NewComponent->RegisterComponent();
		NewComponent->SetRelativeTransform(Node.Transform);
		AddInstanceComponent(NewComponent);
	}
	else
	{
		if (Node.SkinIndex < 0 && !bStaticMeshesAsSkeletal && !(bStaticMeshesAsSkeletalOnMorphTargets && Asset->MeshHasMorphTargets(Node.MeshIndex)))
		{
			UStaticMeshComponent* StaticMeshComponent = nullptr;
			TArray<FTransform> GPUInstancingTransforms;
			if (Asset->GetNodeGPUInstancingTransforms(Node.Index, GPUInstancingTransforms))
			{
				UInstancedStaticMeshComponent* InstancedStaticMeshComponent = NewObject<UInstancedStaticMeshComponent>(this, GetSafeNodeName<UInstancedStaticMeshComponent>(Node));
				for (const FTransform& GPUInstanceTransform : GPUInstancingTransforms)
				{
					InstancedStaticMeshComponent->AddInstance(GPUInstanceTransform);
				}
				StaticMeshComponent = InstancedStaticMeshComponent;
			}
			else
			{
				StaticMeshComponent = NewObject<UStaticMeshComponent>(this, GetSafeNodeName<UStaticMeshComponent>(Node));
			}

			if (!NodeParentComponent)
			{
				SetRootComponent(StaticMeshComponent);
			}
			else
			{
				StaticMeshComponent->SetupAttachment(NodeParentComponent);
			}
			StaticMeshComponent->RegisterComponent();
			StaticMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(StaticMeshComponent);
			if (StaticMeshConfig.Outer == nullptr)
			{
				StaticMeshConfig.Outer = StaticMeshComponent;
			}

			TArray<int32> MeshIndices;
			MeshIndices.Add(Node.MeshIndex);

			TArray<int32> LODNodeIndices;
			if (Asset->GetNodeExtensionIndices(Node.Index, "MSFT_lod", "ids", LODNodeIndices))
			{
				for (const int32 LODNodeIndex : LODNodeIndices)
				{
					FglTFRuntimeNode LODNode;
					// stop the chain at the first invalid node/mesh
					if (!Asset->GetNode(LODNodeIndex, LODNode))
					{
						break;
					}
					if (LODNode.MeshIndex <= INDEX_NONE)
					{
						break;
					}
					MeshIndices.Add(LODNode.MeshIndex);
				}
			}

			if (MeshIndices.Num() > 1)
			{
				TArray<float> ScreenCoverages;
				if (Asset->GetNodeExtrasNumbers(Node.Index, "MSFT_screencoverage", ScreenCoverages))
				{
					for (int32 SCIndex = 0; SCIndex < ScreenCoverages.Num(); SCIndex++)
					{
						StaticMeshConfig.LODScreenSize.Add(SCIndex, ScreenCoverages[SCIndex]);
					}
				}
			}

			UStaticMesh* StaticMesh = Asset->LoadStaticMeshLODs(MeshIndices, StaticMeshConfig);
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
			StaticMeshComponent->SetStaticMesh(StaticMesh);
			ReceiveOnStaticMeshComponentCreated(StaticMeshComponent, Node);
			NewComponent = StaticMeshComponent;
		}
		else
		{
			USkeletalMeshComponent* SkeletalMeshComponent = nullptr;
			if (!SkeletalMeshConfig.bPerPolyCollision)
			{
				SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, GetSafeNodeName<USkeletalMeshComponent>(Node));
			}
			else
			{
				SkeletalMeshComponent = NewObject<UglTFRuntimeSkeletalMeshComponent>(this, GetSafeNodeName<UglTFRuntimeSkeletalMeshComponent>(Node));
				SkeletalMeshComponent->bEnablePerPolyCollision = true;
				SkeletalMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			}
			if (!NodeParentComponent)
			{
				SetRootComponent(SkeletalMeshComponent);
			}
			else
			{
				SkeletalMeshComponent->SetupAttachment(bForceSkinnedMeshToRoot ? GetRootComponent() : NodeParentComponent);
			}
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(SkeletalMeshComponent);
			if (SkeletalMeshConfig.Outer == nullptr)
			{
				SkeletalMeshConfig.Outer = SkeletalMeshComponent;
			}
			USkeletalMesh* SkeletalMesh = Asset->LoadSkeletalMesh(Node.MeshIndex, Node.SkinIndex, SkeletalMeshConfig);
			SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
			DiscoveredSkeletalMeshComponents.Add(SkeletalMeshComponent);
			ReceiveOnSkeletalMeshComponentCreated(SkeletalMeshComponent, Node);
			NewComponent = SkeletalMeshComponent;
		}
	}

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


	TArray<int32> EmitterIndices;
	if (Asset->GetNodeExtensionIndices(Node.Index, "MSFT_audio_emitter", "emitters", EmitterIndices))
	{
		// check for audio emitters
		for (const int32 EmitterIndex : EmitterIndices)
		{
			FglTFRuntimeAudioEmitter AudioEmitter;
			if (Asset->LoadAudioEmitter(EmitterIndex, AudioEmitter))
			{
				UAudioComponent* AudioComponent = NewObject<UAudioComponent>(this, *AudioEmitter.Name);
				AudioComponent->SetupAttachment(NewComponent);
				AudioComponent->RegisterComponent();
				AudioComponent->SetRelativeTransform(Node.Transform);
				AddInstanceComponent(AudioComponent);
				Asset->LoadEmitterIntoAudioComponent(AudioEmitter, AudioComponent);
				AudioComponent->Play();
			}
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

	// check for animations
	if (!NewComponent->IsA<USkeletalMeshComponent>())
	{
		if (bAllowNodeAnimations)
		{
			TArray<UglTFRuntimeAnimationCurve*> ComponentAnimationCurves = Asset->LoadAllNodeAnimationCurves(Node.Index);
			TMap<FString, UglTFRuntimeAnimationCurve*> ComponentAnimationCurvesMap;
			for (UglTFRuntimeAnimationCurve* ComponentAnimationCurve : ComponentAnimationCurves)
			{
				if (!CurveBasedAnimations.Contains(NewComponent))
				{
					CurveBasedAnimations.Add(NewComponent, ComponentAnimationCurve);
					CurveBasedAnimationsTimeTracker.Add(NewComponent, 0);
				}
				DiscoveredCurveAnimationsNames.Add(ComponentAnimationCurve->glTFCurveAnimationName);
				ComponentAnimationCurvesMap.Add(ComponentAnimationCurve->glTFCurveAnimationName, ComponentAnimationCurve);
			}
			DiscoveredCurveAnimations.Add(NewComponent, ComponentAnimationCurvesMap);
		}
	}
	else
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(NewComponent);
		if (bAllowSkeletalAnimations)
		{
			UAnimSequence* SkeletalAnimation = nullptr;
			if (bLoadAllSkeletalAnimations)
			{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
				TMap<FString, UAnimSequence*> SkeletalAnimationsMap = Asset->LoadNodeSkeletalAnimationsMap(SkeletalMeshComponent->GetSkeletalMeshAsset(), Node.Index, SkeletalAnimationConfig);
#else
				TMap<FString, UAnimSequence*> SkeletalAnimationsMap = Asset->LoadNodeSkeletalAnimationsMap(SkeletalMeshComponent->SkeletalMesh, Node.Index, SkeletalAnimationConfig);
#endif
				if (SkeletalAnimationsMap.Num() > 0)
				{
					DiscoveredSkeletalAnimations.Add(SkeletalMeshComponent, SkeletalAnimationsMap);
					
					for (const TPair<FString, UAnimSequence*>& Pair : SkeletalAnimationsMap)
					{
						AllSkeletalAnimations.Add(Pair.Value);
						// set the first animation (TODO: allow this to be configurable)
						if (!SkeletalAnimation)
						{
							SkeletalAnimation = Pair.Value;
						}
					}
				}
			}
			else
			{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
				SkeletalAnimation = Asset->LoadNodeSkeletalAnimation(SkeletalMeshComponent->GetSkeletalMeshAsset(), Node.Index, SkeletalAnimationConfig);
#else
				SkeletalAnimation = Asset->LoadNodeSkeletalAnimation(SkeletalMeshComponent->SkeletalMesh, Node.Index, SkeletalAnimationConfig);
#endif
			}

			if (!SkeletalAnimation && bAllowPoseAnimations)
			{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
				SkeletalAnimation = Asset->CreateAnimationFromPose(SkeletalMeshComponent->GetSkeletalMeshAsset(), SkeletalAnimationConfig, Node.SkinIndex);
#else
				SkeletalAnimation = Asset->CreateAnimationFromPose(SkeletalMeshComponent->SkeletalMesh, SkeletalAnimationConfig, Node.SkinIndex);
#endif
			}

			if (SkeletalAnimation)
			{
				SkeletalMeshComponent->AnimationData.AnimToPlay = SkeletalAnimation;
				SkeletalMeshComponent->AnimationData.bSavedLooping = true;
				SkeletalMeshComponent->AnimationData.bSavedPlaying = bAutoPlayAnimations;
				SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
			}
		}
	}

	OnNodeProcessed.Broadcast(Node, NewComponent);

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

void AglTFRuntimeAssetActor::SetCurveAnimationByName(const FString& CurveAnimationName)
{
	if (!DiscoveredCurveAnimationsNames.Contains(CurveAnimationName))
	{
		return;
	}

	for (TPair<USceneComponent*, UglTFRuntimeAnimationCurve*>& Pair : CurveBasedAnimations)
	{

		TMap<FString, UglTFRuntimeAnimationCurve*> WantedCurveAnimationsMap = DiscoveredCurveAnimations[Pair.Key];
		if (WantedCurveAnimationsMap.Contains(CurveAnimationName))
		{
			Pair.Value = WantedCurveAnimationsMap[CurveAnimationName];
			CurveBasedAnimationsTimeTracker[Pair.Key] = 0;
		}
		else
		{
			Pair.Value = nullptr;
		}

	}

}

// Called every frame
void AglTFRuntimeAssetActor::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	for (TPair<USceneComponent*, UglTFRuntimeAnimationCurve*>& Pair : CurveBasedAnimations)
	{
		// the curve could be null
		if (!Pair.Value)
		{
			continue;
		}
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

void AglTFRuntimeAssetActor::ReceiveOnStaticMeshComponentCreated_Implementation(UStaticMeshComponent* StaticMeshComponent, const FglTFRuntimeNode& Node)
{

}

void AglTFRuntimeAssetActor::ReceiveOnSkeletalMeshComponentCreated_Implementation(USkeletalMeshComponent* SkeletalMeshComponent, const FglTFRuntimeNode& Node)
{

}

void AglTFRuntimeAssetActor::PostUnregisterAllComponents()
{
	if (Asset)
	{
		Asset->ClearCache();
		Asset = nullptr;
	}
	Super::PostUnregisterAllComponents();
}

UAnimSequence* AglTFRuntimeAssetActor::GetSkeletalAnimationByName(USkeletalMeshComponent* SkeletalMeshComponent, const FString& AnimationName) const
{
	if (!SkeletalMeshComponent)
	{
		return nullptr;
	}

	if (!DiscoveredSkeletalAnimations.Contains(SkeletalMeshComponent))
	{
		return nullptr;
	}

	for (const TPair<FString, UAnimSequence*>& Pair : DiscoveredSkeletalAnimations[SkeletalMeshComponent])
	{
		if (Pair.Key == AnimationName)
		{
			SkeletalMeshComponent->AnimationData.AnimToPlay = Pair.Value;
			return Pair.Value;
		}
	}

	return nullptr;
}