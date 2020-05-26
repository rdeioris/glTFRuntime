// Fill out your copyright notice in the Description page of Project Settings.


#include "glTFRuntimeFunctionLibrary.h"

UStaticMesh* UglTFRuntimeFunctionLibrary::glTFLoadStaticMeshFromFilename(FString Filename, int32 Index)
{
	TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromFilename(Filename);
	if (!Parser)
		return nullptr;

	return Parser->LoadStaticMesh(Index);
}

TArray<UStaticMesh*> UglTFRuntimeFunctionLibrary::glTFLoadStaticMeshesFromFilename(FString Filename, bool& bSuccess)
{
	TArray<UStaticMesh*> StaticMeshes;
	bSuccess = false;

	TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromFilename(Filename);
	if (!Parser)
	{
		return TArray<UStaticMesh*>();
	}

	if (!Parser->LoadStaticMeshes(StaticMeshes))
	{
		return TArray<UStaticMesh*>();
	}

	bSuccess = true;
	return StaticMeshes;
}

TArray<FglTFRuntimeNode> UglTFRuntimeFunctionLibrary::glTFLoadNodesFromFilename(FString Filename, bool& bSuccess)
{
	TArray<FglTFRuntimeNode> Nodes;
	bSuccess = false;

	TSharedPtr<FglTFRuntimeParser> Parser = FglTFRuntimeParser::FromFilename(Filename);
	if (!Parser)
	{
		return TArray<FglTFRuntimeNode>();
	}

	if (!Parser->LoadNodes(Nodes))
	{
		return TArray<FglTFRuntimeNode>();
	}

	bSuccess = true;
	return Nodes;
}