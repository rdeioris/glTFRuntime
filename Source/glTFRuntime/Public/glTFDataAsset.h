// Copyright 2015-2022 PixelPool

#pragma once

#include "Engine/DataAsset.h"
#include "glTFDataAsset.generated.h"

UCLASS()
class GLTFRUNTIME_API UglTFDataAsset
	: public UDataAsset
{
	GENERATED_BODY()

public:

	UMaterialInterface* GetOpaqueMaterial() const { return OpaqueMaterial; };
	UMaterialInterface* GetTranslucentMaterial() const { return TranslucentMaterial; };
	UMaterialInterface* GetTwoSidedMaterial() const { return TwoSidedMaterial; };
	UMaterialInterface* GetTwoSidedTranslucentMaterial() const { return TwoSidedTranslucentMaterial; };
	UMaterialInterface* GetMaskedMaterial() const { return MaskedMaterial; };
	UMaterialInterface* GetTwoSidedMaskedMaterial() const { return TwoSidedMaskedMaterial; };
	UMaterialInterface* GetSGOpaqueMaterial() const { return SGOpaqueMaterial; };
	UMaterialInterface* GetSGTranslucentMaterial() const { return SGTranslucentMaterial; };
	UMaterialInterface* GetSGTwoSidedMaterial() const { return SGTwoSidedMaterial; };
	UMaterialInterface* GetSGTwoSidedTranslucentMaterial() const { return SGTwoSidedTranslucentMaterial; };

private:
	
	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* OpaqueMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* TranslucentMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* TwoSidedMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* TwoSidedTranslucentMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* MaskedMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* TwoSidedMaskedMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* SGOpaqueMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* SGTranslucentMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* SGTwoSidedMaterial = nullptr;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, meta = (AllowPrivateAccess = true))
	UMaterialInterface* SGTwoSidedTranslucentMaterial = nullptr;
	
};