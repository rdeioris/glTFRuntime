// Copyright 2020-2022, Roberto De Ioris.


#include "glTFRuntimeParser.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SpotLightComponent.h"


ULightComponent* FglTFRuntimeParser::LoadPunctualLight(const int32 PunctualLightIndex, AActor* Actor)
{
	TSharedPtr<FJsonObject> JsonPunctualLightObject = GetJsonObjectFromRootExtensionIndex("KHR_lights_punctual", "lights", PunctualLightIndex);
	if (!JsonPunctualLightObject)
	{
		AddError("LoadPunctualLight()", "Invalid PunctualLight index.");
		return nullptr;
	}

	FLinearColor LightColor = GetJsonObjectLinearColor(JsonPunctualLightObject.ToSharedRef(), "color", FLinearColor(1.0, 1.0, 1.0));

	FString PunctualLightType = GetJsonObjectString(JsonPunctualLightObject.ToSharedRef(), "type", "");
	if (PunctualLightType == "directional")
	{
		UDirectionalLightComponent* DirectionalLight = NewObject<UDirectionalLightComponent>(Actor);
		DirectionalLight->SetLightColor(LightColor);
	}
	else if (PunctualLightType == "point")
	{
		UPointLightComponent* PointLight = NewObject<UPointLightComponent>(Actor);
		PointLight->SetLightColor(LightColor);
	}
	else if (PunctualLightType == "spot")
	{
		USpotLightComponent* SpotLight = NewObject<USpotLightComponent>(Actor);
		SpotLight->SetLightColor(LightColor);
	}
	else
	{
		AddError("LoadPunctualLight()", "Unsupported PunctualLight type.");
	}

	return nullptr;
}