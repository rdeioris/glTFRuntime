// Copyright 2023, Roberto De Ioris.

#pragma once

#include "CoreMinimal.h"
#include "Components/SkeletalMeshComponent.h"
#include "glTFRuntimeSkeletalMeshComponent.generated.h"

/**
 * 
 */
UCLASS(Blueprintable, meta = (BlueprintSpawnableComponent))
class GLTFRUNTIME_API UglTFRuntimeSkeletalMeshComponent : public USkeletalMeshComponent
{
	GENERATED_BODY()

public:

	bool ContainsPhysicsTriMeshData(bool InUseAllTriData) const override;
	bool GetPhysicsTriMeshData(struct FTriMeshCollisionData* CollisionData, bool InUseAllTriData) override;
	
};
