// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"

#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Modules/ModuleManager.h"


UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	FglTFRuntimeMaterial RuntimeMaterial;

	if (!JsonMaterialObject->TryGetBoolField("doubleSided", RuntimeMaterial.bTwoSided))
	{
		RuntimeMaterial.bTwoSided = false;
	}

	FString AlphaMode;
	if (!JsonMaterialObject->TryGetStringField("alphaMode", AlphaMode))
	{
		AlphaMode = "OPAQUE";
	}

	if (AlphaMode == "BLEND")
	{
		RuntimeMaterial.bTranslucent = true;
	}
	else if (AlphaMode == "MASK")
	{
		RuntimeMaterial.bTranslucent = true;
		double AlphaCutoffDouble;
		if (!JsonMaterialObject->TryGetNumberField("alphaCutoff", AlphaCutoffDouble))
		{
			RuntimeMaterial.AlphaCutoff = 0.5f;
		}
		else
		{
			RuntimeMaterial.AlphaCutoff = AlphaCutoffDouble;
		}
	}
	else if (AlphaMode != "OPAQUE")
	{
		AddError("LoadMaterial_Internal()", "Unsupported alphaMode");
		return nullptr;
	}

	if (RuntimeMaterial.bTranslucent && RuntimeMaterial.bTwoSided)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::TwoSidedTranslucent;
	}
	else if (RuntimeMaterial.bTranslucent)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::Translucent;
	}
	else if (RuntimeMaterial.bTwoSided)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::TwoSided;
	}

	if (!MaterialsMap.Contains(RuntimeMaterial.MaterialType))
	{
		return nullptr;
	}

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

			//Material->SetVectorParameterValue("baseColorFactor", FLinearColor(R, G, B, A));
			RuntimeMaterial.bHasBaseColorFactor = true;
			RuntimeMaterial.BaseColorFactor = FLinearColor(R, G, B, A);
		}

		const TSharedPtr<FJsonObject>* JsonBaseColorTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("baseColorTexture", JsonBaseColorTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonBaseColorTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			RuntimeMaterial.BaseColorTextureCache = LoadTexture(TextureIndex, RuntimeMaterial.BaseColorTextureMips, true, MaterialsConfig);

			if (!(*JsonBaseColorTextureObject)->TryGetNumberField("texCoord", RuntimeMaterial.BaseColorTexCoord))
			{
				RuntimeMaterial.BaseColorTexCoord = 0;
			}

			if (RuntimeMaterial.BaseColorTexCoord < 0 || RuntimeMaterial.BaseColorTexCoord > 3)
			{
				AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for baseColorTexture: %d"), RuntimeMaterial.BaseColorTexCoord));
				return nullptr;
			}
		}


		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", (double&)RuntimeMaterial.MetallicFactor))
		{
			RuntimeMaterial.bHasMetallicFactor = true;
		}

		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", (double&)RuntimeMaterial.RoughnessFactor))
		{
			RuntimeMaterial.bHasRoughnessFactor = true;
		}

		const TSharedPtr<FJsonObject>* JsonMetallicRoughnessTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("metallicRoughnessTexture", JsonMetallicRoughnessTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonMetallicRoughnessTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			RuntimeMaterial.MetallicRoughnessTextureCache = LoadTexture(TextureIndex, RuntimeMaterial.MetallicRoughnessTextureMips, false, MaterialsConfig);

			if (!(*JsonMetallicRoughnessTextureObject)->TryGetNumberField("texCoord", RuntimeMaterial.MetallicRoughnessTexCoord))
			{
				RuntimeMaterial.MetallicRoughnessTexCoord = 0;
			}

			if (RuntimeMaterial.MetallicRoughnessTexCoord < 0 || RuntimeMaterial.MetallicRoughnessTexCoord > 3)
			{
				AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for metallicRoughnessTexture: %d"), RuntimeMaterial.MetallicRoughnessTexCoord));
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

		RuntimeMaterial.NormalTextureCache = LoadTexture(TextureIndex, RuntimeMaterial.NormalTextureMips, false, MaterialsConfig);

		if (!(*JsonNormalTextureObject)->TryGetNumberField("texCoord", RuntimeMaterial.NormalTexCoord))
		{
			RuntimeMaterial.NormalTexCoord = 0;
		}

		if (RuntimeMaterial.NormalTexCoord < 0 || RuntimeMaterial.NormalTexCoord > 3)
		{
			AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for normalTexture: %d"), RuntimeMaterial.NormalTexCoord));
			return nullptr;
		}
	}

	const TSharedPtr<FJsonObject>* JsonOcclusionTextureObject;
	if (JsonMaterialObject->TryGetObjectField("occlusionTexture", JsonOcclusionTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonOcclusionTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		RuntimeMaterial.OcclusionTextureCache = LoadTexture(TextureIndex, RuntimeMaterial.OcclusionTextureMips, false, MaterialsConfig);

		if (!(*JsonOcclusionTextureObject)->TryGetNumberField("texCoord", RuntimeMaterial.OcclusionTexCoord))
		{
			RuntimeMaterial.OcclusionTexCoord = 0;
		}

		if (RuntimeMaterial.OcclusionTexCoord < 0 || RuntimeMaterial.OcclusionTexCoord > 3)
		{
			AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for occlisionTexture: %d"), RuntimeMaterial.OcclusionTexCoord));
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

		RuntimeMaterial.bHasEmissiveFactor = true;
		RuntimeMaterial.EmissiveFactor = FLinearColor(R, G, B);
	}

	const TSharedPtr<FJsonObject>* JsonEmissiveTextureObject;
	if (JsonMaterialObject->TryGetObjectField("emissiveTexture", JsonEmissiveTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonEmissiveTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		RuntimeMaterial.EmissiveTextureCache = LoadTexture(TextureIndex, RuntimeMaterial.EmissiveTextureMips, true, MaterialsConfig);

		if (!(*JsonEmissiveTextureObject)->TryGetNumberField("texCoord", RuntimeMaterial.EmissiveTexCoord))
		{
			RuntimeMaterial.EmissiveTexCoord = 0;
		}

		if (RuntimeMaterial.EmissiveTexCoord < 0 || RuntimeMaterial.EmissiveTexCoord > 3)
		{
			AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for emissiveTexture: %d"), RuntimeMaterial.EmissiveTexCoord));
			return nullptr;
		}

	}

	if (IsInGameThread())
	{
		return BuildMaterial(RuntimeMaterial, MaterialsConfig, bUseVertexColors);
	}

	UMaterialInterface* Material = nullptr;

	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this, &Material, &RuntimeMaterial, MaterialsConfig, bUseVertexColors]()
	{
		Material = BuildMaterial(RuntimeMaterial, MaterialsConfig, bUseVertexColors);
	}, TStatId(), nullptr, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

	return Material;
}

UTexture2D* FglTFRuntimeParser::BuildTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const TEnumAsByte<TextureCompressionSettings> Compression, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	UTexture2D* Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Public);

	Texture->PlatformData = new FTexturePlatformData();
	Texture->PlatformData->SizeX = Mips[0].Width;
	Texture->PlatformData->SizeY = Mips[0].Height;
	Texture->PlatformData->PixelFormat = EPixelFormat::PF_B8G8R8A8;

	for (const FglTFRuntimeMipMap& MipMap : Mips)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		Texture->PlatformData->Mips.Add(Mip);
		Mip->SizeX = MipMap.Width;
		Mip->SizeY = MipMap.Height;

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
		void* Data = Mip->BulkData.Realloc(MipMap.Pixels.Num());
		FMemory::Memcpy(Data, MipMap.Pixels.GetData(), MipMap.Pixels.Num());
		Mip->BulkData.Unlock();
	}

	Texture->CompressionSettings = Compression;
	Texture->SRGB = sRGB;

	Texture->UpdateResource();

	TexturesCache.Add(Mips[0].TextureIndex, Texture);

	return Texture;
}

UMaterialInterface* FglTFRuntimeParser::BuildMaterial(const FglTFRuntimeMaterial& RuntimeMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	UMaterialInterface* BaseMaterial = MaterialsMap[RuntimeMaterial.MaterialType];
	if (MaterialsConfig.UberMaterialsOverrideMap.Contains(RuntimeMaterial.MaterialType))
	{
		BaseMaterial = MaterialsConfig.UberMaterialsOverrideMap[RuntimeMaterial.MaterialType];
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

	Material->SetScalarParameterValue("alphaCutoff", RuntimeMaterial.AlphaCutoff);

	auto ApplyMaterialFactor = [this, Material](bool bHasFactor, const FName& FactorName, FLinearColor FactorValue)
	{
		if (bHasFactor)
		{
			Material->SetVectorParameterValue(FactorName, FactorValue);
		}
	};

	auto ApplyMaterialFloatFactor = [this, Material](bool bHasFactor, const FName& FactorName, float FactorValue)
	{
		if (bHasFactor)
		{
			Material->SetScalarParameterValue(FactorName, FactorValue);
		}
	};

	auto ApplyMaterialTexture = [this, Material, MaterialsConfig](const FName& TextureName, UTexture2D* TextureCache, const TArray<FglTFRuntimeMipMap>& Mips, const FName& TexCoordName, const int32 TexCoord, const TEnumAsByte<TextureCompressionSettings> Compression, const bool sRGB)
	{
		UTexture2D* Texture = TextureCache;
		if (!Texture)
		{
			if (Mips.Num() > 0)
			{
				Texture = BuildTexture(Material, Mips, Compression, sRGB, MaterialsConfig);
			}
		}
		if (Texture)
		{
			Material->SetTextureParameterValue(TextureName, Texture);
			FVector4 UVSet = FVector4(0, 0, 0, 0);
			UVSet[TexCoord] = 1;
			Material->SetVectorParameterValue(TexCoordName, FLinearColor(UVSet));
		}
	};

	ApplyMaterialFactor(RuntimeMaterial.bHasBaseColorFactor, "baseColorFactor", RuntimeMaterial.BaseColorFactor);
	ApplyMaterialTexture("baseColorTexture", RuntimeMaterial.BaseColorTextureCache, RuntimeMaterial.BaseColorTextureMips,
		"baseColorTexCoord", RuntimeMaterial.BaseColorTexCoord,
		TextureCompressionSettings::TC_Default, true);

	ApplyMaterialFloatFactor(RuntimeMaterial.bHasMetallicFactor, "metallicFactor", RuntimeMaterial.MetallicFactor);
	ApplyMaterialFloatFactor(RuntimeMaterial.bHasRoughnessFactor, "roughnessFactor", RuntimeMaterial.RoughnessFactor);

	ApplyMaterialTexture("metallicRoughnessTexture", RuntimeMaterial.MetallicRoughnessTextureCache, RuntimeMaterial.MetallicRoughnessTextureMips,
		"metallicRoughnessTexCoord", RuntimeMaterial.MetallicRoughnessTexCoord,
		TextureCompressionSettings::TC_Default, false);

	ApplyMaterialTexture("normalTexture", RuntimeMaterial.NormalTextureCache, RuntimeMaterial.NormalTextureMips,
		"normalTexCoord", RuntimeMaterial.NormalTexCoord,
		TextureCompressionSettings::TC_Normalmap, false);

	ApplyMaterialTexture("occlusionTexture", RuntimeMaterial.OcclusionTextureCache, RuntimeMaterial.OcclusionTextureMips,
		"occlusionTexCoord", RuntimeMaterial.OcclusionTexCoord,
		TextureCompressionSettings::TC_Default, false);

	ApplyMaterialFactor(RuntimeMaterial.bHasEmissiveFactor, "emissiveFactor", RuntimeMaterial.EmissiveFactor);

	ApplyMaterialTexture("emissiveTexture", RuntimeMaterial.EmissiveTextureCache, RuntimeMaterial.EmissiveTextureMips,
		"emissiveTexCoord", RuntimeMaterial.EmissiveTexCoord,
		TextureCompressionSettings::TC_Default, true);

	Material->SetScalarParameterValue("bUseVertexColors", (bUseVertexColors && !MaterialsConfig.bDisableVertexColors) ? 1.0f : 0.0f);

	return Material;
}

UTexture2D* FglTFRuntimeParser::LoadTexture(const int32 TextureIndex, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (TextureIndex < 0)
	{
		return nullptr;
	}

	if (MaterialsConfig.TexturesOverrideMap.Contains(TextureIndex))
	{
		return MaterialsConfig.TexturesOverrideMap[TextureIndex];
	}

	// first check cache
	if (TexturesCache.Contains(TextureIndex))
	{
		return TexturesCache[TextureIndex];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTextures;
	// no images ?
	if (!Root->TryGetArrayField("textures", JsonTextures))
	{
		return nullptr;
	}

	if (TextureIndex >= JsonTextures->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonTextureObject = (*JsonTextures)[TextureIndex]->AsObject();
	if (!JsonTextureObject)
	{
		return nullptr;
	}

	int64 ImageIndex;
	if (!JsonTextureObject->TryGetNumberField("source", ImageIndex))
	{
		return nullptr;
	}

	if (ImageIndex < 0)
	{
		return nullptr;
	}

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
	{
		return nullptr;
	}

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

		int32 NumOfMips = 1;

		TArray64<FColor> UncompressedColors;

		if (MaterialsConfig.bGeneratesMipMaps && FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height))
		{
			NumOfMips = FMath::FloorLog2(FMath::Max(Width, Height)) + 1;

			for (int32 MipY = 0; MipY < Height; MipY++)
			{
				for (int32 MipX = 0; MipX < Width; MipX++)
				{
					int64 MipColorIndex = ((MipY * Width) + MipX) * 4;
					uint8 MipColorB = UncompressedBytes[MipColorIndex];
					uint8 MipColorG = UncompressedBytes[MipColorIndex + 1];
					uint8 MipColorR = UncompressedBytes[MipColorIndex + 2];
					uint8 MipColorA = UncompressedBytes[MipColorIndex + 3];
					UncompressedColors.Add(FColor(MipColorR, MipColorG, MipColorB, MipColorA));
				}
			}
		}

		int32 MipWidth = Width;
		int32 MipHeight = Height;

		for (int32 MipIndex = 0; MipIndex < NumOfMips; MipIndex++)
		{
			FglTFRuntimeMipMap MipMap(TextureIndex);
			MipMap.Width = MipWidth;
			MipMap.Height = MipHeight;

			// Resize Image
			if (MipIndex > 0)
			{
				TArray64<FColor> ResizedMipData;
				ResizedMipData.AddUninitialized(MipWidth * MipHeight);
				FImageUtils::ImageResize(Width, Height, UncompressedColors, MipWidth, MipHeight, ResizedMipData, sRGB);
				for (FColor& Color : ResizedMipData)
				{
					MipMap.Pixels.Add(Color.B);
					MipMap.Pixels.Add(Color.G);
					MipMap.Pixels.Add(Color.R);
					MipMap.Pixels.Add(Color.A);
				}
			}
			else
			{
				MipMap.Pixels = UncompressedBytes;
			}

			Mips.Add(MipMap);

			MipWidth = FMath::Max(MipWidth / 2, 1);
			MipHeight = FMath::Max(MipHeight / 2, 1);
		}

	}

	return nullptr;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
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

	UMaterialInterface* Material = LoadMaterial_Internal(JsonMaterialObject.ToSharedRef(), MaterialsConfig, bUseVertexColors);
	if (!Material)
		return nullptr;

	MaterialsCache.Add(Index, Material);

	return Material;
}