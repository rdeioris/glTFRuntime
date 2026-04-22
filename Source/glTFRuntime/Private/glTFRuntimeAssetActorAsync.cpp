// Copyright 2021-2023, Roberto De Ioris.


#include "glTFRuntimeAssetActorAsync.h"
#include "Animation/AnimSequence.h"
#include "Animation/AnimSingleNodeInstance.h"
#include "Components/LightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMeshSocket.h"
#include "glTFRuntimeAnimationCurve.h"

void UglTFRuntimeAssetActorAsyncLoadProxy::Initialize(AglTFRuntimeAssetActorAsync* InOwner, UPrimitiveComponent* InPrimitiveComponent)
{
	Owner = InOwner;
	PrimitiveComponentRaw = InPrimitiveComponent;
}

void UglTFRuntimeAssetActorAsyncLoadProxy::OnStaticMeshLoaded(UStaticMesh* StaticMesh)
{
	if (AglTFRuntimeAssetActorAsync* OwnerPtr = Owner.Get())
	{
		OwnerPtr->OnStaticMeshLoadedFromProxy(PrimitiveComponentRaw, StaticMesh, this);
	}
}

void UglTFRuntimeAssetActorAsyncLoadProxy::OnSkeletalMeshLoaded(USkeletalMesh* SkeletalMesh)
{
	if (AglTFRuntimeAssetActorAsync* OwnerPtr = Owner.Get())
	{
		OwnerPtr->OnSkeletalMeshLoadedFromProxy(PrimitiveComponentRaw, SkeletalMesh, this);
	}
}

// Sets default values
AglTFRuntimeAssetActorAsync::AglTFRuntimeAssetActorAsync()
{
	PrimaryActorTick.bCanEverTick = true;

	AssetRoot = CreateDefaultSubobject<USceneComponent>(TEXT("AssetRoot"));
	RootComponent = AssetRoot;

	bShowWhileLoading = true;
	bStaticMeshesAsSkeletal = false;
	bStaticMeshesAsSkeletalOnMorphTargets = true;
	bAllowSkeletalAnimations = true;
	bAllowNodeAnimations = false;
	bAllowPoseAnimations = true;
	bAutoPlayAnimations = true;
	bAllowLights = true;
	MaxConcurrentMeshLoads = 8;
	bLoadAllSkeletalAnimations = false;

	bStopLoadingRequested = false;
	bDestroyInitiated = false;
	bScenesLoadedTriggered = false;
	bComponentsCleanedUp = false;
	bAnimationsPaused = false;
	bNodeAnimationsPlaying = false;
	LoadingStartTime = 0.0;
}

void AglTFRuntimeAssetActorAsync::BeginPlay()
{
	Super::BeginPlay();

	if (!Asset || bStopLoadingRequested)
	{
		TryFinalizeLoadingFlow();
		return;
	}

	LoadingStartTime = FPlatformTime::Seconds();
	bNodeAnimationsPlaying = bAutoPlayAnimations;
	bAnimationsPaused = !bAutoPlayAnimations;

	TArray<FglTFRuntimeScene> Scenes = Asset->GetScenes();
	for (FglTFRuntimeScene& Scene : Scenes)
	{
		if (bStopLoadingRequested)
		{
			break;
		}

		USceneComponent* SceneComponent = NewObject<USceneComponent>(this, *FString::Printf(TEXT("Scene %d"), Scene.Index));
		SceneComponent->SetupAttachment(RootComponent);
		SceneComponent->RegisterComponent();
		AddInstanceComponent(SceneComponent);
		TrackCreatedComponent(SceneComponent);

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

	PumpMeshLoadQueue();
	TryFinalizeLoadingFlow();
}

void AglTFRuntimeAssetActorAsync::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (!bAllowNodeAnimations || !bNodeAnimationsPlaying || bAnimationsPaused)
	{
		return;
	}

	UpdateNodeAnimations(DeltaTime, true);
}

void AglTFRuntimeAssetActorAsync::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	bStopLoadingRequested = true;
	PendingMeshesToLoad.Empty();
	CleanupTrackedComponents();
	Super::EndPlay(EndPlayReason);
}

void AglTFRuntimeAssetActorAsync::Destroyed()
{
	bStopLoadingRequested = true;
	PendingMeshesToLoad.Empty();
	CleanupTrackedComponents();
	Super::Destroyed();
}

void AglTFRuntimeAssetActorAsync::ProcessNode(USceneComponent* NodeParentComponent, const FName SocketName, FglTFRuntimeNode& Node)
{
	if (!Asset || bStopLoadingRequested)
	{
		return;
	}

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
		NewComponent = NewObject<USceneComponent>(this, *GetSafeNodeName<USceneComponent>(Node));
		NewComponent->SetupAttachment(NodeParentComponent);
		NewComponent->RegisterComponent();
		NewComponent->SetRelativeTransform(Node.Transform);
		AddInstanceComponent(NewComponent);
		TrackCreatedComponent(NewComponent);
	}
	else
	{
		const bool bLoadStaticMeshAsSkeletalOnMorphTargets =
			(Node.SkinIndex < 0 && bStaticMeshesAsSkeletalOnMorphTargets && Asset->MeshHasMorphTargets(Node.MeshIndex));

		if (Node.SkinIndex < 0 && !bStaticMeshesAsSkeletal && !bLoadStaticMeshAsSkeletalOnMorphTargets)
		{
			UStaticMeshComponent* StaticMeshComponent = NewObject<UStaticMeshComponent>(this, *GetSafeNodeName<UStaticMeshComponent>(Node));
			StaticMeshComponent->SetupAttachment(NodeParentComponent);
			StaticMeshComponent->RegisterComponent();
			StaticMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(StaticMeshComponent);
			TrackCreatedComponent(StaticMeshComponent);

			FglTFRuntimeMeshLoadContext MeshLoadContext;
			MeshLoadContext.Node = Node;
			PendingMeshesToLoad.Add(StaticMeshComponent, MeshLoadContext);

			NewComponent = StaticMeshComponent;
			ReceiveOnStaticMeshComponentCreated(StaticMeshComponent, Node);
		}
		else
		{
			USkeletalMeshComponent* SkeletalMeshComponent = NewObject<USkeletalMeshComponent>(this, *GetSafeNodeName<USkeletalMeshComponent>(Node));
			SkeletalMeshComponent->SetupAttachment(NodeParentComponent);
			SkeletalMeshComponent->RegisterComponent();
			SkeletalMeshComponent->SetRelativeTransform(Node.Transform);
			AddInstanceComponent(SkeletalMeshComponent);
			TrackCreatedComponent(SkeletalMeshComponent);

			FglTFRuntimeMeshLoadContext MeshLoadContext;
			MeshLoadContext.Node = Node;
			MeshLoadContext.SkeletalMeshConfig = SkeletalMeshConfig;
			MeshLoadContext.bUseCustomSkeletalMeshConfig = true;
			MeshLoadContext.bUseRecursiveSkeletalMeshLoad = false;
			MeshLoadContext.bMorphNodeUsesNodeTreeFallback = false;

			if (Node.SkinIndex < 0 && bLoadStaticMeshAsSkeletalOnMorphTargets)
			{
				const bool bMorphNodeUsesNodeTreeFallback = MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bFallbackToNodesTree;
				MeshLoadContext.bMorphNodeUsesNodeTreeFallback = bMorphNodeUsesNodeTreeFallback;
				if (bMorphNodeUsesNodeTreeFallback)
				{
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bNormalizeSkeletonScale = true;
				}

				if (!MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bFallbackToNodesTree &&
					MeshLoadContext.SkeletalMeshConfig.CustomSkeleton.Num() == 0 &&
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.RootNodeIndex <= INDEX_NONE &&
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.ForceRootNode.IsEmpty())
				{
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bFallbackToNodesTree = true;
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bNormalizeSkeletonScale = true;
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.RootNodeIndex = Node.Index;
					MeshLoadContext.bMorphNodeUsesNodeTreeFallback = true;
				}

				if (MeshLoadContext.bMorphNodeUsesNodeTreeFallback && !Node.Name.IsEmpty())
				{
					FTransform RootSkeletonTransformNoScale = Node.Transform;
					RootSkeletonTransformNoScale.SetScale3D(FVector::OneVector);
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.BonesDeltaTransformMap.Add(Node.Name, RootSkeletonTransformNoScale.Inverse());
					MeshLoadContext.bUseRecursiveSkeletalMeshLoad = true;
				}

				/*UE_LOG(LogGLTFRuntime, Log, TEXT("[MorphFallbackAsync] Node='%s' Index=%d Mesh=%d Skin=%d ForceSkeletal=true FallbackToNodesTree=%s Recursive=%s"),
					*Node.Name,
					Node.Index,
					Node.MeshIndex,
					Node.SkinIndex,
					MeshLoadContext.SkeletalMeshConfig.SkeletonConfig.bFallbackToNodesTree ? TEXT("true") : TEXT("false"),
					MeshLoadContext.bUseRecursiveSkeletalMeshLoad ? TEXT("true") : TEXT("false"));*/
			}

			PendingMeshesToLoad.Add(SkeletalMeshComponent, MeshLoadContext);
			NewComponent = SkeletalMeshComponent;
			ReceiveOnSkeletalMeshComponentCreated(SkeletalMeshComponent, Node);
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
				TrackCreatedComponent(LightComponent);
			}
		}
	}

	ReceiveOnNodeProcessed(Node.Index, NewComponent);

	if (!NewComponent)
	{
		return;
	}

	NewComponent->ComponentTags.Add(*FString::Printf(TEXT("glTFRuntime:NodeName:%s"), *Node.Name));
	NewComponent->ComponentTags.Add(*FString::Printf(TEXT("glTFRuntime:NodeIndex:%d"), Node.Index));

	if (SocketName != NAME_None)
	{
		SocketMapping.Add(NewComponent, SocketName);
	}

	if (!NewComponent->IsA<USkeletalMeshComponent>())
	{
		RegisterNodeAnimationCurves(NewComponent, Node);
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

void AglTFRuntimeAssetActorAsync::TrackCreatedComponent(UActorComponent* Component)
{
	if (!Component || Component == AssetRoot)
	{
		return;
	}

	TrackedCreatedComponents.Add(Component);
}

void AglTFRuntimeAssetActorAsync::RegisterNodeAnimationCurves(USceneComponent* SceneComponent, const FglTFRuntimeNode& Node)
{
	if (!bAllowNodeAnimations || !SceneComponent || !Asset)
	{
		return;
	}

	TArray<UglTFRuntimeAnimationCurve*> ComponentAnimationCurves = Asset->LoadAllNodeAnimationCurves(Node.Index);
	if (ComponentAnimationCurves.Num() == 0)
	{
		return;
	}

	TMap<FString, UglTFRuntimeAnimationCurve*> ComponentAnimationCurvesMap;
	for (UglTFRuntimeAnimationCurve* ComponentAnimationCurve : ComponentAnimationCurves)
	{
		if (!ComponentAnimationCurve)
		{
			continue;
		}

		AllCurveAnimations.Add(ComponentAnimationCurve);
		DiscoveredAnimationNames.Add(ComponentAnimationCurve->glTFCurveAnimationName);
		RememberAnimationDuration(ComponentAnimationCurve->glTFCurveAnimationName, ComponentAnimationCurve->glTFCurveAnimationDuration);
		ComponentAnimationCurvesMap.Add(ComponentAnimationCurve->glTFCurveAnimationName, ComponentAnimationCurve);

		if (!CurveBasedAnimations.Contains(SceneComponent))
		{
			CurveBasedAnimations.Add(SceneComponent, ComponentAnimationCurve);
			CurveBasedAnimationsTimeTracker.Add(SceneComponent, 0.0f);
		}
	}

	DiscoveredCurveAnimations.Add(SceneComponent, MoveTemp(ComponentAnimationCurvesMap));
}

void AglTFRuntimeAssetActorAsync::RememberAnimationDuration(const FString& AnimationName, const float DurationSeconds)
{
	if (DurationSeconds < 0.0f)
	{
		return;
	}

	DiscoveredAnimationNames.Add(AnimationName);
	if (float* ExistingDuration = DiscoveredAnimationDurationsByName.Find(AnimationName))
	{
		*ExistingDuration = FMath::Max(*ExistingDuration, DurationSeconds);
	}
	else
	{
		DiscoveredAnimationDurationsByName.Add(AnimationName, DurationSeconds);
	}
}

void AglTFRuntimeAssetActorAsync::UpdateNodeAnimations(const float DeltaTime, const bool bAdvanceTime)
{
	TArray<USceneComponent*> Components;
	CurveBasedAnimations.GetKeys(Components);

	for (USceneComponent* SceneComponent : Components)
	{
		UglTFRuntimeAnimationCurve* const* CurvePtr = CurveBasedAnimations.Find(SceneComponent);
		if (!IsValid(SceneComponent) || !CurvePtr || !IsValid(*CurvePtr))
		{
			CurveBasedAnimations.Remove(SceneComponent);
			CurveBasedAnimationsTimeTracker.Remove(SceneComponent);
			continue;
		}

		UglTFRuntimeAnimationCurve* Curve = *CurvePtr;
		float CurrentTime = CurveBasedAnimationsTimeTracker.FindRef(SceneComponent);
		const float Duration = Curve->glTFCurveAnimationDuration;

		if (Duration > KINDA_SMALL_NUMBER)
		{
			CurrentTime = FMath::Fmod(CurrentTime, Duration);
			if (CurrentTime < 0.0f)
			{
				CurrentTime += Duration;
			}
		}
		else
		{
			CurrentTime = 0.0f;
		}

		float MinTime = 0.0f;
		float MaxTime = 0.0f;
		Curve->GetTimeRange(MinTime, MaxTime);
		if (CurrentTime >= MinTime)
		{
			SceneComponent->SetRelativeTransform(Curve->GetTransformValue(CurrentTime));
		}

		CurveBasedAnimationsTimeTracker.Add(SceneComponent, bAdvanceTime ? CurrentTime + DeltaTime : CurrentTime);
	}
}

void AglTFRuntimeAssetActorAsync::PumpMeshLoadQueue()
{
	if (!Asset || bStopLoadingRequested || bDestroyInitiated)
	{
		return;
	}

	const int32 EffectiveMaxConcurrentMeshLoads = FMath::Max(1, MaxConcurrentMeshLoads);
	while (PendingMeshesToLoad.Num() > 0 && InFlightMeshesToLoad.Num() < EffectiveMaxConcurrentMeshLoads)
	{
		auto It = PendingMeshesToLoad.CreateIterator();
		UPrimitiveComponent* PrimitiveComponent = It->Key;
		const FglTFRuntimeMeshLoadContext MeshLoadContext = It->Value;
		It.RemoveCurrent();

		if (!DispatchMeshLoad(PrimitiveComponent, MeshLoadContext))
		{
			continue;
		}
	}
}

bool AglTFRuntimeAssetActorAsync::DispatchMeshLoad(UPrimitiveComponent* PrimitiveComponent, const FglTFRuntimeMeshLoadContext& MeshLoadContext)
{
	if (!Asset || !IsValid(PrimitiveComponent))
	{
		return false;
	}

	UglTFRuntimeAssetActorAsyncLoadProxy* Proxy = NewObject<UglTFRuntimeAssetActorAsyncLoadProxy>(this);
	Proxy->Initialize(this, PrimitiveComponent);
	ActiveLoadProxies.Add(Proxy);
	InFlightMeshesToLoad.Add(PrimitiveComponent, MeshLoadContext);

	if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
	{
		FglTFRuntimeStaticMeshConfig StaticMeshConfigForNode = OverrideStaticMeshConfig(MeshLoadContext.Node.Index, StaticMeshComponent);
		if (StaticMeshConfigForNode.Outer == nullptr)
		{
			StaticMeshConfigForNode.Outer = StaticMeshComponent;
		}

		FglTFRuntimeStaticMeshAsync Delegate;
		Delegate.BindUFunction(Proxy, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAssetActorAsyncLoadProxy, OnStaticMeshLoaded));
		Asset->LoadStaticMeshAsync(MeshLoadContext.Node.MeshIndex, Delegate, StaticMeshConfigForNode);
		return true;
	}

	if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
	{
		FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfigForNode = SkeletalMeshConfig;
		if (MeshLoadContext.bUseCustomSkeletalMeshConfig)
		{
			SkeletalMeshConfigForNode = MeshLoadContext.SkeletalMeshConfig;
		}
		if (SkeletalMeshConfigForNode.Outer == nullptr)
		{
			SkeletalMeshConfigForNode.Outer = SkeletalMeshComponent;
		}

		FglTFRuntimeSkeletalMeshAsync Delegate;
		Delegate.BindUFunction(Proxy, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAssetActorAsyncLoadProxy, OnSkeletalMeshLoaded));
		if (MeshLoadContext.bUseRecursiveSkeletalMeshLoad && !MeshLoadContext.Node.Name.IsEmpty())
		{
			TArray<FString> ExcludeNodes;
			Asset->LoadSkeletalMeshRecursiveAsync(MeshLoadContext.Node.Name, ExcludeNodes, Delegate, SkeletalMeshConfigForNode, EglTFRuntimeRecursiveMode::Ignore);
		}
		else
		{
			Asset->LoadSkeletalMeshAsync(MeshLoadContext.Node.MeshIndex, MeshLoadContext.Node.SkinIndex, Delegate, SkeletalMeshConfigForNode);
		}
		return true;
	}

	InFlightMeshesToLoad.Remove(PrimitiveComponent);
	ActiveLoadProxies.Remove(Proxy);
	return false;
}

void AglTFRuntimeAssetActorAsync::OnStaticMeshLoadedFromProxy(UPrimitiveComponent* PrimitiveComponent, UStaticMesh* StaticMesh, UglTFRuntimeAssetActorAsyncLoadProxy* Proxy)
{
	if (Proxy)
	{
		ActiveLoadProxies.Remove(Proxy);
	}

	const bool bWasInFlight = InFlightMeshesToLoad.Remove(PrimitiveComponent) > 0;
	if (!bWasInFlight)
	{
		TryFinalizeLoadingFlow();
		return;
	}

	if (!bStopLoadingRequested && IsValid(PrimitiveComponent))
	{
		if (UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(PrimitiveComponent))
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
	}

	PumpMeshLoadQueue();
	TryFinalizeLoadingFlow();
}

void AglTFRuntimeAssetActorAsync::OnSkeletalMeshLoadedFromProxy(UPrimitiveComponent* PrimitiveComponent, USkeletalMesh* SkeletalMesh, UglTFRuntimeAssetActorAsyncLoadProxy* Proxy)
{
	if (Proxy)
	{
		ActiveLoadProxies.Remove(Proxy);
	}

	FglTFRuntimeMeshLoadContext MeshLoadContext;
	const FglTFRuntimeMeshLoadContext* ExistingContext = InFlightMeshesToLoad.Find(PrimitiveComponent);
	if (ExistingContext)
	{
		MeshLoadContext = *ExistingContext;
	}

	const bool bWasInFlight = InFlightMeshesToLoad.Remove(PrimitiveComponent) > 0;
	if (!bWasInFlight)
	{
		TryFinalizeLoadingFlow();
		return;
	}

	if (!bStopLoadingRequested && IsValid(PrimitiveComponent))
	{
		if (USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(PrimitiveComponent))
		{
			DiscoveredSkeletalMeshComponents.Add(SkeletalMeshComponent, SkeletalMesh);
			if (bShowWhileLoading)
			{
				SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
			}

			if (SkeletalMesh && bAllowSkeletalAnimations)
			{
				FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfigForNode = SkeletalAnimationConfig;
				const FglTFRuntimeNode& Node = MeshLoadContext.Node;

				if (MeshLoadContext.bMorphNodeUsesNodeTreeFallback)
				{
					SkeletalAnimationConfigForNode.RootNodeIndex = INDEX_NONE;
					if (!Node.Name.IsEmpty())
					{
						const FTransform RootRebaseTransformForAnimation = Node.Transform.Inverse();
						SkeletalAnimationConfigForNode.TransformPose.Add(Node.Name, RootRebaseTransformForAnimation);
						if (SkeletalMeshComponent->GetNumBones() > 0)
						{
							const FString RootBoneName = SkeletalMeshComponent->GetBoneName(0).ToString();
							if (!RootBoneName.IsEmpty())
							{
								SkeletalAnimationConfigForNode.TransformPose.Add(RootBoneName, RootRebaseTransformForAnimation);
							}
						}
					}
				}

				UAnimSequence* SkeletalAnimation = nullptr;
				if (bLoadAllSkeletalAnimations)
				{
					TMap<FString, UAnimSequence*> SkeletalAnimationsMap = Asset->LoadNodeSkeletalAnimationsMap(SkeletalMesh, Node.Index, SkeletalAnimationConfigForNode);
					if (SkeletalAnimationsMap.Num() > 0)
					{
						DiscoveredSkeletalAnimations.Add(SkeletalMeshComponent, SkeletalAnimationsMap);
						for (const TPair<FString, UAnimSequence*>& Pair : SkeletalAnimationsMap)
						{
							if (!Pair.Value)
							{
								continue;
							}

							AllSkeletalAnimations.Add(Pair.Value);
							DiscoveredAnimationNames.Add(Pair.Key);
							RememberAnimationDuration(Pair.Key, Pair.Value->GetPlayLength());
							if (!SkeletalAnimation)
							{
								SkeletalAnimation = Pair.Value;
							}
						}
					}
				}

				if (!SkeletalAnimation)
				{
					SkeletalAnimation = Asset->LoadNodeSkeletalAnimation(SkeletalMesh, Node.Index, SkeletalAnimationConfigForNode);
				}

				if (!SkeletalAnimation && bAllowPoseAnimations)
				{
					SkeletalAnimation = Asset->CreateAnimationFromPose(SkeletalMesh, SkeletalAnimationConfigForNode, Node.SkinIndex);
				}

				if (SkeletalAnimation)
				{
					AllSkeletalAnimations.Add(SkeletalAnimation);
					SkeletalMeshComponent->AnimationData.AnimToPlay = SkeletalAnimation;
					SkeletalMeshComponent->AnimationData.bSavedLooping = true;
					SkeletalMeshComponent->AnimationData.bSavedPlaying = bAutoPlayAnimations;
					SkeletalMeshComponent->SetAnimationMode(EAnimationMode::AnimationSingleNode);
					if (bAutoPlayAnimations)
					{
						SkeletalMeshComponent->PlayAnimation(SkeletalAnimation, true);
					}
					else
					{
						SkeletalMeshComponent->bPauseAnims = true;
					}
					/*UE_LOG(LogGLTFRuntime, Log, TEXT("[MorphFallbackAsync] Node='%s' Index=%d SkeletalAnimationLoaded=true"),
						*Node.Name,
						Node.Index)*/;
				}
				else
				{
					/*UE_LOG(LogGLTFRuntime, Log, TEXT("[MorphFallbackAsync] Node='%s' Index=%d SkeletalAnimationLoaded=false"),
						*Node.Name,
						Node.Index);*/
				}
			}
		}
	}

	PumpMeshLoadQueue();
	TryFinalizeLoadingFlow();
}

void AglTFRuntimeAssetActorAsync::TryFinalizeLoadingFlow()
{
	if (bStopLoadingRequested)
	{
		if (InFlightMeshesToLoad.Num() == 0)
		{
			BeginSafeDestroy();
		}
		return;
	}

	if (!bScenesLoadedTriggered && PendingMeshesToLoad.Num() == 0 && InFlightMeshesToLoad.Num() == 0)
	{
		ScenesLoaded();
	}
}

void AglTFRuntimeAssetActorAsync::BeginSafeDestroy()
{
	if (bDestroyInitiated)
	{
		return;
	}

	bDestroyInitiated = true;
	SetActorTickEnabled(false);
	CleanupTrackedComponents();

	if (!IsActorBeingDestroyed())
	{
		Destroy();
	}
}

void AglTFRuntimeAssetActorAsync::CleanupTrackedComponents()
{
	if (bComponentsCleanedUp)
	{
		return;
	}

	bComponentsCleanedUp = true;

	TArray<TObjectPtr<UActorComponent>> ComponentsToDestroy = TrackedCreatedComponents.Array();
	for (UActorComponent* Component : ComponentsToDestroy)
	{
		if (!IsValid(Component) || Component->IsBeingDestroyed())
		{
			continue;
		}

		if (USceneComponent* SceneComponent = Cast<USceneComponent>(Component))
		{
			SceneComponent->DetachFromComponent(FDetachmentTransformRules::KeepWorldTransform);
		}

		Component->DestroyComponent();
	}

	TrackedCreatedComponents.Empty();
}

void AglTFRuntimeAssetActorAsync::StopLoadingAndDestroy()
{
	if (bStopLoadingRequested)
	{
		return;
	}

	bStopLoadingRequested = true;
	bNodeAnimationsPlaying = false;
	bAnimationsPaused = true;
	PendingMeshesToLoad.Empty();
	TryFinalizeLoadingFlow();
}

void AglTFRuntimeAssetActorAsync::PlayAnimations(FString AnimationName)
{
	bNodeAnimationsPlaying = true;
	bAnimationsPaused = false;
	const bool bUseRequestedAnimationName = !AnimationName.IsEmpty();

	if (bUseRequestedAnimationName)
	{
		for (TPair<USceneComponent*, UglTFRuntimeAnimationCurve*>& Pair : CurveBasedAnimations)
		{
			TMap<FString, UglTFRuntimeAnimationCurve*>* ComponentAnimations = DiscoveredCurveAnimations.Find(Pair.Key);
			if (!ComponentAnimations)
			{
				Pair.Value = nullptr;
				continue;
			}

			if (UglTFRuntimeAnimationCurve** RequestedCurve = ComponentAnimations->Find(AnimationName))
			{
				Pair.Value = *RequestedCurve;
			}
			else
			{
				Pair.Value = nullptr;
			}
		}
	}

	for (TPair<USceneComponent*, float>& Pair : CurveBasedAnimationsTimeTracker)
	{
		Pair.Value = 0.0f;
	}
	UpdateNodeAnimations(0.0f, false);

	for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Pair.Key;
		if (!IsValid(SkeletalMeshComponent))
		{
			continue;
		}

		UAnimSequence* SequenceToPlay = Cast<UAnimSequence>(SkeletalMeshComponent->AnimationData.AnimToPlay);
		if (bUseRequestedAnimationName)
		{
			SequenceToPlay = nullptr;
			TMap<FString, UAnimSequence*>& SkeletalAnimationsMap = DiscoveredSkeletalAnimations.FindOrAdd(SkeletalMeshComponent);
			if (UAnimSequence** ExistingSequence = SkeletalAnimationsMap.Find(AnimationName))
			{
				SequenceToPlay = *ExistingSequence;
			}
			else if (Asset && IsValid(Pair.Value))
			{
				SequenceToPlay = Asset->LoadSkeletalAnimationByName(Pair.Value, AnimationName, SkeletalAnimationConfig, false);
				if (SequenceToPlay)
				{
					SkeletalAnimationsMap.Add(AnimationName, SequenceToPlay);
					AllSkeletalAnimations.Add(SequenceToPlay);
					DiscoveredAnimationNames.Add(AnimationName);
					RememberAnimationDuration(AnimationName, SequenceToPlay->GetPlayLength());
				}
			}
			SkeletalMeshComponent->AnimationData.AnimToPlay = SequenceToPlay;
		}

		if (SequenceToPlay)
		{
			SkeletalMeshComponent->PlayAnimation(SequenceToPlay, true);
			SkeletalMeshComponent->SetPosition(0.0f, false);
			SkeletalMeshComponent->bPauseAnims = false;
		}
		else if (UAnimSingleNodeInstance* SingleNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
		{
			if (bUseRequestedAnimationName)
			{
				SingleNodeInstance->SetPlaying(false);
				SkeletalMeshComponent->bPauseAnims = true;
			}
			else
			{
				SingleNodeInstance->SetPlaying(true);
				SingleNodeInstance->SetPosition(0.0f, false);
				SkeletalMeshComponent->bPauseAnims = false;
			}
		}
		else
		{
			SkeletalMeshComponent->bPauseAnims = bUseRequestedAnimationName;
		}
	}
}

void AglTFRuntimeAssetActorAsync::PauseAnimations()
{
	bAnimationsPaused = true;

	for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Pair.Key;
		if (!IsValid(SkeletalMeshComponent))
		{
			continue;
		}

		SkeletalMeshComponent->bPauseAnims = true;
		if (UAnimSingleNodeInstance* SingleNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
		{
			SingleNodeInstance->SetPlaying(false);
		}
	}
}

void AglTFRuntimeAssetActorAsync::ResumeAnimations()
{
	bNodeAnimationsPlaying = true;
	bAnimationsPaused = false;

	for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Pair.Key;
		if (!IsValid(SkeletalMeshComponent))
		{
			continue;
		}

		SkeletalMeshComponent->bPauseAnims = false;
		if (UAnimSingleNodeInstance* SingleNodeInstance = SkeletalMeshComponent->GetSingleNodeInstance())
		{
			SingleNodeInstance->SetPlaying(true);
		}
	}
}

void AglTFRuntimeAssetActorAsync::SeekAnimations(float TimeSeconds, bool bFireNotifies)
{
	const float SafeTime = FMath::Max(0.0f, TimeSeconds);

	for (TPair<USceneComponent*, float>& Pair : CurveBasedAnimationsTimeTracker)
	{
		Pair.Value = SafeTime;
	}
	UpdateNodeAnimations(0.0f, false);

	for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
	{
		USkeletalMeshComponent* SkeletalMeshComponent = Pair.Key;
		if (!IsValid(SkeletalMeshComponent))
		{
			continue;
		}

		SkeletalMeshComponent->SetPosition(SafeTime, bFireNotifies);
	}
}

int32 AglTFRuntimeAssetActorAsync::GetNumAnimations() const
{
	if (Asset)
	{
		return Asset->GetNumAnimations();
	}

	return DiscoveredAnimationNames.Num();
}

TArray<FString> AglTFRuntimeAssetActorAsync::GetAnimationNames(const bool bIncludeUnnameds) const
{
	if (Asset)
	{
		return Asset->GetAnimationsNames(bIncludeUnnameds);
	}

	TArray<FString> Names = DiscoveredAnimationNames.Array();
	if (!bIncludeUnnameds)
	{
		Names.RemoveAll([](const FString& AnimationName)
			{
				return AnimationName.IsEmpty();
			});
	}
	Names.Sort();
	return Names;
}

bool AglTFRuntimeAssetActorAsync::GetAnimationDurationByName(const FString& Name, float& OutDuration) const
{
	OutDuration = 0.0f;

	if (const float* ExistingDuration = DiscoveredAnimationDurationsByName.Find(Name))
	{
		OutDuration = *ExistingDuration;
		return true;
	}

	for (const TPair<USceneComponent*, TMap<FString, UglTFRuntimeAnimationCurve*>>& Pair : DiscoveredCurveAnimations)
	{
		if (const UglTFRuntimeAnimationCurve* const* AnimationCurve = Pair.Value.Find(Name))
		{
			if (IsValid(*AnimationCurve))
			{
				OutDuration = (*AnimationCurve)->glTFCurveAnimationDuration;
				return true;
			}
		}
	}

	for (const TPair<USkeletalMeshComponent*, TMap<FString, UAnimSequence*>>& Pair : DiscoveredSkeletalAnimations)
	{
		if (UAnimSequence* const* Sequence = Pair.Value.Find(Name))
		{
			if (IsValid(*Sequence))
			{
				OutDuration = (*Sequence)->GetPlayLength();
				return true;
			}
		}
	}

	if (!Asset || Name.IsEmpty())
	{
		return false;
	}

	for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
	{
		USkeletalMesh* SkeletalMesh = Pair.Value;
		if (!IsValid(SkeletalMesh))
		{
			continue;
		}

		UAnimSequence* Sequence = Asset->LoadSkeletalAnimationByName(SkeletalMesh, Name, SkeletalAnimationConfig, false);
		if (Sequence)
		{
			OutDuration = Sequence->GetPlayLength();
			return true;
		}
	}

	return false;
}

void AglTFRuntimeAssetActorAsync::ScenesLoaded()
{
	if (bScenesLoadedTriggered || bStopLoadingRequested)
	{
		return;
	}

	bScenesLoadedTriggered = true;

	if (!bShowWhileLoading)
	{
		for (const TPair<UStaticMeshComponent*, UStaticMesh*>& Pair : DiscoveredStaticMeshComponents)
		{
			if (IsValid(Pair.Key))
			{
				Pair.Key->SetStaticMesh(Pair.Value);
			}
		}

		for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& Pair : DiscoveredSkeletalMeshComponents)
		{
			if (IsValid(Pair.Key))
			{
				Pair.Key->SetSkeletalMesh(Pair.Value);
			}
		}
	}

	for (TPair<USceneComponent*, FName>& Pair : SocketMapping)
	{
		if (!IsValid(Pair.Key))
		{
			continue;
		}

		for (const TPair<USkeletalMeshComponent*, USkeletalMesh*>& MeshPair : DiscoveredSkeletalMeshComponents)
		{
			if (!IsValid(MeshPair.Key))
			{
				continue;
			}

			if (MeshPair.Key->DoesSocketExist(Pair.Value))
			{
				Pair.Key->AttachToComponent(MeshPair.Key, FAttachmentTransformRules::KeepRelativeTransform, Pair.Value);
				Pair.Key->SetRelativeTransform(FTransform::Identity);
				CurveBasedAnimations.Remove(Pair.Key);
				CurveBasedAnimationsTimeTracker.Remove(Pair.Key);
				break;
			}
		}
	}

	if (bAutoPlayAnimations)
	{
		PlayAnimations();
	}
	else
	{
		PauseAnimations();
	}

	UE_LOG(LogGLTFRuntime, Log, TEXT("Asset loaded asynchronously in %f seconds"), FPlatformTime::Seconds() - LoadingStartTime);
	ReceiveOnScenesLoaded();
}

void AglTFRuntimeAssetActorAsync::ReceiveOnScenesLoaded_Implementation()
{

}

void AglTFRuntimeAssetActorAsync::PostUnregisterAllComponents()
{
	PendingMeshesToLoad.Empty();
	InFlightMeshesToLoad.Empty();
	ActiveLoadProxies.Empty();

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

void AglTFRuntimeAssetActorAsync::ReceiveOnStaticMeshComponentCreated_Implementation(UStaticMeshComponent* StaticMeshComponent, const FglTFRuntimeNode& Node)
{

}

void AglTFRuntimeAssetActorAsync::ReceiveOnSkeletalMeshComponentCreated_Implementation(USkeletalMeshComponent* SkeletalMeshComponent, const FglTFRuntimeNode& Node)
{

}
