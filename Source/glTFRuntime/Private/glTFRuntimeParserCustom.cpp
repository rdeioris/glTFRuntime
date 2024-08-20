// Copyright 2021-2022, Roberto De Ioris.

#include "glTFRuntimeParser.h"

TSharedPtr<FJsonValue> FglTFRuntimeParser::GetJSONObjectFromRelativePath(TSharedRef<FJsonObject> JsonObject, const TArray<FglTFRuntimePathItem>& Path)
{
	if (Path.Num() == 0)
	{
		return nullptr;
	}

	TSharedPtr<FJsonValue> CurrentItem = MakeShared<FJsonValueObject>(JsonObject);

	for (int32 PathIndex = 0; PathIndex < Path.Num(); PathIndex++)
	{
		const FString& Part = Path[PathIndex].Path;

		TSharedPtr<FJsonValue> Value = nullptr;

		if (!Part.IsEmpty())
		{
			const TSharedPtr<FJsonObject>* IsObject = nullptr;
			if (!CurrentItem->TryGetObject(IsObject))
			{
				return nullptr;
			}

			Value = (*IsObject)->TryGetField(Part);
		}
		// pure array?
		else if (Path[PathIndex].Index > INDEX_NONE)
		{
			Value = CurrentItem;
		}

		if (!Value)
		{
			return nullptr;
		}

		if (Path[PathIndex].Index <= INDEX_NONE)
		{
			CurrentItem = Value;
		}
		else
		{
			const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
			if (!Value->TryGetArray(IsArray))
			{
				return nullptr;
			}

			if (!IsArray->IsValidIndex(Path[PathIndex].Index))
			{
				return nullptr;
			}


			CurrentItem = (*IsArray)[Path[PathIndex].Index];
		}

	}

	return CurrentItem;
}

TSharedPtr<FJsonValue> FglTFRuntimeParser::GetJSONObjectFromPath(const TArray<FglTFRuntimePathItem>& Path) const
{
	return GetJSONObjectFromRelativePath(Root, Path);
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

FVector4 FglTFRuntimeParser::GetJSONVectorFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	FVector4 Vector = FVector4(0, 0, 0, 1);

	int32 ReturnValue = -1;
	bFound = false;

	TSharedPtr<FJsonValue> CurrentObject = GetJSONObjectFromPath(Path);
	if (!CurrentObject)
	{
		return Vector;
	}

	const TArray<TSharedPtr<FJsonValue>>* IsArray = nullptr;
	bFound = CurrentObject->TryGetArray(IsArray);
	if (!bFound)
	{
		return Vector;
	}

	for (int32 Index = 0; Index < FMath::Min<int32>(IsArray->Num(), 4); Index++)
	{
		double Value = 0;
		if ((*IsArray)[Index]->TryGetNumber(Value))
		{
			Vector[Index] = Value;
		}
	}

	return Vector;
}