// Copyright 2020-2022, Roberto De Ioris.


#include "glTFRuntimeParser.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SpotLightComponent.h"


ULightComponent* FglTFRuntimeParser::LoadPunctualLight(const int32 PunctualLightIndex, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig)
{
	ULightComponent* LightComponent = nullptr;

	TSharedPtr<FJsonObject> JsonPunctualLightObject = GetJsonObjectFromRootExtensionIndex("KHR_lights_punctual", "lights", PunctualLightIndex);
	if (!JsonPunctualLightObject)
	{
		AddError("LoadPunctualLight()", "Invalid PunctualLight index.");
		return nullptr;
	}

	FLinearColor LightColor = FLinearColor(GetJsonObjectVector4(JsonPunctualLightObject.ToSharedRef(), "color", FVector4(1.0, 1.0, 1.0, 1.0)));

	FString PunctualLightType = GetJsonObjectString(JsonPunctualLightObject.ToSharedRef(), "type", "");
	if (PunctualLightType == "directional")
	{
		UDirectionalLightComponent* DirectionalLight = NewObject<UDirectionalLightComponent>(Actor);
		DirectionalLight->SetLightColor(LightColor);

		double Intensity = GetJsonObjectNumber(JsonPunctualLightObject.ToSharedRef(), "intensity", 1.0);
		DirectionalLight->SetIntensity(Intensity);

		LightComponent = DirectionalLight;
	}
	else if (PunctualLightType == "point")
	{
		UPointLightComponent* PointLight = NewObject<UPointLightComponent>(Actor);
		PointLight->SetLightColor(LightColor);

		PointLight->SetIntensityUnits(ELightUnits::Candelas);
		PointLight->bUseInverseSquaredFalloff = true;

		double Intensity = GetJsonObjectNumber(JsonPunctualLightObject.ToSharedRef(), "intensity", 1.0);
		PointLight->SetIntensity(Intensity);

		float DefaultAttenuation = Intensity * LightConfig.DefaultAttenuationMultiplier;

		double Attenuation = GetJsonObjectNumber(JsonPunctualLightObject.ToSharedRef(), "range", DefaultAttenuation / SceneScale);
		PointLight->SetAttenuationRadius(Attenuation * SceneScale);


		LightComponent = PointLight;
	}
	else if (PunctualLightType == "spot")
	{
		USpotLightComponent* SpotLight = NewObject<USpotLightComponent>(Actor);
		SpotLight->SetLightColor(LightColor);

		SpotLight->SetIntensityUnits(ELightUnits::Candelas);
		SpotLight->bUseInverseSquaredFalloff = true;

		double Intensity = GetJsonObjectNumber(JsonPunctualLightObject.ToSharedRef(), "intensity", 1.0);
		SpotLight->SetIntensity(Intensity);

		float DefaultAttenuation = Intensity * LightConfig.DefaultAttenuationMultiplier;

		double Attenuation = GetJsonObjectNumber(JsonPunctualLightObject.ToSharedRef(), "range", DefaultAttenuation / SceneScale);
		SpotLight->SetAttenuationRadius(Attenuation * SceneScale);


		float InnerConeAngle = 0;
		float OuterConeAngle = PI / 2.0;
		TSharedPtr<FJsonObject> SpotJson = GetJsonObjectFromObject(JsonPunctualLightObject.ToSharedRef(), "spot");
		if (SpotJson)
		{
			InnerConeAngle = GetJsonObjectNumber(SpotJson.ToSharedRef(), "innerConeAngle", InnerConeAngle);
			OuterConeAngle = GetJsonObjectNumber(SpotJson.ToSharedRef(), "outerConeAngle", OuterConeAngle);
		}
		 
		SpotLight->SetInnerConeAngle(FMath::RadiansToDegrees(InnerConeAngle));
		SpotLight->SetOuterConeAngle(FMath::RadiansToDegrees(OuterConeAngle));

		LightComponent = SpotLight;
	}
	else
	{
		AddError("LoadPunctualLight()", "Unsupported PunctualLight type.");
	}

	return LightComponent;
}