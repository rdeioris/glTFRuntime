// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeEditor.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Basic_BlenderEmpty_Copyright, "glTFRuntime.UnitTests.Basic.BlenderEmpty.Copyright", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Basic_BlenderEmpty_Copyright::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixture32 Fixture("Blender/BlenderEmpty.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Fixture.Blob, LoaderConfig);

	TestTrue("Asset != nullptr", Asset != nullptr);
	TestEqual("Copyright == \"Dummy Copyright Line\"", Asset->GetAssetMeta()["copyright"], "Dummy Copyright Line");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Basic_BlenderEmpty_Scene, "glTFRuntime.UnitTests.Basic.BlenderEmpty.Scene", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Basic_BlenderEmpty_Scene::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixture32 Fixture("Blender/BlenderEmpty.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Fixture.Blob, LoaderConfig);

	TestTrue("Asset != nullptr", Asset != nullptr);
	TestEqual("Scenes.Num() == 1", Asset->GetScenes().Num(), 1);
	TestEqual("Scenes[0].Name == \"Scene\"", Asset->GetScenes()[0].Name, "Scene");
	TestEqual("Asset->Scene == 0", Asset->GetParser()->GetDefaultSceneIndex(), 0);
	FglTFRuntimeScene DefaultScene;
	Asset->GetDefaultScene(DefaultScene);
	TestEqual("Scenes[0].Name == \"Scene\"", DefaultScene.Name, "Scene");

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Basic_BlenderEmpty_SingleNode, "glTFRuntime.UnitTests.Basic.BlenderEmpty.SingleNode", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Basic_BlenderEmpty_SingleNode::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixture32 Fixture("Blender/BlenderSingleNode.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Fixture.Blob, LoaderConfig);

	TestTrue("Asset != nullptr", Asset != nullptr);
	TestEqual("Scenes.Num() == 1", Asset->GetScenes().Num(), 1);
	TestEqual("Scenes[0].Name == \"Scene\"", Asset->GetScenes()[0].Name, "Scene");
	TestEqual("Asset->Scene == 0", Asset->GetParser()->GetDefaultSceneIndex(), 0);
	FglTFRuntimeScene DefaultScene;
	Asset->GetDefaultScene(DefaultScene);
	TestEqual("Scenes[0].Name == \"Scene\"", DefaultScene.Name, "Scene");

	TestEqual("Scenes[0].RootNodesIndices == { 0 }", DefaultScene.RootNodesIndices, { 0 });

	TestEqual("Nodes[0].Name == \"Empty\"", Asset->GetNodes()[0].Name, "Empty");

	return true;
}


IMPLEMENT_SIMPLE_AUTOMATION_TEST(FglTFRuntimeTests_Basic_BlenderEmpty_TwoNodes, "glTFRuntime.UnitTests.Basic.BlenderEmpty.TwoNodes", EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FglTFRuntimeTests_Basic_BlenderEmpty_TwoNodes::RunTest(const FString& Parameters)
{
	glTFRuntime::Tests::FFixture32 Fixture("Blender/BlenderTwoNodes.gltf");

	FglTFRuntimeConfig LoaderConfig;
	UglTFRuntimeAsset* Asset = UglTFRuntimeFunctionLibrary::glTFLoadAssetFromData(Fixture.Blob, LoaderConfig);

	TestTrue("Asset != nullptr", Asset != nullptr);
	TestEqual("Scenes.Num() == 1", Asset->GetScenes().Num(), 1);
	TestEqual("Scenes[0].Name == \"Scene\"", Asset->GetScenes()[0].Name, "Scene");
	TestEqual("Asset->Scene == 0", Asset->GetParser()->GetDefaultSceneIndex(), 0);
	FglTFRuntimeScene DefaultScene;
	Asset->GetDefaultScene(DefaultScene);
	TestEqual("Scenes[0].Name == \"Scene\"", DefaultScene.Name, "Scene");

	TestEqual("Scenes[0].RootNodesIndices == { 0, 1 }", DefaultScene.RootNodesIndices, { 0, 1 });

	TestEqual("Nodes[0].Name == \"Empty\"", Asset->GetNodes()[0].Name, "Empty");
	TestEqual("Nodes[1].Name == \"Empty\"", Asset->GetNodes()[1].Name, "Empty.001");

	return true;
}


#endif
