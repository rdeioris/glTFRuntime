// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeParser.h"

#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Modules/ModuleManager.h"


UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(const int32 Index, const FString& MaterialName, TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	FglTFRuntimeMaterial RuntimeMaterial;

	RuntimeMaterial.BaseSpecularFactor = MaterialsConfig.SpecularFactor;

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

	auto GetMaterialVector = [](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, const int32 Fields, bool& bHasParam, FLinearColor& ParamValue)
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonValues;
		if (JsonMaterialObject->TryGetArrayField(ParamName, JsonValues))
		{
			if (JsonValues->Num() != Fields)
			{
				return;
			}

			double Values[4];
			// default alpha
			Values[3] = 1;

			for (int32 Index = 0; Index < Fields; Index++)
			{
				(*JsonValues)[Index]->TryGetNumber(Values[Index]);
			}

			bHasParam = true;
			ParamValue = FLinearColor(Values[0], Values[1], Values[2], Values[3]);
		}
	};

	auto GetMaterialTexture = [this, MaterialsConfig](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, const bool sRGB, UTexture2D*& ParamTextureCache, TArray<FglTFRuntimeMipMap>& ParamMips, int32& ParamTexCoord)
	{
		const TSharedPtr<FJsonObject>* JsonTextureObject;
		if (JsonMaterialObject->TryGetObjectField(ParamName, JsonTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonTextureObject)->TryGetNumberField("index", TextureIndex))
			{
				return;
			}

			if (!(*JsonTextureObject)->TryGetNumberField("texCoord", ParamTexCoord))
			{
				ParamTexCoord = 0;
			}

			if (ParamTexCoord < 0 || ParamTexCoord > 3)
			{
				AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for %s: %d"), *ParamName, ParamTexCoord));
				return;
			}

			ParamTextureCache = LoadTexture(TextureIndex, ParamMips, sRGB, MaterialsConfig);
		}
	};

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField("pbrMetallicRoughness", JsonPBRObject))
	{
		GetMaterialVector(JsonPBRObject->ToSharedRef(), "baseColorFactor", 4, RuntimeMaterial.bHasBaseColorFactor, RuntimeMaterial.BaseColorFactor);
		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "baseColorTexture", true, RuntimeMaterial.BaseColorTextureCache, RuntimeMaterial.BaseColorTextureMips, RuntimeMaterial.BaseColorTexCoord);

		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", RuntimeMaterial.MetallicFactor))
		{
			RuntimeMaterial.bHasMetallicFactor = true;
		}

		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", RuntimeMaterial.RoughnessFactor))
		{
			RuntimeMaterial.bHasRoughnessFactor = true;
		}

		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "metallicRoughnessTexture", false, RuntimeMaterial.MetallicRoughnessTextureCache, RuntimeMaterial.MetallicRoughnessTextureMips, RuntimeMaterial.MetallicRoughnessTexCoord);
	}

	GetMaterialTexture(JsonMaterialObject, "normalTexture", false, RuntimeMaterial.NormalTextureCache, RuntimeMaterial.NormalTextureMips, RuntimeMaterial.NormalTexCoord);

	GetMaterialTexture(JsonMaterialObject, "occlusionTexture", false, RuntimeMaterial.OcclusionTextureCache, RuntimeMaterial.OcclusionTextureMips, RuntimeMaterial.OcclusionTexCoord);

	GetMaterialVector(JsonMaterialObject, "emissiveFactor", 3, RuntimeMaterial.bHasEmissiveFactor, RuntimeMaterial.EmissiveFactor);

	GetMaterialTexture(JsonMaterialObject, "emissiveTexture", true, RuntimeMaterial.EmissiveTextureCache, RuntimeMaterial.EmissiveTextureMips, RuntimeMaterial.EmissiveTexCoord);

	const TSharedPtr<FJsonObject>* JsonExtensions;
	if (JsonMaterialObject->TryGetObjectField("extensions", JsonExtensions))
	{
		// KHR_materials_pbrSpecularGlossiness
		const TSharedPtr<FJsonObject>* JsonPbrSpecularGlossiness;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_pbrSpecularGlossiness", JsonPbrSpecularGlossiness))
		{
			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseFactor", 4, RuntimeMaterial.bHasDiffuseFactor, RuntimeMaterial.DiffuseFactor);
			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseTexture", true, RuntimeMaterial.DiffuseTextureCache, RuntimeMaterial.DiffuseTextureMips, RuntimeMaterial.DiffuseTexCoord);

			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "specularFactor", 3, RuntimeMaterial.bHasSpecularFactor, RuntimeMaterial.SpecularFactor);

			if ((*JsonPbrSpecularGlossiness)->TryGetNumberField("glossinessFactor", RuntimeMaterial.GlossinessFactor))
			{
				RuntimeMaterial.bHasGlossinessFactor = true;
			}

			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "specularGlossinessTexture", true, RuntimeMaterial.SpecularGlossinessTextureCache, RuntimeMaterial.SpecularGlossinessTextureMips, RuntimeMaterial.SpecularGlossinessTexCoord);

			RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness = true;
		}
	}

	if (IsInGameThread())
	{
		return BuildMaterial(Index, MaterialName, RuntimeMaterial, MaterialsConfig, bUseVertexColors);
	}

	UMaterialInterface* Material = nullptr;

	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Index, MaterialName, &Material, &RuntimeMaterial, MaterialsConfig, bUseVertexColors]()
	{
		// this is mainly for editor ...
		if (IsGarbageCollecting())
		{
			return;
		}
		Material = BuildMaterial(Index, MaterialName, RuntimeMaterial, MaterialsConfig, bUseVertexColors);
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

UMaterialInterface* FglTFRuntimeParser::BuildMaterial(const int32 Index, const FString& MaterialName, const FglTFRuntimeMaterial& RuntimeMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	UMaterialInterface* BaseMaterial = nullptr;

	if (MetallicRoughnessMaterialsMap.Contains(RuntimeMaterial.MaterialType))
	{
		BaseMaterial = MetallicRoughnessMaterialsMap[RuntimeMaterial.MaterialType];
	}

	if (RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness)
	{
		if (SpecularGlossinessMaterialsMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = SpecularGlossinessMaterialsMap[RuntimeMaterial.MaterialType];
		}
	}

	if (MaterialsConfig.UberMaterialsOverrideMap.Contains(RuntimeMaterial.MaterialType))
	{
		BaseMaterial = MaterialsConfig.UberMaterialsOverrideMap[RuntimeMaterial.MaterialType];
	}

	if (MaterialsConfig.MaterialsOverrideMap.Contains(Index))
	{
		BaseMaterial = MaterialsConfig.MaterialsOverrideMap[Index];
	}

	if (MaterialsConfig.MaterialsOverrideByNameMap.Contains(MaterialName))
	{
		BaseMaterial = MaterialsConfig.MaterialsOverrideByNameMap[MaterialName];
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

	// make it public to allow exports
	Material->SetFlags(EObjectFlags::RF_Public);

	Material->SetScalarParameterValue("specularFactor", RuntimeMaterial.BaseSpecularFactor);

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


	if (RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness)
	{
		ApplyMaterialFactor(RuntimeMaterial.bHasDiffuseFactor, "baseColorFactor", RuntimeMaterial.DiffuseFactor);
		ApplyMaterialTexture("baseColorTexture", RuntimeMaterial.DiffuseTextureCache, RuntimeMaterial.DiffuseTextureMips,
			"baseColorTexCoord", RuntimeMaterial.DiffuseTexCoord,
			TextureCompressionSettings::TC_Default, true);

		ApplyMaterialFactor(RuntimeMaterial.bHasSpecularFactor, "specularFactor", RuntimeMaterial.SpecularFactor);
		ApplyMaterialFloatFactor(RuntimeMaterial.bHasGlossinessFactor, "glossinessFactor", RuntimeMaterial.GlossinessFactor);
		ApplyMaterialTexture("specularGlossinessTexture", RuntimeMaterial.SpecularGlossinessTextureCache, RuntimeMaterial.SpecularGlossinessTextureMips,
			"specularGlossinessTexCoord", RuntimeMaterial.SpecularGlossinessTexCoord,
			TextureCompressionSettings::TC_Default, false);
	}

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

	if (!GetJsonObjectBytes(JsonImageObject.ToSharedRef(), Bytes))
	{
		AddError("LoadTexture()", FString::Printf(TEXT("Unable to load image %d"), ImageIndex));
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

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, FString& MaterialName)
{
	if (Index < 0)
	{
		return nullptr;
	}

	if (!MaterialsConfig.bMaterialsOverrideMapInjectParams && MaterialsConfig.MaterialsOverrideMap.Contains(Index))
	{
		return MaterialsConfig.MaterialsOverrideMap[Index];
	}

	// first check cache
	if (CanReadFromCache(MaterialsConfig.CacheMode) && MaterialsCache.Contains(Index))
	{
		if (MaterialsNameCache.Contains(MaterialsCache[Index]))
		{
			MaterialName = MaterialsNameCache[MaterialsCache[Index]];
		}
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
	{
		return nullptr;
	}


	if (!JsonMaterialObject->TryGetStringField("name", MaterialName))
	{
		MaterialName = "";
	}

	if (!MaterialsConfig.bMaterialsOverrideMapInjectParams && MaterialsConfig.MaterialsOverrideByNameMap.Contains(MaterialName))
	{
		return MaterialsConfig.MaterialsOverrideByNameMap[MaterialName];
	}

	UMaterialInterface* Material = LoadMaterial_Internal(Index, MaterialName, JsonMaterialObject.ToSharedRef(), MaterialsConfig, bUseVertexColors);
	if (!Material)
	{
		return nullptr;
	}

	if (CanWriteToCache(MaterialsConfig.CacheMode))
	{
		MaterialsNameCache.Add(Material, MaterialName);
		MaterialsCache.Add(Index, Material);
	}

	return Material;
}