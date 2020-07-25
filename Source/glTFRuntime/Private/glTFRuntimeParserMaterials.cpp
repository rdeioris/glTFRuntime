// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"

#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Modules/ModuleManager.h"

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
		AddError("LoadMaterial_Internal()", "Unsupported alphaMode");
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
	if (MaterialsConfig.UberMaterialsOverrideMap.Contains(MaterialType))
	{
		BaseMaterial = MaterialsConfig.UberMaterialsOverrideMap[MaterialType];
	}

	if (!BaseMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
	if (!Material)
	{
		return nullptr;
	}

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

			UTexture2D* Texture = LoadTexture(Material, TextureIndex, MaterialsConfig);
			if (!Texture)
			{
				return nullptr;
			}

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

			UTexture2D* Texture = LoadTexture(Material, TextureIndex, MaterialsConfig);
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

		UTexture2D* Texture = LoadTexture(Material, TextureIndex, MaterialsConfig);
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

		UTexture2D* Texture = LoadTexture(Material, TextureIndex, MaterialsConfig);
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
		AddError("AssignTexCoord", FString::Printf(TEXT("Invalid UV Set for %s: %lld"), *MaterialParam.ToString(), TexCoord));
		return false;
	}
	FVector4 UVSet = FVector4(0, 0, 0, 0);
	UVSet[TexCoord] = 1;
	Material->SetVectorParameterValue(MaterialParam, FLinearColor(UVSet));
	return true;
}

UTexture2D* FglTFRuntimeParser::LoadTexture(UObject* Outer, const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (Index < 0)
		return nullptr;

	if (MaterialsConfig.TexturesOverrideMap.Contains(Index))
	{
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
		return MaterialsConfig.ImagesOverrideMap[ImageIndex];
	}

	TSharedPtr<FJsonObject> JsonImageObject = (*JsonImages)[ImageIndex]->AsObject();
	if (!JsonImageObject)
		return nullptr;

	TArray64<uint8> Bytes;
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
				AddError("LoadTexture()", FString::Printf(TEXT("Unable to get bufferView: %d"), BufferViewIndex));
				return nullptr;
			}
		}
	}

	if (Bytes.Num() == 0)
	{
		return nullptr;
	}

	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		AddError("LoadTexture()", "Unable to detect image format");
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid())
	{
		AddError("LoadTexture()", "Unable to create ImageWrapper");
		return nullptr;
	}
	if (!ImageWrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		AddError("LoadTexture()", "Unable to parse image data");
		return nullptr;
	}

	TArray64<uint8> UncompressedBytes;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBytes))
	{
		AddError("LoadTexture()", "Unable to get raw image data");
		return nullptr;
	}

	EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	if (Width > 0 && Height > 0 &&
		(Width % GPixelFormats[PixelFormat].BlockSizeX) == 0 &&
		(Height % GPixelFormats[PixelFormat].BlockSizeY) == 0)
	{

		UTexture2D* Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Public);

		Texture->PlatformData = new FTexturePlatformData();
		Texture->PlatformData->SizeX = Width;
		Texture->PlatformData->SizeY = Height;
		Texture->PlatformData->PixelFormat = PixelFormat;

		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Texture->PlatformData->Mips.Add(Mip);
		Mip->SizeX = Width;
		Mip->SizeY = Height;
#if !WITH_EDITOR
#if !NO_LOGGING
		ELogVerbosity::Type CurrentLogSerializationVerbosity = LogSerialization.GetVerbosity();
		bool bResetLogVerbosity = false;
		if (CurrentLogSerializationVerbosity >= ELogVerbosity::Warning)
		{
			LogSerialization.SetVerbosity(ELogVerbosity::Error);
			bResetLogVerbosity = true;
		}
#endif
#endif
		Mip->BulkData.Lock(LOCK_READ_WRITE);
#if !WITH_EDITOR
#if !NO_LOGGING
		if (bResetLogVerbosity)
		{
			LogSerialization.SetVerbosity(CurrentLogSerializationVerbosity);
		}
#endif
#endif
		void* Data = Mip->BulkData.Realloc(UncompressedBytes.Num());
		FMemory::Memcpy(Data, UncompressedBytes.GetData(), UncompressedBytes.Num());
		Mip->BulkData.Unlock();

		Texture->UpdateResource();

		TexturesCache.Add(Index, Texture);

		return Texture;
	}

	return nullptr;
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