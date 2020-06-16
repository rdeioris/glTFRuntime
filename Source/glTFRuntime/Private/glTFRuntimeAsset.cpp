// Copyright 2020 Roberto De Ioris.


#include "glTFRuntimeAsset.h"

#define GLTF_CHECK_PARSER(RetValue) if (!Parser)\
	{\
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));\
		return RetValue;\
	}\

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
	GLTF_CHECK_PARSER(TArray<FglTFRuntimeScene>());

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
	GLTF_CHECK_PARSER(TArray<FglTFRuntimeNode>());
	
	TArray<FglTFRuntimeNode> Nodes;
	if (!Parser->GetAllNodes(Nodes))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to retrieve Nodes from glTF Asset."));
		return TArray<FglTFRuntimeNode>();
	}
	return Nodes;
}

bool UglTFRuntimeAsset::GetNode(const int32 NodeIndex, FglTFRuntimeNode& Node)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadNode(NodeIndex, Node);
}

bool UglTFRuntimeAsset::GetNodeByName(const FString NodeName, FglTFRuntimeNode& Node)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadNodeByName(NodeName, Node);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMesh(const int32 MeshIndex)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadStaticMesh(MeshIndex);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshByName(const FString MeshName)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadStaticMeshByName(MeshName);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadSkeletalMesh(MeshIndex, SkinIndex);
}

UAnimSequence* UglTFRuntimeAsset::LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex)
{
	if (!Parser)
	{
		UE_LOG(LogTemp, Error, TEXT("No glTF Asset loaded."));
		return false;
	}

	return Parser->LoadSkeletalAnimation(SkeletalMesh, AnimationIndex);
}

bool UglTFRuntimeAsset::BuildTransformFromNodeBackward(const int32 NodeIndex, FTransform& Transform)
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