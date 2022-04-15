// Copyright 2020-2022, Roberto De Ioris 
// Copyright 2022, Avatus LLC

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "glTFRuntimeMaterialBaker.generated.h"

UCLASS()
class GLTFRUNTIME_API AglTFRuntimeMaterialBaker : public AActor
{
	GENERATED_BODY()
	
public:	
	// Sets default values for this actor's properties
	AglTFRuntimeMaterialBaker();

protected:
	// Called when the game starts or when spawned
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere)
	class USceneCaptureComponent2D* SceneCaptureComponent;

	UPROPERTY(EditAnywhere)
	class UStaticMeshComponent* RenderingPlaneComponent;

public:	
	// Called every frame
	virtual void Tick(float DeltaTime) override;

	bool BakeMaterialToPng(UMaterialInterface* Material, TArray<uint8>& BaseColor, TArray<uint8>& NormalMap, TArray<uint8>& MetallicRoughness);

};
