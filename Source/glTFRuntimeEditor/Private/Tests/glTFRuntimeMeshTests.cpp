// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeEditor.h"
#include "glTFRuntimeAnimationCurve.h"
#include "glTFRuntimeAssetActorAsync.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/AutomationTest.h"
#include "StaticMeshResources.h"

struct FglTFRuntimeAsyncMatrixExpected
{
	FBox InitialWorldBox;
	FBox AnimatedWorldBox;
	float AnimationTime = 0.0f;
};

DEFINE_LATENT_AUTOMATION_COMMAND_FOUR_PARAMETER(FglTFRuntimeWaitForAsyncMatrixActor, TWeakObjectPtr<AglTFRuntimeAssetActorAsync>, Actor, FglTFRuntimeAsyncMatrixExpected, Expected, FAutomationTestBase*, Test, double, StartTime);

bool FglTFRuntimeWaitForAsyncMatrixActor::Update()
{
	AglTFRuntimeAssetActorAsync* ActorPtr = Actor.Get();
	if (!ActorPtr)
	{
		Test->AddError("Async matrix actor became invalid");
		return true;
	}

	TArray<UStaticMeshComponent*> StaticMeshComponents;
	ActorPtr->GetComponents(StaticMeshComponents);
	for (UStaticMeshComponent* StaticMeshComponent : StaticMeshComponents)
	{
		UStaticMesh* StaticMesh = StaticMeshComponent ? StaticMeshComponent->GetStaticMesh() : nullptr;
		if (!StaticMesh || !StaticMesh->GetRenderData() || StaticMesh->GetRenderData()->LODResources.Num() == 0)
		{
			continue;
		}

		// NullRHI commandlets do not retain a CPU-readable position buffer after
		// initialization, so validate the generated local bounds in world space.
		const FBox LocalBox = StaticMesh->GetRenderData()->Bounds.GetBox();
		const FBox ActualWorldBox = LocalBox.TransformBy(StaticMeshComponent->GetComponentTransform().ToMatrixWithScale());

		Test->AddInfo(FString::Printf(TEXT("Expected initial world box: Min=%s Max=%s"), *Expected.InitialWorldBox.Min.ToString(), *Expected.InitialWorldBox.Max.ToString()));
		Test->AddInfo(FString::Printf(TEXT("Actual initial world box:   Min=%s Max=%s"), *ActualWorldBox.Min.ToString(), *ActualWorldBox.Max.ToString()));
		Test->AddInfo(FString::Printf(TEXT("Local mesh box:     Min=%s Max=%s; component=%s"), *LocalBox.Min.ToString(), *LocalBox.Max.ToString(), *StaticMeshComponent->GetComponentTransform().ToHumanReadableString()));
		Test->TestTrue("Async actor corrected initial world-box minimum", ActualWorldBox.Min.Equals(Expected.InitialWorldBox.Min, 0.1));
		Test->TestTrue("Async actor corrected initial world-box maximum", ActualWorldBox.Max.Equals(Expected.InitialWorldBox.Max, 0.1));

		ActorPtr->SeekAnimations(Expected.AnimationTime);
		const FBox AnimatedWorldBox = LocalBox.TransformBy(StaticMeshComponent->GetComponentTransform().ToMatrixWithScale());
		Test->AddInfo(FString::Printf(TEXT("Expected animated world box: Min=%s Max=%s"), *Expected.AnimatedWorldBox.Min.ToString(), *Expected.AnimatedWorldBox.Max.ToString()));
		Test->AddInfo(FString::Printf(TEXT("Actual animated world box:   Min=%s Max=%s"), *AnimatedWorldBox.Min.ToString(), *AnimatedWorldBox.Max.ToString()));
		Test->TestTrue("Async actor corrected animated world-box minimum", AnimatedWorldBox.Min.Equals(Expected.AnimatedWorldBox.Min, 0.1));
		Test->TestTrue("Async actor corrected animated world-box maximum", AnimatedWorldBox.Max.Equals(Expected.AnimatedWorldBox.Max, 0.1));
		ActorPtr->Destroy();
		return true;
	}

	if (FPlatformTime::Seconds() - StartTime > 10.0)
	{
		Test->AddError("Timed out waiting for the async matrix-corrected static mesh");
		ActorPtr->Destroy();
		return true;
	}

	return false;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_Blender_Plane, "glTFRuntime.UnitTests.Mesh.Blender.Plane", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_Blender_Plane::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Blender/BlenderPlane.gltf");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAllowExternalFiles = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 1", LOD.Primitives.Num(), 1);
	TestEqual("LOD.Primitives[0].Indices.Num() == 6", LOD.Primitives[0].Indices.Num(), 6);
	TestEqual("LOD.Primitives[0].Positions.Num() == 4", LOD.Primitives[0].Positions.Num(), 4);
	TestEqual("LOD.Primitives[0].Normals.Num() == 0", LOD.Primitives[0].Normals.Num(), 0);

	TestEqual("LOD.Primitives[0].Indices = { 0, 1, 3, 0, 3, 2 }", LOD.Primitives[0].Indices, { 0, 1, 3, 0, 3, 2 });
	TestEqual("LOD.Primitives[0].Positions = { { -100, -100, 0 }, { -100, 100, 0 }, { 100, -100, 0 }, { 100, 100, 0 } }", LOD.Primitives[0].Positions, { { -100, -100, 0 }, { -100, 100, 0 }, { 100, -100, 0 }, { 100, 100, 0 } });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_Blender_PlaneWeightMaps, "glTFRuntime.UnitTests.Mesh.Blender.PlaneWeightMaps", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_Blender_PlaneWeightMaps::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Blender/BlenderPlaneWeightMaps.gltf");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.bAllowExternalFiles = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	MaterialsConfig.CollectWeightMaps = { "_One", "_Two" };
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 1", LOD.Primitives.Num(), 1);
	TestEqual("LOD.Primitives[0].Indices.Num() == 6", LOD.Primitives[0].Indices.Num(), 6);
	TestEqual("LOD.Primitives[0].Positions.Num() == 4", LOD.Primitives[0].Positions.Num(), 4);
	TestEqual("LOD.Primitives[0].Normals.Num() == 0", LOD.Primitives[0].Normals.Num(), 0);

	TestEqual("LOD.Primitives[0].Indices = { 0, 1, 3, 0, 3, 2 }", LOD.Primitives[0].Indices, { 0, 1, 3, 0, 3, 2 });
	TestEqual("LOD.Primitives[0].Positions = { { -100, -100, 0 }, { -100, 100, 0 }, { 100, -100, 0 }, { 100, 100, 0 } }", LOD.Primitives[0].Positions, { { -100, -100, 0 }, { -100, 100, 0 }, { 100, -100, 0 }, { 100, 100, 0 } });

	TestEqual("LOD.Primitives[0].WeightMaps.Num() == 2", LOD.Primitives[0].WeightMaps.Num(), 2);

	TestEqual("LOD.Primitives[0].WeightMaps[\"_One\"] == { 0.0, 1.0, 2.0, 3.0 }", LOD.Primitives[0].WeightMaps["_One"], { 0.0, 1.0, 2.0, 3.0 });
	TestEqual("LOD.Primitives[0].WeightMaps[\"_Two\"] == { 0.0, 2.0, 4.0, 6.0 }", LOD.Primitives[0].WeightMaps["_Two"], { 0.0, 2.0, 4.0, 6.0 });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_Triangle, "glTFRuntime.UnitTests.Mesh.Triangle", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_Triangle::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Triangle.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 1", LOD.Primitives.Num(), 1);
	TestEqual("LOD.Primitives[0].bHasIndices == false", LOD.Primitives[0].bHasIndices, false);
	TestEqual("LOD.Primitives[0].Indices.Num() == 3", LOD.Primitives[0].Indices.Num(), 3);
	TestEqual("LOD.Primitives[0].Indices = { 0, 1, 2 }", LOD.Primitives[0].Indices, { 0, 1, 2 });
	TestEqual("LOD.Primitives[0].Positions.Num() == 3", LOD.Primitives[0].Positions.Num(), 3);
	TestEqual("LOD.Primitives[0].Normals.Num() == 0", LOD.Primitives[0].Normals.Num(), 0);

	TestEqual("LOD.Primitives[0].Positions = { { -10000, 0, 0 }, { 0, -10000, 0 }, { 0, 10000, 0 } }", LOD.Primitives[0].Positions, { { -10000, 0, 0 }, { 0, -10000, 0 }, { 0, 10000, 0 } });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_TriangleSceneScaled, "glTFRuntime.UnitTests.Mesh.TriangleSceneScaled", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_TriangleSceneScaled::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Triangle.gltf");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.SceneScale = 1;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 1", LOD.Primitives.Num(), 1);
	TestEqual("LOD.Primitives[0].bHasIndices == false", LOD.Primitives[0].bHasIndices, false);
	TestEqual("LOD.Primitives[0].Indices.Num() == 3", LOD.Primitives[0].Indices.Num(), 3);
	TestEqual("LOD.Primitives[0].Indices = { 0, 1, 2 }", LOD.Primitives[0].Indices, { 0, 1, 2 });
	TestEqual("LOD.Primitives[0].Positions.Num() == 3", LOD.Primitives[0].Positions.Num(), 3);
	TestEqual("LOD.Primitives[0].Normals.Num() == 0", LOD.Primitives[0].Normals.Num(), 0);

	TestEqual("LOD.Primitives[0].Positions = { { -100, 0, 0 }, { 0, -100, 0 }, { 0, 100, 0 } }", LOD.Primitives[0].Positions, { { -100, 0, 0 }, { 0, -100, 0 }, { 0, 100, 0 } });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_TriangleIdentity, "glTFRuntime.UnitTests.Mesh.TriangleIdentity", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_TriangleIdentity::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Triangle.gltf");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.SceneScale = 1;
	LoaderConfig.TransformBaseType = EglTFRuntimeTransformBaseType::Identity;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 1", LOD.Primitives.Num(), 1);
	TestEqual("LOD.Primitives[0].bHasIndices == false", LOD.Primitives[0].bHasIndices, false);
	TestEqual("LOD.Primitives[0].Indices.Num() == 3", LOD.Primitives[0].Indices.Num(), 3);
	TestEqual("LOD.Primitives[0].Indices = { 0, 1, 2 }", LOD.Primitives[0].Indices, { 0, 1, 2 });
	TestEqual("LOD.Primitives[0].Positions.Num() == 3", LOD.Primitives[0].Positions.Num(), 3);
	TestEqual("LOD.Primitives[0].Normals.Num() == 0", LOD.Primitives[0].Normals.Num(), 0);

	TestEqual("LOD.Primitives[0].Positions = { { 0, 0, 100 }, { -100, 0, 0 }, { 100, 0, 0 } }", LOD.Primitives[0].Positions, { { 0, 0, 100 }, { -100, 0, 0 }, { 100, 0, 0 } });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_BadMesh, "glTFRuntime.UnitTests.Mesh.BadMesh", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_BadMesh::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("BadMesh.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);

	FglTFRuntimeMaterialsConfig MaterialsConfig;
	FglTFRuntimeMeshLOD LOD;
	Asset->LoadMeshAsRuntimeLOD(0, LOD, MaterialsConfig);

	TestEqual("LOD.Primitives.Num() == 0", LOD.Primitives.Num(), 0);

	TestEqual("Asset->GetErrors() == { \"LoadPrimitive(): POSITION attribute is required\" }", Asset->GetErrors(), { "LoadPrimitive(): POSITION attribute is required" });

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_FullMatrixBake, "glTFRuntime.UnitTests.Mesh.FullMatrixBake", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_FullMatrixBake::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixturePath Fixture("Blender/BlenderPlane.gltf");

	FglTFRuntimeConfig LoaderConfig;
	LoaderConfig.SceneScale = 1;
	LoaderConfig.bAllowExternalFiles = true;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(Fixture.Path, false, LoaderConfig);
	TestNotNull("Asset", Asset);
	if (!Asset)
	{
		return false;
	}

	FglTFRuntimeStaticMeshConfig StaticMeshConfig;
	StaticMeshConfig.CacheMode = EglTFRuntimeCacheMode::None;
	StaticMeshConfig.bApplyMeshBakeTransform = true;
	StaticMeshConfig.MeshBakeTransform = FScaleMatrix(FVector(2.0, 0.5, 1.0)) * FTranslationMatrix(FVector(10.0, 20.0, 30.0));

	UStaticMesh* StaticMesh = Asset->LoadStaticMesh(0, StaticMeshConfig);
	TestNotNull("StaticMesh", StaticMesh);
	if (!StaticMesh)
	{
		return false;
	}

	const FBox BoundingBox = StaticMesh->GetBoundingBox();
	TestTrue("Baked bounding-box minimum", BoundingBox.Min.Equals(FVector(8.0, 19.5, 30.0), KINDA_SMALL_NUMBER));
	TestTrue("Baked bounding-box maximum", BoundingBox.Max.Equals(FVector(12.0, 20.5, 30.0), KINDA_SMALL_NUMBER));

	// Regression for a rotated child below a non-uniformly scaled parent. A
	// component hierarchy loses the scale-axis swap, while the correction matrix
	// restores the full matrix result before the component transform is applied.
	const FTransform ParentTransform(FQuat::Identity, FVector::ZeroVector, FVector(0.2879579365, 4.1493000984, 1.0042906999));
	const FTransform ChildTransform(FQuat(FVector::UpVector, -HALF_PI), FVector::ZeroVector, FVector(0.3277319372, 4.7224183083, 1.3540498018));
	const FMatrix DesiredWorldMatrix = ChildTransform.ToMatrixWithScale() * ParentTransform.ToMatrixWithScale();
	const FMatrix ActualWorldMatrix = (ChildTransform * ParentTransform).ToMatrixWithScale();
	const FMatrix CorrectionMatrix = DesiredWorldMatrix * ActualWorldMatrix.Inverse();
	const FVector TestPosition(100.0, 2.0, 50.0);
	const FVector DesiredPosition = DesiredWorldMatrix.TransformPosition(TestPosition);
	const FVector UncorrectedPosition = ActualWorldMatrix.TransformPosition(TestPosition);
	const FVector CorrectedPosition = ActualWorldMatrix.TransformPosition(CorrectionMatrix.TransformPosition(TestPosition));

	TestFalse("FTransform hierarchy reproduces the full matrix", UncorrectedPosition.Equals(DesiredPosition, 0.01));
	TestTrue("Correction restores the full matrix", CorrectedPosition.Equals(DesiredPosition, 0.01));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Mesh_AsyncActorMatrixHierarchy, "glTFRuntime.UnitTests.Mesh.AsyncActorMatrixHierarchy", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Mesh_AsyncActorMatrixHierarchy::RunTest(const FString& Parameters)
{
	const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("glTFRuntime"));
	TestTrue("glTFRuntime plugin is available", Plugin.IsValid());
	if (!Plugin.IsValid())
	{
		return false;
	}

	const FString AssetFilename = FPaths::Combine(Plugin->GetBaseDir(), TEXT("无标题.glb"));
	if (!FPaths::FileExists(AssetFilename))
	{
		AddWarning(FString::Printf(TEXT("Skipping sample-specific async actor test because '%s' is missing"), *AssetFilename));
		return true;
	}

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(AssetFilename, false, LoaderConfig);
	TestNotNull("Sample asset", Asset);
	if (!Asset)
	{
		return false;
	}

	FglTFRuntimeNode MeshNode;
	TestTrue("Mesh node", Asset->GetNode(0, MeshNode));

	FglTFRuntimeMeshLOD RuntimeLOD;
	FglTFRuntimeMaterialsConfig MaterialsConfig;
	TestTrue("Runtime LOD", Asset->LoadMeshAsRuntimeLOD(MeshNode.MeshIndex, RuntimeLOD, MaterialsConfig));
	if (RuntimeLOD.Primitives.Num() == 0)
	{
		AddError("Sample mesh has no primitives");
		return false;
	}

	UglTFRuntimeAnimationCurve* AnimatedNodeCurve = nullptr;
	int32 AnimatedNodeIndex = INDEX_NONE;
	FglTFRuntimeNode CurrentNode = MeshNode;
	while (CurrentNode.ParentIndex > INDEX_NONE)
	{
		if (!Asset->GetNode(CurrentNode.ParentIndex, CurrentNode))
		{
			AddError("Unable to load a parent node from the sample hierarchy");
			return false;
		}

		if (UglTFRuntimeAnimationCurve* Curve = Asset->LoadNodeAnimationCurve(CurrentNode.Index))
		{
			AnimatedNodeCurve = Curve;
			AnimatedNodeIndex = CurrentNode.Index;
			break;
		}
	}
	TestNotNull("Animated ancestor curve", AnimatedNodeCurve);
	if (!AnimatedNodeCurve)
	{
		return false;
	}

	FglTFRuntimeAsyncMatrixExpected Expected;
	Expected.AnimationTime = AnimatedNodeCurve->glTFCurveAnimationDuration * 0.5f;
	const auto BuildExpectedWorldBox = [&](const float Time)
		{
			FMatrix DesiredWorldMatrix = FMatrix::Identity;
			FglTFRuntimeNode HierarchyNode = MeshNode;
			while (true)
			{
				const FTransform LocalTransform = HierarchyNode.Index == AnimatedNodeIndex
					? AnimatedNodeCurve->GetTransformValue(Time)
					: HierarchyNode.Transform;
				DesiredWorldMatrix *= LocalTransform.ToMatrixWithScale();
				if (HierarchyNode.ParentIndex <= INDEX_NONE || !Asset->GetNode(HierarchyNode.ParentIndex, HierarchyNode))
				{
					break;
				}
			}

			FBox WorldBox(ForceInit);
			for (const FglTFRuntimePrimitive& Primitive : RuntimeLOD.Primitives)
			{
				for (const FVector& Position : Primitive.Positions)
				{
					WorldBox += DesiredWorldMatrix.TransformPosition(Position);
				}
			}
			return WorldBox;
		};

	Expected.InitialWorldBox = BuildExpectedWorldBox(0.0f);
	Expected.AnimatedWorldBox = BuildExpectedWorldBox(Expected.AnimationTime);
	// LoadMeshAsRuntimeLOD transfers its cached LOD into RuntimeLOD, so use a
	// fresh parser instance for the actor path under test.
	UglTFRuntimeAsset* ActorAsset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromFilename(AssetFilename, false, LoaderConfig);
	TestNotNull("Actor sample asset", ActorAsset);
	if (!ActorAsset)
	{
		return false;
	}

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	TestNotNull("Editor world", World);
	if (!World)
	{
		return false;
	}

	AglTFRuntimeAssetActorAsync* Actor = World->SpawnActorDeferred<AglTFRuntimeAssetActorAsync>(AglTFRuntimeAssetActorAsync::StaticClass(), FTransform::Identity);
	TestNotNull("Async actor", Actor);
	if (!Actor)
	{
		return false;
	}

	Actor->SetFlags(RF_Transient);
	Actor->Asset = ActorAsset;
	// Exercise stale/existing Blueprint settings: a static asset must still be
	// corrected even if node animations are allowed and the new option is false.
	Actor->bAllowNodeAnimations = true;
	Actor->bPreserveStaticMeshTransformMatrices = false;
	Actor->bAutoPlayAnimations = false;
	Actor->StaticMeshConfig.bAllowCPUAccess = true;
	Actor->FinishSpawning(FTransform::Identity);
	Actor->DispatchBeginPlay();

	ADD_LATENT_AUTOMATION_COMMAND(FglTFRuntimeWaitForAsyncMatrixActor(Actor, Expected, this, FPlatformTime::Seconds()));
	return true;
}

#endif
