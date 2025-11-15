// Copyright 2025 - Roberto De Ioris

#if WITH_DEV_AUTOMATION_TESTS
#include "glTFRuntimeEditor.h"
#include "glTFRuntimeFunctionLibrary.h"
#include "Misc/AutomationTest.h"

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

#endif
