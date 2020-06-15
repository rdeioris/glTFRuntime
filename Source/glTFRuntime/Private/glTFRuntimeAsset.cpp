// Copyright 2020 Roberto De Ioris.


#include "glTFRuntimeAsset.h"

bool UglTFRuntimeAsset::LoadFromFilename(const FString Filename)
{
	// asset already loaded ?
	if (Parser)
		return false;

	Parser = FglTFRuntimeParser::FromFilename(Filename);
	if (!Parser)
		return false;

	return true;
}

TArray<FglTFRuntimeScene> UglTFRuntimeAsset::GetScenes()
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return TArray<FglTFRuntimeScene>();
	}

	TArray<FglTFRuntimeScene> Scenes;
	if (!Parser->LoadScenes(Scenes))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to retrieve Scenes from glTF Asset."));
		return TArray<FglTFRuntimeScene>();
	}
	return Scenes;
}

TArray<FglTFRuntimeNode> UglTFRuntimeAsset::GetNodes()
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return TArray<FglTFRuntimeNode>();
	}

	TArray<FglTFRuntimeNode> Nodes;
	if (!Parser->GetAllNodes(Nodes))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to retrieve Nodes from glTF Asset."));
		return TArray<FglTFRuntimeNode>();
	}
	return Nodes;
}

bool UglTFRuntimeAsset::GetNode(int32 NodeIndex, FglTFRuntimeNode& Node)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadNode(NodeIndex, Node);
}

bool UglTFRuntimeAsset::GetNodeByName(FString Name, FglTFRuntimeNode& Node)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadNodeByName(Name, Node);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMesh(int32 MeshIndex)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadStaticMesh(MeshIndex);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMesh(int32 MeshIndex, int32 SkinIndex)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadSkeletalMesh(MeshIndex, SkinIndex);
}

UAnimSequence* UglTFRuntimeAsset::LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, int32 AnimationIndex)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadSkeletalAnimation(SkeletalMesh, AnimationIndex);
}

bool UglTFRuntimeAsset::BuildTransformFromNodeBackward(int32 NodeIndex, FTransform& Transform)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	Transform = FTransform::Identity;

	FglTFRuntimeNode Node;
	Node.ParentIndex = NodeIndex;

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!Parser->LoadNode(Node.ParentIndex, Node))
			return false;
		Transform *= Node.Transform;
	}

	return true;
}