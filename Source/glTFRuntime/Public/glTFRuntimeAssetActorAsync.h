// Copyright 2021, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeAssetActorAsync.generated.h"

class UAnimSequence;
class UPrimitiveComponent;
class USkeletalMesh;
class USkeletalMeshComponent;
class UStaticMesh;
class UStaticMeshComponent;
class UglTFRuntimeAnimationCurve;

UCLASS()
class GLTFRUNTIME_API UglTFRuntimeAssetActorAsyncLoadProxy : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(class AglTFRuntimeAssetActorAsync* InOwner, UPrimitiveComponent* InPrimitiveComponent);

	UFUNCTION()
	void OnStaticMeshLoaded(UStaticMesh* StaticMesh);

	UFUNCTION()
	void OnSkeletalMeshLoaded(USkeletalMesh* SkeletalMesh);

private:
	UPROPERTY(Transient)
	TWeakObjectPtr<class AglTFRuntimeAssetActorAsync> Owner;

	UPrimitiveComponent* PrimitiveComponentRaw = nullptr;
};

UCLASS()
class GLTFRUNTIME_API AglTFRuntimeAssetActorAsync : public AActor
{
	GENERATED_BODY()
	friend class UglTFRuntimeAssetActorAsyncLoadProxy;

	struct FglTFRuntimeMeshLoadContext
	{
		FglTFRuntimeNode Node;
		FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;
		bool bUseCustomSkeletalMeshConfig = false;
		bool bUseRecursiveSkeletalMeshLoad = false;
		bool bMorphNodeUsesNodeTreeFallback = false;
	};
	
public:	
	// Sets default values for this actor's properties
	AglTFRuntimeAssetActorAsync();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Destroyed() override;

	virtual void ProcessNode(USceneComponent* NodeParentComponent, const FName SocketName, FglTFRuntimeNode& Node);

	template<typename T>
	FString GetSafeNodeName(const FglTFRuntimeNode& Node)
	{
		return Node.Name;
		//return MakeUniqueObjectName(this, T::StaticClass(), *Node.Name);
	}

	TMap<USceneComponent*, FName> SocketMapping;
	TMap<USkeletalMeshComponent*, USkeletalMesh*> DiscoveredSkeletalMeshComponents;
	TMap<UStaticMeshComponent*, UStaticMesh*> DiscoveredStaticMeshComponents;
	TMap<USceneComponent*, float> CurveBasedAnimationsTimeTracker;
	TMap<USceneComponent*, UglTFRuntimeAnimationCurve*> CurveBasedAnimations;
	TMap<USceneComponent*, TMap<FString, UglTFRuntimeAnimationCurve*>> DiscoveredCurveAnimations;
	TMap<USkeletalMeshComponent*, TMap<FString, UAnimSequence*>> DiscoveredSkeletalAnimations;
	TSet<FString> DiscoveredAnimationNames;
	TMap<FString, float> DiscoveredAnimationDurationsByName;

	void ScenesLoaded();
	void PumpMeshLoadQueue();
	bool DispatchMeshLoad(UPrimitiveComponent* PrimitiveComponent, const FglTFRuntimeMeshLoadContext& MeshLoadContext);
	void OnStaticMeshLoadedFromProxy(UPrimitiveComponent* PrimitiveComponent, UStaticMesh* StaticMesh, UglTFRuntimeAssetActorAsyncLoadProxy* Proxy);
	void OnSkeletalMeshLoadedFromProxy(UPrimitiveComponent* PrimitiveComponent, USkeletalMesh* SkeletalMesh, UglTFRuntimeAssetActorAsyncLoadProxy* Proxy);
	void TryFinalizeLoadingFlow();
	void BeginSafeDestroy();
	void CleanupTrackedComponents();
	void TrackCreatedComponent(UActorComponent* Component);
	void RegisterNodeAnimationCurves(USceneComponent* SceneComponent, const FglTFRuntimeNode& Node);
	void RememberAnimationDuration(const FString& AnimationName, const float DurationSeconds);
	void UpdateNodeAnimations(const float DeltaTime, const bool bAdvanceTime);

public:	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	UglTFRuntimeAsset* Asset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeStaticMeshConfig StaticMeshConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeSkeletalAnimationConfig SkeletalAnimationConfig;

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "On Scenes Loaded"))
	void ReceiveOnScenesLoaded();

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "On Node Processed"))
	void ReceiveOnNodeProcessed(const int32 NodeIndex, USceneComponent* NodeSceneComponent);

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "On StaticMeshComponent Created"))
	void ReceiveOnStaticMeshComponentCreated(UStaticMeshComponent* StaticMeshComponent, const FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "On SkeletalMeshComponent Created"))
	void ReceiveOnSkeletalMeshComponentCreated(USkeletalMeshComponent* SkeletalMeshComponent, const FglTFRuntimeNode& Node);

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "Override StaticMeshConfig"))
	FglTFRuntimeStaticMeshConfig OverrideStaticMeshConfig(const int32 NodeIndex, UStaticMeshComponent* NodeStaticMeshComponent);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bShowWhileLoading;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bStaticMeshesAsSkeletal;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bStaticMeshesAsSkeletalOnMorphTargets;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bAllowSkeletalAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bAllowNodeAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bAllowPoseAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bAutoPlayAnimations;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true, ClampMin = "1", UIMin = "1"), Category = "glTFRuntime")
	int32 MaxConcurrentMeshLoads;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bLoadAllSkeletalAnimations;

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void StopLoadingAndDestroy();

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void PlayAnimations(FString AnimationName = "");

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void PauseAnimations();

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void ResumeAnimations();

	UFUNCTION(BlueprintCallable, Category = "glTFRuntime")
	void SeekAnimations(float TimeSeconds, bool bFireNotifies = false);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	int32 GetNumAnimations() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	TArray<FString> GetAnimationNames(const bool bIncludeUnnameds = true) const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "glTFRuntime")
	bool GetAnimationDurationByName(const FString& Name, float& OutDuration) const;

	virtual void PostUnregisterAllComponents() override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	bool bAllowLights;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeLightConfig LightConfig;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category="glTFRuntime")
	USceneComponent* AssetRoot;

	TMap<UPrimitiveComponent*, FglTFRuntimeMeshLoadContext> PendingMeshesToLoad;
	TMap<UPrimitiveComponent*, FglTFRuntimeMeshLoadContext> InFlightMeshesToLoad;

	UPROPERTY(Transient)
	TSet<TObjectPtr<UglTFRuntimeAssetActorAsyncLoadProxy>> ActiveLoadProxies;

	UPROPERTY(Transient)
	TSet<TObjectPtr<UActorComponent>> TrackedCreatedComponents;

	UPROPERTY()
	TArray<TObjectPtr<UAnimSequence>> AllSkeletalAnimations;

	UPROPERTY()
	TArray<TObjectPtr<UglTFRuntimeAnimationCurve>> AllCurveAnimations;

	bool bStopLoadingRequested;
	bool bDestroyInitiated;
	bool bScenesLoadedTriggered;
	bool bComponentsCleanedUp;
	bool bAnimationsPaused;
	bool bNodeAnimationsPlaying;

	double LoadingStartTime;

};
