// Copyright 2021, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "glTFRuntimeAsset.h"
#include "glTFRuntimeAssetActorAsync.generated.h"

UCLASS()
class GLTFRUNTIME_API AglTFRuntimeAssetActorAsync : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AglTFRuntimeAssetActorAsync();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	virtual void ProcessNode(USceneComponent* NodeParentComponent, FglTFRuntimeNode& Node);

	template<typename T>
	FName GetSafeNodeName(const FglTFRuntimeNode& Node)
	{
		return MakeUniqueObjectName(this, T::StaticClass(), *Node.Name);
	}

public:	

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	UglTFRuntimeAsset* Asset;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeStaticMeshConfig StaticMeshConfig;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Meta = (ExposeOnSpawn = true), Category = "glTFRuntime")
	FglTFRuntimeSkeletalMeshConfig SkeletalMeshConfig;

	UFUNCTION(BlueprintNativeEvent, Category = "glTFRuntime", meta = (DisplayName = "On Scenes Loaded"))
	void ReceiveOnScenesLoaded();

private:
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = "true"), Category="glTFRuntime")
	USceneComponent* AssetRoot;

	TMap<UPrimitiveComponent*, FglTFRuntimeNode> MeshesToLoad;

	void LoadNextMeshAsync();

	UFUNCTION()
	void LoadStaticMeshAsync(UStaticMesh* StaticMesh);

	UFUNCTION()
	void LoadSkeletalMeshAsync(USkeletalMesh* SkeletalMesh);

	// this is safe to share between game and async threads because everything is sequential
	UPrimitiveComponent* CurrentPrimitiveComponent;

};
