// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeFunctionLibrary.h"
#include "glTFRuntimeParser.h"

UStaticMesh* UglTFRuntimeFunctionLibrary::glTFLoadStaticMeshFromFilename(FString Filename, int32 Index)
{
	TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromFilename(Filename);
	if (!Parser)
		return nullptr;

	return Parser->LoadStaticMesh(Index);
}