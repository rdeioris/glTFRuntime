// Copyright 2020-2021, Roberto De Ioris.

#include "glTFRuntimeParser.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonSerializer.h"
#include "Animation/Skeleton.h"
#include "Materials/Material.h"
#include "Misc/Base64.h"
#include "Misc/Compression.h"
#include "Interfaces/IPluginManager.h"

DEFINE_LOG_CATEGORY(LogGLTFRuntime);

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromFilename(const FString& Filename, const FglTFRuntimeConfig& LoaderConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromFilename, FColor::Magenta);

	FString TruePath = Filename;

	if (LoaderConfig.bSearchContentDir)
	{
		TruePath = FPaths::Combine(FPaths::ProjectContentDir(), Filename);
	}

	if (!FPaths::FileExists(TruePath))
	{
		bool bAssetFound = false;
		for (const FString& PluginName : LoaderConfig.ContentPluginsToScan)
		{
			TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(PluginName);
			if (Plugin)
			{
				TruePath = FPaths::Combine(Plugin->GetContentDir(), Filename);
				if (FPaths::FileExists(TruePath))
				{
					bAssetFound = true;
					break;
				}
			}
		}

		if (!bAssetFound)
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to open file %s"), *Filename);
			return nullptr;
		}
	}

	TArray64<uint8> Content;
	if (!FFileHelper::LoadFileToArray(Content, *TruePath))
	{
		UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to load file %s"), *Filename);
		return nullptr;
	}

	TSharedPtr<FglTFRuntimeParser> Parser = FromData(Content.GetData(), Content.Num(), LoaderConfig);

	if (Parser && LoaderConfig.bAllowExternalFiles)
	{
		// allows to load external files
		Parser->BaseDirectory = FPaths::GetPath(TruePath);
	}

	return Parser;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromData(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromData, FColor::Magenta);

	// required for Gzip;
	TArray<uint8> UncompressedData;

	// Gzip Compressed ? 10 bytes header and 8 bytes footer
	if (DataNum > 18 && DataPtr[0] == 0x1F && DataPtr[1] == 0x8B && DataPtr[2] == 0x08)
	{
		uint32* GzipOriginalSize = (uint32*)(&DataPtr[DataNum - 4]);
		int64 StartOfBuffer = 10;
		// FEXTRA
		if (DataPtr[3] & 0x04)
		{
			uint16* FExtraXLen = (uint16*)(&DataPtr[StartOfBuffer]);
			if (StartOfBuffer + 2 >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FEXTRA header."));
				return nullptr;
			}
			StartOfBuffer += 2 + *FExtraXLen;
			if (StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FEXTRA XLEN."));
				return nullptr;
			}
		}

		// FNAME
		if (DataPtr[3] & 0x08)
		{
			while (DataPtr[StartOfBuffer] != 0)
			{
				StartOfBuffer++;
				if (StartOfBuffer >= DataNum)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FNAME header."));
					return nullptr;
				}
			}
			if (++StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FNAME header."));
				return nullptr;
			}
		}

		// FCOMMENT
		if (DataPtr[3] & 0x10)
		{
			while (DataPtr[StartOfBuffer] != 0)
			{
				StartOfBuffer++;
				if (StartOfBuffer >= DataNum)
				{
					UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FCOMMENT header."));
					return nullptr;
				}
			}
			if (++StartOfBuffer >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FCOMMENT header."));
				return nullptr;
			}
		}

		// FHCRC
		if (DataPtr[3] & 0x02)
		{
			if (StartOfBuffer + 2 >= DataNum)
			{
				UE_LOG(LogGLTFRuntime, Error, TEXT("Invalid Gzip FHCRC header."));
				return nullptr;
			}
			StartOfBuffer += 2;
		}

		UncompressedData.AddUninitialized(*GzipOriginalSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, UncompressedData.GetData(), *GzipOriginalSize, &DataPtr[StartOfBuffer], DataNum - StartOfBuffer - 8, COMPRESS_NoFlags, -15))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to uncompress Gzip data."));
			return nullptr;
		}

		DataPtr = UncompressedData.GetData();
		DataNum = *GzipOriginalSize;
	}

	// Zip archive ?
	TSharedPtr<FglTFRuntimeZipFile> ZipFile = nullptr;
	TArray64<uint8> UnzippedData;
	if (DataNum > 4 && DataPtr[0] == 0x50 && DataPtr[1] == 0x4b && DataPtr[2] == 0x03 && DataPtr[3] == 0x04)
	{
		ZipFile = MakeShared<FglTFRuntimeZipFile>();
		if (!ZipFile->FromData(DataPtr, DataNum))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to parse Zip archive."));
			return nullptr;
		}

		FString Filename = LoaderConfig.ArchiveEntryPoint;

		if (Filename.IsEmpty())
		{
			TArray<FString> Extensions;
			LoaderConfig.ArchiveAutoEntryPointExtensions.ParseIntoArray(Extensions, TEXT(" "), true);
			for (const FString& Extension : Extensions)
			{
				Filename = ZipFile->GetFirstFilenameByExtension(Extension);
				if (!Filename.IsEmpty())
				{
					break;
				}
			}
		}

		if (Filename.IsEmpty())
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to find entry point from Zip archive."), *Filename);
			return nullptr;
		}

		if (!ZipFile->GetFileContent(Filename, UnzippedData))
		{
			UE_LOG(LogGLTFRuntime, Error, TEXT("Unable to get %s from Zip archive."), *Filename);
			return nullptr;
		}

		if (UnzippedData.Num() > 0)
		{
			DataPtr = UnzippedData.GetData();
			DataNum = UnzippedData.Num();
		}
	}

	// detect binary format
	if (DataNum > 20)
	{
		if (DataPtr[0] == 0x67 &&
			DataPtr[1] == 0x6C &&
			DataPtr[2] == 0x54 &&
			DataPtr[3] == 0x46)
		{
			return FromBinary(DataPtr, DataNum, LoaderConfig, ZipFile);
		}
	}

	if (DataNum > 0 && DataNum <= INT32_MAX)
	{
		FString JsonData;
		FFileHelper::BufferToString(JsonData, DataPtr, (int32)DataNum);
		return FromString(JsonData, LoaderConfig, ZipFile);
	}

	return nullptr;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromString(const FString& JsonData, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeZipFile> InZipFile)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromString, FColor::Magenta);

	TSharedPtr<FJsonValue> RootValue;

	TSharedRef<TJsonReader<TCHAR>> JsonReader = TJsonReaderFactory<TCHAR>::Create(JsonData);
	if (!FJsonSerializer::Deserialize(JsonReader, RootValue))
	{
		return nullptr;
	}

	TSharedPtr<FJsonObject> JsonObject = RootValue->AsObject();
	if (!JsonObject)
		return nullptr;

	TSharedPtr<FglTFRuntimeParser> Parser = MakeShared<FglTFRuntimeParser>(JsonObject.ToSharedRef(), LoaderConfig.GetMatrix(), LoaderConfig.SceneScale);

	if (Parser)
	{
		if (LoaderConfig.bAllowExternalFiles && !LoaderConfig.OverrideBaseDirectory.IsEmpty())
		{
			if (LoaderConfig.bOverrideBaseDirectoryFromContentDir)
			{
				Parser->BaseDirectory = FPaths::Combine(FPaths::ProjectContentDir(), LoaderConfig.OverrideBaseDirectory);
			}
			else
			{
				Parser->BaseDirectory = LoaderConfig.OverrideBaseDirectory;
			}
		}

		Parser->ZipFile = InZipFile;
	}

	return Parser;
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromBinary(const uint8* DataPtr, int64 DataNum, const FglTFRuntimeConfig& LoaderConfig, TSharedPtr<FglTFRuntimeZipFile> InZipFile)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_FromBinary, FColor::Magenta);

	FString JsonData;
	TArray64<uint8> BinaryBuffer;

	bool bJsonFound = false;
	bool bBinaryFound = false;
	int64 BlobIndex = 12;

	while (BlobIndex < DataNum)
	{
		if (BlobIndex + 8 > DataNum)
		{
			return nullptr;
		}

		uint32* ChunkLength = (uint32*)&DataPtr[BlobIndex];
		uint32* ChunkType = (uint32*)&DataPtr[BlobIndex + 4];

		BlobIndex += 8;

		if ((BlobIndex + *ChunkLength) > DataNum)
		{
			return nullptr;
		}

		if (*ChunkType == 0x4E4F534A && !bJsonFound)
		{
			bJsonFound = true;
			FFileHelper::BufferToString(JsonData, &DataPtr[BlobIndex], *ChunkLength);
		}

		else if (*ChunkType == 0x004E4942 && !bBinaryFound)
		{
			bBinaryFound = true;
			BinaryBuffer.Append(&DataPtr[BlobIndex], *ChunkLength);
		}

		BlobIndex += *ChunkLength;
	}

	if (!bJsonFound)
	{
		return nullptr;
	}

	TSharedPtr<FglTFRuntimeParser> Parser = FromString(JsonData, LoaderConfig, InZipFile);

	if (Parser)
	{
		if (bBinaryFound)
		{
			Parser->SetBinaryBuffer(BinaryBuffer);
		}
	}

	return Parser;
}

FglTFRuntimeParser::FglTFRuntimeParser(TSharedRef<FJsonObject> JsonObject, const FMatrix& InSceneBasis, float InSceneScale) : Root(JsonObject), SceneBasis(InSceneBasis), SceneScale(InSceneScale)
{
	bAllNodesCached = false;

	UMaterialInterface* OpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeBase"));
	if (OpaqueMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, OpaqueMaterial);
	}

	UMaterialInterface* TranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTranslucent_Inst"));
	if (OpaqueMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, TranslucentMaterial);
	}

	UMaterialInterface* TwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSided_Inst"));
	if (TwoSidedMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, TwoSidedMaterial);
	}

	UMaterialInterface* TwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntimeTwoSidedTranslucent_Inst"));
	if (TwoSidedTranslucentMaterial)
	{
		MetallicRoughnessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, TwoSidedTranslucentMaterial);
	}

	// KHR_materials_pbrSpecularGlossiness
	UMaterialInterface* SGOpaqueMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_Base"));
	if (SGOpaqueMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::Opaque, SGOpaqueMaterial);
	}

	UMaterialInterface* SGTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_Translucent_Inst"));
	if (SGTranslucentMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::Translucent, SGTranslucentMaterial);
	}

	UMaterialInterface* SGTwoSidedMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_TwoSided_Inst"));
	if (SGTwoSidedMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSided, SGTwoSidedMaterial);
	}

	UMaterialInterface* SGTwoSidedTranslucentMaterial = LoadObject<UMaterialInterface>(nullptr, TEXT("/glTFRuntime/M_glTFRuntime_SG_TwoSidedTranslucent_Inst"));
	if (SGTwoSidedTranslucentMaterial)
	{
		SpecularGlossinessMaterialsMap.Add(EglTFRuntimeMaterialType::TwoSidedTranslucent, SGTwoSidedTranslucentMaterial);
	}

	JsonObject->TryGetStringArrayField("extensionsUsed", ExtensionsUsed);
	JsonObject->TryGetStringArrayField("extensionsRequired", ExtensionsRequired);
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

bool FglTFRuntimeParser::LoadNodesRecursive(const int32 NodeIndex, TArray<FglTFRuntimeNode>& Nodes)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		AddError("LoadNodesRecursive()", FString::Printf(TEXT("Unable to load node %d"), NodeIndex));
		return false;
	}

	Nodes.Add(Node);

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		if (!LoadNodesRecursive(ChildIndex, Nodes))
		{
			return false;
		}
	}

	return true;
}

int32 FglTFRuntimeParser::GetNumMeshes() const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField("meshes", JsonArray))
	{	
		return JsonArray->Num();
	}
	return 0;
}

int32 FglTFRuntimeParser::GetNumImages() const
{
	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (Root->TryGetArrayField("images", JsonArray))
	{
		return JsonArray->Num();
	}
	return 0;
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

bool FglTFRuntimeParser::CheckJsonIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems)
{
	if (Index < 0)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!JsonObject->TryGetArrayField(FieldName, JsonArray))
	{
		return false;
	}

	if (Index >= JsonArray->Num())
	{
		return false;
	}

	for (TSharedPtr<FJsonValue> JsonItem : (*JsonArray))
	{
		JsonItems.Add(JsonItem.ToSharedRef());
	}

	return true;
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 Index)
{
	TArray<TSharedRef<FJsonValue>> JsonArray;
	if (!CheckJsonIndex(JsonObject, FieldName, Index, JsonArray))
	{
		return nullptr;
	}

	return JsonArray[Index]->AsObject();
}

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromExtensionIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 Index)
{
	if (Index < 0)
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField("extensions", JsonExtensionsObject))
	{
		return nullptr;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return nullptr;
	}

	return GetJsonObjectFromIndex(JsonExtensionObject->ToSharedRef(), FieldName, Index);
}


FString FglTFRuntimeParser::GetJsonObjectString(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const FString& DefaultValue)
{
	FString Value;
	if (!JsonObject->TryGetStringField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

double FglTFRuntimeParser::GetJsonObjectNumber(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const double DefaultValue)
{
	double Value;
	if (!JsonObject->TryGetNumberField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

bool FglTFRuntimeParser::GetJsonObjectBool(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const bool DefaultValue)
{
	bool Value;
	if (!JsonObject->TryGetBoolField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

int32 FglTFRuntimeParser::GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& FieldName, const int32 DefaultValue)
{
	int64 Value;
	if (!JsonObject->TryGetNumberField(FieldName, Value))
	{
		return DefaultValue;
	}
	return (int32)Value;
}

int32 FglTFRuntimeParser::GetJsonExtensionObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName, const int32 DefaultValue)
{
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField("extensions", JsonExtensionsObject))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return false;
	}

	return GetJsonObjectIndex(JsonExtensionObject->ToSharedRef(), FieldName, DefaultValue);
}

TArray<int32> FglTFRuntimeParser::GetJsonExtensionObjectIndices(TSharedRef<FJsonObject> JsonObject, const FString& ExtensionName, const FString& FieldName)
{
	TArray<int32> Indices;
	const TSharedPtr<FJsonObject>* JsonExtensionsObject;
	if (!JsonObject->TryGetObjectField("extensions", JsonExtensionsObject))
	{
		return Indices;
	}

	const TSharedPtr<FJsonObject>* JsonExtensionObject = nullptr;
	if (!(*JsonExtensionsObject)->TryGetObjectField(ExtensionName, JsonExtensionObject))
	{
		return Indices;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray;
	if (!(*JsonExtensionObject)->TryGetArrayField(FieldName, JsonArray))
	{
		return Indices;
	}

	for (TSharedPtr<FJsonValue> JsonItem : *JsonArray)
	{
		int32 Index;
		if (!JsonItem->TryGetNumber(Index))
		{
			return Indices;
		}
		Indices.Add(Index);
	}

	return Indices;
}

bool FglTFRuntimeParser::LoadScene(int32 SceneIndex, FglTFRuntimeScene& Scene)
{
	TSharedPtr<FJsonObject> JsonSceneObject = GetJsonObjectFromRootIndex("scenes", SceneIndex);
	if (!JsonSceneObject)
	{
		return false;
	}

	Scene.Index = SceneIndex;
	Scene.Name = GetJsonObjectString(JsonSceneObject.ToSharedRef(), "name", FString::FromInt(Scene.Index));

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

bool FglTFRuntimeParser::LoadNodeByName(const FString& Name, FglTFRuntimeNode& Node)
{
	// a bit hacky, but allows zero-copy for cached values
	if (!bAllNodesCached)
	{
		if (!LoadNodes())
		{
			return false;
		}
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

void FglTFRuntimeParser::AddError(const FString& ErrorContext, const FString& ErrorMessage)
{
	FString FullMessage = ErrorContext + ": " + ErrorMessage;
	Errors.Add(FullMessage);
	UE_LOG(LogGLTFRuntime, Error, TEXT("%s"), *FullMessage);
	if (OnError.IsBound())
	{
		OnError.Broadcast(ErrorContext, ErrorMessage);
	}
}

void FglTFRuntimeParser::ClearErrors()
{
	Errors.Empty();
}

bool FglTFRuntimeParser::FillJsonMatrix(const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues, FMatrix& Matrix)
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

	return true;
}

bool FglTFRuntimeParser::LoadNode_Internal(int32 Index, TSharedRef<FJsonObject> JsonNodeObject, int32 NodesCount, FglTFRuntimeNode& Node)
{
	Node.Index = Index;
	Node.Name = GetJsonObjectString(JsonNodeObject, "name", FString::FromInt(Node.Index));

	Node.MeshIndex = GetJsonObjectIndex(JsonNodeObject, "mesh", INDEX_NONE);

	Node.SkinIndex = GetJsonObjectIndex(JsonNodeObject, "skin", INDEX_NONE);

	Node.CameraIndex = GetJsonObjectIndex(JsonNodeObject, "camera", INDEX_NONE);

	Node.EmitterIndices = GetJsonExtensionObjectIndices(JsonNodeObject, "MSFT_audio_emitter", "emitters");

	FMatrix Matrix = FMatrix::Identity;

	const TArray<TSharedPtr<FJsonValue>>* JsonMatrixValues;
	if (JsonNodeObject->TryGetArrayField("matrix", JsonMatrixValues))
	{
		if (!FillJsonMatrix(JsonMatrixValues, Matrix))
		{
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonScaleValues;
	if (JsonNodeObject->TryGetArrayField("scale", JsonScaleValues))
	{
		FVector MatrixScale;
		if (!GetJsonVector<3>(JsonScaleValues, MatrixScale))
		{
			return false;
		}

		Matrix *= FScaleMatrix(MatrixScale);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonRotationValues;
	if (JsonNodeObject->TryGetArrayField("rotation", JsonRotationValues))
	{
		FVector4 Vector;
		if (!GetJsonVector<4>(JsonRotationValues, Vector))
		{
			return false;
		}
		FQuat Quat = { Vector.X, Vector.Y, Vector.Z, Vector.W };
		Matrix *= FQuatRotationMatrix(Quat);
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTranslationValues;
	if (JsonNodeObject->TryGetArrayField("translation", JsonTranslationValues))
	{
		FVector Translation;
		if (!GetJsonVector<3>(JsonTranslationValues, Translation))
		{
			return false;
		}

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

bool FglTFRuntimeParser::LoadAnimation_Internal(TSharedRef<FJsonObject> JsonAnimationObject, float& Duration, FString& Name, TFunctionRef<void(const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)> Callback, TFunctionRef<bool(const FglTFRuntimeNode& Node)> NodeFilter)
{
	Name = GetJsonObjectString(JsonAnimationObject, "name", "");

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
		{
			return false;
		}

		TArray<float> Timeline;
		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "input", Timeline, { 5126 }, false))
		{
			AddError("LoadAnimation_Internal()", FString::Printf(TEXT("Unable to retrieve \"input\" from sampler %d"), SamplerIndex));
			return false;
		}

		TArray<FVector4> Values;
		if (!BuildFromAccessorField(JsonSamplerObject.ToSharedRef(), "output", Values, { 1, 3, 4 }, { 5126, 5120, 5121, 5122, 5123 }, true))
		{
			AddError("LoadAnimation_Internal()", FString::Printf(TEXT("Unable to retrieve \"output\" from sampler %d"), SamplerIndex));
			return false;
		}

		FString SamplerInterpolation;
		if (!JsonSamplerObject->TryGetStringField("interpolation", SamplerInterpolation))
		{
			SamplerInterpolation = "LINEAR";
		}

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

		if (!NodeFilter(Node))
		{
			continue;
		}

		FString Path;
		if (!(*JsonTargetObject)->TryGetStringField("path", Path))
		{
			return false;
		}

		Callback(Node, Path, Samplers[Sampler].Key, Samplers[Sampler].Value);
	}

	return true;
}

TArray<FString> FglTFRuntimeParser::GetCamerasNames()
{
	TArray<FString> CamerasNames;
	const TArray<TSharedPtr<FJsonValue>>* JsonCameras;
	if (!Root->TryGetArrayField("cameras", JsonCameras))
	{
		return CamerasNames;
	}

	for (TSharedPtr<FJsonValue> JsonCamera : *JsonCameras)
	{
		TSharedPtr<FJsonObject> JsonCameraObject = JsonCamera->AsObject();
		if (!JsonCameraObject)
		{
			continue;
		}

		FString CameraName;
		if (!JsonCameraObject->TryGetStringField("name", CameraName))
		{
			continue;
		}

		CamerasNames.Add(CameraName);
	}

	return CamerasNames;
}

UglTFRuntimeAnimationCurve* FglTFRuntimeParser::LoadNodeAnimationCurve(const int32 NodeIndex)
{
	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
		return nullptr;

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return nullptr;
	}

	UglTFRuntimeAnimationCurve* AnimationCurve = NewObject<UglTFRuntimeAnimationCurve>(GetTransientPackage(), NAME_None, RF_Public);

	FTransform OriginalTransform = FTransform(SceneBasis * Node.Transform.ToMatrixWithScale() * SceneBasis.Inverse());

	AnimationCurve->SetDefaultValues(OriginalTransform.GetLocation(), OriginalTransform.Rotator().Euler(), OriginalTransform.GetScale3D());

	bool bAnimationFound = false;

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)
	{
		if (Path == "translation")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddLocationValue(Timeline[TimeIndex], Values[TimeIndex] * SceneScale, ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "rotation")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				FVector4 RotationValue = Values[TimeIndex];
				FQuat Quat(RotationValue.X, RotationValue.Y, RotationValue.Z, RotationValue.W);
				FVector Euler = Quat.Euler();
				AnimationCurve->AddRotationValue(Timeline[TimeIndex], Euler, ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "scale")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadNodeAnimationCurve()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddScaleValue(Timeline[TimeIndex], Values[TimeIndex], ERichCurveInterpMode::RCIM_Linear);
			}
		}
		bAnimationFound = true;
	};

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
			return nullptr;
		float Duration;
		FString Name;
		if (!LoadAnimation_Internal(JsonAnimationObject.ToSharedRef(), Duration, Name, Callback, [&](const FglTFRuntimeNode& Node) -> bool { return Node.Index == NodeIndex; }))
		{
			return nullptr;
		}
		// stop at the first found animation
		if (bAnimationFound)
		{
			AnimationCurve->glTFCurveAnimationIndex = JsonAnimationIndex;
			AnimationCurve->glTFCurveAnimationName = Name;
			AnimationCurve->glTFCurveAnimationDuration = Duration;
			AnimationCurve->BasisMatrix = SceneBasis;
			return AnimationCurve;
		}
	}

	return nullptr;
}

TArray<UglTFRuntimeAnimationCurve*> FglTFRuntimeParser::LoadAllNodeAnimationCurves(const int32 NodeIndex)
{
	TArray<UglTFRuntimeAnimationCurve*> AnimationCurves;

	FglTFRuntimeNode Node;
	if (!LoadNode(NodeIndex, Node))
	{
		return AnimationCurves;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonAnimations;
	if (!Root->TryGetArrayField("animations", JsonAnimations))
	{
		return AnimationCurves;
	}

	UglTFRuntimeAnimationCurve* AnimationCurve = nullptr;

	FTransform OriginalTransform = FTransform(SceneBasis * Node.Transform.ToMatrixWithScale() * SceneBasis.Inverse());

	bool bAnimationFound = false;

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)
	{
		if (Path == "translation")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for translation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddLocationValue(Timeline[TimeIndex], Values[TimeIndex] * SceneScale, ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "rotation")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for rotation on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				FVector4 RotationValue = Values[TimeIndex];
				FQuat Quat(RotationValue.X, RotationValue.Y, RotationValue.Z, RotationValue.W);
				FVector Euler = Quat.Euler();
				AnimationCurve->AddRotationValue(Timeline[TimeIndex], Euler, ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "scale")
		{
			if (Timeline.Num() != Values.Num())
			{
				AddError("LoadAllNodeAnimationCurves()", FString::Printf(TEXT("Animation input/output mismatch (%d/%d) for scale on node %d"), Timeline.Num(), Values.Num(), Node.Index));
				return;
			}
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddScaleValue(Timeline[TimeIndex], Values[TimeIndex], ERichCurveInterpMode::RCIM_Linear);
			}
		}
		bAnimationFound = true;
	};

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
			continue;
		float Duration;
		FString Name;
		bAnimationFound = false;
		AnimationCurve = NewObject<UglTFRuntimeAnimationCurve>(GetTransientPackage(), NAME_None, RF_Public);
		AnimationCurve->SetDefaultValues(OriginalTransform.GetLocation(), OriginalTransform.Rotator().Euler(), OriginalTransform.GetScale3D());
		if (!LoadAnimation_Internal(JsonAnimationObject.ToSharedRef(), Duration, Name, Callback, [&](const FglTFRuntimeNode& Node) -> bool { return Node.Index == NodeIndex; }))
		{
			continue;
		}
		// stop at the first found animation
		if (bAnimationFound)
		{
			AnimationCurve->glTFCurveAnimationIndex = JsonAnimationIndex;
			AnimationCurve->glTFCurveAnimationName = Name;
			AnimationCurve->glTFCurveAnimationDuration = Duration;
			AnimationCurve->BasisMatrix = SceneBasis;
			AnimationCurves.Add(AnimationCurve);
		}
	}

	return AnimationCurves;
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

int32 FglTFRuntimeParser::FindCommonRoot(const TArray<int32>& Indices)
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

bool FglTFRuntimeParser::LoadCameraIntoCameraComponent(const int32 CameraIndex, UCameraComponent* CameraComponent)
{
	if (!CameraComponent)
	{
		AddError("LoadCameraIntoCameraComponent()", "No valid CameraComponent specified.");
		return false;
	}

	TSharedPtr<FJsonObject> CameraObject = GetJsonObjectFromRootIndex("cameras", CameraIndex);
	if (!CameraObject)
	{
		AddError("LoadCameraIntoCameraComponent()", "Invalid Camera Index.");
		return false;
	}

	FString CameraType = GetJsonObjectString(CameraObject.ToSharedRef(), "type", "");
	if (CameraType.IsEmpty())
	{
		AddError("LoadCameraIntoCameraComponent()", "No Camera type specified.");
		return false;
	}

	if (CameraType.Equals("perspective", ESearchCase::IgnoreCase))
	{
		CameraComponent->ProjectionMode = ECameraProjectionMode::Perspective;
		const TSharedPtr<FJsonObject>* PerspectiveObject;
		if (CameraObject->TryGetObjectField("perspective", PerspectiveObject))
		{
			double AspectRatio;
			if ((*PerspectiveObject)->TryGetNumberField("aspectRatio", AspectRatio))
			{
				CameraComponent->AspectRatio = AspectRatio;
			}

			double YFov;
			if ((*PerspectiveObject)->TryGetNumberField("yfov", YFov))
			{
				CameraComponent->FieldOfView = FMath::RadiansToDegrees(YFov) * CameraComponent->AspectRatio;
			}
		}

		return true;
	}

	if (CameraType.Equals("orthographic", ESearchCase::IgnoreCase))
	{
		CameraComponent->ProjectionMode = ECameraProjectionMode::Orthographic;
		const TSharedPtr<FJsonObject>* OrthographicObject;
		if (CameraObject->TryGetObjectField("orthographic", OrthographicObject))
		{
			double XMag;
			if (!(*OrthographicObject)->TryGetNumberField("xmag", XMag))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Width specified.");
				return false;
			}
			double YMag;
			if (!(*OrthographicObject)->TryGetNumberField("ymag", YMag))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Height specified.");
				return false;
			}
			double ZFar;
			if (!(*OrthographicObject)->TryGetNumberField("zfar", ZFar))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Far specified.");
				return false;
			}
			double ZNear;
			if (!(*OrthographicObject)->TryGetNumberField("znear", ZNear))
			{
				AddError("LoadCameraIntoCameraComponent()", "No Orthographic Near specified.");
				return false;
			}

			CameraComponent->AspectRatio = XMag / YMag;
			CameraComponent->OrthoWidth = XMag * SceneScale;

			CameraComponent->OrthoFarClipPlane = ZFar * SceneScale;
			CameraComponent->OrthoNearClipPlane = ZNear * SceneScale;
		}
		return true;
	}

	AddError("LoadCameraIntoCameraComponent()", "Unsupported Camera Type.");
	return false;
}

USkeleton* FglTFRuntimeParser::LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	TSharedPtr<FJsonObject> JsonSkinObject = GetJsonObjectFromRootIndex("skins", SkinIndex);
	if (!JsonSkinObject)
	{
		return nullptr;
	}

	if (CanReadFromCache(SkeletonConfig.CacheMode) && SkeletonsCache.Contains(SkinIndex))
	{
		return SkeletonsCache[SkinIndex];
	}

	TMap<int32, FName> BoneMap;

	USkeletalMesh* SkeletalMesh = NewObject<USkeletalMesh>(GetTransientPackage(), NAME_None, RF_Public);
	USkeleton* Skeleton = NewObject<USkeleton>(GetTransientPackage(), NAME_None, RF_Public);

#if ENGINE_MAJOR_VERSION > 4 || ENGINE_MINOR_VERSION > 26
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->GetRefSkeleton();
#else
	FReferenceSkeleton& RefSkeleton = SkeletalMesh->RefSkeleton;
#endif

	if (!FillReferenceSkeleton(JsonSkinObject.ToSharedRef(), RefSkeleton, BoneMap, SkeletonConfig))
	{
		AddError("FillReferenceSkeleton()", "Unable to fill RefSkeleton.");
		return nullptr;
	}

	if (SkeletonConfig.bNormalizeSkeletonScale)
	{
		NormalizeSkeletonScale(RefSkeleton);
	}

	if (SkeletonConfig.bClearRotations || SkeletonConfig.CopyRotationsFrom)
	{
		ClearSkeletonRotations(RefSkeleton);
	}

	if (SkeletonConfig.CopyRotationsFrom)
	{
		CopySkeletonRotationsFrom(RefSkeleton, SkeletonConfig.CopyRotationsFrom->GetReferenceSkeleton());
	}

	Skeleton->MergeAllBonesToBoneTree(SkeletalMesh);

	if (CanWriteToCache(SkeletonConfig.CacheMode))
	{
		SkeletonsCache.Add(SkinIndex, Skeleton);
	}

	return Skeleton;
}

bool FglTFRuntimeParser::NodeIsBone(const int32 NodeIndex)
{
	const TArray<TSharedPtr<FJsonValue>>* JsonSkins;
	if (!Root->TryGetArrayField("skins", JsonSkins))
	{
		return false;
	}

	for (TSharedPtr<FJsonValue> JsonSkin : *JsonSkins)
	{
		TSharedPtr<FJsonObject> JsonSkinObject = JsonSkin->AsObject();
		if (!JsonSkinObject)
		{
			continue;
		}

		const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
		if (!JsonSkinObject->TryGetArrayField("joints", JsonJoints))
		{
			continue;
		}

		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
			{
				continue;
			}
			if (JointIndex == NodeIndex)
			{
				return true;
			}
		}
	}

	return false;
}

bool FglTFRuntimeParser::FillFakeSkeleton(FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	RefSkeleton.Empty();

	FReferenceSkeletonModifier Modifier = FReferenceSkeletonModifier(RefSkeleton, nullptr);

	if (SkeletalMeshConfig.CustomSkeleton.Num() > 0)
	{
		bool bFoundRoot = false;
		for (int32 BoneIndex = 0; BoneIndex < SkeletalMeshConfig.CustomSkeleton.Num(); BoneIndex++)
		{
			const FString CurrentBoneName = SkeletalMeshConfig.CustomSkeleton[BoneIndex].BoneName;
			const int32 CurrentBoneParentIndex = SkeletalMeshConfig.CustomSkeleton[BoneIndex].ParentIndex;
			const FTransform CurrentBoneTransform = SkeletalMeshConfig.CustomSkeleton[BoneIndex].Transform;
			if (CurrentBoneParentIndex == INDEX_NONE)
			{
				if (bFoundRoot)
				{
					AddError("FillFakeSkeleton()", "Only one root bone can be defined.");
					return false;
				}
				bFoundRoot = true;
			}
			else if (CurrentBoneParentIndex >= 0)
			{
				if (CurrentBoneParentIndex >= SkeletalMeshConfig.CustomSkeleton.Num())
				{
					AddError("FillFakeSkeleton()", "Bone ParentIndex is not valid.");
					return false;
				}
			}
			else
			{
				AddError("FillFakeSkeleton()", "The only supported negative ParentIndex is -1 (for root bone)");
				return false;
			}

			// now check for duplicated bone names
			for (int32 CheckBoneIndex = 0; CheckBoneIndex < SkeletalMeshConfig.CustomSkeleton.Num(); CheckBoneIndex++)
			{
				if (CheckBoneIndex == BoneIndex)
				{
					continue;
				}

				if (SkeletalMeshConfig.CustomSkeleton[CheckBoneIndex].BoneName == CurrentBoneName)
				{
					AddError("FillFakeSkeleton()", "Duplicated bone name found");
					return false;
				}
			}
			FName BoneName = FName(CurrentBoneName);
			Modifier.Add(FMeshBoneInfo(BoneName, CurrentBoneName, CurrentBoneParentIndex), CurrentBoneTransform);

			BoneMap.Add(BoneIndex, BoneName);
		}
	}
	else
	{
		FName RootBoneName = FName("root");
		Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), FTransform::Identity);
		BoneMap.Add(0, RootBoneName);
	}

	return true;
}

bool FglTFRuntimeParser::FillReferenceSkeleton(TSharedRef<FJsonObject> JsonSkinObject, FReferenceSkeleton& RefSkeleton, TMap<int32, FName>& BoneMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	// get the list of valid joints	
	const TArray<TSharedPtr<FJsonValue>>* JsonJoints;
	TArray<int32> Joints;
	if (JsonSkinObject->TryGetArrayField("joints", JsonJoints))
	{
		for (TSharedPtr<FJsonValue> JsonJoint : *JsonJoints)
		{
			int64 JointIndex;
			if (!JsonJoint->TryGetNumber(JointIndex))
			{
				return false;
			}
			Joints.Add(JointIndex);
		}
	}

	if (Joints.Num() == 0)
	{
		AddError("FillReferenceSkeleton()", "No Joints available");
		return false;
	}

	// fill the root bone
	FglTFRuntimeNode RootNode;
	int64 RootBoneIndex;
	bool bHasSpecificRoot = false;

	if (SkeletonConfig.RootNodeIndex > INDEX_NONE)
	{
		RootBoneIndex = SkeletonConfig.RootNodeIndex;
	}
	else if (JsonSkinObject->TryGetNumberField("skeleton", RootBoneIndex))
	{
		// use the "skeleton" field as the root bone
		bHasSpecificRoot = true;
	}
	else
	{
		RootBoneIndex = FindCommonRoot(Joints);
	}

	if (RootBoneIndex == INDEX_NONE)
		return false;

	if (!LoadNode(RootBoneIndex, RootNode))
	{
		AddError("FillReferenceSkeleton()", "Unable to load joint node.");
		return false;
	}

	if (bHasSpecificRoot && !Joints.Contains(RootBoneIndex))
	{
		FglTFRuntimeNode ParentNode = RootNode;
		while (ParentNode.ParentIndex != INDEX_NONE)
		{
			if (!LoadNode(ParentNode.ParentIndex, ParentNode))
			{
				return false;
			}
			RootNode.Transform *= ParentNode.Transform;
		}
	}

	TMap<int32, FMatrix> InverseBindMatricesMap;
	int64 inverseBindMatricesIndex;
	if (JsonSkinObject->TryGetNumberField("inverseBindMatrices", inverseBindMatricesIndex))
	{
		TArray64<uint8> InverseBindMatricesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		bool bNormalized = false;
		if (!GetAccessor(inverseBindMatricesIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, InverseBindMatricesBytes))
		{
			AddError("FillReferenceSkeleton()", FString::Printf(TEXT("Unable to load accessor: %lld."), inverseBindMatricesIndex));
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
	if (!TraverseJoints(Modifier, INDEX_NONE, RootNode, Joints, BoneMap, InverseBindMatricesMap, SkeletonConfig))
		return false;

	return true;
}

bool FglTFRuntimeParser::TraverseJoints(FReferenceSkeletonModifier& Modifier, int32 Parent, FglTFRuntimeNode& Node, const TArray<int32>& Joints, TMap<int32, FName>& BoneMap, const TMap<int32, FMatrix>& InverseBindMatricesMap, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	// add fake root bone ?
	if (Parent == INDEX_NONE && SkeletonConfig.bAddRootBone)
	{
		FName RootBoneName = FName("root");
		if (!SkeletonConfig.RootBoneName.IsEmpty())
		{
			RootBoneName = FName(SkeletonConfig.RootBoneName);
		}
		Modifier.Add(FMeshBoneInfo(RootBoneName, RootBoneName.ToString(), INDEX_NONE), FTransform::Identity);
		Parent = 0;
	}

	FName BoneName = FName(*Node.Name);
	if (SkeletonConfig.BonesNameMap.Contains(BoneName.ToString()))
	{
		FString BoneNameMapValue = SkeletonConfig.BonesNameMap[BoneName.ToString()];
		if (BoneNameMapValue.IsEmpty())
		{
			AddError("TraverseJoints()", FString::Printf(TEXT("Invalid Bone Name Map for %s"), *BoneName.ToString()));
			return false;
		}
		BoneName = FName(BoneNameMapValue);
	}

	// Check if a bone with the same name exists
	int32 CollidingIndex = Modifier.FindBoneIndex(BoneName);
	while (CollidingIndex != INDEX_NONE)
	{
		AddError("TraverseJoints()", FString::Printf(TEXT("Bone %s already exists."), *BoneName.ToString()));
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

		M.ScaleTranslation(FVector(SceneScale, SceneScale, SceneScale));
		FMatrix SkeletonBasis = SceneBasis;
		Transform = FTransform(SkeletonBasis.Inverse() * M * SkeletonBasis);
	}
	else if (Joints.Contains(Node.Index))
	{
		AddError("TraverseJoints()", FString::Printf(TEXT("No bind transform for node %d %s"), Node.Index, *Node.Name));
	}

	Modifier.Add(FMeshBoneInfo(BoneName, Node.Name, Parent), Transform);

	int32 NewParentIndex = Modifier.FindBoneIndex(BoneName);
	// something horrible happened...
	if (NewParentIndex == INDEX_NONE)
	{
		return false;
	}

	if (Joints.Contains(Node.Index))
	{
		BoneMap.Add(Joints.IndexOfByKey(Node.Index), BoneName);
	}

	for (int32 ChildIndex : Node.ChildrenIndices)
	{
		FglTFRuntimeNode ChildNode;
		if (!LoadNode(ChildIndex, ChildNode))
			return false;

		if (!TraverseJoints(Modifier, NewParentIndex, ChildNode, Joints, BoneMap, InverseBindMatricesMap, SkeletonConfig))
			return false;
	}

	return true;
}

bool FglTFRuntimeParser::LoadPrimitives(TSharedRef<FJsonObject> JsonMeshObject, TArray<FglTFRuntimePrimitive>& Primitives, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		AddError("LoadPrimitives()", "No primitives defined in the asset.");
		return false;
	}

	int32 FirstPrimitive = Primitives.Num();

	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
		{
			return false;
		}

		FglTFRuntimePrimitive Primitive;
		if (!LoadPrimitive(JsonPrimitiveObject.ToSharedRef(), Primitive, MaterialsConfig))
		{
			return false;
		}

		Primitives.Add(Primitive);
	}

	const TSharedPtr<FJsonObject>* JsonExtrasObject;
	if (JsonMeshObject->TryGetObjectField("extras", JsonExtrasObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonTargetNamesArray;
		if ((*JsonExtrasObject)->TryGetArrayField("targetNames", JsonTargetNamesArray))
		{
			auto ApplyTargetName = [FirstPrimitive, &Primitives](const int32 TargetNameIndex, const FString& TargetName)
			{
				int32 MorphTargetCounter = 0;
				for (int32 PrimitiveIndex = FirstPrimitive; PrimitiveIndex < Primitives.Num(); PrimitiveIndex++)
				{
					FglTFRuntimePrimitive& Primitive = Primitives[PrimitiveIndex];
					for (FglTFRuntimeMorphTarget& MorphTarget : Primitive.MorphTargets)
					{
						if (MorphTargetCounter == TargetNameIndex)
						{
							MorphTarget.Name = TargetName;
							return;
						}
						MorphTargetCounter++;
					}
				}
			};
			for (int32 TargetNameIndex = 0; TargetNameIndex < JsonTargetNamesArray->Num(); TargetNameIndex++)
			{
				const FString TargetName = (*JsonTargetNamesArray)[TargetNameIndex]->AsString();
				ApplyTargetName(TargetNameIndex, TargetName);
			}
		}
	}

	if (MaterialsConfig.bMergeSectionsByMaterial)
	{
		TMap<UMaterialInterface*, TArray<FglTFRuntimePrimitive>> PrimitivesMap;
		for (FglTFRuntimePrimitive& Primitive : Primitives)
		{
			if (PrimitivesMap.Contains(Primitive.Material))
			{
				PrimitivesMap[Primitive.Material].Add(Primitive);
			}
			else
			{
				TArray<FglTFRuntimePrimitive> NewPrimitives;
				NewPrimitives.Add(Primitive);
				PrimitivesMap.Add(Primitive.Material, NewPrimitives);
			}
		}

		TArray<FglTFRuntimePrimitive> MergedPrimitives;
		for (TPair<UMaterialInterface*, TArray<FglTFRuntimePrimitive>>& Pair : PrimitivesMap)
		{
			FglTFRuntimePrimitive MergedPrimitive;
			if (MergePrimitives(Pair.Value, MergedPrimitive))
			{
				MergedPrimitives.Add(MergedPrimitive);
			}
			else
			{
				// unable to merge, just leave as is
				for (FglTFRuntimePrimitive& Primitive : Pair.Value)
				{
					MergedPrimitives.Add(Primitive);
				}
			}
		}

		Primitives = MergedPrimitives;

	}

	return true;
}

bool FglTFRuntimeParser::LoadPrimitive(TSharedRef<FJsonObject> JsonPrimitiveObject, FglTFRuntimePrimitive& Primitive, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	SCOPED_NAMED_EVENT(FglTFRuntimeParser_LoadPrimitive, FColor::Magenta);

	const TSharedPtr<FJsonObject>* JsonAttributesObject;
	if (!JsonPrimitiveObject->TryGetObjectField("attributes", JsonAttributesObject))
	{
		AddError("LoadPrimitive()", "No attributes array available");
		return false;
	}

	// POSITION is required for generating a valid Mesh
	if (!(*JsonAttributesObject)->HasField("POSITION"))
	{
		AddError("LoadPrimitive()", "POSITION attribute is required");
		return false;
	}

	const bool bHasMeshQuantization = ExtensionsRequired.Contains("KHR_mesh_quantization");

	TArray<int64> SupportedPositionComponentTypes = { 5126 };
	TArray<int64> SupportedNormalComponentTypes = { 5126 };
	TArray<int64> SupportedTangentComponentTypes = { 5126 };
	TArray<int64> SupportedTexCoordComponentTypes = { 5126, 5121, 5123 };
	if (bHasMeshQuantization)
	{
		SupportedPositionComponentTypes.Append({ 5120, 5121, 5122, 5123 });
		SupportedNormalComponentTypes.Append({ 5120, 5122 });
		SupportedTangentComponentTypes.Append({ 5120, 5122 });
		SupportedTexCoordComponentTypes.Append({ 5120, 5122 });
	}

	if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "POSITION", Primitive.Positions,
		{ 3 }, SupportedPositionComponentTypes, false, [&](FVector Value) -> FVector {return SceneBasis.TransformPosition(Value) * SceneScale; }))
	{
		AddError("LoadPrimitive()", "Unable to load POSITION attribute");
		return false;
	}

	if ((*JsonAttributesObject)->HasField("NORMAL"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "NORMAL", Primitive.Normals,
			{ 3 }, SupportedNormalComponentTypes, false, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }))
		{
			AddError("LoadPrimitive()", "Unable to load NORMAL attribute");
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField("TANGENT"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TANGENT", Primitive.Tangents,
			{ 4 }, SupportedTangentComponentTypes, false, [&](FVector4 Value) -> FVector4 { return SceneBasis.TransformFVector4(Value); }))
		{
			AddError("LoadPrimitive()", "Unable to load TANGENT attribute");
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_0"))
	{
		TArray<FVector2D> UV;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_0", UV,
			{ 2 }, SupportedTexCoordComponentTypes, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }))
		{
			AddError("LoadPrimitive()", "Error loading TEXCOORD_0");
			return false;
		}

		Primitive.UVs.Add(UV);
	}

	if ((*JsonAttributesObject)->HasField("TEXCOORD_1"))
	{
		TArray<FVector2D> UV;
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TEXCOORD_1", UV,
			{ 2 }, SupportedTexCoordComponentTypes, true, [&](FVector2D Value) -> FVector2D {return FVector2D(Value.X, Value.Y); }))
		{
			AddError("LoadPrimitive()", "Error loading TEXCOORD_1");
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
			AddError("LoadPrimitive()", "Error loading JOINTS_0");
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
			AddError("LoadPrimitive()", "Error loading JOINTS_1");
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
			AddError("LoadPrimitive()", "Error loading WEIGHTS_0");
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
			AddError("LoadPrimitive()", "Error loading WEIGHTS_1");
			return false;
		}
		Primitive.Weights.Add(Weights);
	}

	if ((*JsonAttributesObject)->HasField("COLOR_0"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "COLOR_0", Primitive.Colors,
			{ 3, 4 }, { 5126, 5121, 5123 }, true))
		{
			AddError("LoadPrimitive()", "Error loading COLOR_0");
			return false;
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonTargetsArray;
	if (JsonPrimitiveObject->TryGetArrayField("targets", JsonTargetsArray))
	{
		for (TSharedPtr<FJsonValue> JsonTargetItem : *JsonTargetsArray)
		{
			TSharedPtr<FJsonObject> JsonTargetObject = JsonTargetItem->AsObject();
			if (!JsonTargetObject)
			{
				AddError("LoadPrimitive()", "Error on MorphTarget item: expected an object.");
				return false;
			}

			FglTFRuntimeMorphTarget MorphTarget;

			bool bValid = false;

			if (JsonTargetObject->HasField("POSITION"))
			{
				if (!BuildFromAccessorField(JsonTargetObject.ToSharedRef(), "POSITION", MorphTarget.Positions,
					{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector { return SceneBasis.TransformPosition(Value) * SceneScale; }))
				{
					AddError("LoadPrimitive()", "Unable to load POSITION attribute for MorphTarget");
					return false;
				}
				if (MorphTarget.Positions.Num() != Primitive.Positions.Num())
				{
					AddError("LoadPrimitive()", "Invalid POSITION attribute size for MorphTarget.");
					return false;
				}
				bValid = true;
			}

			if (JsonTargetObject->HasField("NORMAL"))
			{
				if (!BuildFromAccessorField(JsonTargetObject.ToSharedRef(), "NORMAL", MorphTarget.Normals,
					{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }))
				{
					AddError("LoadPrimitive()", "Unable to load NORMAL attribute for MorphTarget");
					return false;
				}
				if (MorphTarget.Normals.Num() != Primitive.Normals.Num())
				{
					AddError("LoadPrimitive()", "Invalid NORMAL attribute size for MorphTarget.");
					return false;
				}
				bValid = true;
			}

			if (bValid)
			{
				Primitive.MorphTargets.Add(MorphTarget);
			}
		}
	}

	int64 IndicesAccessorIndex;
	if (JsonPrimitiveObject->TryGetNumberField("indices", IndicesAccessorIndex))
	{
		TArray64<uint8> IndicesBytes;
		int64 ComponentType, Stride, Elements, ElementSize, Count;
		bool bNormalized = false;
		if (!GetAccessor(IndicesAccessorIndex, ComponentType, Stride, Elements, ElementSize, Count, bNormalized, IndicesBytes))
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Unable to load accessor: %lld"), IndicesAccessorIndex));
			return false;
		}

		if (Elements != 1)
		{
			return false;
		}

		if (IndicesBytes.Num() < (Count * Stride))
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Invalid size for accessor indices: %lld"), IndicesBytes.Num()));
			return false;
		}

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
				AddError("LoadPrimitive()", FString::Printf(TEXT("Invalid component type for indices: %lld"), ComponentType));
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

	// Draco decompression
	const TSharedPtr<FJsonObject>* JsonExtensions;
	if (JsonPrimitiveObject->TryGetObjectField("extensions", JsonExtensions))
	{
		// KHR_draco_mesh_compression
		const TSharedPtr<FJsonObject>* KHR_draco_mesh_compression;
		if ((*JsonExtensions)->TryGetObjectField("KHR_draco_mesh_compression", KHR_draco_mesh_compression))
		{
			int64 BufferView;
			if (!(*KHR_draco_mesh_compression)->TryGetNumberField("bufferView", BufferView))
			{
				AddError("LoadPrimitive()", "KHR_draco_mesh_compression requires a valid bufferView");
				return false;
			}

			TArray64<uint8> DracoData;
			int64 Stride;
			if (!GetBufferView(BufferView, DracoData, Stride))
			{
				AddError("LoadPrimitive()", "KHR_draco_mesh_compression has an invalid bufferView");
				return false;
			}

			AddError("LoadPrimitive()", "KHR_draco_mesh_compression extension is currently not supported");
			return false;
		}
	}

	int64 MaterialIndex;
	if (JsonPrimitiveObject->TryGetNumberField("material", MaterialIndex))
	{
		Primitive.Material = LoadMaterial(MaterialIndex, MaterialsConfig, Primitive.Colors.Num() > 0, Primitive.MaterialName);
		if (!Primitive.Material)
		{
			AddError("LoadPrimitive()", FString::Printf(TEXT("Unable to load material %lld"), MaterialIndex));
			return false;
		}
	}
	else
	{
		Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	return true;
}


bool FglTFRuntimeParser::GetBuffer(int32 Index, TArray64<uint8>& Bytes)
{
	if (Index < 0)
		return false;

	if (Index == 0 && BinaryBuffer.Num() > 0)
	{
		Bytes = BinaryBuffer;
		return true;
	}

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
	{
		return false;
	}

	// check it is a valid base64 data uri
	if (Uri.StartsWith("data:"))
	{
		if (ParseBase64Uri(Uri, Bytes))
		{
			BuffersCache.Add(Index, Bytes);
			return true;
		}
		return false;
	}

	if (ZipFile)
	{
		if (ZipFile->GetFileContent(Uri, Bytes))
		{
			BuffersCache.Add(Index, Bytes);
			return true;
		}
	}

	// fallback
	if (!BaseDirectory.IsEmpty())
	{
		if (FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(BaseDirectory, Uri)))
		{
			BuffersCache.Add(Index, Bytes);
			return true;
		}
	}

	AddError("GetBuffer()", FString::Printf(TEXT("Unable to load buffer %d from Uri %s (you may want to enable external files loading...)"), Index, *Uri));
	return false;
}

bool FglTFRuntimeParser::ParseBase64Uri(const FString& Uri, TArray64<uint8>& Bytes)
{
	const FString Base64Signature = ";base64,";

	int32 StringIndex = Uri.Find(Base64Signature, ESearchCase::IgnoreCase, ESearchDir::FromStart, 5);

	if (StringIndex < 5)
		return false;

	StringIndex += Base64Signature.Len();

	TArray<uint8> BytesBase64;

	bool bSuccess = FBase64::Decode(Uri.Mid(StringIndex), BytesBase64);
	if (bSuccess)
	{
		Bytes.Append(BytesBase64);
	}
	return bSuccess;
}

bool FglTFRuntimeParser::GetBufferView(int32 Index, TArray64<uint8>& Bytes, int64& Stride)
{
	if (Index < 0)
	{
		return false;
	}

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
	{
		return false;
	}


	int64 BufferIndex;
	if (!JsonBufferObject->TryGetNumberField("buffer", BufferIndex))
	{
		return false;
	}

	TArray64<uint8> WholeData;
	if (!GetBuffer(BufferIndex, WholeData))
	{
		return false;
	}

	int64 ByteLength;
	if (!JsonBufferObject->TryGetNumberField("byteLength", ByteLength))
	{
		return false;
	}

	int64 ByteOffset;
	if (!JsonBufferObject->TryGetNumberField("byteOffset", ByteOffset))
	{
		ByteOffset = 0;
	}

	if (!JsonBufferObject->TryGetNumberField("byteStride", Stride))
	{
		Stride = 0;
	}

	if (ByteOffset + ByteLength > WholeData.Num())
	{
		return false;
	}

	Bytes.Append(&WholeData[ByteOffset], ByteLength);
	return true;
}

bool FglTFRuntimeParser::GetAccessor(int32 Index, int64& ComponentType, int64& Stride, int64& Elements, int64& ElementSize, int64& Count, bool& bNormalized, TArray64<uint8>& Bytes)
{

	TSharedPtr<FJsonObject> JsonAccessorObject = GetJsonObjectFromRootIndex("accessors", Index);
	if (!JsonAccessorObject)
	{
		return false;
	}

	bool bInitWithZeros = false;
	bool bHasSparse = false;

	int64 BufferViewIndex;
	if (!JsonAccessorObject->TryGetNumberField("bufferView", BufferViewIndex))
	{
		bInitWithZeros = true;
	}

	const TSharedPtr<FJsonObject>* JsonSparseObject = nullptr;
	if (JsonAccessorObject->TryGetObjectField("sparse", JsonSparseObject))
	{
		bHasSparse = true;
	}

	int64 ByteOffset;
	if (!JsonAccessorObject->TryGetNumberField("byteOffset", ByteOffset))
	{
		ByteOffset = 0;
	}

	if (!JsonAccessorObject->TryGetBoolField("normalized", bNormalized))
	{
		bNormalized = false;
	}

	if (!JsonAccessorObject->TryGetNumberField("componentType", ComponentType))
	{
		return false;
	}

	if (!JsonAccessorObject->TryGetNumberField("count", Count))
	{
		return false;
	}

	FString Type;
	if (!JsonAccessorObject->TryGetStringField("type", Type))
	{
		return false;
	}

	ElementSize = GetComponentTypeSize(ComponentType);
	if (ElementSize == 0)
	{
		return false;
	}

	Elements = GetTypeSize(Type);
	if (Elements == 0)
	{
		return false;
	}

	uint64 FinalSize = ElementSize * Elements * Count;

	if (bInitWithZeros)
	{
		Bytes.AddZeroed(FinalSize);
		if (!bHasSparse)
		{
			Stride = ElementSize * Elements;
			return true;
		}
	}
	else
	{
		if (!GetBufferView(BufferViewIndex, Bytes, Stride))
		{
			return false;
		}

		if (Stride == 0)
		{
			Stride = ElementSize * Elements;
		}

		FinalSize = Stride * Count;

		if (ByteOffset > 0)
		{
			TArray64<uint8> OffsetBytes;
			OffsetBytes.Append(&Bytes[ByteOffset], FinalSize);
			Bytes = OffsetBytes;
		}

		if (FinalSize > (uint64)Bytes.Num())
		{
			return false;
		}

		if (!bHasSparse)
		{
			return true;
		}
	}

	int64 SparseCount;
	if (!(*JsonSparseObject)->TryGetNumberField("count", SparseCount))
	{
		return false;
	}

	if (((uint64)SparseCount > FinalSize) || (SparseCount < 1))
	{
		return false;
	}

	const TSharedPtr<FJsonObject>* JsonSparseIndicesObject = nullptr;
	if (!(*JsonSparseObject)->TryGetObjectField("indices", JsonSparseIndicesObject))
	{
		return true;
	}

	int32 SparseBufferViewIndex = GetJsonObjectIndex(JsonSparseIndicesObject->ToSharedRef(), "bufferView", INDEX_NONE);
	if (SparseBufferViewIndex < 0)
	{
		return false;
	}

	int64 SparseByteOffset;
	if (!(*JsonSparseIndicesObject)->TryGetNumberField("byteOffset", SparseByteOffset))
	{
		SparseByteOffset = 0;
	}

	int64 SparseComponentType;
	if (!(*JsonSparseIndicesObject)->TryGetNumberField("componentType", SparseComponentType))
	{
		return false;
	}

	TArray64<uint8> SparseBytesIndices;
	int64 SparseBufferViewIndicesStride;
	if (!GetBufferView(SparseBufferViewIndex, SparseBytesIndices, SparseBufferViewIndicesStride))
	{
		return false;
	}

	if (SparseBufferViewIndicesStride == 0)
	{
		SparseBufferViewIndicesStride = GetComponentTypeSize(SparseComponentType);
	}


	if (((SparseBytesIndices.Num() - SparseByteOffset) / SparseBufferViewIndicesStride) < SparseCount)
	{
		return false;
	}

	TArray<uint32> SparseIndices;
	uint8* SparseIndicesBase = &SparseBytesIndices[SparseByteOffset];


	for (int32 SparseIndexOffset = 0; SparseIndexOffset < SparseCount; SparseIndexOffset++)
	{
		// UNSIGNED_BYTE
		if (SparseComponentType == 5121)
		{
			SparseIndices.Add(*SparseIndicesBase);
		}
		// UNSIGNED_SHORT
		else if (SparseComponentType == 5123)
		{
			uint16* SparseIndicesBaseUint16 = (uint16*)SparseIndicesBase;
			SparseIndices.Add(*SparseIndicesBaseUint16);
		}
		// UNSIGNED_INT
		else if (SparseComponentType == 5125)
		{
			uint32* SparseIndicesBaseUint32 = (uint32*)SparseIndicesBase;
			SparseIndices.Add(*SparseIndicesBaseUint32);
		}
		else
		{
			return false;
		}
		SparseIndicesBase += SparseBufferViewIndicesStride;
	}

	const TSharedPtr<FJsonObject>* JsonSparseValuesObject = nullptr;
	if (!(*JsonSparseObject)->TryGetObjectField("values", JsonSparseValuesObject))
	{
		return true;
	}

	int32 SparseValueBufferViewIndex = GetJsonObjectIndex(JsonSparseValuesObject->ToSharedRef(), "bufferView", INDEX_NONE);
	if (SparseValueBufferViewIndex < 0)
	{
		return false;
	}

	int64 SparseValueByteOffset;
	if (!(*JsonSparseValuesObject)->TryGetNumberField("byteOffset", SparseValueByteOffset))
	{
		SparseValueByteOffset = 0;
	}

	TArray64<uint8> SparseBytesValues;
	int64 SparseBufferViewValuesStride;
	if (!GetBufferView(SparseValueBufferViewIndex, SparseBytesValues, SparseBufferViewValuesStride))
	{
		return false;
	}

	if (SparseBufferViewValuesStride == 0)
	{
		SparseBufferViewValuesStride = ElementSize * Elements;
	}

	Stride = SparseBufferViewValuesStride;

	for (int32 IndexToChange = 0; IndexToChange < SparseCount; IndexToChange++)
	{
		uint32 SparseIndexToChange = SparseIndices[IndexToChange];
		if (SparseIndexToChange >= (Bytes.Num() / Stride))
		{
			return false;
		}

		uint8* OriginalValuePtr = (uint8*)(Bytes.GetData() + Stride * SparseIndexToChange);
		uint8* NewValuePtr = (uint8*)(SparseBytesValues.GetData() + SparseBufferViewValuesStride * IndexToChange);
		FMemory::Memcpy(OriginalValuePtr, NewValuePtr, SparseBufferViewValuesStride);
	}

	return true;
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

int64 FglTFRuntimeParser::GetTypeSize(const FString& Type) const
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
	Collector.AddReferencedObjects(MetallicRoughnessMaterialsMap);
	Collector.AddReferencedObjects(SpecularGlossinessMaterialsMap);
}

float FglTFRuntimeParser::FindBestFrames(const TArray<float>& FramesTimes, float WantedTime, int32& FirstIndex, int32& SecondIndex)
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

bool FglTFRuntimeParser::MergePrimitives(TArray<FglTFRuntimePrimitive> SourcePrimitives, FglTFRuntimePrimitive& OutPrimitive)
{
	if (SourcePrimitives.Num() < 1)
	{
		return false;
	}

	FglTFRuntimePrimitive& MainPrimitive = SourcePrimitives[0];
	for (FglTFRuntimePrimitive& SourcePrimitive : SourcePrimitives)
	{
		if (FMath::Clamp(SourcePrimitive.Positions.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Positions.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Normals.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Normals.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Tangents.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Tangents.Num(), 0, 1))
		{
			return false;
		}

		if (FMath::Clamp(SourcePrimitive.Colors.Num(), 0, 1) != FMath::Clamp(MainPrimitive.Colors.Num(), 0, 1))
		{
			return false;
		}

		if (SourcePrimitive.UVs.Num() != MainPrimitive.UVs.Num())
		{
			return false;
		}

		if (SourcePrimitive.Joints.Num() != MainPrimitive.Joints.Num())
		{
			return false;
		}

		if (SourcePrimitive.Weights.Num() != MainPrimitive.Weights.Num())
		{
			return false;
		}

		if (SourcePrimitive.MorphTargets.Num() != MainPrimitive.MorphTargets.Num())
		{
			return false;
		}
	}

	uint32 BaseIndex = 0;
	for (FglTFRuntimePrimitive& SourcePrimitive : SourcePrimitives)
	{
		OutPrimitive.Material = SourcePrimitive.Material;
		for (uint32 Index : SourcePrimitive.Indices)
		{
			OutPrimitive.Indices.Add(Index + BaseIndex);
		}

		if (BaseIndex == 0)
		{
			OutPrimitive.UVs = SourcePrimitive.UVs;
			OutPrimitive.Joints = SourcePrimitive.Joints;
			OutPrimitive.Weights = SourcePrimitive.Weights;
			OutPrimitive.MorphTargets = SourcePrimitive.MorphTargets;
		}
		else
		{
			for (int32 UVChannel = 0; UVChannel < OutPrimitive.UVs.Num(); UVChannel++)
			{
				OutPrimitive.UVs[UVChannel].Append(SourcePrimitive.UVs[UVChannel]);
			}

			for (int32 JointsIndex = 0; JointsIndex < OutPrimitive.Joints.Num(); JointsIndex++)
			{
				OutPrimitive.Joints[JointsIndex].Append(SourcePrimitive.Joints[JointsIndex]);

			}

			for (int32 WeightsIndex = 0; WeightsIndex < OutPrimitive.Weights.Num(); WeightsIndex++)
			{
				OutPrimitive.Weights[WeightsIndex].Append(SourcePrimitive.Weights[WeightsIndex]);
			}

			for (int32 MorphTargetsIndex = 0; MorphTargetsIndex < OutPrimitive.MorphTargets.Num(); MorphTargetsIndex++)
			{
				OutPrimitive.MorphTargets[MorphTargetsIndex].Positions.Append(SourcePrimitive.MorphTargets[MorphTargetsIndex].Positions);
				OutPrimitive.MorphTargets[MorphTargetsIndex].Normals.Append(SourcePrimitive.MorphTargets[MorphTargetsIndex].Normals);
			}
		}

		OutPrimitive.Positions.Append(SourcePrimitive.Positions);
		OutPrimitive.Normals.Append(SourcePrimitive.Normals);
		OutPrimitive.Tangents.Append(SourcePrimitive.Tangents);
		OutPrimitive.Colors.Append(SourcePrimitive.Colors);

		BaseIndex += SourcePrimitive.Positions.Num();
	}

	return true;
}

bool FglTFRuntimeParser::GetMorphTargetNames(const int32 MeshIndex, TArray<FName>& MorphTargetNames)
{
	TSharedPtr<FJsonObject> JsonMeshObject = GetJsonObjectFromRootIndex("meshes", MeshIndex);
	if (!JsonMeshObject)
	{
		AddError("GetMorphTargetNames()", FString::Printf(TEXT("Unable to find Mesh with index %d"), MeshIndex));
		return false;
	}
	// get primitives
	const TArray<TSharedPtr<FJsonValue>>* JsonPrimitives;
	if (!JsonMeshObject->TryGetArrayField("primitives", JsonPrimitives))
	{
		AddError("GetMorphTargetNames()", "No primitives defined in the asset.");
		return false;
	}

	int32 MorphTargetIndex = 0;

	for (TSharedPtr<FJsonValue> JsonPrimitive : *JsonPrimitives)
	{
		TSharedPtr<FJsonObject> JsonPrimitiveObject = JsonPrimitive->AsObject();
		if (!JsonPrimitiveObject)
			return false;

		const TArray<TSharedPtr<FJsonValue>>* JsonTargetsArray;
		if (!JsonPrimitiveObject->TryGetArrayField("targets", JsonTargetsArray))
		{
			AddError("GetMorphTargetNames()", "No MorphTarget defined in the asset.");
			return false;
		}

		for (int32 MorphIndex = 0; MorphIndex < JsonTargetsArray->Num(); MorphIndex++)
		{
			FName MorphTargetName = FName(FString::Printf(TEXT("MorphTarget_%d"), MorphTargetIndex++));
			MorphTargetNames.Add(MorphTargetName);
		}
	}

	// eventually cleanup names using targetNames extras
	const TSharedPtr<FJsonObject>* JsonExtrasObject;
	if (JsonMeshObject->TryGetObjectField("extras", JsonExtrasObject))
	{
		const TArray<TSharedPtr<FJsonValue>>* JsonTargetNamesArray;
		if ((*JsonExtrasObject)->TryGetArrayField("targetNames", JsonTargetNamesArray))
		{
			for (int32 TargetNameIndex = 0; TargetNameIndex < JsonTargetNamesArray->Num(); TargetNameIndex++)
			{
				if (MorphTargetNames.IsValidIndex(TargetNameIndex))
				{
					MorphTargetNames[TargetNameIndex] = FName((*JsonTargetNamesArray)[TargetNameIndex]->AsString());
				}
			}
		}
	}

	return true;
}

bool FglTFRuntimeZipFile::FromData(const uint8* DataPtr, const int64 DataNum)
{
	Data.Append(DataPtr, DataNum);

	// step0: retrieve the trailer magic
	TArray<uint8> Magic;
	bool bIndexFound = false;
	uint64 Index = 0;
	for (Index = Data.Num() - 1; Index >= 0; Index--)
	{
		Magic.Insert(Data[Index], 0);
		if (Magic.Num() == 4)
		{
			if (Magic[0] == 0x50 && Magic[1] == 0x4b && Magic[2] == 0x05 && Magic[3] == 0x06)
			{
				bIndexFound = true;
				break;
			}
			Magic.Pop();
		}
	}

	if (!bIndexFound)
	{
		return false;
	}

	uint16 DiskEntries = 0;
	uint16 TotalEntries = 0;
	uint32 CentralDirectorySize = 0;
	uint32 CentralDirectoryOffset = 0;
	uint16 CommentLen = 0;

	constexpr uint64 TrailerMinSize = 22;
	constexpr uint64 CentralDirectoryMinSize = 46;

	if (Index + TrailerMinSize > Data.Num())
	{
		return false;
	}

	// skip signature and disk data
	Data.Seek(Index + 8);
	Data << DiskEntries;
	Data << TotalEntries;
	Data << CentralDirectorySize;
	Data << CentralDirectoryOffset;
	Data << CommentLen;

	uint16 DirectoryEntries = FMath::Min(DiskEntries, TotalEntries);

	for (uint16 DirectoryIndex = 0; DirectoryIndex < DirectoryEntries; DirectoryIndex++)
	{
		if (CentralDirectoryOffset + CentralDirectoryMinSize > Data.Num())
		{
			return false;
		}

		uint16 FilenameLen = 0;
		uint16 ExtraFieldLen = 0;
		uint16 EntryCommentLen = 0;
		uint32 EntryOffset = 0;

		// seek to FilenameLen
		Data.Seek(CentralDirectoryOffset + 28);
		Data << FilenameLen;
		Data << ExtraFieldLen;
		Data << EntryCommentLen;
		// seek to EntryOffset
		Data.Seek(CentralDirectoryOffset + 42);
		Data << EntryOffset;

		if (CentralDirectoryOffset + CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen > Data.Num())
		{
			return false;
		}

		TArray64<uint8> FilenameBytes;
		FilenameBytes.Append(Data.GetData() + CentralDirectoryOffset + CentralDirectoryMinSize, FilenameLen);
		FilenameBytes.Add(0);

		FString Filename = FString(UTF8_TO_TCHAR(FilenameBytes.GetData()));

		OffsetsMap.Add(Filename, EntryOffset);

		CentralDirectoryOffset += CentralDirectoryMinSize + FilenameLen + ExtraFieldLen + EntryCommentLen;
	}

	return true;
}

bool FglTFRuntimeZipFile::GetFileContent(const FString& Filename, TArray64<uint8>& OutData)
{
	uint32* Offset = OffsetsMap.Find(Filename);
	if (!Offset)
	{
		return false;
	}

	constexpr uint64 LocalEntryMinSize = 30;

	if (*Offset + LocalEntryMinSize > Data.Num())
	{
		return false;
	}

	uint16 Compression = 0;
	uint32 CompressedSize;
	uint32 UncompressedSize = 0;
	uint16 FilenameLen = 0;
	uint16 ExtraFieldLen = 0;

	// seek to Compression
	Data.Seek(*Offset + 8);
	Data << Compression;
	// seek to CompressedSize
	Data.Seek(*Offset + 18);
	Data << CompressedSize;
	Data << UncompressedSize;
	Data << FilenameLen;
	Data << ExtraFieldLen;

	if (*Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen + CompressedSize > Data.Num())
	{
		return false;
	}

	if (Compression == 8)
	{
		OutData.AddUninitialized(UncompressedSize);
		if (!FCompression::UncompressMemory(NAME_Zlib, OutData.GetData(), UncompressedSize, Data.GetData() + *Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen, CompressedSize, COMPRESS_NoFlags, -15))
		{
			return false;
		}
	}
	else if (Compression == 0 && CompressedSize == UncompressedSize)
	{
		OutData.Append(Data.GetData() + *Offset + LocalEntryMinSize + FilenameLen + ExtraFieldLen, UncompressedSize);
	}
	else
	{
		return false;
	}

	return true;
}

bool FglTFRuntimeZipFile::FileExists(const FString& Filename) const
{
	return OffsetsMap.Contains(Filename);
}

FString FglTFRuntimeZipFile::GetFirstFilenameByExtension(const FString& Extension) const
{
	for (const TPair<FString, uint32>& Pair : OffsetsMap)
	{
		if (Pair.Key.EndsWith(Extension, ESearchCase::IgnoreCase))
		{
			return Pair.Key;
		}
	}

	return "";
}

bool FglTFRuntimeParser::GetJsonObjectBytes(TSharedRef<FJsonObject> JsonObject, TArray64<uint8>& Bytes)
{
	FString Uri;
	if (JsonObject->TryGetStringField("uri", Uri))
	{
		// check it is a valid base64 data uri
		if (Uri.StartsWith("data:"))
		{
			if (!ParseBase64Uri(Uri, Bytes))
			{
				return false;
			}
		}
		else
		{
			bool bFound = false;
			if (ZipFile)
			{
				if (ZipFile->GetFileContent(Uri, Bytes))
				{
					bFound = true;
				}
			}

			if (!bFound && !BaseDirectory.IsEmpty())
			{
				if (!FFileHelper::LoadFileToArray(Bytes, *FPaths::Combine(BaseDirectory, Uri)))
				{
					AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to load bytes from uri %s"), *Uri));
					return false;
				}
				bFound = true;
			}

			if (!bFound)
			{
				AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to open uri %s, you may want to enable external files loading..."), *Uri));
				return false;
			}
		}
	}
	else
	{
		int64 BufferViewIndex;
		if (JsonObject->TryGetNumberField("bufferView", BufferViewIndex))
		{
			int64 Stride;
			if (!GetBufferView(BufferViewIndex, Bytes, Stride))
			{
				AddError("GetJsonObjectBytes()", FString::Printf(TEXT("Unable to get bufferView: %d"), BufferViewIndex));
				return false;
			}
		}
	}

	return Bytes.Num() > 0;
}

FVector FglTFRuntimeParser::ComputeTangentY(const FVector Normal, const FVector TangetX)
{
	float Determinant = GetBasisDeterminantSign(Normal.GetSafeNormal(),
		(Normal ^ TangetX).GetSafeNormal(),
		Normal.GetSafeNormal());

	return (Normal ^ TangetX) * Determinant;
}

FVector FglTFRuntimeParser::ComputeTangentYWithW(const FVector Normal, const FVector TangetX, const float W)
{
	return (Normal ^ TangetX) * W;
}