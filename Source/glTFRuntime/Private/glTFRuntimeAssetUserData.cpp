// Copyright 2024, Roberto De Ioris.


#include "glTFRuntimeAssetUserData.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

void UglTFRuntimeAssetUserData::SetParser(TSharedRef<FglTFRuntimeParser> InParser)
{
	Parser = InParser;
}

FString UglTFRuntimeAssetUserData::GetStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return "";
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return "";
	}

	FString Value;
	bFound = JsonValue->TryGetString(Value);
	return Value;
}

int32 UglTFRuntimeAssetUserData::GetIntegerFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return 0;
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return 0;
	}

	int32 Value;
	bFound = JsonValue->TryGetNumber(Value);
	return Value;
}

float UglTFRuntimeAssetUserData::GetFloatFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return 0;
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return 0;
	}

	float Value;
	bFound = JsonValue->TryGetNumber(Value);
	return Value;
}

bool UglTFRuntimeAssetUserData::GetBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return false;
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return false;
	}

	bool Value;
	bFound = JsonValue->TryGetBool(Value);
	return Value;
}

int32 UglTFRuntimeAssetUserData::GetArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return 0;
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return 0;
	}

	const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
	if (!JsonValue->TryGetArray(IsArray))
	{
		return 0;
	}

	bFound = true;
	return IsArray->Num();
}


void UglTFRuntimeAssetUserData::ReceiveFillAssetUserData_Implementation(const int32 Index)
{

}

FString UglTFRuntimeAssetUserData::GetJsonFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bFound = false;
	TSharedPtr<FJsonObject> CurrentObject = Parser ? Parser->GetJsonRoot() : nullptr;
	if (!CurrentObject)
	{
		return "";
	}

	TSharedPtr<FJsonValue> JsonValue = FglTFRuntimeParser::GetJSONObjectFromRelativePath(CurrentObject.ToSharedRef(), Path);
	if (!JsonValue)
	{
		return "";
	}

	bFound = true;

	FString Json;
	TSharedRef<TJsonWriter<>> JsonWriter = TJsonWriterFactory<>::Create(&Json);
	FJsonSerializer::Serialize(JsonValue, "", JsonWriter);

	return Json;
}