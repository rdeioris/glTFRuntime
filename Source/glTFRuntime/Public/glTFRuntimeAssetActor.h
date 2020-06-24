// Copyright 2020, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeAssetActor.generated.h"

UCLASS()
class GLTFRUNTIME_API AglTFRuntimeAssetActor : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AglTFRuntimeAssetActor();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void ProcessNode(USceneComponent* NodeParentComponent, FglTFRuntimeNode& Node);

	TMap<USceneComponent*, float>  CurveBasedAnimationsTimeTracker;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	UglTFRuntimeAsset* Asset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "glTFRuntime")
	TMap<USceneComponent*, UglTFRuntimeAnimationCurve*> CurveBasedAnimations;

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category="glTFRuntime")
	USceneComponent* AssetRoot;

};
