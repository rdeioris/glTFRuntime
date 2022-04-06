// Copyright 2020, Roberto De Ioris.

#include "glTFRuntimeAsset.h"
#include "Animation/AnimSequence.h"

#define GLTF_CHECK_ERROR_MESSAGE() UE_LOG(LogGLTFRuntime, Error, TEXT("No glTF Asset loaded."))

#define GLTF_CHECK_PARSER(RetValue) if (!Parser)\
	{\
		GLTF_CHECK_ERROR_MESSAGE();\
		return RetValue;\
	}\

#define GLTF_CHECK_PARSER_VOID() if (!Parser)\
	{\
		GLTF_CHECK_ERROR_MESSAGE();\
		return;\
	}\


bool UglTFRuntimeAsset::LoadFromFilename(const FString& Filename, const FglTFRuntimeConfig& LoaderConfig)
{
	// asset already loaded ?
	if (Parser)
	{
		return false;
	}

	Parser = FglTFRuntimeParser::FromFilename(Filename, LoaderConfig);
	if (Parser)
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnErrorProxy));
		Parser->OnError.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnStaticMeshCreatedProxy));
		Parser->OnStaticMeshCreated.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnSkeletalMeshCreatedProxy));
		Parser->OnSkeletalMeshCreated.Add(Delegate);
	}
	return Parser != nullptr;
}

bool UglTFRuntimeAsset::LoadFromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig)
{
	// asset already loaded ?
	if (Parser)
	{
		return false;
	}

	Parser = FglTFRuntimeParser::FromString(JsonData, LoaderConfig, nullptr);
	if (Parser)
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnErrorProxy));
		Parser->OnError.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnStaticMeshCreatedProxy));
		Parser->OnStaticMeshCreated.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnSkeletalMeshCreatedProxy));
		Parser->OnSkeletalMeshCreated.Add(Delegate);
	}
	return Parser != nullptr;
}

bool UglTFRuntimeAsset::LoadFromData(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig)
{
	// asset already loaded ?
	if (Parser)
	{
		return false;
	}

	Parser = FglTFRuntimeParser::FromData(DataPtr, DataNum, LoaderConfig);
	if (Parser)
	{
		FScriptDelegate Delegate;
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnErrorProxy));
		Parser->OnError.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnStaticMeshCreatedProxy));
		Parser->OnStaticMeshCreated.Add(Delegate);
		Delegate.BindUFunction(this, GET_FUNCTION_NAME_CHECKED(UglTFRuntimeAsset, OnSkeletalMeshCreatedProxy));
		Parser->OnSkeletalMeshCreated.Add(Delegate);
	}
	return Parser != nullptr;
}

void UglTFRuntimeAsset::OnErrorProxy(const FString& ErrorContext, const FString& ErrorMessage)
{
	if (OnError.IsBound())
	{
		OnError.Broadcast(ErrorContext, ErrorMessage);
	}
}

void UglTFRuntimeAsset::OnStaticMeshCreatedProxy(UStaticMesh* StaticMesh)
{
	if (OnStaticMeshCreated.IsBound())
	{
		OnStaticMeshCreated.Broadcast(StaticMesh);
	}
}

void UglTFRuntimeAsset::OnSkeletalMeshCreatedProxy(USkeletalMesh* SkeletalMesh)
{
	if (OnSkeletalMeshCreated.IsBound())
	{
		OnSkeletalMeshCreated.Broadcast(SkeletalMesh);
	}
}

TArray<FglTFRuntimeScene> UglTFRuntimeAsset::GetScenes()
{
	GLTF_CHECK_PARSER(TArray<FglTFRuntimeScene>());

	TArray<FglTFRuntimeScene> Scenes;
	if (!Parser->LoadScenes(Scenes))
	{
		Parser->AddError("UglTFRuntimeAsset::GetScenes()", "Unable to retrieve Scenes from glTF Asset.");
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
		Parser->AddError("UglTFRuntimeAsset::GetScenes()", "Unable to retrieve Nodes from glTF Asset.");
		return TArray<FglTFRuntimeNode>();
	}
	return Nodes;
}

bool UglTFRuntimeAsset::GetNode(const int32 NodeIndex, FglTFRuntimeNode& Node)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadNode(NodeIndex, Node);
}

ACameraActor* UglTFRuntimeAsset::LoadNodeCamera(UObject* WorldContextObject, const int32 NodeIndex, TSubclassOf<ACameraActor> CameraActorClass)
{
	GLTF_CHECK_PARSER(nullptr);

	if (!CameraActorClass)
	{
		Parser->AddError("UglTFRuntimeAsset::LoadNodeCamera()", "Invalid Camera Actor Class.");
		return nullptr;
	}

	FglTFRuntimeNode Node;
	if (!Parser->LoadNode(NodeIndex, Node))
	{
		return nullptr;
	}

	if (Node.CameraIndex == INDEX_NONE)
	{
		Parser->AddError("UglTFRuntimeAsset::LoadNodeCamera()", "Node has no valid associated Camera.");
		return nullptr;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		Parser->AddError("UglTFRuntimeAsset::LoadNodeCamera()", "Unable to retrieve World.");
		return nullptr;
	}

	FActorSpawnParameters SpawnParameters;
	SpawnParameters.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ACameraActor* NewCameraActor = World->SpawnActor<ACameraActor>(CameraActorClass, Node.Transform, SpawnParameters);
	if (!NewCameraActor)
	{
		return nullptr;
	}

	UCameraComponent* CameraComponent = NewCameraActor->FindComponentByClass<UCameraComponent>();
	if (!Parser->LoadCameraIntoCameraComponent(Node.CameraIndex, CameraComponent))
	{
		return nullptr;
	}
	return NewCameraActor;
}

bool UglTFRuntimeAsset::LoadCamera(const int32 CameraIndex, UCameraComponent* CameraComponent)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadCameraIntoCameraComponent(CameraIndex, CameraComponent);
}

TArray<int32> UglTFRuntimeAsset::GetCameraNodesIndices()
{
	TArray<int32> NodeIndices;

	GLTF_CHECK_PARSER(NodeIndices);

	TArray<FglTFRuntimeNode> Nodes;
	if (Parser->GetAllNodes(Nodes))
	{
		for (FglTFRuntimeNode& Node : Nodes)
		{
			if (Node.CameraIndex == INDEX_NONE)
			{
				continue;
			}
			NodeIndices.Add(Node.Index);
		}
	}

	return NodeIndices;
}

bool UglTFRuntimeAsset::GetNodeByName(const FString& NodeName, FglTFRuntimeNode& Node)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadNodeByName(NodeName, Node);
}

TArray<FString> UglTFRuntimeAsset::GetCamerasNames()
{
	GLTF_CHECK_PARSER(TArray<FString>());

	return Parser->GetCamerasNames();
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMesh(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadStaticMesh(MeshIndex, StaticMeshConfig);
}

TArray<UStaticMesh*> UglTFRuntimeAsset::LoadStaticMeshesFromPrimitives(const int32 MeshIndex, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(TArray<UStaticMesh*>());

	return Parser->LoadStaticMeshesFromPrimitives(MeshIndex, StaticMeshConfig);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshLODs(const TArray<int32> MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadStaticMeshLODs(MeshIndices, StaticMeshConfig);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMeshLODs(const TArray<int32> MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalMeshLODs(MeshIndices, SkinIndex, SkeletalMeshConfig);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshByName(const FString& MeshName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadStaticMeshByName(MeshName, StaticMeshConfig);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshByNodeName(const FString& NodeName, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	FglTFRuntimeNode Node;
	if (!Parser->LoadNodeByName(NodeName, Node))
	{
		return nullptr;
	}

	return Parser->LoadStaticMesh(Node.MeshIndex, StaticMeshConfig);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMesh(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalMesh(MeshIndex, SkinIndex, SkeletalMeshConfig);
}

void UglTFRuntimeAsset::LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadSkeletalMeshAsync(MeshIndex, SkinIndex, AsyncCallback, SkeletalMeshConfig);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalMeshRecursive(NodeName, SkeletalMeshConfig.OverrideSkinIndex, ExcludeNodes, SkeletalMeshConfig);
}

void UglTFRuntimeAsset::LoadSkeletalMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, FglTFRuntimeSkeletalMeshAsync AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadSkeletalMeshRecursiveAsync(NodeName, SkeletalMeshConfig.OverrideSkinIndex, ExcludeNodes, AsyncCallback, SkeletalMeshConfig);
}

USkeleton* UglTFRuntimeAsset::LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeleton(SkinIndex, SkeletonConfig);
}

UAnimSequence* UglTFRuntimeAsset::LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalAnimation(SkeletalMesh, AnimationIndex, SkeletalAnimationConfig);
}

UAnimSequence* UglTFRuntimeAsset::LoadSkeletalAnimationByName(USkeletalMesh* SkeletalMesh, const FString& AnimationName, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalAnimationByName(SkeletalMesh, AnimationName, SkeletalAnimationConfig);
}

bool UglTFRuntimeAsset::BuildTransformFromNodeBackward(const int32 NodeIndex, FTransform& Transform)
{
	GLTF_CHECK_PARSER(false);

	Transform = FTransform::Identity;

	FglTFRuntimeNode Node;
	Node.ParentIndex = NodeIndex;

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!Parser->LoadNode(Node.ParentIndex, Node))
		{
			return false;
		}
		Transform *= Node.Transform;
	}

	return true;
}

bool UglTFRuntimeAsset::NodeIsBone(const int32 NodeIndex)
{
	GLTF_CHECK_PARSER(false);

	return Parser->NodeIsBone(NodeIndex);
}

bool UglTFRuntimeAsset::BuildTransformFromNodeForward(const int32 NodeIndex, const int32 LastNodeIndex, FTransform& Transform)
{
	GLTF_CHECK_PARSER(false);

	Transform = FTransform::Identity;

	TArray<FTransform> NodesTree;

	FglTFRuntimeNode Node;
	Node.ParentIndex = LastNodeIndex;

	bool bFoundNode = false;

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!Parser->LoadNode(Node.ParentIndex, Node))
			return false;
		NodesTree.Add(Node.Transform);
		if (Node.Index == NodeIndex)
		{
			bFoundNode = true;
			break;
		}
	}

	if (!bFoundNode)
		return false;

	for (int32 ChildIndex = NodesTree.Num() - 1; ChildIndex >= 0; ChildIndex--)
	{
		FTransform& ChildTransform = NodesTree[ChildIndex];
		Transform *= ChildTransform;
	}

	return true;
}

UAnimMontage* UglTFRuntimeAsset::LoadSkeletalAnimationAsMontage(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FString& SlotNodeName, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig)
{
	UAnimSequence* AnimSequence = LoadSkeletalAnimation(SkeletalMesh, AnimationIndex, AnimationConfig);
	if (!AnimSequence)
	{
		return nullptr;
	}

	UAnimMontage* AnimMontage = UAnimMontage::CreateSlotAnimationAsDynamicMontage(AnimSequence, FName(SlotNodeName), 0, 0, 1);
	if (!AnimMontage)
	{
		return nullptr;
	}

	AnimMontage->SetPreviewMesh(SkeletalMesh);

	return AnimMontage;
}

UglTFRuntimeAnimationCurve* UglTFRuntimeAsset::LoadNodeAnimationCurve(const int32 NodeIndex)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadNodeAnimationCurve(NodeIndex);
}

TArray<UglTFRuntimeAnimationCurve*> UglTFRuntimeAsset::LoadAllNodeAnimationCurves(const int32 NodeIndex)
{
	GLTF_CHECK_PARSER(TArray<UglTFRuntimeAnimationCurve*>());

	return Parser->LoadAllNodeAnimationCurves(NodeIndex);
}

UAnimSequence* UglTFRuntimeAsset::LoadNodeSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadNodeSkeletalAnimation(SkeletalMesh, NodeIndex, SkeletalAnimationConfig);
}

bool UglTFRuntimeAsset::FindNodeByNameInArray(const TArray<int32>& NodeIndices, const FString& NodeName, FglTFRuntimeNode& Node)
{
	GLTF_CHECK_PARSER(false);

	for (int32 NodeIndex : NodeIndices)
	{
		FglTFRuntimeNode CurrentNode;
		if (Parser->LoadNode(NodeIndex, CurrentNode))
		{
			if (CurrentNode.Name == NodeName)
			{
				Node = CurrentNode;
				return true;
			}
		}
	}
	return false;
}

bool UglTFRuntimeAsset::LoadStaticMeshIntoProceduralMeshComponent(const int32 MeshIndex, UProceduralMeshComponent* ProceduralMeshComponent, const FglTFRuntimeProceduralMeshConfig& ProceduralMeshConfig)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadStaticMeshIntoProceduralMeshComponent(MeshIndex, ProceduralMeshComponent, ProceduralMeshConfig);
}

UMaterialInterface* UglTFRuntimeAsset::LoadMaterial(const int32 MaterialIndex, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const bool bUseVertexColors)
{
	GLTF_CHECK_PARSER(nullptr);
	FString MaterialName;
	return Parser->LoadMaterial(MaterialIndex, MaterialsConfig, bUseVertexColors, MaterialName);
}

FString UglTFRuntimeAsset::GetStringFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const
{
	GLTF_CHECK_PARSER("");
	return Parser->GetJSONStringFromPath(Path, bFound);
}

int64 UglTFRuntimeAsset::GetIntegerFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(0);
	return static_cast<int64>(Parser->GetJSONNumberFromPath(Path, bFound));
}

float UglTFRuntimeAsset::GetFloatFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(0);
	return static_cast<float>(Parser->GetJSONNumberFromPath(Path, bFound));
}

bool UglTFRuntimeAsset::GetBooleanFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetJSONBooleanFromPath(Path, bFound);
}

int32 UglTFRuntimeAsset::GetArraySizeFromPath(const TArray<FglTFRuntimePathItem> Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(-1);
	return Parser->GetJSONArraySizeFromPath(Path, bFound);
}

bool UglTFRuntimeAsset::LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter)
{
	GLTF_CHECK_PARSER(false);
	return Parser->LoadAudioEmitter(EmitterIndex, Emitter);
}

bool UglTFRuntimeAsset::LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent)
{
	GLTF_CHECK_PARSER(false);
	return Parser->LoadEmitterIntoAudioComponent(Emitter, AudioComponent);
}

void UglTFRuntimeAsset::LoadStaticMeshAsync(const int32 MeshIndex, FglTFRuntimeStaticMeshAsync AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadStaticMeshAsync(MeshIndex, AsyncCallback, StaticMeshConfig);
}

void UglTFRuntimeAsset::LoadStaticMeshLODsAsync(const TArray<int32> MeshIndices, FglTFRuntimeStaticMeshAsync AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadStaticMeshLODsAsync(MeshIndices, AsyncCallback, StaticMeshConfig);
}

int32 UglTFRuntimeAsset::GetNumMeshes() const
{
	GLTF_CHECK_PARSER(0);

	return Parser->GetNumMeshes();
}

int32 UglTFRuntimeAsset::GetNumImages() const
{
	GLTF_CHECK_PARSER(0);

	return Parser->GetNumImages();
}

UTexture2D* UglTFRuntimeAsset::LoadImage(const int32 ImageIndex, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray64<uint8> UncompressedBytes;
	int32 Width = 0;
	int32 Height = 0;
	if (!Parser->LoadImage(ImageIndex, UncompressedBytes, Width, Height, ImagesConfig))
	{
		return nullptr;
	}

	if (Width > 0 && Height > 0)
	{
		FglTFRuntimeMipMap Mip(-1);
		Mip.Pixels = UncompressedBytes;
		Mip.Width = Width;
		Mip.Height = Height;
		TArray<FglTFRuntimeMipMap> Mips = { Mip };
		return Parser->BuildTexture(this, Mips, ImagesConfig);
	}

	return nullptr;
}

TArray<FString> UglTFRuntimeAsset::GetExtensionsUsed() const
{
	GLTF_CHECK_PARSER(TArray<FString>());
	return Parser->ExtensionsUsed;
}

TArray<FString> UglTFRuntimeAsset::GetExtensionsRequired() const
{
	GLTF_CHECK_PARSER(TArray<FString>());
	return Parser->ExtensionsRequired;
}