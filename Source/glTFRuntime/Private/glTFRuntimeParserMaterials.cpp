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


UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(const int32 Index, const FString& MaterialName, TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_LoadMaterial_Internal, FColor::Magenta);
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
		RuntimeMaterial.bMasked = true;
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

	auto GetMaterialTexture = [this, MaterialsConfig](const TSharedRef<FJsonObject> JsonMaterialObject, const FString& ParamName, const bool sRGB, UTexture2D*& ParamTextureCache, TArray<FglTFRuntimeMipMap>& ParamMips, FglTFRuntimeTextureTransform& ParamTransform, FglTFRuntimeTextureSampler& Sampler) -> const TSharedPtr<FJsonObject>
	{
		const TSharedPtr<FJsonObject>* JsonTextureObject;
		if (JsonMaterialObject->TryGetObjectField(ParamName, JsonTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonTextureObject)->TryGetNumberField("index", TextureIndex))
			{
				return nullptr;
			}

			if (!(*JsonTextureObject)->TryGetNumberField("texCoord", ParamTransform.TexCoord))
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

			ParamTextureCache = LoadTexture(TextureIndex, ParamMips, sRGB, MaterialsConfig, Sampler);
			return *JsonTextureObject;
		}
		return nullptr;
	};

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField("pbrMetallicRoughness", JsonPBRObject))
	{
		GetMaterialVector(JsonPBRObject->ToSharedRef(), "baseColorFactor", 4, RuntimeMaterial.bHasBaseColorFactor, RuntimeMaterial.BaseColorFactor);
		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "baseColorTexture", true, RuntimeMaterial.BaseColorTextureCache, RuntimeMaterial.BaseColorTextureMips, RuntimeMaterial.BaseColorTransform, RuntimeMaterial.BaseColorSampler);

		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", RuntimeMaterial.MetallicFactor))
		{
			RuntimeMaterial.bHasMetallicFactor = true;
		}

		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", RuntimeMaterial.RoughnessFactor))
		{
			RuntimeMaterial.bHasRoughnessFactor = true;
		}

		GetMaterialTexture(JsonPBRObject->ToSharedRef(), "metallicRoughnessTexture", false, RuntimeMaterial.MetallicRoughnessTextureCache, RuntimeMaterial.MetallicRoughnessTextureMips, RuntimeMaterial.MetallicRoughnessTransform, RuntimeMaterial.MetallicRoughnessSampler);
	}

	if (const TSharedPtr<FJsonObject> JsonNormalTexture = GetMaterialTexture(JsonMaterialObject, "normalTexture", false, RuntimeMaterial.NormalTextureCache, RuntimeMaterial.NormalTextureMips, RuntimeMaterial.NormalTransform, RuntimeMaterial.NormalSampler))
	{
		JsonNormalTexture->TryGetNumberField("scale", RuntimeMaterial.NormalTextureScale);
	}

	GetMaterialTexture(JsonMaterialObject, "occlusionTexture", false, RuntimeMaterial.OcclusionTextureCache, RuntimeMaterial.OcclusionTextureMips, RuntimeMaterial.OcclusionTransform, RuntimeMaterial.OcclusionSampler);

	GetMaterialVector(JsonMaterialObject, "emissiveFactor", 3, RuntimeMaterial.bHasEmissiveFactor, RuntimeMaterial.EmissiveFactor);

	GetMaterialTexture(JsonMaterialObject, "emissiveTexture", true, RuntimeMaterial.EmissiveTextureCache, RuntimeMaterial.EmissiveTextureMips, RuntimeMaterial.EmissiveTransform, RuntimeMaterial.EmissiveSampler);

	const TSharedPtr<FJsonObject>* JsonExtensions;
	if (JsonMaterialObject->TryGetObjectField("extensions", JsonExtensions))
	{
		// KHR_materials_pbrSpecularGlossiness
		const TSharedPtr<FJsonObject>* JsonPbrSpecularGlossiness;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_pbrSpecularGlossiness", JsonPbrSpecularGlossiness))
		{
			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseFactor", 4, RuntimeMaterial.bHasDiffuseFactor, RuntimeMaterial.DiffuseFactor);
			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "diffuseTexture", true, RuntimeMaterial.DiffuseTextureCache, RuntimeMaterial.DiffuseTextureMips, RuntimeMaterial.DiffuseTransform, RuntimeMaterial.DiffuseSampler);

			GetMaterialVector(JsonPbrSpecularGlossiness->ToSharedRef(), "specularFactor", 3, RuntimeMaterial.bHasSpecularFactor, RuntimeMaterial.SpecularFactor);

			if ((*JsonPbrSpecularGlossiness)->TryGetNumberField("glossinessFactor", RuntimeMaterial.GlossinessFactor))
			{
				RuntimeMaterial.bHasGlossinessFactor = true;
			}

			GetMaterialTexture(JsonPbrSpecularGlossiness->ToSharedRef(), "specularGlossinessTexture", true, RuntimeMaterial.SpecularGlossinessTextureCache, RuntimeMaterial.SpecularGlossinessTextureMips, RuntimeMaterial.SpecularGlossinessTransform, RuntimeMaterial.SpecularGlossinessSampler);

			RuntimeMaterial.bKHR_materials_pbrSpecularGlossiness = true;
		}

		// KHR_materials_transmission
		const TSharedPtr<FJsonObject>* JsonMaterialTransmission;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_transmission", JsonMaterialTransmission))
		{
			if ((*JsonMaterialTransmission)->TryGetNumberField("transmissionFactor", RuntimeMaterial.TransmissionFactor))
			{
				RuntimeMaterial.bHasTransmissionFactor = true;
			}
			GetMaterialTexture(JsonMaterialTransmission->ToSharedRef(), "transmissionTexture", false, RuntimeMaterial.TransmissionTextureCache, RuntimeMaterial.TransmissionTextureMips, RuntimeMaterial.TransmissionTransform, RuntimeMaterial.TransmissionSampler);

			RuntimeMaterial.bKHR_materials_transmission = true;
		}

		// KHR_materials_unlit 
		const TSharedPtr<FJsonObject>* JsonMaterialUnlit;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_unlit", JsonMaterialUnlit))
		{
			RuntimeMaterial.bKHR_materials_unlit = true;
		}

		// KHR_materials_ior
		const TSharedPtr<FJsonObject>* JsonMaterialIOR;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_ior", JsonMaterialIOR))
		{
			if (!(*JsonMaterialIOR)->TryGetNumberField("ior", RuntimeMaterial.IOR))
			{
				RuntimeMaterial.IOR = 1.5;
			}
			RuntimeMaterial.bHasIOR = true;
		}

		// KHR_materials_specular
		const TSharedPtr<FJsonObject>* JsonMaterialSpecular;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_specular", JsonMaterialSpecular))
		{
			if (!(*JsonMaterialSpecular)->TryGetNumberField("specularFactor", RuntimeMaterial.BaseSpecularFactor))
			{
				RuntimeMaterial.BaseSpecularFactor = 1;
			}
		}

		// KHR_materials_clearcoat
		const TSharedPtr<FJsonObject>* JsonMaterialClearCoat;
		if ((*JsonExtensions)->TryGetObjectField("KHR_materials_clearcoat", JsonMaterialClearCoat))
		{
			if (!(*JsonMaterialClearCoat)->TryGetNumberField("clearcoatFactor", RuntimeMaterial.ClearCoatFactor))
			{
				RuntimeMaterial.ClearCoatFactor = 0;
			}

			if (!(*JsonMaterialClearCoat)->TryGetNumberField("clearcoatRoughnessFactor", RuntimeMaterial.ClearCoatRoughnessFactor))
			{
				RuntimeMaterial.ClearCoatRoughnessFactor = 0;
			}

			RuntimeMaterial.bKHR_materials_clearcoat = true;
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

UTexture2D* FglTFRuntimeParser::BuildTexture(UObject* Outer, const TArray<FglTFRuntimeMipMap>& Mips, const FglTFRuntimeImagesConfig& ImagesConfig, const FglTFRuntimeTextureSampler& Sampler)
{
	UTexture2D* Texture = NewObject<UTexture2D>(Outer, NAME_None, RF_Public);
	FTexturePlatformData* PlatformData = new FTexturePlatformData();
	PlatformData->SizeX = Mips[0].Width;
	PlatformData->SizeY = Mips[0].Height;
	PlatformData->PixelFormat = EPixelFormat::PF_B8G8R8A8;

#if ENGINE_MAJOR_VERSION > 4
	Texture->SetPlatformData(PlatformData);
#else
	Texture->PlatformData = PlatformData;
#endif

	Texture->NeverStream = true;

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

	Texture->AddressX = Sampler.TileX;
	Texture->AddressY = Sampler.TileY;

	Texture->UpdateResource();

	TexturesCache.Add(Mips[0].TextureIndex, Texture);

	return Texture;
}

UMaterialInterface* FglTFRuntimeParser::BuildVertexColorOnlyMaterial(const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	UMaterialInterface* BaseMaterial = MetallicRoughnessMaterialsMap[EglTFRuntimeMaterialType::TwoSided];

	if (MaterialsConfig.VertexColorOnlyMaterial)
	{
		BaseMaterial = MaterialsConfig.VertexColorOnlyMaterial;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
	if (!Material)
	{
		AddError("BuildVertexColorOnlyMaterial()", "Unable to create material instance, falling back to default material");
		return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
	}

	Material->SetScalarParameterValue("bUseVertexColors", true);

	return Material;
}

UMaterialInterface* FglTFRuntimeParser::BuildMaterial(const int32 Index, const FString& MaterialName, const FglTFRuntimeMaterial& RuntimeMaterial, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_BuildMaterial, FColor::Magenta);

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

	if (RuntimeMaterial.bKHR_materials_transmission)
	{
		if (TransmissionMaterialsMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = TransmissionMaterialsMap[RuntimeMaterial.MaterialType];
		}
	}

	if (RuntimeMaterial.bKHR_materials_unlit)
	{
		if (UnlitMaterialsMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = UnlitMaterialsMap[RuntimeMaterial.MaterialType];
		}
	}

	if (RuntimeMaterial.bKHR_materials_clearcoat)
	{
		if (ClearCoatMaterialsMap.Contains(RuntimeMaterial.MaterialType))
		{
			BaseMaterial = ClearCoatMaterialsMap[RuntimeMaterial.MaterialType];
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
		AddError("BuildMaterial()", "Unable to find glTFRuntime Material, ensure it has been packaged, falling back to default material");
		return UMaterial::GetDefaultMaterial(EMaterialDomain::MD_Surface);
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
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

	Material->SetScalarParameterValue("bUseVertexColors", (bUseVertexColors && !MaterialsConfig.bDisableVertexColors) ? 1.0f : 0.0f);
	Material->SetScalarParameterValue("AlphaMask", RuntimeMaterial.bMasked ? 1.0f : 0.0f);

	ApplyMaterialFloatFactor(RuntimeMaterial.bHasIOR, "ior", RuntimeMaterial.IOR);

	ApplyMaterialFloatFactor(RuntimeMaterial.bKHR_materials_clearcoat, "clearcoatFactor", RuntimeMaterial.ClearCoatFactor);
	ApplyMaterialFloatFactor(RuntimeMaterial.bKHR_materials_clearcoat, "clearcoatRoughnessFactor", RuntimeMaterial.ClearCoatRoughnessFactor);

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

	return Material;
}

bool FglTFRuntimeParser::LoadImageFromBlob(TArray64<uint8>& Blob, TSharedRef<FJsonObject> JsonImageObject, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	OnTexturePixels.Broadcast(AsShared(), JsonImageObject, Blob, Width, Height, UncompressedBytes);

	if (UncompressedBytes.Num() > 0)
	{
		return true;
	}

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Blob.GetData(), Blob.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		AddError("LoadImageFromBlob()", "Unable to detect image format");
		return false;
	}

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

	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBytes))
	{
		AddError("LoadImageFromBlob()", "Unable to get raw image data");
		return false;
	}

	Width = ImageWrapper->GetWidth();
	Height = ImageWrapper->GetHeight();

	return true;
}

bool FglTFRuntimeParser::LoadImage(const int32 ImageIndex, TArray64<uint8>& UncompressedBytes, int32& Width, int32& Height, const FglTFRuntimeImagesConfig& ImagesConfig)
{

	TSharedPtr<FJsonObject> JsonImageObject = GetJsonObjectFromRootIndex("images", ImageIndex);
	if (!JsonImageObject)
	{
		AddError("LoadImage()", FString::Printf(TEXT("Unable to load image %d"), ImageIndex));
		return false;
	}

	TArray64<uint8> Bytes;
	if (!GetJsonObjectBytes(JsonImageObject.ToSharedRef(), Bytes))
	{
		AddError("LoadImage()", FString::Printf(TEXT("Unable to load image %d"), ImageIndex));
		return false;
	}

	return LoadImageFromBlob(Bytes, JsonImageObject.ToSharedRef(), UncompressedBytes, Width, Height, ImagesConfig);
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

	if (MaterialsConfig.ImagesOverrideMap.Contains(ImageIndex))
	{
		return MaterialsConfig.ImagesOverrideMap[ImageIndex];
	}

	TArray64<uint8> UncompressedBytes;
	constexpr EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
	int32 Width = 0;
	int32 Height = 0;
	if (!LoadImage(ImageIndex, UncompressedBytes, Width, Height, MaterialsConfig.ImagesConfig))
	{
		return nullptr;
	}

	OnLoadedTexturePixels.Broadcast(AsShared(), JsonTextureObject.ToSharedRef(), Width, Height, reinterpret_cast<FColor*>(UncompressedBytes.GetData()));

	if (Width > 0 && Height > 0 &&
		(Width % GPixelFormats[PixelFormat].BlockSizeX) == 0 &&
		(Height % GPixelFormats[PixelFormat].BlockSizeY) == 0)
	{

		// limit image size
		if (MaterialsConfig.ImagesConfig.MaxWidth > 0 || MaterialsConfig.ImagesConfig.MaxHeight > 0)
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

	int64 SamplerIndex;
	if (JsonTextureObject->TryGetNumberField("sampler", SamplerIndex))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonSamplers;
		// no samplers ?
		if (!Root->TryGetArrayField("samplers", JsonSamplers))
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
					if (JsonSamplerObject->TryGetNumberField("minFilter", MinFilter))
					{
						if (MinFilter == 9728)
						{
							Sampler.MinFilter = TextureFilter::TF_Nearest;
						}
					}
					int64 MagFilter;
					if (JsonSamplerObject->TryGetNumberField("magFilter", MagFilter))
					{
						if (MagFilter == 9728)
						{
							Sampler.MagFilter = TextureFilter::TF_Nearest;
						}
					}
					int64 WrapS;
					if (JsonSamplerObject->TryGetNumberField("wrapS", WrapS))
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
					if (JsonSamplerObject->TryGetNumberField("wrapT", WrapT))
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
		AddError("LoadMaterial()", "Unable to load material");
		return nullptr;
	}

	if (CanWriteToCache(MaterialsConfig.CacheMode))
	{
		MaterialsNameCache.Add(Material, MaterialName);
		MaterialsCache.Add(Index, Material);
	}

	return Material;
}
