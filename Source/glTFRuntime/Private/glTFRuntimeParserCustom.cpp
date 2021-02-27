// Copyright 2021, Roberto De Ioris.

#include "glTFRuntimeParser.h"

TSharedPtr<FJsonValue> FglTFRuntimeParser::GetJSONObjectFromPath(const TArray<FglTFRuntimePathItem>& Path) const
{
	if (Path.Num() == 0)
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> CurrentObject = Root;
	for (int32 PathIndex = 0; PathIndex < Path.Num() - 1; PathIndex++)
	{
		const FString& Part = Path[PathIndex].Path;

		TSharedPtr<FJsonValue> CurrentValue = CurrentObject->TryGetField(Part);
		if (!CurrentValue)
		{
			return nullptr;
		}

		if (Path[PathIndex].Index <= INDEX_NONE)
		{
			const TSharedPtr<FJsonObject>* IsObject = nullptr;
			if (!CurrentValue->TryGetObject(IsObject))
			{
				return nullptr;
			}
			CurrentObject = *IsObject;
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
			if (!CurrentValue->TryGetArray(IsArray))
			{
				return nullptr;
			}

			if (!IsArray->IsValidIndex(Path[PathIndex].Index))
			{
				return nullptr;
			}

			const TSharedPtr<FJsonObject>* IsObject = nullptr;
			if (!(*IsArray)[Path[PathIndex].Index]->TryGetObject(IsObject))
			{
				return nullptr;
			}

			CurrentObject = *IsObject;
		}

	}

	TSharedPtr<FJsonValue> FinalValue = CurrentObject->TryGetField(Path.Last().Path);
	if (!FinalValue)
	{
		return nullptr;
	}

	if (Path.Last().Index <= INDEX_NONE)
	{
		return FinalValue;
	}

	const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
	if (!FinalValue->TryGetArray(IsArray))
	{
		return nullptr;
	}

	if (!IsArray->IsValidIndex(Path.Last().Index))
	{
		return nullptr;
	}

	return (*IsArray)[Path.Last().Index];
}

FString FglTFRuntimeParser::GetJSONStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	FString ReturnValue = "";
	bFound = false;

	TSharedPtr<FJsonValue> CurrentObject = GetJSONObjectFromPath(Path);
	if (!CurrentObject)
	{
		return ReturnValue;
	}

	bFound = CurrentObject->TryGetString(ReturnValue);
	return ReturnValue;
}


double FglTFRuntimeParser::GetJSONNumberFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	double ReturnValue = 0;
	bFound = false;

	TSharedPtr<FJsonValue> CurrentObject = GetJSONObjectFromPath(Path);
	if (!CurrentObject)
	{
		return ReturnValue;
	}

	bFound = CurrentObject->TryGetNumber(ReturnValue);
	return ReturnValue;
}

bool FglTFRuntimeParser::GetJSONBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	bool ReturnValue = false;
	bFound = false;

	TSharedPtr<FJsonValue> CurrentObject = GetJSONObjectFromPath(Path);
	if (!CurrentObject)
	{
		return ReturnValue;
	}

	bFound = CurrentObject->TryGetBool(ReturnValue);
	return ReturnValue;
}

int32 FglTFRuntimeParser::GetJSONArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	int32 ReturnValue = -1;
	bFound = false;

	TSharedPtr<FJsonValue> CurrentObject = GetJSONObjectFromPath(Path);
	if (!CurrentObject)
	{
		return ReturnValue;
	}

	const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
	bFound = CurrentObject->TryGetArray(IsArray);
	if (bFound)
	{
		ReturnValue = IsArray->Num();
	}
	return ReturnValue;
}