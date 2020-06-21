#include "glTFRuntimeParser.h"

#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	bool bTwoSided = false;
	bool bTranslucent = false;
	float AlphaCutoff = 0;

	EglTFRuntimeMaterialType MaterialType = EglTFRuntimeMaterialType::Opaque;

	if (!JsonMaterialObject->TryGetBoolField("doubleSided", bTwoSided))
	{
		bTwoSided = false;
	}

	FString AlphaMode;
	if (!JsonMaterialObject->TryGetStringField("alphaMode", AlphaMode))
	{
		AlphaMode = "OPAQUE";
	}

	if (AlphaMode == "BLEND")
	{
		bTranslucent = true;
	}
	else if (AlphaMode == "MASK")
	{
		bTranslucent = true;
		double AlphaCutoffDouble;
		if (!JsonMaterialObject->TryGetNumberField("alphaCutoff", AlphaCutoffDouble))
		{
			AlphaCutoff = 0.5f;
		}
		else
		{
			AlphaCutoff = AlphaCutoffDouble;
		}
	}
	else if (AlphaMode != "OPAQUE")
	{
		UE_LOG(LogTemp, Error, TEXT("Unsupported alphaMode"));
		return nullptr;
	}

	if (bTranslucent && bTwoSided)
	{
		MaterialType = EglTFRuntimeMaterialType::TwoSidedTranslucent;
	}
	else if (bTranslucent)
	{
		MaterialType = EglTFRuntimeMaterialType::Translucent;
	}
	else if (bTwoSided)
	{
		MaterialType = EglTFRuntimeMaterialType::TwoSided;
	}


	if (!MaterialsMap.Contains(MaterialType))
	{
		return nullptr;
	}

	UMaterialInterface* BaseMaterial = MaterialsMap[MaterialType];
	if (!BaseMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
	if (!Material)
		return nullptr;

	Material->SetScalarParameterValue("alphaCutoff", AlphaCutoff);

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField("pbrMetallicRoughness", JsonPBRObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* baseColorFactorValues;
		if ((*JsonPBRObject)->TryGetArrayField("baseColorFactor", baseColorFactorValues))
		{
			if (baseColorFactorValues->Num() != 4)
				return nullptr;

			double R, G, B, A;
			if (!(*baseColorFactorValues)[0]->TryGetNumber(R))
				return nullptr;
			if (!(*baseColorFactorValues)[1]->TryGetNumber(G))
				return nullptr;
			if (!(*baseColorFactorValues)[2]->TryGetNumber(B))
				return nullptr;
			if (!(*baseColorFactorValues)[3]->TryGetNumber(A))
				return nullptr;

			Material->SetVectorParameterValue("baseColorFactor", FLinearColor(R, G, B, A));
		}

		const TSharedPtr<FJsonObject>* JsonBaseColorTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("baseColorTexture", JsonBaseColorTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonBaseColorTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
			if (!Texture)
				return nullptr;

			Material->SetTextureParameterValue("baseColorTexture", Texture);
			if (!AssignTexCoord(*JsonBaseColorTextureObject, Material, "baseColorTexCoord"))
			{
				return nullptr;
			}
		}

		double metallicFactor;
		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", metallicFactor))
		{
			Material->SetScalarParameterValue("metallicFactor", metallicFactor);
		}
		double roughnessFactor;
		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", roughnessFactor))
		{
			Material->SetScalarParameterValue("roughnessFactor", roughnessFactor);
		}

		const TSharedPtr<FJsonObject>* JsonMetallicRoughnessTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("metallicRoughnessTexture", JsonMetallicRoughnessTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonMetallicRoughnessTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
			if (!Texture)
				return nullptr;

			Material->SetTextureParameterValue("metallicRoughnessTexture", Texture);
			if (!AssignTexCoord(*JsonMetallicRoughnessTextureObject, Material, "metallicRoughnessTexCoord"))
			{
				return nullptr;
			}
		}
	}

	const TSharedPtr<FJsonObject>* JsonNormalTextureObject;
	if (JsonMaterialObject->TryGetObjectField("normalTexture", JsonNormalTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonNormalTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
		if (!Texture)
			return nullptr;

		Material->SetTextureParameterValue("normalTexture", Texture);
		if (!AssignTexCoord(*JsonNormalTextureObject, Material, "normalTexCoord"))
		{
			return nullptr;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* emissiveFactorValues;
	if (JsonMaterialObject->TryGetArrayField("emissiveFactor", emissiveFactorValues))
	{
		if (emissiveFactorValues->Num() != 3)
			return nullptr;

		double R, G, B;
		if (!(*emissiveFactorValues)[0]->TryGetNumber(R))
			return nullptr;
		if (!(*emissiveFactorValues)[1]->TryGetNumber(G))
			return nullptr;
		if (!(*emissiveFactorValues)[2]->TryGetNumber(B))
			return nullptr;

		Material->SetVectorParameterValue("emissiveFactor", FLinearColor(R, G, B));
	}

	const TSharedPtr<FJsonObject>* JsonEmissiveTextureObject;
	if (JsonMaterialObject->TryGetObjectField("emissiveTexture", JsonEmissiveTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonEmissiveTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
		if (!Texture)
			return nullptr;

		Material->SetTextureParameterValue("emissiveTexture", Texture);
		if (!AssignTexCoord(*JsonEmissiveTextureObject, Material, "emissiveTexCoord"))
		{
			return nullptr;
		}
	}

	return Material;
}

bool FglTFRuntimeParser::AssignTexCoord(TSharedPtr<FJsonObject> JsonTextureObject, UMaterialInstanceDynamic* Material, const FName MaterialParam)
{
	int64 TexCoord;
	if (!JsonTextureObject->TryGetNumberField("texCoord", TexCoord))
	{
		TexCoord = 0;
	}
	if (TexCoord < 0 || TexCoord > 3)
	{
		UE_LOG(LogTemp, Error, TEXT("Invalid UV Set for %s: %lld"), *MaterialParam.ToString(), TexCoord);
		return false;
	}
	FVector4 UVSet = FVector4(0, 0, 0, 0);
	UVSet[TexCoord] = 1;
	Material->SetVectorParameterValue(MaterialParam, FLinearColor(UVSet));
	return true;
}

UTexture2D* FglTFRuntimeParser::LoadTexture(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	UE_LOG(LogTemp, Error, TEXT("attempting to load image: %d"), Index);
	if (Index < 0)
		return nullptr;

	if (MaterialsConfig.TexturesOverrideMap.Contains(Index))
	{
		UE_LOG(LogTemp, Error, TEXT("Found overriden texture for %d"), Index);
		return MaterialsConfig.TexturesOverrideMap[Index];
	}

	// first check cache
	if (TexturesCache.Contains(Index))
	{
		return TexturesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTextures;
	// no images ?
	if (!Root->TryGetArrayField("textures", JsonTextures))
	{
		return nullptr;
	}

	if (Index >= JsonTextures->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonTextureObject = (*JsonTextures)[Index]->AsObject();
	if (!JsonTextureObject)
		return nullptr;

	int64 ImageIndex;
	if (!JsonTextureObject->TryGetNumberField("source", ImageIndex))
		return nullptr;

	if (ImageIndex < 0)
		return nullptr;

	const TArray<TSharedPtr<FJsonValue>>* JsonImages;
	// no images ?
	if (!Root->TryGetArrayField("images", JsonImages))
	{
		return nullptr;
	}

	if (ImageIndex >= JsonImages->Num())
	{
		return nullptr;
	}

	if (MaterialsConfig.ImagesOverrideMap.Contains(ImageIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Found overriden image for %d"), ImageIndex);
		return MaterialsConfig.ImagesOverrideMap[ImageIndex];
	}

	TSharedPtr<FJsonObject> JsonImageObject = (*JsonImages)[ImageIndex]->AsObject();
	if (!JsonImageObject)
		return nullptr;

	TArray<uint8> Bytes;
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	FString Uri;
	if (JsonImageObject->TryGetStringField("uri", Uri))
	{
		if (!ParseBase64Uri(Uri, Bytes))
		{
			return nullptr;
		}
	}
	else
	{
		int64 BufferViewIndex;
		if (JsonImageObject->TryGetNumberField("bufferView", BufferViewIndex))
		{
			int64 Stride;
			if (!GetBufferView(BufferViewIndex, Bytes, Stride))
			{
				UE_LOG(LogTemp, Error, TEXT("unable to get bufferView: %d"), BufferViewIndex);
				return nullptr;
			}
		}
	}

	UE_LOG(LogTemp, Error, TEXT("detected image bytes: %d"), Bytes.Num());

	if (Bytes.Num() == 0)
		return nullptr;

	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to detect image format"));
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to create ImageWrapper"));
		return nullptr;
	}
	if (!ImageWrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to parse image data"));
		return nullptr;
	}

	TArray<uint8> UncompressedBytes;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBytes))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to get raw image data"));
		return nullptr;
	}

	EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PixelFormat);
	if (!Texture)
		return nullptr;

	FTexture2DMipMap& Mip = Texture->PlatformData->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, UncompressedBytes.GetData(), UncompressedBytes.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	TexturesCache.Add(Index, Texture);

	return Texture;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (MaterialsCache.Contains(Index))
	{
		return MaterialsCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMaterials;

	// no materials ?
	if (!Root->TryGetArrayField("materials", JsonMaterials))
	{
		return nullptr;
	}

	if (Index >= JsonMaterials->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMaterialObject = (*JsonMaterials)[Index]->AsObject();
	if (!JsonMaterialObject)
		return nullptr;

	UMaterialInterface* Material = LoadMaterial_Internal(JsonMaterialObject.ToSharedRef(), MaterialsConfig);
	if (!Material)
		return nullptr;

	MaterialsCache.Add(Index, Material);

	return Material;
}