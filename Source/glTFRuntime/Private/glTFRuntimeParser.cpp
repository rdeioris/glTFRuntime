// Copyright 2020, Roberto De Ioris


#include "glTFRuntimeParser.h"
#include "StaticMeshDescription.h"
#include "Rendering/SkeletalMeshLODModel.h"
#include "Rendering/SkeletalMeshModel.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshVertexBuffer.h"
#if WITH_EDITOR
#include "IMeshBuilderModule.h"
#include "LODUtilities.h"
#include "MeshUtilities.h"
#endif

#include "glTfAnimBoneCompressionCodec.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"
#include "Engine/SkeletalMeshSocket.h"

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromFilename(FString Filename)
{
	FString JsonData;
	// TODO: spit out errors
	if (!FFileHelper::LoadFileToString(JsonData, *Filename))
		return nullptr;

	TSharedPtr<FJsonValue> RootValue;

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(JsonReader, RootValue))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonObject = RootValue->AsObject();
	if (!JsonObject)
		return nullptr;

	return MakeShared<FglTFRuntimeParser>(JsonObject.ToSharedRef());
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, FMatrix InSceneBasis, float InSceneScale) : Root(JsonObject), SceneBasis(InSceneBasis), SceneScale(InSceneScale)
{
	bAllNodesCached = false;

	UMaterialInterface* OpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeBase"));
	if (OpaqueMaterial)
	{
		MaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, OpaqueMaterial);
	}

	UMaterialInterface* TranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTranslucent_Inst"));
	if (OpaqueMaterial)
	{
		MaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, TranslucentMaterial);
	}

	UMaterialInterface* TwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSided_Inst"));
	if (TwoSidedMaterial)
	{
		MaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, TwoSidedMaterial);
	}

	UMaterialInterface* TwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedTranslucent_Inst"));
	if (TwoSidedTranslucentMaterial)
	{
		MaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, TwoSidedTranslucentMaterial);
	}
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject) : FglTFRuntimeParser(JsonObject, FBasisVectorMatrix(FVector(0, 0, -1), FVector(1, 0, 0), FVector(0, 1, 0), FVector::ZeroVector), 100)
{

}

bool FglTFRuntimeParser::LoadNodes()
{
	if (bAllNodesCached)
	{
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonNodes;

	// no meshes ?
	if (!Root->TryGetArrayField("nodes", JsonNodes))
	{
		return false;
	}

	// first round for getting all nodes
	for (int32 Index = 0; Index < JsonNodes->Num(); Index++)
	{
		TSharedPtr<FJsonObject> JsonNodeObject = (*JsonNodes)[Index]->AsObject();
		if (!JsonNodeObject)
			return false;
		FglTFRuntimeNode Node;
		if (!LoadNode_Internal(Index, JsonNodeObject.ToSharedRef(), JsonNodes->Num(), Node))
			return false;

		AllNodesCache.Add(Node);
	}

	for (FglTFRuntimeNode& Node : AllNodesCache)
	{
		FixNodeParent(Node);
	}

	bAllNodesCached = true;

	return true;
}

void FglTFRuntimeParser::FixNodeParent(FglTFRuntimeNode& Node)
{
	for (int32 Index : Node.ChildrenIndices)
	{
		AllNodesCache[Index].ParentIndex = Node.Index;
		FixNodeParent(AllNodesCache[Index]);
	}
}

bool FglTFRuntimeParser::LoadScenes(TArray<FglTFRuntimeScene>& Scenes)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonScenes;
	// no scenes ?
	if (!Root->TryGetArrayField("scenes", JsonScenes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonScenes->Num(); Index++)
	{
		FglTFRuntimeScene Scene;
		if (!LoadScene(Index, Scene))
			return false;
		Scenes.Add(Scene);
	}

	return true;
}

bool FglTFRuntimeParser::LoadScene(int32 Index, FglTFRuntimeScene& Scene)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonScenes;
	// no scenes ?
	if (!Root->TryGetArrayField("scenes", JsonScenes))
	{
		return false;
	}

	if (Index >= JsonScenes->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonSceneObject = (*JsonScenes)[Index]->AsObject();
	if (!JsonSceneObject)
		return nullptr;

	Scene.Index = Index;
	Scene.Name = FString::FromInt(Scene.Index);

	FString Name;
	if (JsonSceneObject->TryGetStringField("name", Name))
	{
		Scene.Name = Name;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonSceneNodes;
	if (JsonSceneObject->TryGetArrayField("nodes", JsonSceneNodes))
	{
		for (TSharedPtr<FJsonValue> JsonSceneNode : *JsonSceneNodes)
		{
			int64 NodeIndex;
			if (!JsonSceneNode->TryGetNumber(NodeIndex))
				return false;
			FglTFRuntimeNode SceneNode;
			if (!LoadNode(NodeIndex, SceneNode))
				return false;
			Scene.RootNodesIndices.Add(SceneNode.Index);
		}
	}

	return true;
}

bool FglTFRuntimeParser::LoadStaticMeshes(TArray<UStaticMesh*>& StaticMeshes)
{

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return false;
	}

	for (int32 Index = 0; Index < JsonMeshes->Num(); Index++)
	{
		UStaticMesh* StaticMesh = LoadStaticMesh(Index);
		if (!StaticMesh)
		{
			return false;
		}
		StaticMeshes.Add(StaticMesh);
	}

	return true;
}

bool FglTFRuntimeParser::GetAllNodes(TArray<FglTFRuntimeNode>& Nodes)
{
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
			return false;
	}

	Nodes = AllNodesCache;

	return true;
}

bool FglTFRuntimeParser::LoadNode(int32 Index, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
			return false;
	}

	if (Index >= AllNodesCache.Num())
		return false;

	Node = AllNodesCache[Index];
	return true;
}

bool FglTFRuntimeParser::LoadNodeByName(FString Name, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
			return false;
	}

	for (FglTFRuntimeNode& NodeRef : AllNodesCache)
	{
		if (NodeRef.Name == Name)
		{
			Node = NodeRef;
			return true;
		}
	}

	return false;
}

UTexture2D* FglTFRuntimeParser::LoadTexture(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	UE_LOG(LogTemp, Error, TEXT("attempting to load image: %d"), Index);
	if (Index < 0)
		return nullptr;

	if (MaterialsConfig.TexturesOverrideMap.Contains(Index))
	{
		UE_LOG(LogTemp, Error, TEXT("Found overriden texture for %d"), Index);
		return MaterialsConfig.TexturesOverrideMap[Index];
	}

	// first check cache
	if (TexturesCache.Contains(Index))
	{
		return TexturesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTextures;
	// no images ?
	if (!Root->TryGetArrayField("textures", JsonTextures))
	{
		return nullptr;
	}

	if (Index >= JsonTextures->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonTextureObject = (*JsonTextures)[Index]->AsObject();
	if (!JsonTextureObject)
		return nullptr;

	int64 ImageIndex;
	if (!JsonTextureObject->TryGetNumberField("source", ImageIndex))
		return nullptr;

	if (ImageIndex < 0)
		return nullptr;

	const TArray<TSharedPtr<FJsonValue>>* JsonImages;
	// no images ?
	if (!Root->TryGetArrayField("images", JsonImages))
	{
		return nullptr;
	}

	if (ImageIndex >= JsonImages->Num())
	{
		return nullptr;
	}

	if (MaterialsConfig.ImagesOverrideMap.Contains(ImageIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("Found overriden image for %d"), ImageIndex);
		return MaterialsConfig.ImagesOverrideMap[ImageIndex];
	}

	TSharedPtr<FJsonObject> JsonImageObject = (*JsonImages)[ImageIndex]->AsObject();
	if (!JsonImageObject)
		return nullptr;

	TArray<uint8> Bytes;
	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	FString Uri;
	if (JsonImageObject->TryGetStringField("uri", Uri))
	{
		if (!ParseBase64Uri(Uri, Bytes))
		{
			return nullptr;
		}
	}
	else
	{
		int64 BufferViewIndex;
		if (JsonImageObject->TryGetNumberField("bufferView", BufferViewIndex))
		{
			int64 Stride;
			if (!GetBufferView(BufferViewIndex, Bytes, Stride))
			{
				UE_LOG(LogTemp, Error, TEXT("unable to get bufferView: %d"), BufferViewIndex);
				return nullptr;
			}
		}
	}

	UE_LOG(LogTemp, Error, TEXT("detected image bytes: %d"), Bytes.Num());

	if (Bytes.Num() == 0)
		return nullptr;

	EImageFormat ImageFormat = ImageWrapperModule.DetectImageFormat(Bytes.GetData(), Bytes.Num());
	if (ImageFormat == EImageFormat::Invalid)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to detect image format"));
		return nullptr;
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(ImageFormat);
	if (!ImageWrapper.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to create ImageWrapper"));
		return nullptr;
	}
	if (!ImageWrapper->SetCompressed(Bytes.GetData(), Bytes.Num()))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to parse image data"));
		return nullptr;
	}

	TArray<uint8> UncompressedBytes;
	if (!ImageWrapper->GetRaw(ERGBFormat::BGRA, 8, UncompressedBytes))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to get raw image data"));
		return nullptr;
	}

	EPixelFormat PixelFormat = EPixelFormat::PF_B8G8R8A8;
	int32 Width = ImageWrapper->GetWidth();
	int32 Height = ImageWrapper->GetHeight();

	UTexture2D* Texture = UTexture2D::CreateTransient(Width, Height, PixelFormat);
	if (!Texture)
		return nullptr;

	FTexture2DMipMap& Mip = Texture->PlatformData->Mips[0];
	void* Data = Mip.BulkData.Lock(LOCK_READ_WRITE);
	FMemory::Memcpy(Data, UncompressedBytes.GetData(), UncompressedBytes.Num());
	Mip.BulkData.Unlock();
	Texture->UpdateResource();

	TexturesCache.Add(Index, Texture);

	return Texture;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial(const int32 Index, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (MaterialsCache.Contains(Index))
	{
		return MaterialsCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMaterials;

	// no materials ?
	if (!Root->TryGetArrayField("materials", JsonMaterials))
	{
		return nullptr;
	}

	if (Index >= JsonMaterials->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMaterialObject = (*JsonMaterials)[Index]->AsObject();
	if (!JsonMaterialObject)
		return nullptr;

	UMaterialInterface* Material = LoadMaterial_Internal(JsonMaterialObject.ToSharedRef(), MaterialsConfig);
	if (!Material)
		return nullptr;

	MaterialsCache.Add(Index, Material);

	return Material;
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh(const int32 Index, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (SkeletalMeshesCache.Contains(Index))
	{
		return SkeletalMeshesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;
	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	if (Index >= JsonMeshes->Num())
	{
		UE_LOG(LogTemp, Error, TEXT("unable to find mesh %d"), Index);
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonSkins;
	// no skins ?
	if (!Root->TryGetArrayField("skins", JsonSkins))
	{
		UE_LOG(LogTemp, Error, TEXT("unable to find skin %d"), Index);
		return nullptr;
	}

	if (SkinIndex >= JsonSkins->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[Index]->AsObject();
	if (!JsonMeshObject)
		return nullptr;

	TSharedPtr<FJsonObject> JsonSkinObject = (*JsonSkins)[SkinIndex]->AsObject();
	if (!JsonSkinObject)
		return nullptr;

	USkeletalMesh* SkeletalMesh = LoadSkeletalMesh_Internal(JsonMeshObject.ToSharedRef(), JsonSkinObject.ToSharedRef(), SkeletalMeshConfig);
	if (!SkeletalMesh)
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load skeletal mesh"));
		return nullptr;
	}

	SkeletalMeshesCache.Add(Index, SkeletalMesh);

	return SkeletalMesh;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh(int32 Index)
{
	if (Index < 0)
		return nullptr;

	// first check cache
	if (StaticMeshesCache.Contains(Index))
	{
		return StaticMeshesCache[Index];
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	if (Index >= JsonMeshes->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[Index]->AsObject();
	if (!JsonMeshObject)
	{
		return nullptr;
	}

	UStaticMesh* StaticMesh = LoadStaticMesh_Internal(JsonMeshObject.ToSharedRef());
	if (!StaticMesh)
	{
		return nullptr;
	}

	StaticMeshesCache.Add(Index, StaticMesh);

	return StaticMesh;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMeshByName(const FString Name)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonMeshes;

	// no meshes ?
	if (!Root->TryGetArrayField("meshes", JsonMeshes))
	{
		return nullptr;
	}

	for (int32 MeshIndex = 0; MeshIndex < JsonMeshes->Num(); MeshIndex++)
	{
		TSharedPtr<FJsonObject> JsonMeshObject = (*JsonMeshes)[MeshIndex]->AsObject();
		if (!JsonMeshObject)
		{
			return nullptr;
		}
		FString MeshName;
		if (JsonMeshObject->TryGetStringField("name", MeshName))
		{
			if (MeshName == Name)
			{
				return LoadStaticMesh(MeshIndex);
			}
		}
	}

	return nullptr;
}

bool FglTFRuntimeParser::LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node)
{
	Node.Index = Index;
	Node.Name = FString::FromInt(Node.Index);

	FString Name;
	if (JsonNodeObject->TryGetStringField("name", Name))
	{
		Node.Name = Name;
	}

	int64 MeshIndex;
	if (JsonNodeObject->TryGetNumberField("mesh", MeshIndex))
	{
		Node.MeshIndex = MeshIndex;
	}

	int64 SkinIndex;
	if (JsonNodeObject->TryGetNumberField("skin", SkinIndex))
	{
		Node.SkinIndex = SkinIndex;
	}


	FMatrix Matrix = FMatrix::Identity;

	const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues;
	if (JsonNodeObject->TryGetArrayField("matrix", JsonMatrixValues))
	{
		if (JsonMatrixValues->Num() != 16)
			return false;

		for (int32 i = 0; i < 16; i++)
		{
			double Value;
			if (!(*JsonMatrixValues)[i]->TryGetNumber(Value))
			{
				return false;
			}

			Matrix.M[i / 4][i % 4] = Value;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonScaleValues;
	if (JsonNodeObject->TryGetArrayField("scale", JsonScaleValues))
	{
		if (JsonScaleValues->Num() != 3)
			return false;

		float X, Y, Z;
		if (!(*JsonScaleValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonScaleValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonScaleValues)[2]->TryGetNumber(Z))
			return false;

		FVector MatrixScale = { X, Y, Z };

		Matrix *= FScaleMatrix(MatrixScale);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonRotationValues;
	if (JsonNodeObject->TryGetArrayField("rotation", JsonRotationValues))
	{
		if (JsonRotationValues->Num() != 4)
			return false;

		float X, Y, Z, W;
		if (!(*JsonRotationValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonRotationValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonRotationValues)[2]->TryGetNumber(Z))
			return false;
		if (!(*JsonRotationValues)[3]->TryGetNumber(W))
			return false;

		FQuat Quat = { X, Y, Z, W };

		Matrix *= FQuatRotationMatrix(Quat);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTranslationValues;
	if (JsonNodeObject->TryGetArrayField("translation", JsonTranslationValues))
	{
		if (JsonTranslationValues->Num() != 3)
			return false;

		float X, Y, Z;
		if (!(*JsonTranslationValues)[0]->TryGetNumber(X))
			return false;
		if (!(*JsonTranslationValues)[1]->TryGetNumber(Y))
			return false;
		if (!(*JsonTranslationValues)[2]->TryGetNumber(Z))
			return false;

		FVector Translation = { X, Y, Z };

		Matrix *= FTranslationMatrix(Translation);
	}

	Matrix.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
	Node.Transform = FTransform(SceneBasis.Inverse() * Matrix * SceneBasis);

	const TArray<TSharedPtr<FJsonValue>>* JsonChildren;
	if (JsonNodeObject->TryGetArrayField("children", JsonChildren))
	{
		for (int32 i = 0; i < JsonChildren->Num(); i++)
		{
			int64 ChildIndex;
			if (!(*JsonChildren)[i]->TryGetNumber(ChildIndex))
			{
				return false;
			}

			if (ChildIndex >= NodesCount)
				return false;

			Node.ChildrenIndices.Add(ChildIndex);
		}
	}

	return true;
}

UMaterialInterface* FglTFRuntimeParser::LoadMaterial_Internal(TSharedRef<FJsonObject> JsonMaterialObject, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	bool bTwoSided = false;
	bool bTranslucent = false;
	float AlphaCutoff = 0;

	EglTFRuntimeMaterialType MaterialType = EglTFRuntimeMaterialType::Opaque;

	if (!JsonMaterialObject->TryGetBoolField("doubleSided", bTwoSided))
	{
		bTwoSided = false;
	}

	FString AlphaMode;
	if (!JsonMaterialObject->TryGetStringField("alphaMode", AlphaMode))
	{
		AlphaMode = "OPAQUE";
	}

	if (AlphaMode == "BLEND")
	{
		bTranslucent = true;
	}
	else if (AlphaMode == "MASK")
	{
		bTranslucent = true;
		double AlphaCutoffDouble;
		if (!JsonMaterialObject->TryGetNumberField("alphaCutoff", AlphaCutoffDouble))
		{
			AlphaCutoff = 0.5f;
		}
		else
		{
			AlphaCutoff = AlphaCutoffDouble;
		}
	}
	else if (AlphaMode != "OPAQUE")
	{
		UE_LOG(LogTemp, Error, TEXT("Unsupported alphaMode"));
		return nullptr;
	}

	if (bTranslucent && bTwoSided)
	{
		MaterialType = EglTFRuntimeMaterialType::TwoSidedTranslucent;
	}
	else if (bTranslucent)
	{
		MaterialType = EglTFRuntimeMaterialType::Translucent;
	}
	else if (bTwoSided)
	{
		MaterialType = EglTFRuntimeMaterialType::TwoSided;
	}


	if (!MaterialsMap.Contains(MaterialType))
	{
		return nullptr;
	}

	UMaterialInterface* BaseMaterial = MaterialsMap[MaterialType];
	if (!BaseMaterial)
	{
		return nullptr;
	}

	UMaterialInstanceDynamic* Material = UMaterialInstanceDynamic::Create(BaseMaterial, BaseMaterial);
	if (!Material)
		return nullptr;

	Material->SetScalarParameterValue("alphaCutoff", AlphaCutoff);

	const TSharedPtr<FJsonObject>* JsonPBRObject;
	if (JsonMaterialObject->TryGetObjectField("pbrMetallicRoughness", JsonPBRObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* baseColorFactorValues;
		if ((*JsonPBRObject)->TryGetArrayField("baseColorFactor", baseColorFactorValues))
		{
			if (baseColorFactorValues->Num() != 4)
				return nullptr;

			double R, G, B, A;
			if (!(*baseColorFactorValues)[0]->TryGetNumber(R))
				return nullptr;
			if (!(*baseColorFactorValues)[1]->TryGetNumber(G))
				return nullptr;
			if (!(*baseColorFactorValues)[2]->TryGetNumber(B))
				return nullptr;
			if (!(*baseColorFactorValues)[3]->TryGetNumber(A))
				return nullptr;

			Material->SetVectorParameterValue("baseColorFactor", FLinearColor(R, G, B, A));
		}

		const TSharedPtr<FJsonObject>* JsonBaseColorTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("baseColorTexture", JsonBaseColorTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonBaseColorTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
			if (!Texture)
				return nullptr;

			Material->SetTextureParameterValue("baseColorTexture", Texture);
		}

		double metallicFactor;
		if ((*JsonPBRObject)->TryGetNumberField("metallicFactor", metallicFactor))
		{
			Material->SetScalarParameterValue("metallicFactor", metallicFactor);
		}
		double roughnessFactor;
		if ((*JsonPBRObject)->TryGetNumberField("roughnessFactor", roughnessFactor))
		{
			Material->SetScalarParameterValue("roughnessFactor", roughnessFactor);
		}

		const TSharedPtr<FJsonObject>* JsonMetallicRoughnessTextureObject;
		if ((*JsonPBRObject)->TryGetObjectField("metallicRoughnessTexture", JsonMetallicRoughnessTextureObject))
		{
			int64 TextureIndex;
			if (!(*JsonMetallicRoughnessTextureObject)->TryGetNumberField("index", TextureIndex))
				return nullptr;

			UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
			if (!Texture)
				return nullptr;

			Material->SetTextureParameterValue("metallicRoughnessTexture", Texture);
		}
	}

	const TSharedPtr<FJsonObject>* JsonNormalTextureObject;
	if (JsonMaterialObject->TryGetObjectField("normalTexture", JsonNormalTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonNormalTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
		if (!Texture)
			return nullptr;

		Material->SetTextureParameterValue("normalTexture", Texture);
	}

	const TArray<TSharedPtr<FJsonValue>>* emissiveFactorValues;
	if (JsonMaterialObject->TryGetArrayField("emissiveFactor", emissiveFactorValues))
	{
		if (emissiveFactorValues->Num() != 3)
			return nullptr;

		double R, G, B;
		if (!(*emissiveFactorValues)[0]->TryGetNumber(R))
			return nullptr;
		if (!(*emissiveFactorValues)[1]->TryGetNumber(G))
			return nullptr;
		if (!(*emissiveFactorValues)[2]->TryGetNumber(B))
			return nullptr;

		Material->SetVectorParameterValue("emissiveFactor", FLinearColor(R, G, B));
	}

	const TSharedPtr<FJsonObject>* JsonEmissiveTextureObject;
	if (JsonMaterialObject->TryGetObjectField("emissiveTexture", JsonEmissiveTextureObject))
	{
		int64 TextureIndex;
		if (!(*JsonEmissiveTextureObject)->TryGetNumberField("index", TextureIndex))
			return nullptr;

		UTexture2D* Texture = LoadTexture(TextureIndex, MaterialsConfig);
		if (!Texture)
			return nullptr;

		Material->SetTextureParameterValue("emissiveTexture", Texture);
	}

	return Material;
}

void FglTFRuntimeParser::NormalizeSkeletonScale(FReferenceSkeleton& RefSkeleton)
{
	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);
	NormalizeSkeletonBoneScale(Modifier, 0, FVector::OneVector);
}

void FglTFRuntimeParser::NormalizeSkeletonBoneScale(FReferenceSkeletonModifier& Modifier, const int32 BoneIndex, FVector BoneScale)
{
	TArray<FTransform> BonesTransforms = Modifier.GetReferenceSkeleton().GetRefBonePose();

	FTransform BoneTransform = BonesTransforms[BoneIndex];
	FVector ParentScale = BoneTransform.GetScale3D();
	BoneTransform.ScaleTranslation(BoneScale);
	BoneTransform.SetScale3D(FVector::OneVector);

	Modifier.UpdateRefPoseTransform(BoneIndex, BoneTransform);

	TArray<FMeshBoneInfo> MeshBoneInfos = Modifier.GetRefBoneInfo();
	for (int32 MeshBoneIndex = 0; MeshBoneIndex < MeshBoneInfos.Num(); MeshBoneIndex++)
	{
		FMeshBoneInfo& MeshBoneInfo = MeshBoneInfos[MeshBoneIndex];
		if (MeshBoneInfo.ParentIndex == BoneIndex)
		{
			NormalizeSkeletonBoneScale(Modifier, MeshBoneIndex, ParentScale * BoneScale);
		}
	}
}

USkeletalMesh* FglTFRuntimeParser::LoadSkeletalMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject, TSharedRef<FJsonObject> JsonSkinObject, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{

	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives, SkeletalMeshConfig.MaterialsConfig))
		return nullptr;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Public);

	TMap<int32, FName> BoneMap;

	if (!FillReferenceSkeleton(JsonSkinObject, SkeletalMesh->RefSkeleton, BoneMap, SkeletalMeshConfig))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to fill skeleton!"));
		return nullptr;
	}

	//NormalizeSkeletonScale(SkeletalMesh->RefSkeleton);

	TArray<FVector> Points;
	TArray<int32> PointToRawMap;
	int32 MatIndex = 0;
	TMap<int32, int32> BonesCache;

#if WITH_EDITOR
	TArray<SkeletalMeshImportData::FVertex> Wedges;
	TArray<SkeletalMeshImportData::FTriangle> Triangles;
	TArray<SkeletalMeshImportData::FRawBoneInfluence> Influences;


	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		int32 Base = Points.Num();
		Points.Append(Primitive.Positions);

		int32 TriangleIndex = 0;
		TSet<TPair<int32, int32>> InfluencesMap;

		for (int32 i = 0; i < Primitive.Indices.Num(); i++)
		{
			int32 PrimitiveIndex = Primitive.Indices[i];

			SkeletalMeshImportData::FVertex Wedge;
			Wedge.VertexIndex = Base + PrimitiveIndex;

			for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
			{
				Wedge.UVs[UVIndex] = Primitive.UVs[UVIndex][PrimitiveIndex];
			}

			int32 WedgeIndex = Wedges.Add(Wedge);

			for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
			{
				FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][PrimitiveIndex];
				FVector4 Weights = Primitive.Weights[JointsIndex][PrimitiveIndex];
				// 4 bones for each joints list
				for (int32 JointPartIndex = 0; JointPartIndex < 4; JointPartIndex++)
				{
					if (BoneMap.Contains(Joints[JointPartIndex]))
					{
						SkeletalMeshImportData::FRawBoneInfluence Influence;
						Influence.VertexIndex = Wedge.VertexIndex;
						int32 BoneIndex = INDEX_NONE;
						if (BonesCache.Contains(Joints[JointPartIndex]))
						{
							BoneIndex = BonesCache[Joints[JointPartIndex]];
						}
						else
						{
							BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMap[Joints[JointPartIndex]]);
							BonesCache.Add(Joints[JointPartIndex], BoneIndex);
						}
						Influence.BoneIndex = BoneIndex;
						Influence.Weight = Weights[JointPartIndex];
						TPair<int32, int32> InfluenceKey = TPair<int32, int32>(Influence.VertexIndex, Influence.BoneIndex);
						// do not waste cpu time processing zero influences
						if (!FMath::IsNearlyZero(Influence.Weight, KINDA_SMALL_NUMBER) && !InfluencesMap.Contains(InfluenceKey))
						{
							Influences.Add(Influence);
							InfluencesMap.Add(InfluenceKey);
						}
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Unable to find map for bone %u"), Joints[JointPartIndex]);
						return nullptr;
					}
				}
			}

			TriangleIndex++;
			if (TriangleIndex == 3)
			{
				SkeletalMeshImportData::FTriangle Triangle;

				Triangle.WedgeIndex[0] = WedgeIndex - 2;
				Triangle.WedgeIndex[1] = WedgeIndex - 1;
				Triangle.WedgeIndex[2] = WedgeIndex;

				if (Primitive.Normals.Num() > 0)
				{
					Triangle.TangentZ[0] = Primitive.Normals[Primitive.Indices[i - 2]];
					Triangle.TangentZ[1] = Primitive.Normals[Primitive.Indices[i - 1]];
					Triangle.TangentZ[2] = Primitive.Normals[Primitive.Indices[i]];
				}

				if (Primitive.Tangents.Num() > 0)
				{
					Triangle.TangentX[0] = Primitive.Tangents[Primitive.Indices[i - 2]];
					Triangle.TangentX[1] = Primitive.Tangents[Primitive.Indices[i - 1]];
					Triangle.TangentX[2] = Primitive.Tangents[Primitive.Indices[i]];
				}

				Triangle.MatIndex = MatIndex;

				Triangles.Add(Triangle);
				TriangleIndex = 0;
			}
		}

		MatIndex++;
	}

	FSkeletalMeshImportData ImportData;

	for (int32 i = 0; i < Points.Num(); i++)
		PointToRawMap.Add(i);

	FLODUtilities::ProcessImportMeshInfluences(Wedges.Num(), Influences);

	ImportData.bHasNormals = true;
	ImportData.bHasVertexColors = false;
	ImportData.bHasTangents = false;
	ImportData.Faces = Triangles;
	ImportData.Points = Points;
	ImportData.PointToRawMap = PointToRawMap;
	ImportData.NumTexCoords = 1;
	ImportData.Wedges = Wedges;
	ImportData.Influences = Influences;

	FSkeletalMeshModel* ImportedResource = SkeletalMesh->GetImportedModel();
	ImportedResource->LODModels.Empty();
	ImportedResource->LODModels.Add(new FSkeletalMeshLODModel());
	FSkeletalMeshLODModel& LODModel = ImportedResource->LODModels[0];

	SkeletalMesh->SaveLODImportedData(0, ImportData);
#else
	FSkeletalMeshLODRenderData* LodRenderData = new FSkeletalMeshLODRenderData();
	SkeletalMesh->AllocateResourceForRendering();
	SkeletalMesh->GetResourceForRendering()->LODRenderData.Add(LodRenderData);

	LodRenderData->RenderSections.SetNumUninitialized(Primitives.Num());

	int32 NumIndices = 0;
	for (int32 i = 0; i < Primitives.Num(); i++)
	{
		NumIndices += Primitives[i].Indices.Num();
	}

	LodRenderData->StaticVertexBuffers.PositionVertexBuffer.Init(NumIndices);
	LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.Init(NumIndices, 1);

	for (TPair<int32, FName>& Pair : BoneMap)
	{
		int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(Pair.Value);
		if (BoneIndex > INDEX_NONE)
		{
			LodRenderData->RequiredBones.Add(BoneIndex);
			LodRenderData->ActiveBoneIndices.Add(BoneIndex);
		}
	}

	TArray<FSkinWeightInfo> InWeights;
	InWeights.AddUninitialized(NumIndices);

	int32 TotalVertexIndex = 0;

	for (int32 i = 0; i < Primitives.Num(); i++)
	{
		FglTFRuntimePrimitive& Primitive = Primitives[i];

		int32 Base = Points.Num();
		Points.Append(Primitive.Positions);

		new(&LodRenderData->RenderSections[i]) FSkelMeshRenderSection();
		FSkelMeshRenderSection& MeshSection = LodRenderData->RenderSections[i];

		MeshSection.MaterialIndex = i;
		MeshSection.BaseIndex = TotalVertexIndex;
		MeshSection.NumTriangles = Primitive.Indices.Num() / 3;
		MeshSection.BaseVertexIndex = Base;
		MeshSection.MaxBoneInfluences = 4;

		MeshSection.NumVertices = Primitive.Positions.Num();

		TMap<int32, TArray<int32>> OverlappingVertices;
		MeshSection.DuplicatedVerticesBuffer.Init(MeshSection.NumVertices, OverlappingVertices);

		for (int32 VertexIndex = 0; VertexIndex < Primitive.Indices.Num(); VertexIndex++)
		{
			int32 Index = Primitive.Indices[VertexIndex];
			FModelVertex ModelVertex;
			ModelVertex.Position = Primitive.Positions[Index];
			ModelVertex.TangentX = FVector::ZeroVector;
			ModelVertex.TangentZ = FVector::ZeroVector;
			if (Index < Primitive.Normals.Num())
			{
				ModelVertex.TangentZ = Primitive.Normals[Index];
			}
			if (Index < Primitive.Tangents.Num())
			{
				ModelVertex.TangentX = Primitive.Tangents[Index];
			}
			if (Primitive.UVs.Num() > 0 && Index < Primitive.UVs[0].Num())
			{
				ModelVertex.TexCoord = Primitive.UVs[0][Index];
			}
			else
			{
				ModelVertex.TexCoord = FVector2D::ZeroVector;
			}

			LodRenderData->StaticVertexBuffers.PositionVertexBuffer.VertexPosition(TotalVertexIndex) = ModelVertex.Position;
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexTangents(TotalVertexIndex, ModelVertex.TangentX, ModelVertex.GetTangentY(), ModelVertex.TangentZ);
			LodRenderData->StaticVertexBuffers.StaticMeshVertexBuffer.SetVertexUV(TotalVertexIndex, 0, ModelVertex.TexCoord);

			for (int32 JointsIndex = 0; JointsIndex < Primitive.Joints.Num(); JointsIndex++)
			{
				FglTFRuntimeUInt16Vector4 Joints = Primitive.Joints[JointsIndex][Index];
				FVector4 Weights = Primitive.Weights[JointsIndex][Index];

				for (int32 j = 0; j < 4; j++)
				{
					if (BoneMap.Contains(Joints[j]))
					{
						int32 BoneIndex = INDEX_NONE;
						if (BonesCache.Contains(Joints[j]))
						{
							BoneIndex = BonesCache[Joints[j]];
						}
						else
						{
							BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(BoneMap[Joints[j]]);
							BonesCache.Add(Joints[j], BoneIndex);
						}
						InWeights[TotalVertexIndex].InfluenceWeights[j] = Weights[j] * 255;
						InWeights[TotalVertexIndex].InfluenceBones[j] = BoneIndex;
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("Unable to find map for bone %u"), Joints[j]);
						return nullptr;
					}
				}
			}

			TotalVertexIndex++;
		}

		for (TPair<int32, FName>& Pair : BoneMap)
		{
			int32 BoneIndex = SkeletalMesh->RefSkeleton.FindBoneIndex(Pair.Value);
			if (BoneIndex > INDEX_NONE)
			{
				MeshSection.BoneMap.Add(BoneIndex);
			}
		}
	}

	LodRenderData->SkinWeightVertexBuffer.SetMaxBoneInfluences(4);
	LodRenderData->SkinWeightVertexBuffer = InWeights;
	LodRenderData->MultiSizeIndexContainer.CreateIndexBuffer(sizeof(uint32_t));

	for (int32 Index = 0; Index < NumIndices; Index++)
	{
		LodRenderData->MultiSizeIndexContainer.GetIndexBuffer()->AddItem(Index);
	}
#endif

	SkeletalMesh->ResetLODInfo();
	FSkeletalMeshLODInfo& LODInfo = SkeletalMesh->AddLODInfo();
	LODInfo.ReductionSettings.NumOfTrianglesPercentage = 1.0f;
	LODInfo.ReductionSettings.NumOfVertPercentage = 1.0f;
	LODInfo.ReductionSettings.MaxDeviationPercentage = 0.0f;
	LODInfo.BuildSettings.bRecomputeNormals = false;
	LODInfo.LODHysteresis = 0.02f;

	SkeletalMesh->CalculateInvRefMatrices();

	FBox BoundingBox(Points.GetData(), Points.Num());
	SkeletalMesh->SetImportedBounds(FBoxSphereBounds(BoundingBox));

	SkeletalMesh->bHasVertexColors = false;
#if WITH_EDITOR
	SkeletalMesh->VertexColorGuid = SkeletalMesh->bHasVertexColors ? FGuid::NewGuid() : FGuid();
#endif

	for (MatIndex = 0; MatIndex < Primitives.Num(); MatIndex++)
	{
		LODInfo.LODMaterialMap.Add(MatIndex);
		SkeletalMesh->Materials.Add(Primitives[MatIndex].Material);
		SkeletalMesh->Materials[MatIndex].UVChannelData.bInitialized = true;
	}

#if WITH_EDITOR
	IMeshBuilderModule& MeshBuilderModule = IMeshBuilderModule::GetForRunningPlatform();
	if (!MeshBuilderModule.BuildSkeletalMesh(SkeletalMesh, 0, false))
		return nullptr;

	SkeletalMesh->Build();
#endif

	SkeletalMesh->Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);
	SkeletalMesh->Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	for (const TPair<FString, FglTFRuntimeSocket>& Pair : SkeletalMeshConfig.Sockets)
	{
		USkeletalMeshSocket* SkeletalSocket = NewObject<USkeletalMeshSocket>(SkeletalMesh->Skeleton);
		SkeletalSocket->SocketName = FName(Pair.Key);
		SkeletalSocket->BoneName = FName(Pair.Value.BoneName);
		SkeletalSocket->RelativeLocation = Pair.Value.Transform.GetLocation();
		SkeletalSocket->RelativeRotation = Pair.Value.Transform.GetRotation().Rotator();
		SkeletalSocket->RelativeScale = Pair.Value.Transform.GetScale3D();
		SkeletalMesh->Skeleton->Sockets.Add(SkeletalSocket);
	}



#if !WITH_EDITOR
	SkeletalMesh->PostLoad();
#endif

	return SkeletalMesh;
}

UAnimSequence* FglTFRuntimeParser::LoadSkeletalAnimation(USkeletalMesh* SkeletalMesh, const int32 AnimationIndex, const FglTFRuntimeSkeletalAnimationConfig& AnimationConfig)
{
	if (!SkeletalMesh)
	{
		return nullptr;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return nullptr;
	}

	if (AnimationIndex >= JsonAnimations->Num())
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[AnimationIndex]->AsObject();
	if (!JsonAnimationObject)
	{
		return nullptr;
	}

	float Duration;
	int32 NumFrames;

	TMap<FString, FRawAnimSequenceTrack> Tracks;
	if (!LoadSkeletalAnimation_Internal(JsonAnimationObject.ToSharedRef(), Tracks, Duration, NumFrames))
	{
		return nullptr;
	}

	UAnimSequence* AnimSequence = NewObject<UAnimSequence>(GetTransientPackage(), NAME_None, RF_Public);
	AnimSequence->SetSkeleton(SkeletalMesh->Skeleton);
	AnimSequence->SetPreviewMesh(SkeletalMesh);
	AnimSequence->SetRawNumberOfFrame(NumFrames);
	AnimSequence->SequenceLength = Duration;
	AnimSequence->bEnableRootMotion = AnimationConfig.bRootMotion;

	const TArray<FTransform> BonesPoses = AnimSequence->GetSkeleton()->GetReferenceSkeleton().GetRefBonePose();

	/*
	bool bTrueRootFound = false;

	for (TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		FName BoneName = FName(Pair.Key);
		int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);

		if (BoneIndex == 1)
		{
			bTrueRootFound = true;
			break;
		}
	}

	if (!bTrueRootFound)
	{
		return nullptr;
	}

	FRawAnimSequenceTrack RootTrack;
	for (int32 FrameIndex = 0; FrameIndex < Tracks["Hips"].RotKeys.Num(); FrameIndex++)
	{
		RootTrack.PosKeys.Add(Tracks["Hips"].PosKeys[FrameIndex]);
		RootTrack.RotKeys.Add(FQuat::Identity);
		RootTrack.ScaleKeys.Add(FVector(1, 1, 1));

		Tracks["Hips"].PosKeys[FrameIndex] = Tracks["Hips"].PosKeys[0];
	}*/
	//Tracks.Add("Root", RootTrack);

#if !WITH_EDITOR
	UglTFAnimBoneCompressionCodec* CompressionCodec = NewObject<UglTFAnimBoneCompressionCodec>();
	CompressionCodec->Tracks.AddDefaulted(Tracks.Num());
#endif

	// tracks here will be already sanitized
	for (TPair<FString, FRawAnimSequenceTrack>& Pair : Tracks)
	{
		FName BoneName = FName(Pair.Key);
		int32 BoneIndex = AnimSequence->GetSkeleton()->GetReferenceSkeleton().FindBoneIndex(BoneName);
		if (BoneIndex == INDEX_NONE)
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to find bone %s"), *Pair.Key);
			continue;
		}

		UE_LOG(LogTemp, Warning, TEXT("Found %s at %d"), *BoneName.ToString(), BoneIndex);

		if (BoneIndex == 0)
		{
			if (AnimationConfig.RootNodeIndex > INDEX_NONE)
			{
				FglTFRuntimeNode AnimRootNode;
				if (!LoadNode(AnimationConfig.RootNodeIndex, AnimRootNode))
				{
					return nullptr;
				}
				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
					FVector Pos = Pair.Value.PosKeys[FrameIndex];
					FQuat Quat = Pair.Value.RotKeys[FrameIndex];
					FVector Scale = Pair.Value.ScaleKeys[FrameIndex];

					FTransform FrameTransform = FTransform(Quat, Pos, Scale) * AnimRootNode.Transform;

					Pair.Value.PosKeys[FrameIndex] = FrameTransform.GetLocation();
					Pair.Value.RotKeys[FrameIndex] = FrameTransform.GetRotation();
					Pair.Value.ScaleKeys[FrameIndex] = FrameTransform.GetScale3D();
				}
			}

			if (AnimationConfig.bRemoveRootMotion)
			{
				for (int32 FrameIndex = 0; FrameIndex < Pair.Value.RotKeys.Num(); FrameIndex++)
				{
					Pair.Value.PosKeys[FrameIndex] = Pair.Value.PosKeys[0];
				}
			}
		}

#if WITH_EDITOR
		AnimSequence->AddNewRawTrack(BoneName, &Pair.Value);
#else
		CompressionCodec->Tracks[BoneIndex] = Pair.Value;
		AnimSequence->CompressedData.CompressedTrackToSkeletonMapTable.Add(FTrackToSkeletonMap(BoneIndex));
#endif

	}

#if WITH_EDITOR
	AnimSequence->PostProcessSequence();
#else
	AnimSequence->CompressedData.CompressedDataStructure = MakeUnique<FUECompressedAnimData>();
	AnimSequence->CompressedData.BoneCompressionCodec = CompressionCodec;
	AnimSequence->PostLoad();
#endif

	return AnimSequence;
}

bool FglTFRuntimeParser::LoadSkeletalAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, TMap<FString, FRawAnimSequenceTrack>& Tracks, float& Duration, int32& NumFrames)
{

	const TArray<TSharedPtr<FJsonValue>>* JsonSamplers;
	if (!JsonAnimationObject->TryGetArrayField("samplers", JsonSamplers))
	{
		return false;
	}

	Duration = 0.f;

	TArray<TPair<TArray<float>, TArray<FVector4>>> Samplers;

	for (int32 SamplerIndex = 0; SamplerIndex < JsonSamplers->Num(); SamplerIndex++)
	{
		TSharedPtr<FJsonObject> JsonSamplerObject = (*JsonSamplers)[SamplerIndex]->AsObject();
		if (!JsonSamplerObject)
			return false;

		TArray<float> Timeline;
		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "input", Timeline, { 5126 }, false))
		{
			UE_LOG(LogTemp, Error, TEXT("unable to retrieve \"input\" from sampler"));
			return false;
		}

		TArray<FVector4> Values;
		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "output", Values, { 3, 4 }, { 5126, 5120, 5121, 5122, 5123 }, true))
		{
			UE_LOG(LogTemp, Error, TEXT("unable to retrieve \"output\" from sampler"));
			return false;
		}

		FString SamplerInterpolation;
		if (!JsonSamplerObject->TryGetStringField("interpolation", SamplerInterpolation))
		{
			SamplerInterpolation = "LINEAR";
		}

		//UE_LOG(LogTemp, Error, TEXT("Found sample with %d keyframes and %d values"), Timeline.Num(), Values.Num());

		if (Timeline.Num() != Values.Num())
			return false;

		// get animation valid duration
		for (float Time : Timeline)
		{
			if (Time > Duration)
			{
				Duration = Time;
			}
		}

		Samplers.Add(TPair<TArray<float>, TArray<FVector4>>(Timeline, Values));
	}


	const TArray<TSharedPtr<FJsonValue>>* JsonChannels;
	if (!JsonAnimationObject->TryGetArrayField("channels", JsonChannels))
	{
		return false;
	}

	for (int32 ChannelIndex = 0; ChannelIndex < JsonChannels->Num(); ChannelIndex++)
	{
		TSharedPtr<FJsonObject> JsonChannelObject = (*JsonChannels)[ChannelIndex]->AsObject();
		if (!JsonChannelObject)
			return false;

		int32 Sampler;
		if (!JsonChannelObject->TryGetNumberField("sampler", Sampler))
			return false;

		if (Sampler >= Samplers.Num())
			return false;

		const TSharedPtr<FJsonObject>* JsonTargetObject;
		if (!JsonChannelObject->TryGetObjectField("target", JsonTargetObject))
		{
			return false;
		}

		int64 NodeIndex;
		if (!(*JsonTargetObject)->TryGetNumberField("node", NodeIndex))
		{
			return false;
		}

		FglTFRuntimeNode Node;
		if (!LoadNode(NodeIndex, Node))
			return false;

		FString Path;
		if (!(*JsonTargetObject)->TryGetStringField("path", Path))
		{
			return false;
		}

		NumFrames = Duration * 30;
		float FrameDelta = 1.f / 30;

		if (Path == "rotation")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Samplers[Sampler].Key, FrameBase, FirstIndex, SecondIndex);
				FVector4 FirstQuatV = Samplers[Sampler].Value[FirstIndex];
				FVector4 SecondQuatV = Samplers[Sampler].Value[SecondIndex];
				FQuat FirstQuat = { FirstQuatV.X, FirstQuatV.Y, FirstQuatV.Z, FirstQuatV.W };
				FQuat SecondQuat = { SecondQuatV.X, SecondQuatV.Y, SecondQuatV.Z, SecondQuatV.W };
				FMatrix FirstMatrix = SceneBasis.Inverse() * FRotationMatrix(FirstQuat.Rotator()) * SceneBasis;
				FMatrix SecondMatrix = SceneBasis.Inverse() * FRotationMatrix(SecondQuat.Rotator()) * SceneBasis;
				FirstQuat = FirstMatrix.ToQuat();
				SecondQuat = SecondMatrix.ToQuat();
				FQuat AnimQuat = FMath::Lerp(FirstQuat, SecondQuat, Alpha);
				Track.RotKeys.Add(AnimQuat);
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "translation")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			//UE_LOG(LogTemp, Error, TEXT("Found translation for %s"), *Node.Name);

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Samplers[Sampler].Key, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Samplers[Sampler].Value[FirstIndex];
				FVector4 Second = Samplers[Sampler].Value[SecondIndex];
				FVector AnimLocation = SceneBasis.TransformPosition(FMath::Lerp(First, Second, Alpha)) * SceneScale;
				Track.PosKeys.Add(AnimLocation);
				FrameBase += FrameDelta;
			}
		}
		else if (Path == "scale")
		{
			if (!Tracks.Contains(Node.Name))
			{
				Tracks.Add(Node.Name, FRawAnimSequenceTrack());
			}

			//UE_LOG(LogTemp, Error, TEXT("Found translation for %s"), *Node.Name);

			FRawAnimSequenceTrack& Track = Tracks[Node.Name];

			float FrameBase = 0.f;
			for (int32 Frame = 0; Frame < NumFrames; Frame++)
			{
				int32 FirstIndex;
				int32 SecondIndex;
				float Alpha = FindBestFrames(Samplers[Sampler].Key, FrameBase, FirstIndex, SecondIndex);
				FVector4 First = Samplers[Sampler].Value[FirstIndex];
				FVector4 Second = Samplers[Sampler].Value[SecondIndex];
				Track.ScaleKeys.Add(FMath::Lerp(First, Second, Alpha));
				FrameBase += FrameDelta;
			}
		}
	}

	return true;
}

bool FglTFRuntimeParser::HasRoot(int32 Index, int32 RootIndex)
{
	if (Index == RootIndex)
		return true;

	FglTFRuntimeNode Node;
	if (!LoadNode(Index, Node))
		return false;

	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!LoadNode(Node.ParentIndex, Node))
			return false;
		if (Node.Index == RootIndex)
			return true;
	}

	return false;
}

int32 FglTFRuntimeParser::FindTopRoot(int32 Index)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(Index, Node))
		return INDEX_NONE;
	while (Node.ParentIndex != INDEX_NONE)
	{
		if (!LoadNode(Node.ParentIndex, Node))
			return INDEX_NONE;
	}

	return Node.Index;
}

int32 FglTFRuntimeParser::FindCommonRoot(TArray<int32> Indices)
{
	int32 CurrentRootIndex = Indices[0];
	bool bTryNextParent = true;

	while (bTryNextParent)
	{
		FglTFRuntimeNode Node;
		if (!LoadNode(CurrentRootIndex, Node))
			return INDEX_NONE;

		bTryNextParent = false;
		for (int32 Index : Indices)
		{
			if (!HasRoot(Index, CurrentRootIndex))
			{
				bTryNextParent = true;
				CurrentRootIndex = Node.ParentIndex;
				break;
			}
		}
	}

	return CurrentRootIndex;
}

bool FglTFRuntimeParser::FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	//RootTransform = FTransform::Identity;

	// get the list of valid joints	
	const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
	TArray<int32> Joints;
	if (JsonSkinObject->TryGetArrayField("joints", JsonJoints))
	{
		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
				return false;
			Joints.Add(JointIndex);
		}
	}

	if (Joints.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("No Joints available"));
		return false;
	}

	// fill the root bone
	FglTFRuntimeNode RootNode;
	int64 RootBoneIndex;

	RootBoneIndex = FindCommonRoot(Joints);

	if (RootBoneIndex == INDEX_NONE)
		return false;

	if (!LoadNode(RootBoneIndex, RootNode))
	{
		UE_LOG(LogTemp, Error, TEXT("Unable to load joint node"));
		return false;
	}

	TMap<int32, FMatrix> InverseBindMatricesMap;
	int64 inverseBindMatricesIndex;
	if (JsonSkinObject->TryGetNumberField("inverseBindMatrices", inverseBindMatricesIndex))
	{
		TArray<uint8> InverseBindMatricesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(inverseBindMatricesIndex, ComponentType, Stride, Elements, ElementSize, Count, InverseBindMatricesBytes))
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to load accessor: %lld"), inverseBindMatricesIndex);
			return false;
		}

		if (Elements != 16 && ComponentType != 5126)
			return false;

		for (int64 i = 0; i < Count; i++)
		{
			FMatrix Matrix;
			int64 MatrixIndex = i * Stride;

			float* MatrixCell = (float*)&InverseBindMatricesBytes[MatrixIndex];

			for (int32 j = 0; j < 16; j++)
			{
				float Value = MatrixCell[j];

				Matrix.M[j / 4][j % 4] = Value;
			}

			if (i < Joints.Num())
			{
				InverseBindMatricesMap.Add(Joints[i], Matrix);
			}
		}
	}

	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	// now traverse from the root and check if the node is in the "joints" list
	if (!TraverseJoints(Modifier, INDEX_NONE, RootNode, Joints, BoneMap, InverseBindMatricesMap))
		return false;

	return true;
}

bool FglTFRuntimeParser::TraverseJoints(FReferenceSkeletonModifier& Modifier, int32 Parent, FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap)
{
	// add fake root bone
	/*if (Parent == INDEX_NONE)
	{
		Modifier.Add(FMeshBoneInfo("Root", "Root", INDEX_NONE), FTransform::Identity);
		Parent = 0;
	}*/

	FName BoneName = FName(*Node.Name);

	// first check if a bone with the same name exists, on collision, append an underscore
	int32 CollidingIndex = Modifier.FindBoneIndex(BoneName);
	while (CollidingIndex != INDEX_NONE)
	{
		UE_LOG(LogTemp, Error, TEXT("bone %s already exists"), *BoneName.ToString());
		return false;
	}

	FTransform Transform = Node.Transform;
	if (InverseBindMatricesMap.Contains(Node.Index))
	{
		FMatrix M = InverseBindMatricesMap[Node.Index].Inverse();
		if (Node.ParentIndex != INDEX_NONE && Joints.Contains(Node.ParentIndex))
		{
			M *= InverseBindMatricesMap[Node.ParentIndex];
		}

		/*UE_LOG(LogTemp, Error, TEXT("***** %d *****"), Node.Index);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[0][0], M.M[0][1], M.M[0][2], M.M[0][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[1][0], M.M[1][1], M.M[1][2], M.M[1][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[2][0], M.M[2][1], M.M[2][2], M.M[2][3]);
		UE_LOG(LogTemp, Error, TEXT("%f %f %f %f"), M.M[3][0], M.M[3][1], M.M[3][2], M.M[3][3]);*/


		M.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
		Transform = FTransform(SceneBasis.Inverse() * M * SceneBasis);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("No bind transform for node %d %s"), Node.Index, *Node.Name);
	}

	Modifier.Add(FMeshBoneInfo(BoneName, Node.Name, Parent), Transform);

	int32 NewParentIndex = Modifier.FindBoneIndex(BoneName);
	// something horrible happened...
	if (NewParentIndex == INDEX_NONE)
		return false;

	if (Joints.Contains(Node.Index))
	{
		BoneMap.Add(Joints.IndexOfByKey(Node.Index), BoneName);
	}

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		FglTFRuntimeNode ChildNode;
		if (!LoadNode(ChildIndex, ChildNode))
			return false;

		if (!TraverseJoints(Modifier, NewParentIndex, ChildNode, Joints, BoneMap, InverseBindMatricesMap))
			return false;
	}

	return true;
}

bool FglTFRuntimeParser::LoadPrimitives(const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives, TArray<FglTFRuntimePrimitive>& Primitives, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
			return false;

		FglTFRuntimePrimitive Primitive;
		if (!LoadPrimitive(JsonPrimitiveObject.ToSharedRef(), Primitive, MaterialsConfig))
			return false;

		Primitives.Add(Primitive);
	}
	return true;
}

bool FglTFRuntimeParser::LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	const TSharedPtr<FJsonObject>* JsonAttributesObject;
	if (!JsonPrimitiveObject->TryGetObjectField("attributes", JsonAttributesObject))
		return false;

	// POSITION is required for generating a valid Mesh
	if (!(*JsonAttributesObject)->HasField("POSITION"))
	{
		UE_LOG(LogTemp, Error, TEXT("Error loading position"));
		return false;
	}

	if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "POSITION", Primitive.Positions,
		{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector {return SceneBasis.TransformPosition(Value) * SceneScale; }))
		return false;

	if ((*JsonAttributesObject)->HasField("NORMAL"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "NORMAL", Primitive.Normals,
			{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading normals"));
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField("TANGENT"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TANGENT", Primitive.Tangents,
			{ 4 }, { 5126 }, false, [&](FVector4 Value) -> FVector4 { return SceneBasis.TransformVector(Value); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading tangents"));
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_0"))
	{
		TArray<FVector2D> UV;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_0", UV,
			{ 2 }, { 5126, 5121, 5123 }, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading uvs 0"));
			return false;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_1"))
	{
		TArray<FVector2D> UV;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_1", UV,
			{ 2 }, { 5126, 5121, 5123 }, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading uvs 1"));
			return false;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField("JOINTS_0"))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "JOINTS_0", Joints,
			{ 4 }, { 5121, 5123 }, false))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading joints 0"));
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField("JOINTS_1"))
	{
		TArray<FglTFRuntimeUInt16Vector4> Joints;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "JOINTS_1", Joints,
			{ 4 }, { 5121, 5123 }, false))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading joints 1"));
			return false;
		}

		Primitive.Joints.Add(Joints);
	}

	if ((*JsonAttributesObject)->HasField("WEIGHTS_0"))
	{
		TArray<FVector4> Weights;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "WEIGHTS_0", Weights,
			{ 4 }, { 5126, 5121, 5123 }, true))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading weights 0"));
			return false;
		}
		Primitive.Weights.Add(Weights);
	}

	if ((*JsonAttributesObject)->HasField("WEIGHTS_1"))
	{
		TArray<FVector4> Weights;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "WEIGHTS_1", Weights,
			{ 4 }, { 5126, 5121, 5123 }, true))
		{
			UE_LOG(LogTemp, Error, TEXT("Error loading weights 1"));
			return false;
		}
		Primitive.Weights.Add(Weights);
	}

	int64 IndicesAccessorIndex;
	if (JsonPrimitiveObject->TryGetNumberField("indices", IndicesAccessorIndex))
	{
		TArray<uint8> IndicesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		if (!GetAccessor(IndicesAccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, IndicesBytes))
		{
			UE_LOG(LogTemp, Error, TEXT("Unable to load accessor: %lld"), IndicesAccessorIndex);
			return false;
		}

		if (Elements != 1)
			return false;

		for (int64 i = 0; i < Count; i++)
		{
			int64 IndexIndex = i * Stride;

			uint32 VertexIndex;
			if (ComponentType == 5121)
			{
				VertexIndex = IndicesBytes[IndexIndex];
			}
			else if (ComponentType == 5123)
			{
				uint16* IndexPtr = (uint16*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else if (ComponentType == 5125)
			{
				uint32* IndexPtr = (uint32*)&(IndicesBytes[IndexIndex]);
				VertexIndex = *IndexPtr;
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("Invalid component type for indices: %lld"), ComponentType);
				return false;
			}

			Primitive.Indices.Add(VertexIndex);
		}
	}
	else
	{
		for (int32 VertexIndex = 0; VertexIndex < Primitive.Positions.Num(); VertexIndex++)
		{
			Primitive.Indices.Add(VertexIndex);
		}
	}


	int64 MaterialIndex;
	if (JsonPrimitiveObject->TryGetNumberField("material", MaterialIndex))
	{
		Primitive.Material = LoadMaterial(MaterialIndex, MaterialsConfig);
		if (!Primitive.Material)
		{
			UE_LOG(LogTemp, Error, TEXT("unable to load material %lld"), MaterialIndex);
			return false;
		}
	}
	else
	{
		Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	return true;
}

UStaticMesh* FglTFRuntimeParser::LoadStaticMesh_Internal(TSharedRef<FJsonObject> JsonMeshObject)
{
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	// no meshes ?
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		return nullptr;
	}

	FglTFRuntimeMaterialsConfig MaterialsConfig;

	TArray<FglTFRuntimePrimitive> Primitives;
	if (!LoadPrimitives(JsonPrimitives, Primitives, MaterialsConfig))
		return nullptr;

	UStaticMesh* StaticMesh = NewObject<UStaticMesh>(GetTransientPackage(), NAME_None, RF_Public);

	UStaticMeshDescription* MeshDescription = UStaticMesh::CreateStaticMeshDescription();

	TArray<FStaticMaterial> StaticMaterials;

	int32 NumUVs = 1;
	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		if (Primitive.UVs.Num() > NumUVs)
		{
			NumUVs = Primitive.UVs.Num();
		}
	}

	for (FglTFRuntimePrimitive& Primitive : Primitives)
	{
		FPolygonGroupID PolygonGroupID = MeshDescription->CreatePolygonGroup();

		TPolygonGroupAttributesRef<FName> PolygonGroupMaterialSlotNames = MeshDescription->GetPolygonGroupMaterialSlotNames();
		PolygonGroupMaterialSlotNames[PolygonGroupID] = Primitive.Material->GetFName();
		FStaticMaterial StaticMaterial(Primitive.Material, Primitive.Material->GetFName());
		StaticMaterial.UVChannelData.bInitialized = true;
		StaticMaterials.Add(StaticMaterial);

		TVertexAttributesRef<FVector> PositionsAttributesRef = MeshDescription->GetVertexPositions();
		TVertexInstanceAttributesRef<FVector> NormalsInstanceAttributesRef = MeshDescription->GetVertexInstanceNormals();
		TVertexInstanceAttributesRef<FVector> TangentsInstanceAttributesRef = MeshDescription->GetVertexInstanceTangents();
		TVertexInstanceAttributesRef<FVector2D> UVsInstanceAttributesRef = MeshDescription->GetVertexInstanceUVs();

		UVsInstanceAttributesRef.SetNumIndices(NumUVs);

		TArray<FVertexInstanceID> VertexInstancesIDs;
		TArray<FVertexID> VerticesIDs;
		TArray<FVertexID> TriangleVerticesIDs;


		for (FVector& Position : Primitive.Positions)
		{
			FVertexID VertexID = MeshDescription->CreateVertex();
			PositionsAttributesRef[VertexID] = Position;
			VerticesIDs.Add(VertexID);
		}

		for (uint32 VertexIndex : Primitive.Indices)
		{
			if (VertexIndex >= (uint32)VerticesIDs.Num())
				return false;

			FVertexInstanceID NewVertexInstanceID = MeshDescription->CreateVertexInstance(VerticesIDs[VertexIndex]);
			if (Primitive.Normals.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Normals.Num())
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					NormalsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Normals[VertexIndex];
				}
			}

			if (Primitive.Tangents.Num() > 0)
			{
				if (VertexIndex >= (uint32)Primitive.Tangents.Num())
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = FVector::ZeroVector;
				}
				else
				{
					TangentsInstanceAttributesRef[NewVertexInstanceID] = Primitive.Tangents[VertexIndex];
				}
			}

			for (int32 UVIndex = 0; UVIndex < Primitive.UVs.Num(); UVIndex++)
			{
				if (VertexIndex >= (uint32)Primitive.UVs[UVIndex].Num())
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, FVector2D::ZeroVector);
				}
				else
				{
					UVsInstanceAttributesRef.Set(NewVertexInstanceID, UVIndex, Primitive.UVs[UVIndex][VertexIndex]);
				}
			}

			VertexInstancesIDs.Add(NewVertexInstanceID);
			TriangleVerticesIDs.Add(VerticesIDs[VertexIndex]);

			if (VertexInstancesIDs.Num() == 3)
			{
				// degenerate ?
				if (TriangleVerticesIDs[0] == TriangleVerticesIDs[1] ||
					TriangleVerticesIDs[1] == TriangleVerticesIDs[2] ||
					TriangleVerticesIDs[0] == TriangleVerticesIDs[2])
				{
					VertexInstancesIDs.Empty();
					TriangleVerticesIDs.Empty();
					continue;
				}

				TArray<FEdgeID> Edges;
				// fix winding
				//VertexInstancesIDs.Swap(1, 2);
				FTriangleID TriangleID = MeshDescription->CreateTriangle(PolygonGroupID, VertexInstancesIDs, Edges);
				if (TriangleID == FTriangleID::Invalid)
				{
					return false;
				}
				VertexInstancesIDs.Empty();
				TriangleVerticesIDs.Empty();
			}
		}

	}

	StaticMesh->StaticMaterials = StaticMaterials;

	TArray<UStaticMeshDescription*> MeshDescriptions = { MeshDescription };
	StaticMesh->BuildFromStaticMeshDescriptions(MeshDescriptions, false);

	return StaticMesh;
}

bool FglTFRuntimeParser::GetBuffer(int32 Index, TArray<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	// first check cache
	if (BuffersCache.Contains(Index))
	{
		Bytes = BuffersCache[Index];
		return true;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonBuffers;

	// no buffers ?
	if (!Root->TryGetArrayField("buffers", JsonBuffers))
	{
		return false;
	}

	if (Index >= JsonBuffers->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferObject = (*JsonBuffers)[Index]->AsObject();
	if (!JsonBufferObject)
		return false;

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField("byteLength", ByteLength))
		return false;

	FString Uri;
	if (!JsonBufferObject->TryGetStringField("uri", Uri))
		return false;

	if (ParseBase64Uri(Uri, Bytes))
	{
		BuffersCache.Add(Index, Bytes);
		return true;
	}

	return false;
}

bool FglTFRuntimeParser::ParseBase64Uri(const FString Uri, TArray<uint8>& Bytes)
{
	// check it is a valid base64 data uri
	if (!Uri.StartsWith("data:"))
		return false;

	FString Base64Signature = ";base64,";

	int32 StringIndex = Uri.Find(Base64Signature, ESearchCase::IgnoreCase, ESearchDir::FromStart, 5);

	if (StringIndex < 5)
		return false;

	StringIndex += Base64Signature.Len();

	return FBase64::Decode(Uri.Mid(StringIndex), Bytes);
}

bool FglTFRuntimeParser::GetBufferView(int32 Index, TArray<uint8>& Bytes, int64& Stride)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonBufferViews;

	// no bufferViews ?
	if (!Root->TryGetArrayField("bufferViews", JsonBufferViews))
	{
		return false;
	}

	if (Index >= JsonBufferViews->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonBufferObject = (*JsonBufferViews)[Index]->AsObject();
	if (!JsonBufferObject)
		return false;


	int64 BufferIndex;
	if (!JsonBufferObject->TryGetNumberField("buffer", BufferIndex))
		return false;

	TArray<uint8> WholeData;
	if (!GetBuffer(BufferIndex, WholeData))
		return false;

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField("byteLength", ByteLength))
		return false;

	int64 ByteOffset;
	if (!JsonBufferObject->TryGetNumberField("byteOffset", ByteOffset))
		ByteOffset = 0;

	if (!JsonBufferObject->TryGetNumberField("byteStride", Stride))
		Stride = 0;

	if (ByteOffset + ByteLength > WholeData.Num())
		return false;

	Bytes.Append(&WholeData[ByteOffset], ByteLength);
	return true;
}

bool FglTFRuntimeParser::GetAccessor(int32 Index, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, TArray<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	const TArray<TSharedPtr<FJsonValue>>* JsonAccessors;

	// no accessors ?
	if (!Root->TryGetArrayField("accessors", JsonAccessors))
	{
		return false;
	}

	if (Index >= JsonAccessors->Num())
	{
		return false;
	}

	TSharedPtr<FJsonObject> JsonAccessorObject = (*JsonAccessors)[Index]->AsObject();
	if (!JsonAccessorObject)
		return false;

	bool bInitWithZeros = false;

	int64 BufferViewIndex;
	if (!JsonAccessorObject->TryGetNumberField("bufferView", BufferViewIndex))
		bInitWithZeros = true;

	int64 ByteOffset;
	if (!JsonAccessorObject->TryGetNumberField("byteOffset", ByteOffset))
		ByteOffset = 0;

	if (!JsonAccessorObject->TryGetNumberField("componentType", ComponentType))
		return false;

	if (!JsonAccessorObject->TryGetNumberField("count", Count))
		return false;

	FString Type;
	if (!JsonAccessorObject->TryGetStringField("type", Type))
		return false;

	ElementSize = GetComponentTypeSize(ComponentType);
	if (ElementSize == 0)
		return false;

	Elements = GetTypeSize(Type);
	if (Elements == 0)
		return false;

	uint64 FinalSize = ElementSize * Elements * Count;

	if (bInitWithZeros)
	{
		Bytes.AddZeroed(FinalSize);
		return true;
	}

	if (!GetBufferView(BufferViewIndex, Bytes, Stride))
		return false;

	if (Stride == 0)
	{
		Stride = ElementSize * Elements;
	}

	FinalSize = Stride * Count;

	if (ByteOffset > 0)
	{
		TArray<uint8> OffsetBytes;
		OffsetBytes.Append(&Bytes[ByteOffset], FinalSize);
		Bytes = OffsetBytes;
	}

	return (FinalSize <= Bytes.Num());
}

int64 FglTFRuntimeParser::GetComponentTypeSize(const int64 ComponentType) const
{
	switch (ComponentType)
	{
	case(5120):
		return 1;
	case(5121):
		return 1;
	case(5122):
		return 2;
	case(5123):
		return 2;
	case(5125):
		return 4;
	case(5126):
		return 4;
	default:
		break;
	}

	return 0;
}

int64 FglTFRuntimeParser::GetTypeSize(const FString Type) const
{
	if (Type == "SCALAR")
		return 1;
	else if (Type == "VEC2")
		return 2;
	else if (Type == "VEC3")
		return 3;
	else if (Type == "VEC4")
		return 4;
	else if (Type == "MAT2")
		return 4;
	else if (Type == "MAT3")
		return 9;
	else if (Type == "MAT4")
		return 16;

	return 0;
}

void FglTFRuntimeParser::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObjects(StaticMeshesCache);
	Collector.AddReferencedObjects(MaterialsCache);
	Collector.AddReferencedObjects(SkeletonsCache);
	Collector.AddReferencedObjects(SkeletalMeshesCache);
	Collector.AddReferencedObjects(TexturesCache);
	Collector.AddReferencedObjects(MaterialsMap);
}

float FglTFRuntimeParser::FindBestFrames(TArray<float> FramesTimes, float WantedTime, int32& FirstIndex, int32& SecondIndex)
{
	SecondIndex = INDEX_NONE;
	// first search for second (higher value)
	for (int32 i = 0; i < FramesTimes.Num(); i++)
	{
		float TimeValue = FramesTimes[i];
		if (TimeValue >= WantedTime)
		{
			SecondIndex = i;
			break;
		}
	}

	// not found ? use the last value
	if (SecondIndex == INDEX_NONE)
	{
		SecondIndex = FramesTimes.Num() - 1;
	}

	if (SecondIndex == 0)
	{
		FirstIndex = 0;
		return 1.f;
	}

	FirstIndex = SecondIndex - 1;

	return (WantedTime - FramesTimes[FirstIndex]) / FramesTimes[SecondIndex];
}