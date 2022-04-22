// Copyright 2020-2022, Roberto De Ioris 
// Copyright 2022, Avatus LLC


#include "glTFRuntimeMaterialBaker.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Engine/TextureRenderTarget2D.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"

// Sets default values
AglTFRuntimeMaterialBaker::AglTFRuntimeMaterialBaker()
{
	// Set this actor to call Tick() every frame.  You can turn this off to improve performance if you don't need it.
	PrimaryActorTick.bCanEverTick = false;

	SceneCaptureComponent = CreateDefaultSubobject<USceneCaptureComponent2D>("Root");
	SceneCaptureComponent->bCaptureEveryFrame = false;
	SceneCaptureComponent->bCaptureOnMovement = false;
	SceneCaptureComponent->bAlwaysPersistRenderingState = true;
	SceneCaptureComponent->ShowFlags.AntiAliasing = false;
	SceneCaptureComponent->ShowFlags.Atmosphere = false;
	SceneCaptureComponent->ShowFlags.BSP = false;
	SceneCaptureComponent->ShowFlags.Decals = false;
	SceneCaptureComponent->ShowFlags.Fog = false;
	SceneCaptureComponent->ShowFlags.Landscape = false;
	SceneCaptureComponent->ShowFlags.Particles = false;
	SceneCaptureComponent->ShowFlags.SkeletalMeshes = false;
	SceneCaptureComponent->ShowFlags.DeferredLighting = false;
	SceneCaptureComponent->ShowFlags.AmbientCubemap = false;
	SceneCaptureComponent->ShowFlags.AmbientOcclusion = false;
	SceneCaptureComponent->ShowFlags.Lighting = false;
	SceneCaptureComponent->ShowFlags.InstancedFoliage = false;
	SceneCaptureComponent->ShowFlags.InstancedGrass = false;
	SceneCaptureComponent->ShowFlags.InstancedStaticMeshes = false;
	SceneCaptureComponent->ShowFlags.Paper2DSprites = false;
	SceneCaptureComponent->ShowFlags.TextRender = false;
	SceneCaptureComponent->ShowFlags.Bloom = false;
	SceneCaptureComponent->ShowFlags.EyeAdaptation = false;
	SceneCaptureComponent->ShowFlags.MotionBlur = false;
	SceneCaptureComponent->ShowFlags.ToneCurve = false;
	SceneCaptureComponent->ShowFlags.SkyLighting = false;
	SceneCaptureComponent->ShowFlags.DynamicShadows = false;
	SceneCaptureComponent->ShowFlags.DistanceFieldAO = false;
	SceneCaptureComponent->ShowFlags.LightFunctions = false;
	SceneCaptureComponent->ShowFlags.LightShafts = false;
	SceneCaptureComponent->ShowFlags.ReflectionEnvironment = false;
	SceneCaptureComponent->ShowFlags.ScreenSpaceReflections = false;
	SceneCaptureComponent->ShowFlags.ScreenSpaceAO = false;
	SceneCaptureComponent->ShowFlags.TexturedLightProfiles = false;
	SceneCaptureComponent->ShowFlags.VolumetricFog = false;
	SceneCaptureComponent->ShowFlags.Game = false;
	RootComponent = SceneCaptureComponent;

	RenderingPlaneComponent = CreateDefaultSubobject<UStaticMeshComponent>("Plane");
	RenderingPlaneComponent->AttachToComponent(RootComponent, FAttachmentTransformRules::KeepRelativeTransform);
	RenderingPlaneComponent->SetRelativeRotation(FRotator(0, 90, 90));
	RenderingPlaneComponent->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Plane")));
}

// Called when the game starts or when spawned
void AglTFRuntimeMaterialBaker::BeginPlay()
{
	Super::BeginPlay();

}

// Called every frame
void AglTFRuntimeMaterialBaker::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

}

bool AglTFRuntimeMaterialBaker::BakeMaterialToPng(UMaterialInterface* Material, TArray<uint8>& BaseColor, TArray<uint8>& NormalMap, TArray<uint8>& MetallicRoughness)
{
	const uint32 TextureSize = 2048;

	UMaterial* ExtractBaseColor = LoadObject<UMaterial>(nullptr, TEXT("/glTFRuntime/PPM_glTFRuntimeExtractBaseColor"));
	UMaterial* ExtractNormalMap = LoadObject<UMaterial>(nullptr, TEXT("/glTFRuntime/PPM_glTFRuntimeExtractNormalMap"));
	UMaterial* ExtractMetallicRoughness = LoadObject<UMaterial>(nullptr, TEXT("/glTFRuntime/PPM_glTFRuntimeExtractMetallicRoughness"));

	IImageWrapperModule& ImageWrapperModule = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));

	RenderingPlaneComponent->SetMaterial(0, Material);
	SceneCaptureComponent->ProjectionType = ECameraProjectionMode::Orthographic;
	SceneCaptureComponent->OrthoWidth = 100;
	SceneCaptureComponent->ShowOnlyComponent(RenderingPlaneComponent);

	UTextureRenderTarget2D* RenderTarget = NewObject<UTextureRenderTarget2D>();
	SceneCaptureComponent->TextureTarget = RenderTarget;

	FRenderTarget* RenderTargetResource = nullptr;
	TArray<FColor> Pixels;
	TArray<FLinearColor> AlphaValues;

	/* Alpha (if required) */
	if (Material->GetBlendMode() == EBlendMode::BLEND_Translucent || Material->GetBlendMode() == EBlendMode::BLEND_Masked)
	{
		SceneCaptureComponent->ShowFlags.Translucency = true;
		SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_SceneColorHDR;
		RenderTarget->InitCustomFormat(TextureSize, TextureSize, EPixelFormat::PF_FloatRGBA, false);
		SceneCaptureComponent->CaptureScene();
		RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
		RenderTargetResource->ReadLinearColorPixels(AlphaValues);
	}

	SceneCaptureComponent->ShowFlags.Translucency = true;
	SceneCaptureComponent->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;

	/* BaseColor */
	RenderTarget->InitCustomFormat(TextureSize, TextureSize, EPixelFormat::PF_R8G8B8A8, false);
	if (Material->GetBlendMode() != EBlendMode::BLEND_Translucent)
	{
		SceneCaptureComponent->PostProcessSettings.AddBlendable(ExtractBaseColor, 1);
	}
	SceneCaptureComponent->CaptureScene();

	RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	RenderTargetResource->ReadPixels(Pixels);

	if (Material->GetBlendMode() == EBlendMode::BLEND_Translucent || Material->GetBlendMode() == EBlendMode::BLEND_Masked)
	{
		int32 AlphaPixelsNum = 0;
		for (int32 PixelIndex = 0; PixelIndex < Pixels.Num(); PixelIndex++)
		{
			const float Alpha = 1 - AlphaValues[PixelIndex].A;
			Pixels[PixelIndex].A = Alpha * 255.0f;
			if (Material->GetBlendMode() == EBlendMode::BLEND_Translucent)
			{
				if (Alpha <= Material->GetOpacityMaskClipValue())
				{
					Pixels[PixelIndex].A = 0;
				}
			}
		}
	}

	TSharedPtr<IImageWrapper> ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), TextureSize, TextureSize, ERGBFormat::BGRA, 8);
	BaseColor = ImageWrapper->GetCompressed();

	/* NormalMap */
	RenderTarget->InitCustomFormat(TextureSize, TextureSize, EPixelFormat::PF_R8G8B8A8, true);
	SceneCaptureComponent->PostProcessSettings.RemoveBlendable(ExtractBaseColor);
	SceneCaptureComponent->PostProcessSettings.AddBlendable(ExtractNormalMap, 1);
	SceneCaptureComponent->CaptureScene();

	RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	RenderTargetResource->ReadPixels(Pixels);

	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), TextureSize, TextureSize, ERGBFormat::BGRA, 8);

	NormalMap = ImageWrapper->GetCompressed();

	/* Metallic Roughness */
	RenderTarget->InitCustomFormat(TextureSize, TextureSize, EPixelFormat::PF_R8G8B8A8, true);
	SceneCaptureComponent->PostProcessSettings.RemoveBlendable(ExtractNormalMap);
	SceneCaptureComponent->PostProcessSettings.AddBlendable(ExtractMetallicRoughness, 1);
	SceneCaptureComponent->CaptureScene();

	RenderTargetResource = RenderTarget->GameThread_GetRenderTargetResource();
	RenderTargetResource->ReadPixels(Pixels);

	ImageWrapper = ImageWrapperModule.CreateImageWrapper(EImageFormat::PNG);
	ImageWrapper->SetRaw(Pixels.GetData(), Pixels.Num() * sizeof(FColor), TextureSize, TextureSize, ERGBFormat::BGRA, 8);

	MetallicRoughness = ImageWrapper->GetCompressed();

	return true;
}

