// Copyright 2020-2023, Roberto De Ioris.

#include "glTFRuntimeAsset.h"
#include "Animation/AnimSequence.h"
#include "Engine/World.h"
#include "Runtime/Launch/Resources/Version.h"

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

namespace glTFRuntime
{
	bool LoadCubeMapMipsFromBlob(TSharedRef<FglTFRuntimeParser> Parser, const FglTFRuntimeImagesConfig& ImagesConfig, const bool bSpherical, TArray<FglTFRuntimeMipMap>& MipsXP, TArray<FglTFRuntimeMipMap>& MipsXN, TArray<FglTFRuntimeMipMap>& MipsYP, TArray<FglTFRuntimeMipMap>& MipsYN, TArray<FglTFRuntimeMipMap>& MipsZP, TArray<FglTFRuntimeMipMap>& MipsZN)
	{
		TArray64<uint8> UncompressedBytes;
		int32 Width = 0;
		int32 Height = 0;
		EPixelFormat PixelFormat;

		if (!Parser->LoadImageFromBlob(Parser->GetBlob(), MakeShared<FJsonObject>(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig))
		{
			return false;
		}

		if (Width <= 0 || Height <= 0)
		{
			return false;
		}

		if (bSpherical)
		{
			const int32 Resolution = Height;
#if ENGINE_MAJOR_VERSION >= 5
			auto GetCubemapFace = [Resolution, Width, Height, PixelFormat](const TArray64<uint8>& Pixels, const FVector3f Start, const FVector3f Right, const FVector3f Up, TArray64<uint8>& OutPixels)
#else
			auto GetCubemapFace = [Resolution, Width, Height, PixelFormat](const TArray64<uint8>& Pixels, const FVector Start, const FVector Right, const FVector Up, TArray64<uint8>& OutPixels)
#endif
				{
					const int64 Pitch = Resolution * GPixelFormats[PixelFormat].BlockBytes;
					OutPixels.AddUninitialized(Pitch * Resolution);

					ParallelFor(Resolution, [&](const int32 PixelY)
						{
							for (int32 PixelX = 0; PixelX < Resolution; PixelX++)
							{
								const int64 Offset = PixelY * Pitch + (PixelX * GPixelFormats[PixelFormat].BlockBytes);

								float PX = Start.X + (PixelX * 2 + 0.5) / Resolution * Right.X + (PixelY * 2 + 0.5) / Resolution * Up.X;
								float PY = Start.Y + (PixelX * 2 + 0.5) / Resolution * Right.Y + (PixelY * 2 + 0.5) / Resolution * Up.Y;
								float PZ = Start.Z + (PixelX * 2 + 0.5) / Resolution * Right.Z + (PixelY * 2 + 0.5) / Resolution * Up.Z;

								float Azimuth = FMath::Atan2(PX, -PZ) + PI;
								float Elevation = FMath::Atan(PY / FMath::Sqrt(PX * PX + PZ * PZ)) + PI / 2;

								float X1 = (Azimuth / PI / 2) * Width;
								float Y1 = (Elevation / PI) * Height;

								float IX;
								float FX = FMath::Modf(X1 - 0.5, &IX);

								float IY;
								float FY = FMath::Modf(Y1 - 0.5, &IY);

								int32 X2 = static_cast<int32>(IX);
								int32 Y2 = static_cast<int32>(IY);
								int32 X3 = 0;
								int32 Y3 = 0;

								if (FX < 0)
								{
									X3 = Width - 1;
								}
								else if (X2 == Width - 1)
								{
									X3 = 0;
								}
								else
								{
									X3 = X2 + 1;
								}

								if (FY < 0)
								{
									Y3 = Height - 1;
								}
								else if (Y2 == Height - 1)
								{
									Y3 = 0;
								}
								else
								{
									Y3 = Y2 + 1;
								}

								FX = FMath::Abs(FX);
								FY = FMath::Abs(FY);

#if ENGINE_MAJOR_VERSION >= 5
								if (PixelFormat == EPixelFormat::PF_FloatRGB)
								{
									const FFloat16* Colors = reinterpret_cast<const FFloat16*>(Pixels.GetData());
									const int64 Offset00 = Y2 * (Width * 3) + (X2 * 3);
									const FVector3f Color00 = FVector3f(Colors[Offset00], Colors[Offset00 + 1], Colors[Offset00 + 2]);
									const int64 Offset10 = Y2 * (Width * 3) + (X3 * 3);
									const FVector3f Color10 = FVector3f(Colors[Offset10], Colors[Offset10 + 1], Colors[Offset10 + 2]);

									const int64 Offset01 = Y3 * (Width * 3) + (X2 * 3);
									const FVector3f Color01 = FVector3f(Colors[Offset01], Colors[Offset01 + 1], Colors[Offset01 + 2]);
									const int64 Offset11 = Y3 * (Width * 3) + (X3 * 3);
									const FVector3f Color11 = FVector3f(Colors[Offset11], Colors[Offset11 + 1], Colors[Offset11 + 2]);

									FVector3f Color = FMath::BiLerp(Color00, Color10, Color01, Color11, FX, FY);

									FFloat16Color Color16 = FLinearColor(Color);


									FMemory::Memcpy(OutPixels.GetData() + Offset, &Color16, sizeof(FFloat16) * 3);
								}
								else if (PixelFormat == EPixelFormat::PF_FloatRGBA)
								{
									const FFloat16* Colors = reinterpret_cast<const FFloat16*>(Pixels.GetData());
									const int64 Offset00 = Y2 * (Width * 4) + (X2 * 4);
									const FVector4f Color00 = FVector4f(Colors[Offset00], Colors[Offset00 + 1], Colors[Offset00 + 2], Colors[Offset00 + 3]);
									const int64 Offset10 = Y2 * (Width * 4) + (X3 * 4);
									const FVector4f Color10 = FVector4f(Colors[Offset10], Colors[Offset10 + 1], Colors[Offset10 + 2], Colors[Offset10 + 3]);

									const int64 Offset01 = Y3 * (Width * 4) + (X2 * 4);
									const FVector4f Color01 = FVector4f(Colors[Offset01], Colors[Offset01 + 1], Colors[Offset01 + 2], Colors[Offset01 + 3]);
									const int64 Offset11 = Y3 * (Width * 4) + (X3 * 4);
									const FVector4f Color11 = FVector4f(Colors[Offset11], Colors[Offset11 + 1], Colors[Offset11 + 2], Colors[Offset11 + 3]);

									FVector4f Color = FMath::BiLerp(Color00, Color10, Color01, Color11, FX, FY);

									FFloat16Color Color16 = FLinearColor(Color);

									FMemory::Memcpy(OutPixels.GetData() + Offset, &Color16, sizeof(FFloat16) * 4);
								}

#else
								if (PixelFormat == EPixelFormat::PF_FloatRGB)
								{
									const FFloat16* Colors = reinterpret_cast<const FFloat16*>(Pixels.GetData());
									const int64 Offset00 = Y2 * (Width * 3) + (X2 * 3);
									const FVector Color00 = FVector(Colors[Offset00], Colors[Offset00 + 1], Colors[Offset00 + 2]);
									const int64 Offset10 = Y2 * (Width * 3) + (X3 * 3);
									const FVector Color10 = FVector(Colors[Offset10], Colors[Offset10 + 1], Colors[Offset10 + 2]);

									const int64 Offset01 = Y3 * (Width * 3) + (X2 * 3);
									const FVector Color01 = FVector(Colors[Offset01], Colors[Offset01 + 1], Colors[Offset01 + 2]);
									const int64 Offset11 = Y3 * (Width * 3) + (X3 * 3);
									const FVector Color11 = FVector(Colors[Offset11], Colors[Offset11 + 1], Colors[Offset11 + 2]);

									FVector Color = FMath::BiLerp(Color00, Color10, Color01, Color11, FX, FY);

									FFloat16Color Color16 = FLinearColor(Color);


									FMemory::Memcpy(OutPixels.GetData() + Offset, &Color16, sizeof(FFloat16) * 3);
								}
								else if (PixelFormat == EPixelFormat::PF_FloatRGBA)
								{
									const FFloat16* Colors = reinterpret_cast<const FFloat16*>(Pixels.GetData());
									const int64 Offset00 = Y2 * (Width * 4) + (X2 * 4);
									const FVector4 Color00 = FVector4(Colors[Offset00], Colors[Offset00 + 1], Colors[Offset00 + 2], Colors[Offset00 + 3]);
									const int64 Offset10 = Y2 * (Width * 4) + (X3 * 4);
									const FVector4 Color10 = FVector4(Colors[Offset10], Colors[Offset10 + 1], Colors[Offset10 + 2], Colors[Offset10 + 3]);

									const int64 Offset01 = Y3 * (Width * 4) + (X2 * 4);
									const FVector4 Color01 = FVector4(Colors[Offset01], Colors[Offset01 + 1], Colors[Offset01 + 2], Colors[Offset01 + 3]);
									const int64 Offset11 = Y3 * (Width * 4) + (X3 * 4);
									const FVector4 Color11 = FVector4(Colors[Offset11], Colors[Offset11 + 1], Colors[Offset11 + 2], Colors[Offset11 + 3]);

									FVector4 Color = FMath::BiLerp(Color00, Color10, Color01, Color11, FX, FY);

									FFloat16Color Color16 = FLinearColor(Color);

									FMemory::Memcpy(OutPixels.GetData() + Offset, &Color16, sizeof(FFloat16) * 4);
								}
#endif
								else
								{

								}
							}
						});
				};

			FglTFRuntimeMipMap MipXP(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { 1.0f, -1.0f, -1.0f }, { 0.0f, 0.0f, 1.0f }, { 0.0f, 1.0f, 0.0f }, MipXP.Pixels);
			MipsXP.Add(MoveTemp(MipXP));

			FglTFRuntimeMipMap MipXN(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { -1.0f, -1.0f, 1.0f }, { 0.0f, 0.0f, -1.0f }, { 0.0f, 1.0f, 0.0f }, MipXN.Pixels);
			MipsXN.Add(MoveTemp(MipXN));

			FglTFRuntimeMipMap MipYP(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { -1.0f, -1.0f, 1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, -1.0f }, MipYP.Pixels);
			MipsYP.Add(MoveTemp(MipYP));

			FglTFRuntimeMipMap MipYN(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { -1.0f, 1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 0.0f, 1.0f }, MipYN.Pixels);
			MipsYN.Add(MoveTemp(MipYN));

			FglTFRuntimeMipMap MipZP(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { -1.0f, -1.0f, -1.0f }, { 1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, MipZP.Pixels);
			MipsZP.Add(MoveTemp(MipZP));

			FglTFRuntimeMipMap MipZN(-1, PixelFormat, Resolution, Resolution);
			GetCubemapFace(UncompressedBytes, { 1.0f, -1.0f, 1.0f }, { -1.0f, 0.0f, 0.0f }, { 0.0f, 1.0f, 0.0f }, MipZN.Pixels);
			MipsZN.Add(MoveTemp(MipZN));
		}
		else
		{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
			int64 ImageSize = GPixelFormats[PixelFormat].Get2DImageSizeInBytes(Width, Height);
#else
			const int64 BlockWidth = (Width + GPixelFormats[PixelFormat].BlockSizeX - 1) / GPixelFormats[PixelFormat].BlockSizeX;
			const int64 BlockHeight = (Width + GPixelFormats[PixelFormat].BlockSizeY - 1) / GPixelFormats[PixelFormat].BlockSizeY;
			int64 ImageSize = BlockWidth * BlockHeight * GPixelFormats[PixelFormat].BlockBytes;
#endif
			int32 NumberOfSlices = UncompressedBytes.Num() / ImageSize;
			if (NumberOfSlices != 6)
			{
				Parser->AddError("LoadCubeMapMipsFromBlob", "Expected 6 slices in the texture");
				return false;
			}

			FglTFRuntimeMipMap MipXP(-1, PixelFormat, Width, Height);
			MipXP.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 0), ImageSize);
			MipsXP.Add(MoveTemp(MipXP));
			FglTFRuntimeMipMap MipXN(-1, PixelFormat, Width, Height);
			MipXN.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 1), ImageSize);
			MipsXN.Add(MoveTemp(MipXN));

			FglTFRuntimeMipMap MipYP(-1, PixelFormat, Width, Height);
			MipYP.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 2), ImageSize);
			MipsYP.Add(MoveTemp(MipYP));
			FglTFRuntimeMipMap MipYN(-1, PixelFormat, Width, Height);
			MipYN.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 3), ImageSize);
			MipsYN.Add(MoveTemp(MipYN));

			FglTFRuntimeMipMap MipZP(-1, PixelFormat, Width, Height);
			MipZP.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 4), ImageSize);
			MipsZP.Add(MoveTemp(MipZP));
			FglTFRuntimeMipMap MipZN(-1, PixelFormat, Width, Height);
			MipZN.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * 5), ImageSize);
			MipsZN.Add(MoveTemp(MipZN));
		}

		return true;
	}
}

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

bool UglTFRuntimeAsset::SetParser(TSharedRef<FglTFRuntimeParser> InParser)
{
	// asset already loaded ?
	if (Parser)
	{
		return false;
	}

	Parser = InParser;
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

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadStaticMeshRecursive(NodeName, ExcludeNodes, StaticMeshConfig);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshLODs(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadStaticMeshLODs(MeshIndices, StaticMeshConfig);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMeshLODs(const TArray<int32>& MeshIndices, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
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

void UglTFRuntimeAsset::LoadSkeletalMeshAsync(const int32 MeshIndex, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadSkeletalMeshAsync(MeshIndex, SkinIndex, AsyncCallback, SkeletalMeshConfig);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMeshRecursive(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeletalMeshRecursive(NodeName, SkeletalMeshConfig.OverrideSkinIndex, ExcludeNodes, SkeletalMeshConfig, TransformApplyRecursiveMode);
}

void UglTFRuntimeAsset::LoadSkeletalMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadSkeletalMeshRecursiveAsync(NodeName, SkeletalMeshConfig.OverrideSkinIndex, ExcludeNodes, AsyncCallback, SkeletalMeshConfig, TransformApplyRecursiveMode);
}

void UglTFRuntimeAsset::LoadStaticMeshRecursiveAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadStaticMeshRecursiveAsync(NodeName, ExcludeNodes, AsyncCallback, StaticMeshConfig);
}

USkeleton* UglTFRuntimeAsset::LoadSkeleton(const int32 SkinIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	return Parser->LoadSkeleton(SkinIndex, SkeletonConfig);
}

USkeleton* UglTFRuntimeAsset::LoadSkeletonFromNodeTree(const int32 NodeIndex, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	FglTFRuntimeNode Node;
	if (!Parser->LoadNode(NodeIndex, Node))
	{
		return nullptr;
	}

	return Parser->LoadSkeletonFromNode(Node, SkeletonConfig);
}

USkeleton* UglTFRuntimeAsset::LoadSkeletonFromNodeTreeByName(const FString& NodeName, const FglTFRuntimeSkeletonConfig& SkeletonConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	FglTFRuntimeNode Node;
	if (!Parser->LoadNodeByName(NodeName, Node))
	{
		return nullptr;
	}

	return Parser->LoadSkeletonFromNode(Node, SkeletonConfig);
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
		{
			return false;
		}

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

TMap<FString, UAnimSequence*> UglTFRuntimeAsset::LoadNodeSkeletalAnimationsMap(USkeletalMesh* SkeletalMesh, const int32 NodeIndex, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	TMap<FString, UAnimSequence*> EmptyMap;
	GLTF_CHECK_PARSER(EmptyMap);

	return Parser->LoadNodeSkeletalAnimationsMap(SkeletalMesh, NodeIndex, SkeletalAnimationConfig);
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
	return Parser->LoadMaterial(MaterialIndex, MaterialsConfig, bUseVertexColors, MaterialName, nullptr);
}

UAnimSequence* UglTFRuntimeAsset::CreateSkeletalAnimationFromPath(USkeletalMesh* SkeletalMesh, const TArray<FglTFRuntimePathItem>& BonesPath, const TArray<FglTFRuntimePathItem>& MorphTargetsPath, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	return Parser->CreateSkeletalAnimationFromPath(SkeletalMesh, BonesPath, MorphTargetsPath, SkeletalAnimationConfig);
}

FString UglTFRuntimeAsset::GetStringFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER("");
	return Parser->GetJSONStringFromPath(Path, bFound);
}

int64 UglTFRuntimeAsset::GetIntegerFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(0);
	return static_cast<int64>(Parser->GetJSONNumberFromPath(Path, bFound));
}

float UglTFRuntimeAsset::GetFloatFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(0);
	return static_cast<float>(Parser->GetJSONNumberFromPath(Path, bFound));
}

bool UglTFRuntimeAsset::GetBooleanFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetJSONBooleanFromPath(Path, bFound);
}

int32 UglTFRuntimeAsset::GetArraySizeFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(-1);
	return Parser->GetJSONArraySizeFromPath(Path, bFound);
}

FVector4 UglTFRuntimeAsset::GetVectorFromPath(const TArray<FglTFRuntimePathItem>& Path, bool& bFound) const
{
	GLTF_CHECK_PARSER(FVector4(0, 0, 0, 0));
	return Parser->GetJSONVectorFromPath(Path, bFound);
}


bool UglTFRuntimeAsset::LoadAudioEmitter(const int32 EmitterIndex, FglTFRuntimeAudioEmitter& Emitter)
{
	GLTF_CHECK_PARSER(false);
	return Parser->LoadAudioEmitter(EmitterIndex, Emitter);
}

ULightComponent* UglTFRuntimeAsset::LoadPunctualLight(const int32 PunctualLightIndex, AActor* Actor, const FglTFRuntimeLightConfig& LightConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	return Parser->LoadPunctualLight(PunctualLightIndex, Actor, LightConfig);
}

bool UglTFRuntimeAsset::LoadEmitterIntoAudioComponent(const FglTFRuntimeAudioEmitter& Emitter, UAudioComponent* AudioComponent)
{
	GLTF_CHECK_PARSER(false);
	return Parser->LoadEmitterIntoAudioComponent(Emitter, AudioComponent);
}

void UglTFRuntimeAsset::LoadStaticMeshAsync(const int32 MeshIndex, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadStaticMeshAsync(MeshIndex, AsyncCallback, StaticMeshConfig);
}

void UglTFRuntimeAsset::LoadMeshAsRuntimeLODAsync(const int32 MeshIndex, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadMeshAsRuntimeLODAsync(MeshIndex, AsyncCallback, MaterialsConfig);
}

void UglTFRuntimeAsset::LoadStaticMeshLODsAsync(const TArray<int32>& MeshIndices, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
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

int32 UglTFRuntimeAsset::GetNumAnimations() const
{
	GLTF_CHECK_PARSER(0);

	return Parser->GetNumAnimations();
}

UTexture2D* UglTFRuntimeAsset::LoadImage(const int32 ImageIndex, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray64<uint8> UncompressedBytes;
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat PixelFormat;
	if (!Parser->LoadImage(ImageIndex, UncompressedBytes, Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width > 0 && Height > 0)
	{
		FglTFRuntimeMipMap Mip(-1);
		Mip.Pixels = UncompressedBytes;
		Mip.Width = Width;
		Mip.Height = Height;
		Mip.PixelFormat = PixelFormat;
		TArray<FglTFRuntimeMipMap> Mips = { Mip };
		return Parser->BuildTexture(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler());
	}

	return nullptr;
}

UTextureCube* UglTFRuntimeAsset::LoadCubeMap(const int32 ImageIndexXP, const int32 ImageIndexXN, const int32 ImageIndexYP, const int32 ImageIndexYN, const int32 ImageIndexZP, const int32 ImageIndexZN, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray64<uint8> UncompressedBytes[6];
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat PixelFormat;
	int32 CurrentWidth = 0;
	int32 CurrentHeight = 0;
	if (!Parser->LoadImage(ImageIndexXP, UncompressedBytes[0], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width <= 0 || Height <= 0)
	{
		return nullptr;
	}

	CurrentWidth = Width;
	CurrentHeight = Height;

	if (!Parser->LoadImage(ImageIndexXN, UncompressedBytes[1], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width != CurrentWidth || Height != CurrentHeight)
	{
		return nullptr;
	}

	if (!Parser->LoadImage(ImageIndexYP, UncompressedBytes[2], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width != CurrentWidth || Height != CurrentHeight)
	{
		return nullptr;
	}

	if (!Parser->LoadImage(ImageIndexYN, UncompressedBytes[3], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width != CurrentWidth || Height != CurrentHeight)
	{
		return nullptr;
	}

	if (!Parser->LoadImage(ImageIndexZP, UncompressedBytes[4], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width != CurrentWidth || Height != CurrentHeight)
	{
		return nullptr;
	}

	if (!Parser->LoadImage(ImageIndexZN, UncompressedBytes[5], Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width != CurrentWidth || Height != CurrentHeight)
	{
		return nullptr;
	}


	FglTFRuntimeMipMap MipXP(-1, Width, Height, UncompressedBytes[0]);
	FglTFRuntimeMipMap MipXN(-1, Width, Height, UncompressedBytes[1]);
	FglTFRuntimeMipMap MipYP(-1, Width, Height, UncompressedBytes[2]);
	FglTFRuntimeMipMap MipYN(-1, Width, Height, UncompressedBytes[3]);
	FglTFRuntimeMipMap MipZP(-1, Width, Height, UncompressedBytes[4]);
	FglTFRuntimeMipMap MipZN(-1, Width, Height, UncompressedBytes[5]);


	TArray<FglTFRuntimeMipMap> MipsXP = { MipXP };
	TArray<FglTFRuntimeMipMap> MipsXN = { MipXN };
	TArray<FglTFRuntimeMipMap> MipsYP = { MipYP };
	TArray<FglTFRuntimeMipMap> MipsYN = { MipYN };
	TArray<FglTFRuntimeMipMap> MipsZP = { MipZP };
	TArray<FglTFRuntimeMipMap> MipsZN = { MipZN };
	return Parser->BuildTextureCube(this, MipsXP, MipsXN, MipsYP, MipsYN, MipsZP, MipsZN, bAutoRotate, ImagesConfig, FglTFRuntimeTextureSampler());
}

UTexture2DArray* UglTFRuntimeAsset::LoadImageArray(const TArray<int32>& ImageIndices, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	TArray<FglTFRuntimeMipMap> Mips;

	for (const int32 ImageIndex : ImageIndices)
	{
		FglTFRuntimeMipMap MipMap(-1);
		if (!Parser->LoadImage(ImageIndex, MipMap.Pixels, MipMap.Width, MipMap.Height, MipMap.PixelFormat, ImagesConfig))
		{
			return nullptr;
		}

		Mips.Add(MipMap);
	}

	return Parser->BuildTextureArray(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler());
}

UTexture2D* UglTFRuntimeAsset::LoadImageFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray64<uint8> UncompressedBytes;
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat PixelFormat;
	if (!Parser->LoadImageFromBlob(Parser->GetBlob(), MakeShared<FJsonObject>(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width > 0 && Height > 0)
	{
		FglTFRuntimeMipMap Mip(-1);
		Mip.Pixels = MoveTemp(UncompressedBytes);
		Mip.Width = Width;
		Mip.Height = Height;
		Mip.PixelFormat = PixelFormat;
		TArray<FglTFRuntimeMipMap> Mips;
		Mips.Add(MoveTemp(Mip));
		return Parser->BuildTexture(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler());
	}

	return nullptr;
}

void UglTFRuntimeAsset::LoadImageFromBlobAsync(const FglTFRuntimeTexture2DAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	Async(EAsyncExecution::Thread, [this, ImagesConfig, AsyncCallback]()
		{
			TArray64<uint8> UncompressedBytes;
			int32 Width = 0;
			int32 Height = 0;
			EPixelFormat PixelFormat;

			if (!Parser ||
				!Parser->LoadImageFromBlob(Parser->GetBlob(), MakeShared<FJsonObject>(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig) ||
				Width <= 0 ||
				Height <= 0)
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&, AsyncCallback]()
					{
						AsyncCallback.ExecuteIfBound(nullptr);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

			FglTFRuntimeMipMap Mip(-1);
			Mip.Pixels = MoveTemp(UncompressedBytes);
			Mip.Width = Width;
			Mip.Height = Height;
			Mip.PixelFormat = PixelFormat;
			TArray<FglTFRuntimeMipMap> Mips;
			Mips.Add(MoveTemp(Mip));

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					AsyncCallback.ExecuteIfBound(Parser->BuildTexture(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler()));
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		}
	);
}

UTexture2DArray* UglTFRuntimeAsset::LoadImageArrayFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray64<uint8> UncompressedBytes;
	int32 Width = 0;
	int32 Height = 0;
	EPixelFormat PixelFormat;
	if (!Parser->LoadImageFromBlob(Parser->GetBlob(), MakeShared<FJsonObject>(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig))
	{
		return nullptr;
	}

	if (Width > 0 && Height > 0)
	{
#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
		int64 ImageSize = GPixelFormats[PixelFormat].Get2DImageSizeInBytes(Width, Height);
#else
		const int64 BlockWidth = (Width + GPixelFormats[PixelFormat].BlockSizeX - 1) / GPixelFormats[PixelFormat].BlockSizeX;
		const int64 BlockHeight = (Width + GPixelFormats[PixelFormat].BlockSizeY - 1) / GPixelFormats[PixelFormat].BlockSizeY;
		int64 ImageSize = BlockWidth * BlockHeight * GPixelFormats[PixelFormat].BlockBytes;
#endif
		int32 NumberOfSlices = UncompressedBytes.Num() / ImageSize;
		TArray<FglTFRuntimeMipMap> Mips;

		for (int32 Slice = 0; Slice < NumberOfSlices; Slice++)
		{
			FglTFRuntimeMipMap Mip(-1);
			Mip.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * Slice), ImageSize);
			Mip.Width = Width;
			Mip.Height = Height;
			Mip.PixelFormat = PixelFormat;

			Mips.Add(MoveTemp(Mip));
		}

		return Parser->BuildTextureArray(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler());
	}

	return nullptr;
}

void UglTFRuntimeAsset::LoadImageArrayFromBlobAsync(const FglTFRuntimeTexture2DArrayAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	Async(EAsyncExecution::Thread, [this, ImagesConfig, AsyncCallback]()
		{
			TArray64<uint8> UncompressedBytes;
			int32 Width = 0;
			int32 Height = 0;
			EPixelFormat PixelFormat;
			if (!Parser || !Parser->LoadImageFromBlob(Parser->GetBlob(), MakeShared<FJsonObject>(), UncompressedBytes, Width, Height, PixelFormat, ImagesConfig) ||
				Width <= 0 ||
				Height <= 0)
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&, AsyncCallback]()
					{
						AsyncCallback.ExecuteIfBound(nullptr);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}

#if ENGINE_MAJOR_VERSION >= 5 && ENGINE_MINOR_VERSION >= 1
			int64 ImageSize = GPixelFormats[PixelFormat].Get2DImageSizeInBytes(Width, Height);
#else
			const int64 BlockWidth = (Width + GPixelFormats[PixelFormat].BlockSizeX - 1) / GPixelFormats[PixelFormat].BlockSizeX;
			const int64 BlockHeight = (Width + GPixelFormats[PixelFormat].BlockSizeY - 1) / GPixelFormats[PixelFormat].BlockSizeY;
			int64 ImageSize = BlockWidth * BlockHeight * GPixelFormats[PixelFormat].BlockBytes;
#endif
			int32 NumberOfSlices = UncompressedBytes.Num() / ImageSize;
			TArray<FglTFRuntimeMipMap> Mips;

			for (int32 Slice = 0; Slice < NumberOfSlices; Slice++)
			{
				FglTFRuntimeMipMap Mip(-1);
				Mip.Pixels.Append(UncompressedBytes.GetData() + (ImageSize * Slice), ImageSize);
				Mip.Width = Width;
				Mip.Height = Height;
				Mip.PixelFormat = PixelFormat;

				Mips.Add(MoveTemp(Mip));
			}

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&]()
				{
					AsyncCallback.ExecuteIfBound(Parser->BuildTextureArray(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler()));
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		}
	);
}

UTexture2D* UglTFRuntimeAsset::LoadMipsFromBlob(const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	TArray<FglTFRuntimeMipMap> Mips;
	FglTFRuntimeParser::OnTextureMips.Broadcast(Parser.ToSharedRef(), -1, MakeShared<FJsonObject>(), MakeShared<FJsonObject>(), Parser->GetBlob(), Mips, ImagesConfig);
	// if no Mips have been loaded, attempt parsing a DDS asset
	if (Mips.Num() == 0)
	{
		if (FglTFRuntimeDDS::IsDDS(Parser->GetBlob()))
		{
			FglTFRuntimeDDS DDS(Parser->GetBlob());
			DDS.LoadMips(-1, Mips, 0, ImagesConfig);
		}
	}

	return Parser->BuildTexture(this, Mips, ImagesConfig, FglTFRuntimeTextureSampler());
}

void UglTFRuntimeAsset::LoadCubeMapFromBlobAsync(const bool bSpherical, const bool bAutoRotate, const FglTFRuntimeTextureCubeAsync& AsyncCallback, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	Async(EAsyncExecution::Thread, [this, bSpherical, bAutoRotate, ImagesConfig, AsyncCallback]()
		{
			if (!Parser)
			{
				FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&, AsyncCallback]()
					{
						AsyncCallback.ExecuteIfBound(nullptr);
					}, TStatId(), nullptr, ENamedThreads::GameThread);
				FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
				return;
			}
			TArray<FglTFRuntimeMipMap> MipsXP;
			TArray<FglTFRuntimeMipMap> MipsXN;
			TArray<FglTFRuntimeMipMap> MipsYP;
			TArray<FglTFRuntimeMipMap> MipsYN;
			TArray<FglTFRuntimeMipMap> MipsZP;
			TArray<FglTFRuntimeMipMap> MipsZN;
			bool bLoaded = false;
			if (glTFRuntime::LoadCubeMapMipsFromBlob(Parser.ToSharedRef(), ImagesConfig, bSpherical, MipsXP, MipsXN, MipsYP, MipsYN, MipsZP, MipsZN))
			{
				bLoaded = true;
			}

			FGraphEventRef Task = FFunctionGraphTask::CreateAndDispatchWhenReady([&, AsyncCallback]()
				{
					if (bLoaded)
					{
						AsyncCallback.ExecuteIfBound(Parser->BuildTextureCube(this, MipsXP, MipsXN, MipsYP, MipsYN, MipsZP, MipsZN, bSpherical ? true : bAutoRotate, ImagesConfig, FglTFRuntimeTextureSampler()));
					}
					else
					{
						AsyncCallback.ExecuteIfBound(nullptr);
					}
				}, TStatId(), nullptr, ENamedThreads::GameThread);
			FTaskGraphInterface::Get().WaitUntilTaskCompletes(Task);
		}
	);
}

UTextureCube* UglTFRuntimeAsset::LoadCubeMapFromBlob(const bool bSpherical, const bool bAutoRotate, const FglTFRuntimeImagesConfig& ImagesConfig)
{
	GLTF_CHECK_PARSER(nullptr);

	TArray<FglTFRuntimeMipMap> MipsXP;
	TArray<FglTFRuntimeMipMap> MipsXN;
	TArray<FglTFRuntimeMipMap> MipsYP;
	TArray<FglTFRuntimeMipMap> MipsYN;
	TArray<FglTFRuntimeMipMap> MipsZP;
	TArray<FglTFRuntimeMipMap> MipsZN;

	if (!glTFRuntime::LoadCubeMapMipsFromBlob(Parser.ToSharedRef(), ImagesConfig, bSpherical, MipsXP, MipsXN, MipsYP, MipsYN, MipsZP, MipsZN))
	{
		return nullptr;
	}

	return Parser->BuildTextureCube(this, MipsXP, MipsXN, MipsYP, MipsYN, MipsZP, MipsZN, bSpherical ? true : bAutoRotate, ImagesConfig, FglTFRuntimeTextureSampler());
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

TArray<FString> UglTFRuntimeAsset::GetMaterialsVariants() const
{
	GLTF_CHECK_PARSER(TArray<FString>());
	return Parser->MaterialsVariants;
}

UAnimSequence* UglTFRuntimeAsset::CreateAnimationFromPose(USkeletalMesh* SkeletalMesh, const FglTFRuntimeSkeletalAnimationConfig& SkeletalAnimationConfig, const int32 SkinIndex)
{
	GLTF_CHECK_PARSER(nullptr);
	return Parser->CreateAnimationFromPose(SkeletalMesh, SkinIndex, SkeletalAnimationConfig);
}

bool UglTFRuntimeAsset::LoadMeshAsRuntimeLOD(const int32 MeshIndex, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig)
{
	GLTF_CHECK_PARSER(false);

	return Parser->LoadMeshAsRuntimeLOD(MeshIndex, RuntimeLOD, MaterialsConfig);
}

UStaticMesh* UglTFRuntimeAsset::LoadStaticMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	return Parser->LoadStaticMeshFromRuntimeLODs(RuntimeLODs, StaticMeshConfig);
}

bool UglTFRuntimeAsset::LoadSkinnedMeshRecursiveAsRuntimeLOD(const FString& NodeName, const TArray<FString>& ExcludeNodes, FglTFRuntimeMeshLOD& RuntimeLOD, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, int32& SkinIndex, const int32 OverrideSkinIndex, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode)
{
	GLTF_CHECK_PARSER(false);
	SkinIndex = OverrideSkinIndex;
	return Parser->LoadSkinnedMeshRecursiveAsRuntimeLOD(NodeName, SkinIndex, ExcludeNodes, RuntimeLOD, MaterialsConfig, SkeletonConfig, TransformApplyRecursiveMode);
}

void UglTFRuntimeAsset::LoadSkinnedMeshRecursiveAsRuntimeLODAsync(const FString& NodeName, const TArray<FString>& ExcludeNodes, const FglTFRuntimeMeshLODAsync& AsyncCallback, const FglTFRuntimeMaterialsConfig& MaterialsConfig, const FglTFRuntimeSkeletonConfig& SkeletonConfig, int32& SkinIndex, const int32 OverrideSkinIndex, const EglTFRuntimeRecursiveMode TransformApplyRecursiveMode)
{
	GLTF_CHECK_PARSER_VOID();
	Parser->LoadSkinnedMeshRecursiveAsRuntimeLODAsync(NodeName, SkinIndex, ExcludeNodes, AsyncCallback, MaterialsConfig, SkeletonConfig, TransformApplyRecursiveMode);
}

USkeletalMesh* UglTFRuntimeAsset::LoadSkeletalMeshFromRuntimeLODs(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER(nullptr);
	return Parser->LoadSkeletalMeshFromRuntimeLODs(RuntimeLODs, SkinIndex, SkeletalMeshConfig);
}

bool UglTFRuntimeAsset::GetStringMapFromExtras(const FString& Key, TMap<FString, FString>& StringMap) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetStringMapFromExtras(Key, StringMap);
}

bool UglTFRuntimeAsset::GetStringArrayFromExtras(const FString& Key, TArray<FString>& StringArray) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetStringArrayFromExtras(Key, StringArray);
}

bool UglTFRuntimeAsset::GetNumberFromExtras(const FString& Key, float& Value) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetNumberFromExtras(Key, Value);
}

bool UglTFRuntimeAsset::GetStringFromExtras(const FString& Key, FString& Value) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetStringFromExtras(Key, Value);
}

bool UglTFRuntimeAsset::GetBooleanFromExtras(const FString& Key, bool& Value) const
{
	GLTF_CHECK_PARSER(false);
	return Parser->GetBooleanFromExtras(Key, Value);
}

bool UglTFRuntimeAsset::GetNodeGPUInstancingTransforms(const int32 NodeIndex, TArray<FTransform>& Transforms)
{
	GLTF_CHECK_PARSER(false);

	TSharedPtr<FJsonObject> InstancingExtension = Parser->GetNodeExtensionObject(NodeIndex, "EXT_mesh_gpu_instancing");
	if (!InstancingExtension)
	{
		return false;
	}

	TSharedPtr<FJsonObject> InstancingExtensionAttributes = Parser->GetJsonObjectFromObject(InstancingExtension.ToSharedRef(), "attributes");
	if (!InstancingExtensionAttributes)
	{
		return false;
	}

	TArray<FVector> Translations;
	TArray<FVector4> Rotations;
	TArray<FVector> Scales;

	if (Parser->BuildFromAccessorField(InstancingExtensionAttributes.ToSharedRef(), "TRANSLATION", Translations, { 3 }, { 5126 }, [](FVector V) { return V; }, INDEX_NONE, false, nullptr))
	{
		Transforms.AddUninitialized(Translations.Num());
		for (int32 Index = 0; Index < Translations.Num(); Index++)
		{
			Transforms[Index].SetTranslation(Translations[Index]);
		}
	}

	if (Parser->BuildFromAccessorField(InstancingExtensionAttributes.ToSharedRef(), "ROTATION", Rotations, { 4 }, { 5126, 5120, 5122 }, [](FVector4 Q) { return Q; }, INDEX_NONE, true, nullptr))
	{
		if (Transforms.Num() == 0)
		{
			Transforms.AddUninitialized(Rotations.Num());
		}
		else if (Transforms.Num() != Rotations.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Rotations.Num(); Index++)
		{
			Transforms[Index].SetRotation(FQuat(Rotations[Index].X, Rotations[Index].Y, Rotations[Index].Z, Rotations[Index].W));
		}
	}

	if (Parser->BuildFromAccessorField(InstancingExtensionAttributes.ToSharedRef(), "SCALE", Scales, { 3 }, { 5126 }, [](FVector V) { return V; }, INDEX_NONE, false, nullptr))
	{
		if (Transforms.Num() == 0)
		{
			Transforms.AddUninitialized(Scales.Num());
		}
		else if (Transforms.Num() != Scales.Num())
		{
			return false;
		}

		for (int32 Index = 0; Index < Scales.Num(); Index++)
		{
			Transforms[Index].SetScale3D(Scales[Index]);
		}
	}

	// the extension is present but no attribute is defined (still valid)
	if (Transforms.Num() <= 0)
	{
		return true;
	}

	for (int32 Index = 0; Index < Scales.Num(); Index++)
	{
		Transforms[Index].NormalizeRotation();
		Transforms[Index] = Parser->RebaseTransform(Transforms[Index]);
	}

	return true;
}

bool UglTFRuntimeAsset::GetNodeExtensionIndices(const int32 NodeIndex, const FString& ExtensionName, const FString& FieldName, TArray<int32>& Indices)
{
	GLTF_CHECK_PARSER(false);

	TSharedPtr<FJsonObject> NodeObject = Parser->GetNodeObject(NodeIndex);
	if (!NodeObject)
	{
		return false;
	}

	Indices = Parser->GetJsonExtensionObjectIndices(NodeObject.ToSharedRef(), ExtensionName, FieldName);
	return true;
}

bool UglTFRuntimeAsset::GetNodeExtrasNumbers(const int32 NodeIndex, const FString& Key, TArray<float>& Values)
{
	GLTF_CHECK_PARSER(false);

	TSharedPtr<FJsonObject> NodeObject = Parser->GetNodeObject(NodeIndex);
	if (!NodeObject)
	{
		return false;
	}

	TSharedPtr<FJsonObject> NodeExtrasObject = Parser->GetJsonObjectExtras(NodeObject.ToSharedRef());
	if (!NodeExtrasObject)
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* JsonArray = nullptr;
	if (!NodeExtrasObject->TryGetArrayField(Key, JsonArray))
	{
		return false;
	}

	for (const TSharedPtr<FJsonValue>& JsonItem : *JsonArray)
	{
		double Value = 0;
		if (!JsonItem->TryGetNumber(Value))
		{
			return false;
		}
		Values.Add(Value);
	}

	return true;
}

bool UglTFRuntimeAsset::GetNodeExtensionIndex(const int32 NodeIndex, const FString& ExtensionName, const FString& FieldName, int32& Index)
{
	GLTF_CHECK_PARSER(false);

	TSharedPtr<FJsonObject> NodeObject = Parser->GetNodeObject(NodeIndex);
	if (!NodeObject)
	{
		return false;
	}

	Index = Parser->GetJsonExtensionObjectIndex(NodeObject.ToSharedRef(), ExtensionName, FieldName, INDEX_NONE);
	return Index > INDEX_NONE;
}

void UglTFRuntimeAsset::AddUsedExtension(const FString& ExtensionName)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->ExtensionsUsed.Add(ExtensionName);
}

void UglTFRuntimeAsset::AddRequiredExtension(const FString& ExtensionName)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->ExtensionsRequired.Add(ExtensionName);
}

void UglTFRuntimeAsset::AddUsedExtensions(const TArray<FString>& ExtensionsNames)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->ExtensionsUsed.Append(ExtensionsNames);
}

void UglTFRuntimeAsset::AddRequiredExtensions(const TArray<FString>& ExtensionsNames)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->ExtensionsRequired.Append(ExtensionsNames);
}

FString UglTFRuntimeAsset::ToJsonString() const
{
	GLTF_CHECK_PARSER("");

	return Parser->ToJsonString();
}

FString UglTFRuntimeAsset::GetVersion() const
{
	GLTF_CHECK_PARSER("");

	return Parser->GetVersion();
}

FString UglTFRuntimeAsset::GetGenerator() const
{
	GLTF_CHECK_PARSER("");

	return Parser->GetGenerator();
}

void UglTFRuntimeAsset::ClearCache()
{
	if (Parser)
	{
		Parser->ClearCache();
	}
}

bool UglTFRuntimeAsset::IsArchive() const
{
	GLTF_CHECK_PARSER(false);

	return Parser->IsArchive();
}

TArray<FString> UglTFRuntimeAsset::GetArchiveItems() const
{
	GLTF_CHECK_PARSER(TArray<FString>());

	return Parser->GetArchiveItems();
}

void UglTFRuntimeAsset::LoadStaticMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const FglTFRuntimeStaticMeshAsync& AsyncCallback, const FglTFRuntimeStaticMeshConfig& StaticMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadStaticMeshFromRuntimeLODsAsync(RuntimeLODs, AsyncCallback, StaticMeshConfig);
}

void UglTFRuntimeAsset::LoadSkeletalMeshFromRuntimeLODsAsync(const TArray<FglTFRuntimeMeshLOD>& RuntimeLODs, const int32 SkinIndex, const FglTFRuntimeSkeletalMeshAsync& AsyncCallback, const FglTFRuntimeSkeletalMeshConfig& SkeletalMeshConfig)
{
	GLTF_CHECK_PARSER_VOID();

	Parser->LoadSkeletalMeshFromRuntimeLODsAsync(RuntimeLODs, SkinIndex, AsyncCallback, SkeletalMeshConfig);
}

float UglTFRuntimeAsset::GetDownloadTime() const
{
	GLTF_CHECK_PARSER(0);

	return Parser->GetDownloadTime();
}

TArray<FString> UglTFRuntimeAsset::GetAnimationsNames(const bool bIncludeUnnameds) const
{
	GLTF_CHECK_PARSER(TArray<FString>());

	return Parser->GetAnimationsNames(bIncludeUnnameds);
}

bool UglTFRuntimeAsset::HasErrors() const
{
	GLTF_CHECK_PARSER(false);

	return Parser->HasErrors();
}

TArray<FString> UglTFRuntimeAsset::GetErrors() const
{
	GLTF_CHECK_PARSER(TArray<FString>());

	return Parser->GetErrors();
}

bool UglTFRuntimeAsset::MeshHasMorphTargets(const int32 MeshIndex) const
{
	GLTF_CHECK_PARSER(false);

	return Parser->MeshHasMorphTargets(MeshIndex);
}