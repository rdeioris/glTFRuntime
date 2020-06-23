// Copyright 2020, Roberto De Ioris


#include "glTFRuntimeParser.h"

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromFilename(const FString Filename)
{
	FString JsonData;
	if (!FFileHelper::LoadFileToString(JsonData, *Filename))
	{
		return nullptr;
	}

	return FromString(JsonData);
}

TSharedPtr<FglTFRuntimeParser> FglTFRuntimeParser::FromString(const FString JsonData)
{
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

bool FglTFRuntimeParser::CheckJsonIndex(TSharedRef<FJsonObject> JsonObject, const FString FieldName, const int32 Index, TArray<TSharedRef<FJsonValue>>& JsonItems)
{
	if (Index < 0)
		return false;

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

TSharedPtr<FJsonObject> FglTFRuntimeParser::GetJsonObjectFromIndex(TSharedRef<FJsonObject> JsonObject, const FString FieldName, const int32 Index)
{
	TArray<TSharedRef<FJsonValue>> JsonArray;
	if (!CheckJsonIndex(JsonObject, FieldName, Index, JsonArray))
	{
		return nullptr;
	}

	return JsonArray[Index]->AsObject();
}

FString FglTFRuntimeParser::GetJsonObjectString(TSharedRef<FJsonObject> JsonObject, const FString FieldName, const FString DefaultValue)
{
	FString Value;
	if (!JsonObject->TryGetStringField(FieldName, Value))
	{
		return DefaultValue;
	}
	return Value;
}

int32 FglTFRuntimeParser::GetJsonObjectIndex(TSharedRef<FJsonObject> JsonObject, const FString FieldName, const int32 DefaultValue)
{
	int64 Value;
	if (!JsonObject->TryGetNumberField(FieldName, Value))
	{
		return DefaultValue;
	}
	return (int32)Value;
}

bool FglTFRuntimeParser::LoadScene(int32 SceneIndex, FglTFRuntimeScene& Scene)
{
	TSharedPtr<FJsonObject> JsonSceneObject = GetJsonObjectFromRootIndex("scenes", SceneIndex);
	if (!JsonSceneObject)
		return false;

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

void FglTFRuntimeParser::AddError(const FString ErrorContext, const FString ErrorMessage)
{
	Errors.Add(ErrorContext + ": " + ErrorMessage);
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

		UE_LOG(LogTemp, Error, TEXT("--- --- ---"));
		for (int32 i = 0; i < Timeline.Num(); i++)
		{
			UE_LOG(LogTemp, Error, TEXT("%f = %f, %f, %f, %f"), Timeline[i], Values[i].X, Values[i].Y, Values[i].Z, Values[i].W);
		}
		UE_LOG(LogTemp, Error, TEXT("^^^ ^^^ ^^^"));

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

	AnimationCurve->SetDefaultValues(Node.Transform.GetLocation(), Node.Transform.Rotator().Euler(), Node.Transform.GetScale3D());

	bool bAnimationFound = false;

	auto Callback = [&](const FglTFRuntimeNode& Node, const FString& Path, const TArray<float> Timeline, const TArray<FVector4> Values)
	{
		if (Path == "translation")
		{
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddLocationValue(Timeline[TimeIndex], SceneBasis.TransformPosition(Values[TimeIndex]) * SceneScale, ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "rotation")
		{
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				FVector4 RotationValue = Values[TimeIndex];
				FQuat BaseQuat(RotationValue.X, RotationValue.Y, RotationValue.Z, RotationValue.W);
				FMatrix RotationMatrix = SceneBasis.Inverse() * FQuatRotationMatrix(BaseQuat) * SceneBasis;
				AnimationCurve->AddRotationValue(Timeline[TimeIndex], RotationMatrix.Rotator().Euler(), ERichCurveInterpMode::RCIM_Linear);
			}
		}
		else if (Path == "scale")
		{
			for (int32 TimeIndex = 0; TimeIndex < Timeline.Num(); TimeIndex++)
			{
				AnimationCurve->AddScaleValue(Timeline[TimeIndex], (SceneBasis.Inverse() * FScaleMatrix(Values[TimeIndex]) * SceneBasis).ExtractScaling(), ERichCurveInterpMode::RCIM_Linear);
			}
		}
		bAnimationFound = true;
	};

	for (int32 JsonAnimationIndex = 0; JsonAnimationIndex < JsonAnimations->Num(); JsonAnimationIndex++)
	{
		TSharedPtr<FJsonObject> JsonAnimationObject = (*JsonAnimations)[JsonAnimationIndex]->AsObject();
		if (!JsonAnimationObject)
			return false;
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
			return AnimationCurve;
		}
	}

	return nullptr;
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

	if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "POSITION", Primitive.Positions,
		{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector {return SceneBasis.TransformPosition(Value) * SceneScale; }))
	{
		AddError("LoadPrimitive()", "Unable to load POSITION attribute");
		return false;
	}

	if ((*JsonAttributesObject)->HasField("NORMAL"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "NORMAL", Primitive.Normals,
			{ 3 }, { 5126 }, false, [&](FVector Value) -> FVector { return SceneBasis.TransformVector(Value); }))
		{
			AddError("LoadPrimitive()", "Unable to load NORMAL attribute");
			return false;
		}
	}

	if ((*JsonAttributesObject)->HasField("TANGENT"))
	{
		if (!BuildFromAccessorField(JsonAttributesObject->ToSharedRef(), "TANGENT", Primitive.Tangents,
			{ 4 }, { 5126 }, false, [&](FVector4 Value) -> FVector4 { return SceneBasis.TransformVector(Value); }))
		{
			AddError("LoadPrimitive()", "Unable to load TANGENT attribute");
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
			AddError("LoadMaterial()", FString::Printf(TEXT("Unable to load material %lld"), MaterialIndex));
			return false;
		}
	}
	else
	{
		Primitive.Material = UMaterial::GetDefaultMaterial(MD_Surface);
	}

	return true;
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