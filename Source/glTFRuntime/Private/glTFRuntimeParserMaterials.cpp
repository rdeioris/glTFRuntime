// Copyright 2020-2023, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "Runtime/Launch/Resources/Version.h"
#include "Engine/Texture2D.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 2
#include "MaterialDomain.h"
#else
#include "MaterialShared.h"
#endif
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Math/UnrealMathUtility.h"
#include "Modules/ModuleManager.h"
#include "TextureResource.h"


UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(const int32 Index, const FString& MaterialName, TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, UMaterialInterface* ForceBaseMaterial)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_LoadMaterial_Internal, FColor::Magenta);
	FglTFRuntimeMaterial RuntimeMaterial;

	const FString Generator = GetGenerator();
	bool bSpecularAutoDetected = false;
	if (Generator.Contains("Blender") || Generator.Contains("Unreal Engine"))
	{
		RuntimeMaterial.BaseSpecularFactor = 0.5;
		bSpecularAutoDetected = true;
	}

	if (!bSpecularAutoDetected || MaterialsConfig.SpecularFactor > 0)
	{
		RuntimeMaterial.BaseSpecularFactor = MaterialsConfig.SpecularFactor;
	}

	if (!JsonMaterialObject->TryGetBoolField(TEXT("doubleSided"), RuntimeMaterial.bTwoSided))
	{
		RuntimeMaterial.bTwoSided = false;
	}

	FString AlphaMode;
	if (!JsonMaterialObject->TryGetStringField(TEXT("alphaMode"), AlphaMode))
	{
		AlphaMode = "OPAQUE";
	}

	if (AlphaMode == "BLEND")
	{
		RuntimeMaterial.bTranslucent = true;
	}
	else if (AlphaMode == "MASK")
	{
		RuntimeMaterial.bMasked = true;
		double AlphaCutoffDouble;
		if (!JsonMaterialObject->TryGetNumberField(TEXT("alphaCutoff"), AlphaCutoffDouble))
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
	if (RuntimeMaterial.bMasked && RuntimeMaterial.bTwoSided)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::TwoSidedMasked;
	}
	else if (RuntimeMaterial.bTranslucent)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::Translucent;
	}
	else if (RuntimeMaterial.bMasked)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::Masked;
	}
	else if (RuntimeMaterial.bTwoSided)
	{
		RuntimeMaterial.MaterialType = EglTFRuntimeMaterialType::TwoSided;
	}

	auto GetMaterialVector = [](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, const int32 Fields, bool& bHasParam, FLinearColor& ParamValue)
		{
			bHasParam = false;

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

	auto GetMaterialFactor = [](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, bool& bHasParam, double& Value)
		{
			if (JsonMaterialObject->TryGetNumberField(*ParamName, Value))
			{
				bHasParam = true;
			}
		};

	auto GetMaterialScalar = [](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, double& Value)
		{
			JsonMaterialObject->TryGetNumberField(*ParamName, Value);
		};

	auto GetMaterialTexture = [this, MaterialsConfig](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, const bool sRGB, UTexture2D*& ParamTextureCache, TArray<FglTFRuntimeMipMap>& ParamMips, FglTFRuntimeTextureTransform& ParamTransform, FglTFRuntimeTextureSampler& Sampler, const bool bForceNormalMapCompression) -> const TSharedPtr<FJsonObject>
		{
			const TSharedPtr<FJsonObject>* JsonTextureObject;
			if (JsonMaterialObject->TryGetObjectField(ParamName, JsonTextureObject))
			{
				int64 TextureIndex;
				if (!(*JsonTextureObject)->TryGetNumberField(TEXT("index"), TextureIndex))
				{
					return nullptr;
				}

				if (!(*JsonTextureObject)->TryGetNumberField(TEXT("texCoord"), ParamTransform.TexCoord))
				{
					ParamTransform.TexCoord = 0;
				}

				ParamTransform.Rotation = (1.0 / (PI * 2)) * GetJsonExtensionObjectNumber(JsonTextureObject->ToSharedRef(), "KHR_texture_transform", "rotation", 0) * -1;
				TArray<double> Offset = GetJsonExtensionObjectNumbers(JsonTextureObject->ToSharedRef(), "KHR_texture_transform", "offset");
				if (Offset.Num() >= 2)
				{
					ParamTransform.Offset = FLinearColor(Offset[0], Offset[1], 0, 0);
				}
				TArray<double> Scale = GetJsonExtensionObjectNumbers(JsonTextureObject->ToSharedRef(), "KHR_texture_transform", "scale");
				if (Scale.Num() >= 2)
				{
					ParamTransform.Scale = FLinearColor(Scale[0], Scale[1], 1, 1);
				}
				ParamTransform.TexCoord = GetJsonExtensionObjectIndex(JsonTextureObject->ToSharedRef(), "KHR_texture_transform", "texCoord", ParamTransform.TexCoord);

				if (ParamTransform.TexCoord < 0 || ParamTransform.TexCoord > 3)
				{
					AddError("LoadMaterial_Internal()", FString::Printf(TEXT("Invalid UV Set for %s: %d"), *ParamName, ParamTransform.TexCoord));
					return nullptr;
				}

				// hack for allowing BC5 compression for plugins
				if (bForceNormalMapCompression)
				{
					FglTFRuntimeImagesConfig& ImagesConfig = const_cast<FglTFRuntimeImagesConfig&>(MaterialsConfig.ImagesConfig);
					ImagesConfig.Compression = TextureCompressionSettings::TC_Normalmap;
				}

				ParamTextureCache = LoadTexture(TextureIndex, ParamMips, sRGB, MaterialsConfig, Sampler);

				return *JsonTextureObject;
			}
			return nullptr;
		};

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField(TEXT("pbrMetallicRoughness"), JsonPBRObject))
	{
		GetMaterialVector(JsonPBRObject->ToSharedRef(), "baseColorFactor", 4, RuntimeMaterial.bHasBaseColorFactor, RuntimeMaterial.BaseColorFactor);
		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "baseColorTexture", true, RuntimeMaterial.BaseColorTextureCache, RuntimeMaterial.BaseColorTextureMips, RuntimeMaterial.BaseColorTransform, RuntimeMaterial.BaseColorSampler, false);

		if ((*JsonPBRObject)->TryGetNumberField(TEXT("metallicFactor"), RuntimeMaterial.MetallicFactor))
		{
			RuntimeMaterial.bHasMetallicFactor = true;
		}

		if ((*JsonPBRObject)->TryGetNumberField(TEXT("roughnessFactor"), RuntimeMaterial.RoughnessFactor))
		{
			RuntimeMaterial.bHasRoughnessFactor = true;
		}

		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "metallicRoughnessTexture", false, RuntimeMaterial.MetallicRoughnessTextureCache, RuntimeMaterial.MetallicRoughnessTextureMips, RuntimeMaterial.MetallicRoughnessTransform, RuntimeMaterial.MetallicRoughnessSampler, false);
	}

	if (const TSharedPtr<FJsonObject> JsonNormalTexture = GetMaterialTexture(JsonMaterialObject, "normalTexture", false, RuntimeMaterial.NormalTextureCache, RuntimeMaterial.NormalTextureMips, RuntimeMaterial.NormalTransform, RuntimeMaterial.NormalSampler, true))
	{
		JsonNormalTexture->TryGetNumberField(TEXT("scale"), RuntimeMaterial.NormalTextureScale);
	}

	GetMaterialTexture(JsonMaterialObject, "occlusionTexture", false, RuntimeMaterial.OcclusionTextureCache, RuntimeMaterial.OcclusionTextureMips, RuntimeMaterial.OcclusionTransform, RuntimeMaterial.OcclusionSampler, false);

	GetMaterialVector(JsonMaterialObject, "emissiveFactor", 3, RuntimeMaterial.bHasEmissiveFactor, RuntimeMaterial.EmissiveFactor);

	GetMaterialTexture(JsonMaterialObject, "emissiveTexture", true, RuntimeMaterial.EmissiveTextureCache, RuntimeMaterial.EmissiveTextureMips, RuntimeMaterial.EmissiveTransform, RuntimeMaterial.EmissiveSampler, false);

	bool bDummyValue = false;

	const TSharedPtr<FJsonObject>* JsonExtensions;
	if (JsonMaterialObject->TryGetObjectField(TEXT("extensions"), JsonExtensions))
	{
		// KHR_materials_pbrSpecularGlossiness
		const TSharedPtr<FJsonObject>* JsonPbrSpecularGlossiness;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_pbrSpecularGlossiness"), JsonPbrSpecularGlossiness))
		{
			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseFactor", 4, RuntimeMaterial.bHasDiffuseFactor, RuntimeMaterial.DiffuseFactor);
			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseTexture", true, RuntimeMaterial.DiffuseTextureCache, RuntimeMaterial.DiffuseTextureMips, RuntimeMaterial.DiffuseTransform, RuntimeMaterial.DiffuseSampler, false);

			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "specularFactor", 3, RuntimeMaterial.bHasSpecularFactor, RuntimeMaterial.SpecularFactor);

			GetMaterialFactor(JsonPbrSpecularGlossiness->ToSharedRef(), "glossinessFactor", RuntimeMaterial.bHasGlossinessFactor, RuntimeMaterial.GlossinessFactor);

			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "specularGlossinessTexture", true, RuntimeMaterial.SpecularGlossinessTextureCache, RuntimeMaterial.SpecularGlossinessTextureMips, RuntimeMaterial.SpecularGlossinessTransform, RuntimeMaterial.SpecularGlossinessSampler, false);

			RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness = true;
		}

		// KHR_materials_transmission
		const TSharedPtr<FJsonObject>* JsonMaterialTransmission;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_transmission"), JsonMaterialTransmission))
		{
			if ((*JsonMaterialTransmission)->TryGetNumberField(TEXT("transmissionFactor"), RuntimeMaterial.TransmissionFactor))
			{
				RuntimeMaterial.bHasTransmissionFactor = (RuntimeMaterial.TransmissionFactor > 0.0);
			}
			GetMaterialTexture(JsonMaterialTransmission->ToSharedRef(), "transmissionTexture", false, RuntimeMaterial.TransmissionTextureCache, RuntimeMaterial.TransmissionTextureMips, RuntimeMaterial.TransmissionTransform, RuntimeMaterial.TransmissionSampler, false);

			RuntimeMaterial.bKHR_materials_transmission = (RuntimeMaterial.TransmissionFactor > 0.0);
		}

		// KHR_materials_unlit 
		const TSharedPtr<FJsonObject>* JsonMaterialUnlit;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_unlit"), JsonMaterialUnlit))
		{
			RuntimeMaterial.bKHR_materials_unlit = true;
		}

		// KHR_materials_ior
		const TSharedPtr<FJsonObject>* JsonMaterialIOR;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_ior"), JsonMaterialIOR))
		{
			if (!(*JsonMaterialIOR)->TryGetNumberField(TEXT("ior"), RuntimeMaterial.IOR))
			{
				RuntimeMaterial.IOR = 1.5;
			}
			RuntimeMaterial.bHasIOR = true;
		}

		// KHR_materials_specular
		const TSharedPtr<FJsonObject>* JsonMaterialSpecular;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_specular"), JsonMaterialSpecular))
		{
			if (!(*JsonMaterialSpecular)->TryGetNumberField(TEXT("specularFactor"), RuntimeMaterial.BaseSpecularFactor))
			{
				RuntimeMaterial.BaseSpecularFactor = 1;
			}
			GetMaterialTexture(JsonMaterialSpecular->ToSharedRef(), "specularTexture", false, RuntimeMaterial.SpecularTextureCache, RuntimeMaterial.SpecularTextureMips, RuntimeMaterial.SpecularTransform, RuntimeMaterial.SpecularSampler, false);
			RuntimeMaterial.bKHR_materials_specular = true;
		}

		// KHR_materials_emissive_strength
		const TSharedPtr<FJsonObject>* JsonMaterialEmissiveStrength;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_emissive_strength"), JsonMaterialEmissiveStrength))
		{
			if (!(*JsonMaterialEmissiveStrength)->TryGetNumberField(TEXT("emissiveStrength"), RuntimeMaterial.EmissiveStrength))
			{
				RuntimeMaterial.EmissiveStrength = 1;
			}
			RuntimeMaterial.bKHR_materials_emissive_strength = true;
		}

		// KHR_materials_clearcoat
		const TSharedPtr<FJsonObject>* JsonMaterialClearCoat;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_clearcoat"), JsonMaterialClearCoat))
		{
			if (!(*JsonMaterialClearCoat)->TryGetNumberField(TEXT("clearcoatFactor"), RuntimeMaterial.ClearCoatFactor))
			{
				RuntimeMaterial.ClearCoatFactor = 0;
			}

			if (!(*JsonMaterialClearCoat)->TryGetNumberField(TEXT("clearcoatRoughnessFactor"), RuntimeMaterial.ClearCoatRoughnessFactor))
			{
				RuntimeMaterial.ClearCoatRoughnessFactor = 0;
			}

			RuntimeMaterial.bKHR_materials_clearcoat = true;
		}

		// KHR_materials_volume
		const TSharedPtr<FJsonObject>* JsonMaterialVolume;
		if ((*JsonExtensions)->TryGetObjectField(TEXT("KHR_materials_volume"), JsonMaterialVolume))
		{
			GetMaterialFactor(JsonMaterialVolume->ToSharedRef(), "thicknessFactor", RuntimeMaterial.bHasThicknessFactor, RuntimeMaterial.ThicknessFactor);
			GetMaterialTexture(JsonMaterialVolume->ToSharedRef(), "thicknessTexture", false, RuntimeMaterial.ThicknessTextureCache, RuntimeMaterial.ThicknessTextureMips, RuntimeMaterial.ThicknessTransform, RuntimeMaterial.ThicknessSampler, false);
			GetMaterialScalar(JsonMaterialVolume->ToSharedRef(), "attenuationDistance", RuntimeMaterial.AttenuationDistance);
			GetMaterialVector(JsonMaterialVolume->ToSharedRef(), "attenuationColor", 3, bDummyValue, RuntimeMaterial.AttenuationColor);
			RuntimeMaterial.bKHR_materials_volume = true;
		}
	}

	if (IsInGameThread())
	{
		return BuildMaterial(Index, MaterialName, RuntimeMaterial, MaterialsConfig, bUseVertexColors, ForceBaseMaterial);
	}

	UMaterialInterface* Material = nullptr;

	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this, Index, MaterialName, &Material, &RuntimeMaterial, MaterialsConfig, bUseVertexColors, ForceBaseMaterial]()
		{
			// this is mainly for editor ...
			if (IsGarbageCollecting())
			{
				return;
			}
			Material = BuildMaterial(Index, MaterialName, RuntimeMaterial, MaterialsConfig, bUseVertexColors, ForceBaseMaterial);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

	return Material;
}

UTexture2D* FglTFRuntimeParser::BuildTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler)
{
	if (Mips.Num() == 0)
	{
		return nullptr;
	}

	UTexture2D* Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Public);
	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = Mips[0].Width;
	PlatformData->SizeY = Mips[0].Height;
	PlatformData->PixelFormat = Mips[0].PixelFormat;

#if ENGINE_MAJOR_VERSION > 4
	Texture->SetPlatformData(PlatformData);
#else
	Texture->PlatformData = PlatformData;
#endif

	Texture->LODBias = (ImagesConfig.LODBias >= 0 && ImagesConfig.LODBias < (Mips.Num() - 1)) ? ImagesConfig.LODBias : 0;
	Texture->NeverStream = !ImagesConfig.bStreaming;

	if (ImagesConfig.bStreaming)
	{
		Texture->AddAssetUserData(NewObject<UglTFRuntimeTextureMipDataProviderFactory>());
	}

	for (const FglTFRuntimeMipMap& MipMap : Mips)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		PlatformData->Mips.Add(Mip);
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

#if !WITH_EDITOR
		// this is a hack for allowing texture streaming without messing around with deriveddata
		Mip->BulkData.SetBulkDataFlags(BULKDATA_PayloadInSeperateFile);
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
		uint8* Data = reinterpret_cast<uint8*>(Mip->BulkData.Realloc(MipMap.Pixels.Num()));
		// ETargetPlatformFeatures::NormalmapLAEncodingMode has been added in 5.3 for mobile platforms
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3 && (PLATFORM_ANDROID || PLATFORM_IOS)
		if (ImagesConfig.Compression == TC_Normalmap)
		{
			for (int32 PIndex = 0; PIndex < MipMap.Pixels.Num(); PIndex += 4)
			{
				Data[PIndex + 0] = 0;
				Data[PIndex + 1] = 0;
				Data[PIndex + 2] = MipMap.Pixels[PIndex + 2];
				Data[PIndex + 3] = MipMap.Pixels[PIndex + 1];
			}
		}
		else
		{
#endif
			FMemory::Memcpy(Data, MipMap.Pixels.GetData(), MipMap.Pixels.Num());
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 3 && (PLATFORM_ANDROID || PLATFORM_IOS)
		}
#endif
		Mip->BulkData.Unlock();
	}


	Texture->CompressionSettings = ImagesConfig.Compression;
	Texture->LODGroup = ImagesConfig.Group;
	Texture->SRGB = ImagesConfig.bSRGB;

	if (Sampler.MinFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MinFilter;
	}

	if (Sampler.MagFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MagFilter;
	}

	Texture->AddressX = Sampler.TileX;
	Texture->AddressY = Sampler.TileY;

	Texture->UpdateResource();

	TexturesCache.Add(Mips[0].TextureIndex, Texture);

	FillAssetUserData(Mips[0].TextureIndex, Texture);

	return Texture;
}

UVolumeTexture* FglTFRuntimeParser::BuildVolumeTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const int32 TileZ, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler)
{
	if (Mips.Num() == 0)
	{
		return nullptr;
	}

	UVolumeTexture* Texture = NewObject<UVolumeTexture>(Outer, NAME_None, RF_Public);
	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = Mips[0].Width;
	PlatformData->SizeY = Mips[0].Height;
	PlatformData->PixelFormat = Mips[0].PixelFormat;
	PlatformData->SetNumSlices(TileZ);

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION > 0
	Texture->SetPlatformData(PlatformData);
#else
	Texture->PlatformData = PlatformData;
#endif

	Texture->LODBias = (ImagesConfig.LODBias >= 0 && ImagesConfig.LODBias < (Mips.Num() - 1)) ? ImagesConfig.LODBias : 0;
	Texture->NeverStream = !ImagesConfig.bStreaming;

	if (ImagesConfig.bStreaming)
	{
		Texture->AddAssetUserData(NewObject<UglTFRuntimeTextureMipDataProviderFactory>());
	}

	for (const FglTFRuntimeMipMap& MipMap : Mips)
	{
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		PlatformData->Mips.Add(Mip);
		Mip->SizeX = MipMap.Width;
		Mip->SizeY = MipMap.Height;
		Mip->SizeZ = TileZ;

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

#if !WITH_EDITOR
		// this is a hack for allowing texture streaming without messing around with deriveddata
		Mip->BulkData.SetBulkDataFlags(BULKDATA_PayloadInSeperateFile);
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


	Texture->CompressionSettings = ImagesConfig.Compression;
	Texture->LODGroup = ImagesConfig.Group;
	Texture->SRGB = ImagesConfig.bSRGB;

	if (Sampler.MinFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MinFilter;
	}

	if (Sampler.MagFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MagFilter;
	}

#if WITH_EDITOR
	Texture->Source2DTileSizeX = Mips[0].Width;
	Texture->Source2DTileSizeY = Mips[0].Height;
#endif

	Texture->UpdateResource();

	return Texture;
}


UMaterialInterface* FglTFRuntimeParser::BuildVertexColorOnlyMaterial(const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUnlit)
{
	UMaterialInterface* BaseMaterial = nullptr;

	if (bUnlit)
	{
		if (UnlitMaterialsMap.Contains(EglTFRuntimeMaterialType::Masked))
		{
			BaseMaterial = UnlitMaterialsMap[EglTFRuntimeMaterialType::Masked];
		}
	}
	else
	{
		if (MetallicRoughnessMaterialsMap.Contains(EglTFRuntimeMaterialType::TwoSided))
		{
			BaseMaterial = MetallicRoughnessMaterialsMap[EglTFRuntimeMaterialType::TwoSided];
		}
	}

	if (MaterialsConfig.VertexColorOnlyMaterial)
	{
		BaseMaterial = MaterialsConfig.VertexColorOnlyMaterial;
	}

	if (!BaseMaterial)
	{
		return nullptr;
	}

	if (IsInGameThread())
	{
		UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
		if (!Material)
		{
			AddError("BuildVertexColorOnlyMaterial()", "Unable to create material instance, falling back to default material");
			return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
		}

		Material->SetScalarParameterValue("bUseVertexColors", true);

		return Material;
	}

	UMaterialInstanceDynamic* Material = nullptr;

	FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([this, &Material, BaseMaterial]()
		{
			// this is mainly for editor ...
			if (IsGarbageCollecting())
			{
				return;
			}
			Material = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
			if (!Material)
			{
				AddError("BuildVertexColorOnlyMaterial()", "Unable to create material instance, falling back to default material");
				return;
			}

			Material->SetScalarParameterValue("bUseVertexColors", true);
		}, TStatId(), nullptr, ENamedThreads::GameThread);
	FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);

	if (!Material)
	{
		return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
	}

	return Material;


}

UMaterialInterface* FglTFRuntimeParser::BuildMaterial(const int32 Index, const FString& MaterialName, const FglTFRuntimeMaterial& RuntimeMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, UMaterialInterface* ForceBaseMaterial)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_BuildMaterial, FColor::Magenta);

	UMaterialInterface* BaseMaterial = nullptr;

	if (!ForceBaseMaterial)
	{
		if (MaterialsConfig.MetallicRoughnessOverrideMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = MaterialsConfig.MetallicRoughnessOverrideMap[RuntimeMaterial.MaterialType];
		}
		else if (MetallicRoughnessMaterialsMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = MetallicRoughnessMaterialsMap[RuntimeMaterial.MaterialType];
		}

		if (RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness)
		{
			if (MaterialsConfig.SpecularGlossinessOverrideMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = MaterialsConfig.SpecularGlossinessOverrideMap[RuntimeMaterial.MaterialType];
			}
			else if (SpecularGlossinessMaterialsMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = SpecularGlossinessMaterialsMap[RuntimeMaterial.MaterialType];
			}
		}

		if (RuntimeMaterial.bKHR_materials_unlit)
		{
			if (MaterialsConfig.UnlitOverrideMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = MaterialsConfig.UnlitOverrideMap[RuntimeMaterial.MaterialType];
			}
			else if (UnlitMaterialsMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = UnlitMaterialsMap[RuntimeMaterial.MaterialType];
			}
		}

		if (RuntimeMaterial.bKHR_materials_clearcoat)
		{
			if (MaterialsConfig.ClearCoatOverrideMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = MaterialsConfig.ClearCoatOverrideMap[RuntimeMaterial.MaterialType];
			}
			else if (ClearCoatMaterialsMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = ClearCoatMaterialsMap[RuntimeMaterial.MaterialType];
			}
		}

		// NOTE: ensure to have transmission as the last check given its incompatibility with other materials like clearcoat
		if (RuntimeMaterial.bKHR_materials_transmission)
		{
			if (MaterialsConfig.TransmissionOverrideMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = MaterialsConfig.TransmissionOverrideMap[RuntimeMaterial.MaterialType];
			}
			else if (TransmissionMaterialsMap.Contains(RuntimeMaterial.MaterialType))
			{
				BaseMaterial = TransmissionMaterialsMap[RuntimeMaterial.MaterialType];
			}
		}
	}
	else
	{
		BaseMaterial = ForceBaseMaterial;
	}

	if (MaterialsConfig.ForceMaterial)
	{
		BaseMaterial = MaterialsConfig.ForceMaterial;
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
		AddError("BuildMaterial()", "Unable to find glTFRuntime Material, ensure it has been packaged, falling back to default material");
		return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, GetTransientPackage());
	if (!Material)
	{
		AddError("BuildMaterial()", "Unable to create material instance, falling back to default material");
		return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
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

	auto ApplyMaterialTexture = [this, Material, MaterialsConfig](const FName& TextureName, UTexture2D* TextureCache, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeTextureSampler& Sampler, const FString& TransformPrefix, const FglTFRuntimeTextureTransform& Transform, const TEnumAsByte<TextureCompressionSettings> Compression, const bool sRGB)
		{
			UTexture2D* Texture = TextureCache;
			if (!Texture)
			{
				if (Mips.Num() > 0)
				{
					FglTFRuntimeImagesConfig ImagesConfig = MaterialsConfig.ImagesConfig;
					ImagesConfig.Compression = Compression;
					ImagesConfig.bSRGB = sRGB;
					Texture = BuildTexture(Material, Mips, ImagesConfig, Sampler);
				}
			}
			if (Texture)
			{
				Material->SetTextureParameterValue(TextureName, Texture);
				FVector4 UVSet = FVector4(0, 0, 0, 0);
				UVSet[Transform.TexCoord] = 1;
				Material->SetVectorParameterValue(FName(TransformPrefix + "TexCoord"), FLinearColor(UVSet));
				Material->SetVectorParameterValue(FName(TransformPrefix + "Offset"), Transform.Offset);
				Material->SetScalarParameterValue(FName(TransformPrefix + "Rotation"), Transform.Rotation);
				Material->SetVectorParameterValue(FName(TransformPrefix + "Scale"), Transform.Scale);

				if (MaterialsConfig.bAddEpicInterchangeParams)
				{
					Material->SetVectorParameterValue(FName(TransformPrefix + "Texture_TexCoord"), FLinearColor(UVSet));
					Material->SetVectorParameterValue(FName(TransformPrefix + "Texture_OffsetScale"), FLinearColor(Transform.Offset.R, Transform.Offset.G, Transform.Scale.R, Transform.Scale.G));
					Material->SetScalarParameterValue(FName(TransformPrefix + "Texture_Rotation"), Transform.Rotation);
				}
			}
		};

	ApplyMaterialFactor(RuntimeMaterial.bHasBaseColorFactor, "baseColorFactor", RuntimeMaterial.BaseColorFactor);
	ApplyMaterialTexture("baseColorTexture", RuntimeMaterial.BaseColorTextureCache, RuntimeMaterial.BaseColorTextureMips,
		RuntimeMaterial.BaseColorSampler,
		"baseColor", RuntimeMaterial.BaseColorTransform,
		TextureCompressionSettings::TC_Default, true);

	ApplyMaterialFloatFactor(RuntimeMaterial.bHasMetallicFactor, "metallicFactor", RuntimeMaterial.MetallicFactor);
	ApplyMaterialFloatFactor(RuntimeMaterial.bHasRoughnessFactor, "roughnessFactor", RuntimeMaterial.RoughnessFactor);

	ApplyMaterialTexture("metallicRoughnessTexture", RuntimeMaterial.MetallicRoughnessTextureCache, RuntimeMaterial.MetallicRoughnessTextureMips,
		RuntimeMaterial.MetallicRoughnessSampler,
		"metallicRoughness", RuntimeMaterial.MetallicRoughnessTransform,
		TextureCompressionSettings::TC_Default, false);

	ApplyMaterialTexture("normalTexture", RuntimeMaterial.NormalTextureCache, RuntimeMaterial.NormalTextureMips,
		RuntimeMaterial.NormalSampler,
		"normal", RuntimeMaterial.NormalTransform,
		TextureCompressionSettings::TC_Normalmap, false);
	ApplyMaterialFactor(true, "normalTexScale", FLinearColor(RuntimeMaterial.NormalTextureScale, RuntimeMaterial.NormalTextureScale, 1, 1));

	if (MaterialsConfig.bAddEpicInterchangeParams)
	{
		ApplyMaterialFloatFactor(true, "normalScale", RuntimeMaterial.NormalTextureScale);
	}

	ApplyMaterialTexture("occlusionTexture", RuntimeMaterial.OcclusionTextureCache, RuntimeMaterial.OcclusionTextureMips,
		RuntimeMaterial.OcclusionSampler,
		"occlusion", RuntimeMaterial.OcclusionTransform,
		TextureCompressionSettings::TC_Default, false);

	ApplyMaterialFactor(RuntimeMaterial.bHasEmissiveFactor, "emissiveFactor", RuntimeMaterial.EmissiveFactor);

	ApplyMaterialTexture("emissiveTexture", RuntimeMaterial.EmissiveTextureCache, RuntimeMaterial.EmissiveTextureMips,
		RuntimeMaterial.EmissiveSampler,
		"emissive", RuntimeMaterial.EmissiveTransform,
		TextureCompressionSettings::TC_Default, true);


	if (RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness)
	{
		ApplyMaterialFactor(RuntimeMaterial.bHasDiffuseFactor, "baseColorFactor", RuntimeMaterial.DiffuseFactor);
		ApplyMaterialTexture("baseColorTexture", RuntimeMaterial.DiffuseTextureCache, RuntimeMaterial.DiffuseTextureMips,
			RuntimeMaterial.DiffuseSampler,
			"baseColor", RuntimeMaterial.DiffuseTransform,
			TextureCompressionSettings::TC_Default, true);

		ApplyMaterialFactor(RuntimeMaterial.bHasSpecularFactor, "specularFactor", RuntimeMaterial.SpecularFactor);
		ApplyMaterialFloatFactor(RuntimeMaterial.bHasGlossinessFactor, "glossinessFactor", RuntimeMaterial.GlossinessFactor);
		ApplyMaterialTexture("specularGlossinessTexture", RuntimeMaterial.SpecularGlossinessTextureCache, RuntimeMaterial.SpecularGlossinessTextureMips,
			RuntimeMaterial.SpecularGlossinessSampler,
			"specularGlossiness", RuntimeMaterial.SpecularGlossinessTransform,
			TextureCompressionSettings::TC_Default, false);
	}

	if (RuntimeMaterial.bKHR_materials_transmission)
	{
		ApplyMaterialFloatFactor(RuntimeMaterial.bHasTransmissionFactor, "transmissionFactor", RuntimeMaterial.TransmissionFactor);
		ApplyMaterialTexture("transmissionTexture", RuntimeMaterial.TransmissionTextureCache, RuntimeMaterial.TransmissionTextureMips,
			RuntimeMaterial.TransmissionSampler,
			"transmission", RuntimeMaterial.TransmissionTransform,
			TextureCompressionSettings::TC_Default, false);
	}

	if (RuntimeMaterial.bKHR_materials_specular)
	{
		ApplyMaterialTexture("specularTexture", RuntimeMaterial.SpecularTextureCache, RuntimeMaterial.SpecularTextureMips,
			RuntimeMaterial.SpecularSampler,
			"specular", RuntimeMaterial.SpecularTransform,
			TextureCompressionSettings::TC_Default, false);
	}

	if (RuntimeMaterial.bKHR_materials_volume)
	{
		ApplyMaterialTexture("thicknessTexture", RuntimeMaterial.ThicknessTextureCache, RuntimeMaterial.ThicknessTextureMips,
			RuntimeMaterial.ThicknessSampler,
			"thickness", RuntimeMaterial.ThicknessTransform,
			TextureCompressionSettings::TC_Default, false);
		ApplyMaterialFloatFactor(RuntimeMaterial.bHasThicknessFactor, "thicknessFactor", RuntimeMaterial.ThicknessFactor);
		ApplyMaterialFloatFactor(true, "attenuationDistance", RuntimeMaterial.AttenuationDistance);
		ApplyMaterialFactor(true, "attenuationColor", RuntimeMaterial.AttenuationColor);
	}

	Material->SetScalarParameterValue("bUseVertexColors", (bUseVertexColors && !MaterialsConfig.bDisableVertexColors) ? 1.0f : 0.0f);
	Material->SetScalarParameterValue("AlphaMask", RuntimeMaterial.bMasked ? 1.0f : 0.0f);

	ApplyMaterialFloatFactor(RuntimeMaterial.bHasIOR, "ior", RuntimeMaterial.IOR);

	ApplyMaterialFloatFactor(RuntimeMaterial.bKHR_materials_clearcoat, "clearcoatFactor", RuntimeMaterial.ClearCoatFactor);
	ApplyMaterialFloatFactor(RuntimeMaterial.bKHR_materials_clearcoat, "clearcoatRoughnessFactor", RuntimeMaterial.ClearCoatRoughnessFactor);

	ApplyMaterialFloatFactor(RuntimeMaterial.bKHR_materials_emissive_strength, "emissiveStrength", RuntimeMaterial.EmissiveStrength);

	for (const TPair<FString, float>& Pair : MaterialsConfig.ScalarParamsOverrides)
	{
		float ScalarValue = 0;
		if (Material->GetScalarParameterValue(*Pair.Key, ScalarValue))
		{
			Material->SetScalarParameterValue(*Pair.Key, Pair.Value);
		}
	}

	for (const TPair<FString, float>& Pair : MaterialsConfig.ParamsMultiplier)
	{
		float ScalarValue = 0;
		FLinearColor VectorValue = FLinearColor::Black;
		if (Material->GetScalarParameterValue(*Pair.Key, ScalarValue))
		{
			Material->SetScalarParameterValue(*Pair.Key, ScalarValue * Pair.Value);
		}
		else if (Material->GetVectorParameterValue(*Pair.Key, VectorValue))
		{
			Material->SetVectorParameterValue(*Pair.Key, VectorValue * Pair.Value);
		}
	}

	for (const TPair<FString, float>& Pair : MaterialsConfig.CustomScalarParams)
	{
		Material->SetScalarParameterValue(*Pair.Key, Pair.Value);
	}

	for (const TPair<FString, FLinearColor>& Pair : MaterialsConfig.CustomVectorParams)
	{
		Material->SetVectorParameterValue(*Pair.Key, Pair.Value);
	}

	for (const TPair<FString, UTexture*>& Pair : MaterialsConfig.CustomTextureParams)
	{
		Material->SetTextureParameterValue(*Pair.Key, Pair.Value);
	}

	return Material;
}

bool FglTFRuntimeParser::LoadImageFromBlob(const TArray64<uint8>& Blob, TSharedRef<FJsonObject> JsonImageObject, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, EPixelFormat& PixelFormat, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	OnTexturePixels.Broadcast(AsShared(), JsonImageObject, Blob, Width, Height, PixelFormat, UncompressedBytes, ImagesConfig);

	if (UncompressedBytes.Num() == 0)
	{

		PixelFormat = EPixelFormat::PF_B8G8R8A8;

		// check for DDS first
		if (FglTFRuntimeDDS::IsDDS(Blob))
		{
			FglTFRuntimeDDS DDS(Blob);
			TArray<FglTFRuntimeMipMap> DDSMips;
			DDS.LoadMips(-1, DDSMips, 1, ImagesConfig);
			if (DDSMips.Num() > 0)
			{
				UncompressedBytes = DDSMips[0].Pixels;
				PixelFormat = DDSMips[0].PixelFormat;
				Width = DDSMips[0].Width;
				Height = DDSMips[0].Height;
			}
		}

		if (UncompressedBytes.Num() == 0)
		{
			IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

			EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Blob.GetData(), Blob.Num());
			if (ImageFormat == EImageFormat::Invalid)
			{
				AddError("LoadImageFromBlob()", "Unable to detect image format");
				return false;
			}

			ERGBFormat RGBFormat = ERGBFormat::BGRA;
			int32 BitDepth = 8;

#if ENGINE_MAJOR_VERSION >= 5
			if (ImageFormat == EImageFormat::EXR)
			{
				RGBFormat = ERGBFormat::RGBAF;
				BitDepth = 16;
				PixelFormat = EPixelFormat::PF_FloatRGBA;
			}
#endif

			TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
			if (!ImageWrapper.IsValid())
			{
				AddError("LoadImageFromBlob()", "Unable to create ImageWrapper");
				return false;
			}
			if (!ImageWrapper->SetCompressed(Blob.GetData(), Blob.Num()))
			{
				AddError("LoadImageFromBlob()", "Unable to parse image data");
				return false;
			}

#if ENGINE_MAJOR_VERSION >= 5
			if (!ImageWrapper->GetRaw(ImagesConfig.bForceHDR ? ERGBFormat::RGBAF : RGBFormat, ImagesConfig.bForceHDR ? 16 : BitDepth, UncompressedBytes))
#else
			if (!ImageWrapper->GetRaw(RGBFormat, ImagesConfig.bForceHDR ? 16 : BitDepth, UncompressedBytes))
#endif
			{
				AddError("LoadImageFromBlob()", "Unable to get raw image data");
				return false;
			}

			if (ImagesConfig.bForceHDR)
			{
				PixelFormat = EPixelFormat::PF_FloatRGBA;
			}

			Width = ImageWrapper->GetWidth();
			Height = ImageWrapper->GetHeight();
			}
		}

	if (ImagesConfig.bVerticalFlip && GPixelFormats[PixelFormat].BlockSizeX == 1 && GPixelFormats[PixelFormat].BlockSizeY == 1)
	{
		TArray<uint8> Flipped;
		const int64 Pitch = Width * GPixelFormats[PixelFormat].BlockBytes;
		Flipped.AddUninitialized(Pitch * Height);
		for (int32 ImageY = 0; ImageY < Height; ImageY++)
		{
			FMemory::Memcpy(Flipped.GetData() + (Pitch * (Height - 1 - ImageY)), UncompressedBytes.GetData() + Pitch * ImageY, Pitch);
		}
		UncompressedBytes = Flipped;
	}

	return true;
	}

bool FglTFRuntimeParser::LoadImageBytes(const int32 ImageIndex, TSharedPtr<FJsonObject>& JsonImageObject, TArray64<uint8>& Bytes)
{

	JsonImageObject = GetJsonObjectFromRootIndex("images", ImageIndex);
	if (!JsonImageObject)
	{
		AddError("LoadImageBytes()", FString::Printf(TEXT("Unable to load image %d"), ImageIndex));
		return false;
	}


	if (!GetJsonObjectBytes(JsonImageObject.ToSharedRef(), Bytes))
	{
		AddError("LoadImageBytes()", FString::Printf(TEXT("Unable to load image %d"), ImageIndex));
		return false;
	}

	return true;
}

bool FglTFRuntimeParser::LoadImage(const int32 ImageIndex, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, EPixelFormat& PixelFormat, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	TSharedPtr<FJsonObject> JsonImageObject;
	TArray64<uint8> Bytes;
	if (!LoadImageBytes(ImageIndex, JsonImageObject, Bytes))
	{
		return false;
	}

	return LoadImageFromBlob(Bytes, JsonImageObject.ToSharedRef(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig);
}

UTexture2D* FglTFRuntimeParser::LoadTexture(const int32 TextureIndex, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig, FglTFRuntimeTextureSampler& Sampler)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_LoadTexture, FColor::Magenta);

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
	if (!Root->TryGetArrayField(TEXT("textures"), JsonTextures))
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

	int64 ImageIndex = INDEX_NONE;
	OnTextureImageIndex.Broadcast(AsShared(), JsonTextureObject.ToSharedRef(), ImageIndex);

	if (ImageIndex <= INDEX_NONE && !JsonTextureObject->TryGetNumberField(TEXT("source"), ImageIndex))
	{
		return nullptr;
	}

	if (MaterialsConfig.ImagesOverrideMap.Contains(ImageIndex))
	{
		return MaterialsConfig.ImagesOverrideMap[ImageIndex];
	}

	TSharedPtr<FJsonObject> JsonImageObject;
	TArray64<uint8> CompressedBytes;
	if (!LoadImageBytes(ImageIndex, JsonImageObject, CompressedBytes))
	{
		return nullptr;
	}

	if (!LoadBlobToMips(TextureIndex, JsonTextureObject.ToSharedRef(), JsonImageObject.ToSharedRef(), CompressedBytes, Mips, sRGB, MaterialsConfig))
	{
		return nullptr;
	}

	int64 SamplerIndex;
	if (JsonTextureObject->TryGetNumberField(TEXT("sampler"), SamplerIndex))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonSamplers;
		// no samplers ?
		if (!Root->TryGetArrayField(TEXT("samplers"), JsonSamplers))
		{
			UE_LOG(LogGLTFRuntime, Warning, TEXT("No texture sampler defined!"));
		}
		else
		{
			if (SamplerIndex >= JsonSamplers->Num())
			{
				UE_LOG(LogGLTFRuntime, Warning, TEXT("Invalid texture sampler index: %lld"), SamplerIndex);
			}
			else
			{
				TSharedPtr<FJsonObject> JsonSamplerObject = (*JsonSamplers)[SamplerIndex]->AsObject();
				if (JsonSamplerObject)
				{
					int64 MinFilter;
					if (JsonSamplerObject->TryGetNumberField(TEXT("minFilter"), MinFilter))
					{
						if (MinFilter == 9728)
						{
							Sampler.MinFilter = TextureFilter::TF_Nearest;
						}
					}
					int64 MagFilter;
					if (JsonSamplerObject->TryGetNumberField(TEXT("magFilter"), MagFilter))
					{
						if (MagFilter == 9728)
						{
							Sampler.MagFilter = TextureFilter::TF_Nearest;
						}
					}
					int64 WrapS;
					if (JsonSamplerObject->TryGetNumberField(TEXT("wrapS"), WrapS))
					{
						if (WrapS == 33071)
						{
							Sampler.TileX = TextureAddress::TA_Clamp;
						}
						else if (WrapS == 33648)
						{
							Sampler.TileX = TextureAddress::TA_Mirror;
						}
					}
					int64 WrapT;
					if (JsonSamplerObject->TryGetNumberField(TEXT("wrapT"), WrapT))
					{
						if (WrapT == 33071)
						{
							Sampler.TileY = TextureAddress::TA_Clamp;
						}
						else if (WrapT == 33648)
						{
							Sampler.TileY = TextureAddress::TA_Mirror;
						}
					}
				}
			}
		}
	}

	return nullptr;
}

bool FglTFRuntimeParser::LoadBlobToMips(const int32 TextureIndex, TSharedRef<FJsonObject> JsonTextureObject, TSharedRef<FJsonObject> JsonImageObject, const TArray64<uint8>& Blob, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (MaterialsConfig.bLoadMipMaps)
	{
		OnTextureMips.Broadcast(AsShared(), TextureIndex, JsonTextureObject, JsonImageObject, Blob, Mips, MaterialsConfig.ImagesConfig);
		// if no Mips have been loaded, attempt parsing a DDS asset
		if (Mips.Num() == 0)
		{
			if (FglTFRuntimeDDS::IsDDS(Blob))
			{
				FglTFRuntimeDDS DDS(Blob);
				DDS.LoadMips(TextureIndex, Mips, 0, MaterialsConfig.ImagesConfig);
			}
		}
	}

	// if no Mips have been generated, load it as a plain image and (eventually) generate them
	if (Mips.Num() == 0)
	{
		TArray64<uint8> UncompressedBytes;
		int32 Width = 0;
		int32 Height = 0;
		EPixelFormat PixelFormat;
		if (!LoadImageFromBlob(Blob, JsonImageObject, UncompressedBytes, Width, Height, PixelFormat, MaterialsConfig.ImagesConfig))
		{
			return false;
		}

		OnLoadedTexturePixels.Broadcast(AsShared(), JsonTextureObject, Width, Height, reinterpret_cast<FColor*>(UncompressedBytes.GetData()));

		if (Width > 0 && Height > 0 &&
			(Width % GPixelFormats[PixelFormat].BlockSizeX) == 0 &&
			(Height % GPixelFormats[PixelFormat].BlockSizeY) == 0)
		{

			// limit image size (currently only PF_B8G8R8A8 is supported)
			if (PixelFormat == EPixelFormat::PF_B8G8R8A8 && (MaterialsConfig.ImagesConfig.MaxWidth > 0 || MaterialsConfig.ImagesConfig.MaxHeight > 0) && GPixelFormats[PixelFormat].BlockSizeX == 1 && GPixelFormats[PixelFormat].BlockSizeY == 1)
			{
				const int32 NewWidth = MaterialsConfig.ImagesConfig.MaxWidth > 0 ? MaterialsConfig.ImagesConfig.MaxWidth : Width;
				const int32 NewHeight = MaterialsConfig.ImagesConfig.MaxHeight > 0 ? MaterialsConfig.ImagesConfig.MaxHeight : Height;
				TArray64<FColor> ResizedPixels;
				ResizedPixels.AddUninitialized(NewWidth * NewHeight);
#if ENGINE_MAJOR_VERSION >= 5
				FImageUtils::ImageResize(Width, Height, TArrayView<FColor>(reinterpret_cast<FColor*>(UncompressedBytes.GetData()), UncompressedBytes.Num()), NewWidth, NewHeight, ResizedPixels, sRGB, false);
#else
				FImageUtils::ImageResize(Width, Height, TArrayView<FColor>(reinterpret_cast<FColor*>(UncompressedBytes.GetData()), UncompressedBytes.Num()), NewWidth, NewHeight, ResizedPixels, sRGB);
#endif
				Width = NewWidth;
				Height = NewHeight;
				UncompressedBytes.Empty(ResizedPixels.Num() * 4);
				UncompressedBytes.Append(reinterpret_cast<uint8*>(ResizedPixels.GetData()), ResizedPixels.Num() * 4);
			}

			int32 NumOfMips = 1;

			TArray64<FColor> UncompressedColors;

			if (MaterialsConfig.bGeneratesMipMaps && GPixelFormats[PixelFormat].BlockSizeX == 1 && GPixelFormats[PixelFormat].BlockSizeY == 1 && FMath::IsPowerOfTwo(Width) && FMath::IsPowerOfTwo(Height))
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
				MipMap.PixelFormat = PixelFormat;

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
	}

	OnTextureFilterMips.Broadcast(AsShared(), Mips, MaterialsConfig.ImagesConfig);

	return true;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors, FString& MaterialName, UMaterialInterface* ForceBaseMaterial)
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
	if (!Root->TryGetArrayField(TEXT("materials"), JsonMaterials))
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


	if (!JsonMaterialObject->TryGetStringField(TEXT("name"), MaterialName))
	{
		MaterialName = "";
	}

	if (!MaterialsConfig.bMaterialsOverrideMapInjectParams && MaterialsConfig.MaterialsOverrideByNameMap.Contains(MaterialName))
	{
		return MaterialsConfig.MaterialsOverrideByNameMap[MaterialName];
	}

	UMaterialInterface* Material = LoadMaterial_Internal(Index, MaterialName, JsonMaterialObject.ToSharedRef(), MaterialsConfig, bUseVertexColors, ForceBaseMaterial);
	if (!Material)
	{
		AddError("LoadMaterial()", "Unable to load material");
		return nullptr;
	}

	if (CanWriteToCache(MaterialsConfig.CacheMode))
	{
		MaterialsNameCache.Add(Material, MaterialName);
		MaterialsCache.Add(Index, Material);
	}

	FillAssetUserData(Index, Material);

	return Material;
}

UTextureCube* FglTFRuntimeParser::BuildTextureCube(UObject* Outer, const TArray<FglTFRuntimeMipMap>& MipsXP, const TArray<FglTFRuntimeMipMap>& MipsXN, const TArray<FglTFRuntimeMipMap>& MipsYP, const TArray<FglTFRuntimeMipMap>& MipsYN, const TArray<FglTFRuntimeMipMap>& MipsZP, const TArray<FglTFRuntimeMipMap>& MipsZN, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler)
{
	UTextureCube* Texture = NewObject<UTextureCube>(Outer, NAME_None, RF_Public);
	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = MipsXP[0].Width;
	PlatformData->SizeY = MipsXP[0].Height;
	PlatformData->PixelFormat = MipsXP[0].PixelFormat;
	PlatformData->SetIsCubemap(true);
	PlatformData->SetNumSlices(6);

#if ENGINE_MAJOR_VERSION > 4
	Texture->SetPlatformData(PlatformData);
#else
	Texture->PlatformData = PlatformData;
#endif

	Texture->NeverStream = true;


	for (int32 MipIndex = 0; MipIndex < MipsXP.Num(); MipIndex++)
	{
		const FglTFRuntimeMipMap& MipMap = MipsXP[MipIndex];
		FTexture2DMipMap* Mip = new FTexture2DMipMap();
		PlatformData->Mips.Add(Mip);
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
		void* Data = Mip->BulkData.Realloc(MipMap.Pixels.Num() * 6);
		if (bAutoRotate)
		{
			const int32 BlockBytes = GPixelFormats[PlatformData->PixelFormat].BlockBytes;
			const int32 Pitch = MipMap.Width * BlockBytes;
			for (int32 Row = 0; Row < MipMap.Height; Row++)
			{
				for (int32 Column = 0; Column < MipMap.Width; Column++)
				{
					int32 SourceOffset = Row * Pitch + (Column * BlockBytes);
					// X+
					int32 DestinationOffset = (MipMap.Height - 1 - Column) * Pitch + (Row * BlockBytes);
					for (int32 ByteOffset = 0; ByteOffset < BlockBytes; ByteOffset++)
					{
						reinterpret_cast<uint8*>(Data)[(MipMap.Pixels.Num() * 0) + DestinationOffset + ByteOffset] = MipsXP[MipIndex].Pixels.GetData()[SourceOffset + ByteOffset];
					}
					// X-
					DestinationOffset = Column * Pitch + ((MipMap.Width - 1 - Row) * BlockBytes);
					for (int32 ByteOffset = 0; ByteOffset < BlockBytes; ByteOffset++)
					{
						reinterpret_cast<uint8*>(Data)[(MipMap.Pixels.Num() * 1) + DestinationOffset + ByteOffset] = MipsXN[MipIndex].Pixels.GetData()[SourceOffset + ByteOffset];
					}
					// Y+
					DestinationOffset = (MipMap.Height - 1 - Row) * Pitch + ((MipMap.Width - 1 - Column) * BlockBytes);
					for (int32 ByteOffset = 0; ByteOffset < BlockBytes; ByteOffset++)
					{
						reinterpret_cast<uint8*>(Data)[(MipMap.Pixels.Num() * 2) + DestinationOffset + ByteOffset] = MipsZN[MipIndex].Pixels.GetData()[SourceOffset + ByteOffset];
					}
					// Z-
					DestinationOffset = (MipMap.Height - 1 - Row) * Pitch + ((MipMap.Width - 1 - Column) * BlockBytes);
					for (int32 ByteOffset = 0; ByteOffset < BlockBytes; ByteOffset++)
					{
						reinterpret_cast<uint8*>(Data)[(MipMap.Pixels.Num() * 5) + DestinationOffset + ByteOffset] = MipsYN[MipIndex].Pixels.GetData()[SourceOffset + ByteOffset];
					}
				}
			}
		}
		else
		{
			FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 0), MipsXP[MipIndex].Pixels.GetData(), MipsXP[MipIndex].Pixels.Num());
			FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 1), MipsXN[MipIndex].Pixels.GetData(), MipsXN[MipIndex].Pixels.Num());
			FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 2), MipsZN[MipIndex].Pixels.GetData(), MipsZN[MipIndex].Pixels.Num());
			FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 5), MipsYN[MipIndex].Pixels.GetData(), MipsYN[MipIndex].Pixels.Num());
		}

		FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 3), MipsZP[MipIndex].Pixels.GetData(), MipsXN[MipIndex].Pixels.Num());
		FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * 4), MipsYP[MipIndex].Pixels.GetData(), MipsYP[MipIndex].Pixels.Num());


		Mip->BulkData.Unlock();
	}


	Texture->CompressionSettings = ImagesConfig.Compression;
	Texture->LODGroup = ImagesConfig.Group;
	Texture->SRGB = ImagesConfig.bSRGB;

	if (Sampler.MinFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MinFilter;
	}

	if (Sampler.MagFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MagFilter;
	}

	Texture->UpdateResource();

	return Texture;
}

UTexture2DArray* FglTFRuntimeParser::BuildTextureArray(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler)
{
	if (Mips.Num() == 0)
	{
		return nullptr;
	}

	UTexture2DArray* Texture = NewObject<UTexture2DArray>(Outer, NAME_None, RF_Public);
	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = Mips[0].Width;
	PlatformData->SizeY = Mips[0].Height;
	PlatformData->PixelFormat = Mips[0].PixelFormat;
	PlatformData->SetNumSlices(Mips.Num());

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
	Texture->SetPlatformData(PlatformData);
#else
	Texture->PlatformData = PlatformData;
#endif

	Texture->NeverStream = true;

	const FglTFRuntimeMipMap& MipMap = Mips[0];
	FTexture2DMipMap* Mip = new FTexture2DMipMap();
	PlatformData->Mips.Add(Mip);
	Mip->SizeX = MipMap.Width;
	Mip->SizeY = MipMap.Height;
	Mip->SizeZ = Mips.Num();

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
	void* Data = Mip->BulkData.Realloc(MipMap.Pixels.Num() * Mips.Num());
	for (int32 MipIndex = 0; MipIndex < Mips.Num(); MipIndex++)
	{
		FMemory::Memcpy(reinterpret_cast<uint8*>(Data) + (MipMap.Pixels.Num() * MipIndex), Mips[MipIndex].Pixels.GetData(), Mips[MipIndex].Pixels.Num());
	}

	Mip->BulkData.Unlock();

	Texture->CompressionSettings = ImagesConfig.Compression;
	Texture->LODGroup = ImagesConfig.Group;
	Texture->SRGB = ImagesConfig.bSRGB;

	if (Sampler.MinFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MinFilter;
	}

	if (Sampler.MagFilter != TextureFilter::TF_Default)
	{
		Texture->Filter = Sampler.MagFilter;
	}

#if ENGINE_MAJOR_VERSION >= 5 || ENGINE_MINOR_VERSION > 25 || WITH_EDITOR
	Texture->AddressX = Sampler.TileX;
	Texture->AddressY = Sampler.TileY;
	Texture->AddressZ = Sampler.TileZ;
#endif
	Texture->UpdateResource();

	return Texture;
}

FglTFRuntimeDDS::FglTFRuntimeDDS(const TArray64<uint8>& InData) : Data(InData)
{

}

bool FglTFRuntimeDDS::IsDDS(const TArray64<uint8>& Data)
{
	// Magic + DDS_HEADER
	const uint32* Ptr32 = reinterpret_cast<const uint32*>(Data.GetData());
	return Data.Num() > (4 + 124) && Data[0] == 'D' && Data[1] == 'D' && Data[2] == 'S' && Data[3] == ' ' && Ptr32[1] == 124 && Ptr32[19] == 32;
}

void FglTFRuntimeDDS::LoadMips(const int32 TextureIndex, TArray<FglTFRuntimeMipMap>& Mips, const int32 MaxMip, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	constexpr uint32 DDSD_MIPMAPCOUNT = 0x20000;
	constexpr uint32 DDPF_FOURCC = 0x4;
	constexpr uint32 DDPF_ALPHAPIXELS = 0x1;
	constexpr uint32 DDSCAPS2_CUBEMAP = 0x200;

	const uint32* Ptr32 = reinterpret_cast<const uint32*>(Data.GetData());
	const uint32 Width = Ptr32[4];
	const uint32 Height = Ptr32[3];
	const uint32 Flags = Ptr32[2];

	int32 NumberOfMips = 1;
	if (Flags & DDSD_MIPMAPCOUNT)
	{
		NumberOfMips = Ptr32[7];
	}

	// nothing to load
	if (NumberOfMips <= 0 || Width == 0 || Height == 0)
	{
		return;
	}

	if (MaxMip > 0)
	{
		NumberOfMips = FMath::Min(NumberOfMips, MaxMip);
	}

	EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
	int64 PixelsOffset = 128;
	int32 NumberOfSlices = 1;

	// cubemap ?
	if (Ptr32[28] & DDSCAPS2_CUBEMAP)
	{
		NumberOfSlices = 6;
	}

	// compressed?
	if (Ptr32[20] & DDPF_FOURCC)
	{
		// DXT1
		if (Ptr32[21] == 0x31545844)
		{
			PixelFormat = EPixelFormat::PF_DXT1;
		}
		else if (Ptr32[21] == 0x33545844)
		{
			PixelFormat = EPixelFormat::PF_DXT3;
		}
		else if (Ptr32[21] == 0x35545844)
		{
			PixelFormat = EPixelFormat::PF_DXT5;
		}
		else if (Ptr32[21] == 111)
		{
			PixelFormat = EPixelFormat::PF_R16F;
		}
		else if (Ptr32[21] == 112)
		{
			PixelFormat = EPixelFormat::PF_G16R16F;
		}
		else if (Ptr32[21] == 113)
		{
			PixelFormat = EPixelFormat::PF_FloatRGBA;
		}
		else if (Ptr32[21] == 0x30315844)
		{
			if (Data.Num() <= (4 + 124 + 20))
			{
				return;
			}
			PixelsOffset += 20;
			constexpr uint32 DXGIFormatBC7 = 98;
			constexpr uint32 DXGIFormatBGRA8 = 87;
			constexpr uint32 DXGIFormatBC5 = 83;
			// DDS_HEADER_DXT10
			uint32 DXGIFormat = Ptr32[32];
			if (DXGIFormat == DXGIFormatBC7)
			{
				PixelFormat = EPixelFormat::PF_BC7;
			}
			else if (DXGIFormat == DXGIFormatBGRA8)
			{
				PixelFormat = EPixelFormat::PF_B8G8R8A8;
			}
			else if (DXGIFormat == DXGIFormatBC5)
			{
				PixelFormat = EPixelFormat::PF_BC5;
			}
			else
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Unknown DDS DXGI PixelFormat: %u"), DXGIFormat);
				return;
			}

			if (Ptr32[35] > 0)
			{
				// we multiply as we may have a cube here
				NumberOfSlices *= Ptr32[35];
			}
		}
		else
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unknown DDS FourCC PixelFormat: %u"), Ptr32[21]);
			return;
		}
	}
	else
	{
		if (!(Ptr32[20] & DDPF_ALPHAPIXELS))
		{
			UE_LOG(LogGLTFRuntime, Warning, TEXT("DDS Uncompressed PixelFormat without Alpha is not supported"));
			return;
		}
	}

	int32 MipWidth = Width;
	int32 MipHeight = Height;

	for (int32 MipIndex = 0; MipIndex < NumberOfMips; MipIndex++)
	{
		const int64 BlockX = GPixelFormats[PixelFormat].BlockSizeX;
		const int64 BlockY = GPixelFormats[PixelFormat].BlockSizeY;
		const int64 MipWidthAligned = FMath::Max(((MipWidth / BlockX) + ((MipWidth % BlockX) != 0 ? 1 : 0)) * BlockX, BlockX);
		const int64 MipHeightAligned = FMath::Max(((MipHeight / BlockY) + ((MipHeight % BlockY) != 0 ? 1 : 0)) * BlockY, BlockY);
		const int64 MipSize = ((MipWidthAligned * GPixelFormats[PixelFormat].BlockBytes * MipHeightAligned) / (BlockX * BlockY)) * NumberOfSlices;
		if (PixelsOffset + MipSize > Data.Num())
		{
			return;
		}
		FglTFRuntimeMipMap MipMap(TextureIndex, PixelFormat, MipWidth, MipHeight);
		MipMap.Pixels.AddUninitialized(MipSize);
		FMemory::Memcpy(MipMap.Pixels.GetData(), Data.GetData() + PixelsOffset, MipSize);

		Mips.Add(MoveTemp(MipMap));
		PixelsOffset += MipSize;
		MipWidth = FMath::Max(MipWidth / 2, 1);
		MipHeight = FMath::Max(MipHeight / 2, 1);
	}
}

int32 FglTFRuntimeTextureMipDataProvider::GetMips(const FTextureUpdateContext& Context, int32 StartingMipIndex, const FTextureMipInfoArray& MipInfos, const FTextureUpdateSyncOptions& SyncOptions)
{
#if ENGINE_MAJOR_VERSION == 4 && ENGINE_MINOR_VERSION < 26
	const int32 CurrentFirstLODIdx = Context.CurrentFirstMipIndex;
#endif
	for (int32 MipIndex = StartingMipIndex; MipIndex < CurrentFirstLODIdx; MipIndex++)
	{
#if ENGINE_MAJOR_VERSION >= 5 || ENGINE_MINOR_VERSION >= 27
		const FTexture2DMipMap& MipMap = *Context.MipsView[MipIndex];
#else
		// pretty brutal (we are always assuming UTexture2D), but should be safe
		const FTexture2DMipMap& MipMap = Cast<UTexture2D>(Context.Texture)->PlatformData->Mips[MipIndex];
#endif
		FByteBulkData* ByteBulkData = const_cast<FByteBulkData*>(&MipMap.BulkData);

		const FTextureMipInfo& MipInfo = MipInfos[MipIndex];
		void* Dest = MipInfo.DestData;

		if (ByteBulkData->GetBulkDataSize() > 0)
		{
			ByteBulkData->GetCopy(&Dest, false);
		}
}

	AdvanceTo(ETickState::CleanUp, ETickThread::Async);
	return CurrentFirstLODIdx;
	}

bool FglTFRuntimeParser::LoadBlobToMips(const TArray64<uint8>& Blob, TArray<FglTFRuntimeMipMap>& Mips, const bool sRGB, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	return LoadBlobToMips(-1, MakeShared<FJsonObject>(), MakeShared<FJsonObject>(), Blob, Mips, sRGB, MaterialsConfig);
}
