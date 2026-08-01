// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameGameMode.h"

#include "Components/StaticMeshComponent.h"
#include "Components/DirectionalLightComponent.h"
#include "Components/SkyAtmosphereComponent.h"
#include "Components/SkyLightComponent.h"
#include "Components/VolumetricCloudComponent.h"
#include "DataGenerator/CurriculumDataGenerator.h"
#include "Engine/DirectionalLight.h"
#include "Engine/ExponentialHeightFog.h"
#include "Engine/PostProcessVolume.h"
#include "Engine/SkyLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/WorldSettings.h"
#include "Grenade/ArenaObstacle.h"
#include "Grenade/GrenadeHUD.h"
#include "Kismet/GameplayStatics.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	constexpr float BasicCubeSizeCm = 100.0f;
	const FName CurriculumArenaTag(TEXT("CurriculumArena"));

	template <typename TObjectType>
	void LoadAsset(TObjectPtr<TObjectType>& Destination, const TCHAR* Path)
	{
		ConstructorHelpers::FObjectFinder<TObjectType> Finder(Path);
		if (Finder.Succeeded())
		{
			Destination = Finder.Object;
		}
	}
}

Ahe_grenade_gameGameMode::Ahe_grenade_gameGameMode()
{
	PrimaryActorTick.bCanEverTick = false;
	HUDClass = AGrenadeHUD::StaticClass();

	LoadAsset(CubeMesh, TEXT("/Engine/BasicShapes/Cube.Cube"));
	LoadAsset(SphereMesh, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	LoadAsset(HoopMesh, TEXT("/Game/Meshes/SM_BreakableHoop.SM_BreakableHoop"));
	LoadAsset(
		PyramidMesh,
		TEXT("/Game/Curriculum/Meshes/SM_CurriculumPyramid.SM_CurriculumPyramid"));

	LoadAsset(FloorTileMaterial, TEXT("/Game/Materials/MI_FloorTile.MI_FloorTile"));
	LoadAsset(RectangleShapeMaterial, TEXT("/Game/Materials/MI_Shape_Rectangle.MI_Shape_Rectangle"));
	LoadAsset(TriangleShapeMaterial, TEXT("/Game/Materials/MI_Shape_Triangle.MI_Shape_Triangle"));
	LoadAsset(SphereShapeMaterial, TEXT("/Game/Materials/MI_Shape_Sphere.MI_Shape_Sphere"));
	LoadAsset(HoopShapeMaterial, TEXT("/Game/Materials/MI_Shape_Hoop.MI_Shape_Hoop"));
	LoadAsset(RampShapeMaterial, TEXT("/Game/Materials/MI_LabyrinthPanel.MI_LabyrinthPanel"));
	LoadAsset(ArenaWallMaterial, TEXT("/Game/Materials/MI_ArenaWallGrid.MI_ArenaWallGrid"));
	LoadAsset(GrenadeMaterial, TEXT("/Game/Materials/MI_Grenade.MI_Grenade"));

	LoadAsset(
		MatteFloorMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Floor.MI_Curriculum_Floor"));
	LoadAsset(
		MatteWallMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Wall.MI_Curriculum_Wall"));
	LoadAsset(
		MatteRectangleMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Rectangle.MI_Curriculum_Rectangle"));
	LoadAsset(
		MatteTriangleMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Triangle.MI_Curriculum_Triangle"));
	LoadAsset(
		MatteSphereMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Sphere.MI_Curriculum_Sphere"));
	LoadAsset(
		MatteHoopMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Hoop.MI_Curriculum_Hoop"));
	LoadAsset(
		MatteRampMaterial,
		TEXT("/Game/Curriculum/Materials/MI_Curriculum_Ramp.MI_Curriculum_Ramp"));
}

void Ahe_grenade_gameGameMode::BeginPlay()
{
	BuildFixedCurriculumLighting();
	BuildFixedCurriculumArena();
	Super::BeginPlay();

	if (FParse::Param(FCommandLine::Get(), TEXT("GenerateDataset")) && GetWorld())
	{
		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride =
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		GetWorld()->SpawnActor<ACurriculumDataGenerator>(
			ACurriculumDataGenerator::StaticClass(),
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParams);
	}
}

void Ahe_grenade_gameGameMode::BuildFixedCurriculumLighting()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// Remove level-authored lighting and exposure so every generated episode uses
	// the exact same visual conditions, regardless of map or editor state.
	for (TActorIterator<ADirectionalLight> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<ASkyLight> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<APostProcessVolume> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AVolumetricCloud> It(World); It; ++It)
	{
		It->Destroy();
	}
	for (TActorIterator<AExponentialHeightFog> It(World); It; ++It)
	{
		It->Destroy();
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	// A diagonal key light exposes top and side faces simultaneously. The larger
	// source angle keeps shadows readable without producing razor-sharp edges.
	ADirectionalLight* KeyLight = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector,
		FRotator(-42.0f, -35.0f, 0.0f),
		SpawnParams);
	if (KeyLight)
	{
		KeyLight->Tags.AddUnique(CurriculumArenaTag);

		if (UDirectionalLightComponent* LightComponent =
			Cast<UDirectionalLightComponent>(KeyLight->GetLightComponent()))
		{
			LightComponent->SetMobility(EComponentMobility::Movable);
			LightComponent->SetIntensity(10.0f);
			LightComponent->SetLightColor(FLinearColor(1.0f, 0.96f, 0.90f));
			LightComponent->SetUseTemperature(false);
			LightComponent->SetAtmosphereSunLight(true);
			LightComponent->SetAtmosphereSunLightIndex(0);
			LightComponent->LightSourceAngle = 3.0f;
			LightComponent->ContactShadowLength = 0.08f;
			LightComponent->ContactShadowLengthInWS = false;
			LightComponent->SetCastShadows(true);
		}
	}

	ASkyAtmosphere* SkyAtmosphere = nullptr;
	for (TActorIterator<ASkyAtmosphere> It(World); It; ++It)
	{
		SkyAtmosphere = *It;
		break;
	}
	if (!SkyAtmosphere)
	{
		SkyAtmosphere = World->SpawnActor<ASkyAtmosphere>(
			FVector::ZeroVector,
			FRotator::ZeroRotator,
			SpawnParams);
	}
	if (SkyAtmosphere)
	{
		SkyAtmosphere->Tags.AddUnique(CurriculumArenaTag);
		if (USkyAtmosphereComponent* AtmosphereComponent =
			SkyAtmosphere->GetComponent())
		{
			AtmosphereComponent->SetSkyLuminanceFactor(
				FLinearColor(2.4f, 2.4f, 2.4f, 1.0f));
			AtmosphereComponent->SetSkyAndAerialPerspectiveLuminanceFactor(
				FLinearColor(1.6f, 1.6f, 1.6f, 1.0f));
		}
	}

	// A low-strength opposing fill keeps vertical faces readable. It casts no
	// shadows, so the warm key remains the only source that defines shadow shape.
	ADirectionalLight* OpposingFill = World->SpawnActor<ADirectionalLight>(
		FVector::ZeroVector,
		FRotator(-25.0f, 145.0f, 0.0f),
		SpawnParams);
	if (OpposingFill)
	{
		OpposingFill->Tags.AddUnique(CurriculumArenaTag);
		if (UDirectionalLightComponent* LightComponent =
			Cast<UDirectionalLightComponent>(OpposingFill->GetLightComponent()))
		{
			LightComponent->SetMobility(EComponentMobility::Movable);
			LightComponent->SetIntensity(2.0f);
			LightComponent->SetLightColor(FLinearColor::White);
			LightComponent->SetUseTemperature(false);
			LightComponent->SetCastShadows(false);
		}
	}

	// Low-strength neutral sky fill preserves color and detail on faces turned
	// away from the key light without flattening the directional shading.
	ASkyLight* FillLight = World->SpawnActor<ASkyLight>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);
	if (FillLight)
	{
		FillLight->Tags.AddUnique(CurriculumArenaTag);

		if (USkyLightComponent* LightComponent = FillLight->GetLightComponent())
		{
			LightComponent->SetMobility(EComponentMobility::Movable);
			// This is the curriculum's readability fill, not a second key light.
			// Keep it non-shadowing so inward-facing arena walls retain useful
			// color information instead of collapsing to black.
			LightComponent->SetIntensity(1.4f);
			LightComponent->SetLightColor(FLinearColor::White);
			LightComponent->SetCastShadows(false);
			LightComponent->RecaptureSky();
		}
	}

	// Lock exposure and reinforce local contact cues. Auto exposure would make
	// identical actions produce different pixels depending on recent camera views.
	APostProcessVolume* PostProcess = World->SpawnActor<APostProcessVolume>(
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);
	if (PostProcess)
	{
		PostProcess->Tags.AddUnique(CurriculumArenaTag);
		PostProcess->bUnbound = true;
		PostProcess->BlendWeight = 1.0f;
		PostProcess->Settings.bOverride_AutoExposureMethod = true;
		PostProcess->Settings.AutoExposureMethod = EAutoExposureMethod::AEM_Manual;
		PostProcess->Settings.bOverride_AutoExposureBias = true;
		PostProcess->Settings.AutoExposureBias = 1.45f;
		PostProcess->Settings.bOverride_AutoExposureApplyPhysicalCameraExposure = true;
		PostProcess->Settings.AutoExposureApplyPhysicalCameraExposure = false;
		PostProcess->Settings.bOverride_AmbientOcclusionIntensity = true;
		PostProcess->Settings.AmbientOcclusionIntensity = 0.25f;
		PostProcess->Settings.bOverride_AmbientOcclusionRadius = true;
		PostProcess->Settings.AmbientOcclusionRadius = 140.0f;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Curriculum lighting built: fixed diagonal key, sky fill, locked exposure."));
}

void Ahe_grenade_gameGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	BuildFixedCurriculumArena();
	RestartPlayerAtTransform(NewPlayer, PlayerSpawnTransform);
}

void Ahe_grenade_gameGameMode::BuildFixedCurriculumArena()
{
	if (bFixedArenaBuilt || !GetWorld())
	{
		return;
	}

	bFixedArenaBuilt = true;

	const float SafeArenaSize = FMath::Max(2000.0f, ArenaSizeCm);
	const float SafeFloorThickness = FMath::Max(10.0f, FloorThicknessCm);
	const float SafeWallHeight = FMath::Max(200.0f, ArenaWallHeightCm);
	const float SafeWallThickness = FMath::Max(5.0f, ArenaWallThicknessCm);
	const float HalfArena = SafeArenaSize * 0.5f;
	UMaterialInterface* ReadableFloorMaterial = MatteFloorMaterial;
	if (UMaterialInstanceDynamic* FloorMaterial =
		UMaterialInstanceDynamic::Create(MatteFloorMaterial, this))
	{
		FloorMaterial->SetVectorParameterValue(
			TEXT("BaseColor"),
			FLinearColor(0.32f, 0.32f, 0.32f, 1.0f));
		ReadableFloorMaterial = FloorMaterial;
	}
	const auto MakeWallMaterial =
		[this](const FLinearColor& Color) -> UMaterialInterface*
		{
			UMaterialInstanceDynamic* WallMaterial =
				UMaterialInstanceDynamic::Create(MatteWallMaterial, this);
			if (!WallMaterial)
			{
				return MatteWallMaterial;
			}
			WallMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
			return WallMaterial;
		};
	UMaterialInterface* NorthWallMaterial =
		MakeWallMaterial(FLinearColor(0.30f, 0.20f, 0.16f, 1.0f));
	UMaterialInterface* SouthWallMaterial =
		MakeWallMaterial(FLinearColor(0.16f, 0.24f, 0.30f, 1.0f));
	UMaterialInterface* EastWallMaterial =
		MakeWallMaterial(FLinearColor(0.27f, 0.23f, 0.14f, 1.0f));
	UMaterialInterface* WestWallMaterial =
		MakeWallMaterial(FLinearColor(0.28f, 0.21f, 0.30f, 1.0f));
	const auto MakeObjectMaterial =
		[this](UMaterialInterface* Parent, const FLinearColor& Color)
			-> UMaterialInterface*
		{
			UMaterialInstanceDynamic* ObjectMaterial =
				UMaterialInstanceDynamic::Create(Parent, this);
			if (!ObjectMaterial)
			{
				return Parent;
			}
			ObjectMaterial->SetVectorParameterValue(TEXT("BaseColor"), Color);
			return ObjectMaterial;
		};
	UMaterialInterface* BrownRectangleMaterial = MakeObjectMaterial(
		MatteRectangleMaterial,
		FLinearColor(0.24f, 0.08f, 0.025f, 1.0f));
	UMaterialInterface* YellowPyramidMaterial = MakeObjectMaterial(
		MatteTriangleMaterial,
		FLinearColor(0.70f, 0.48f, 0.04f, 1.0f));
	UMaterialInterface* OrangeSphereMaterial = MakeObjectMaterial(
		MatteSphereMaterial,
		FLinearColor(0.72f, 0.18f, 0.02f, 1.0f));
	UMaterialInterface* MagentaHoopMaterial = MakeObjectMaterial(
		MatteHoopMaterial,
		FLinearColor(0.60f, 0.04f, 0.20f, 1.0f));
	UMaterialInterface* RedRampMaterial = MakeObjectMaterial(
		MatteRampMaterial,
		FLinearColor(0.55f, 0.025f, 0.02f, 1.0f));

	// One continuous, indestructible square floor.
	SpawnBox(
		TEXT("CurriculumFloor"),
		FVector(0.0f, 0.0f, -SafeFloorThickness * 0.5f),
		FRotator::ZeroRotator,
		FVector(SafeArenaSize, SafeArenaSize, SafeFloorThickness),
		ReadableFloorMaterial);

	// Four solid perimeter walls with their inner faces on the square boundary.
	const float WallCenterZ = SafeWallHeight * 0.5f;
	SpawnBox(
		TEXT("CurriculumWall_North"),
		FVector(HalfArena + (SafeWallThickness * 0.5f), 0.0f, WallCenterZ),
		FRotator::ZeroRotator,
		FVector(SafeWallThickness, SafeArenaSize + (SafeWallThickness * 2.0f), SafeWallHeight),
		NorthWallMaterial);
	SpawnBox(
		TEXT("CurriculumWall_South"),
		FVector(-HalfArena - (SafeWallThickness * 0.5f), 0.0f, WallCenterZ),
		FRotator::ZeroRotator,
		FVector(SafeWallThickness, SafeArenaSize + (SafeWallThickness * 2.0f), SafeWallHeight),
		SouthWallMaterial);
	SpawnBox(
		TEXT("CurriculumWall_East"),
		FVector(0.0f, HalfArena + (SafeWallThickness * 0.5f), WallCenterZ),
		FRotator::ZeroRotator,
		FVector(SafeArenaSize, SafeWallThickness, SafeWallHeight),
		EastWallMaterial);
	SpawnBox(
		TEXT("CurriculumWall_West"),
		FVector(0.0f, -HalfArena - (SafeWallThickness * 0.5f), WallCenterZ),
		FRotator::ZeroRotator,
		FVector(SafeArenaSize, SafeWallThickness, SafeWallHeight),
		WestWallMaterial);

	// Five unique object types in a geometrically balanced layout. Their identities
	// intentionally break perceptual symmetry and act as orientation landmarks.
	SpawnBox(
		TEXT("CurriculumObject_Rectangle"),
		FVector(-700.0f, 700.0f, 125.0f),
		FRotator::ZeroRotator,
		FVector(170.0f, 280.0f, 250.0f),
		BrownRectangleMaterial);

	SpawnMesh(
		TEXT("CurriculumObject_Pyramid"),
		PyramidMesh,
		FVector(700.0f, 700.0f, 0.0f),
		FRotator::ZeroRotator,
		FVector(2.8f, 2.8f, 2.5f),
		YellowPyramidMaterial);

	SpawnMesh(
		TEXT("CurriculumObject_Sphere"),
		SphereMesh,
		FVector(-700.0f, -700.0f, 120.0f),
		FRotator::ZeroRotator,
		FVector(2.4f),
		OrangeSphereMaterial);

	// The authored hoop is approximately 82 cm across.
	SpawnMesh(
		TEXT("CurriculumObject_Hoop"),
		HoopMesh,
		FVector(700.0f, -700.0f, 145.0f),
		FRotator::ZeroRotator,
		FVector(3.4f),
		MagentaHoopMaterial);

	constexpr float RampLengthCm = 500.0f;
	constexpr float RampWidthCm = 260.0f;
	constexpr float RampThicknessCm = 36.0f;
	constexpr float RampPitchDegrees = -18.0f;
	const float RampPitchRadians = FMath::DegreesToRadians(RampPitchDegrees);
	const float RampSupportHeightCm =
		(FMath::Abs(FMath::Sin(RampPitchRadians)) * RampLengthCm * 0.5f)
		+ (FMath::Abs(FMath::Cos(RampPitchRadians)) * RampThicknessCm * 0.5f);

	SpawnBox(
		TEXT("CurriculumObject_Ramp"),
		FVector(0.0f, 0.0f, RampSupportHeightCm),
		FRotator(RampPitchDegrees, 0.0f, 0.0f),
		FVector(RampLengthCm, RampWidthCm, RampThicknessCm),
		RedRampMaterial);

	// Single-player inspection spawn, looking from the west side toward the center.
	PlayerSpawnTransform = FTransform(
		FRotator(0.0f, 0.0f, 0.0f),
		FVector(-1250.0f, 0.0f, 100.0f),
		FVector::OneVector);

	if (AWorldSettings* WorldSettings = GetWorld()->GetWorldSettings())
	{
		WorldSettings->KillZ = -800.0f;
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Curriculum MovementV1 arena built: %.0f cm square, fixed five-object layout."),
		SafeArenaSize);
}

void Ahe_grenade_gameGameMode::SpawnBox(
	const FName ObjectTag,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& DimensionsCm,
	UMaterialInterface* Material)
{
	if (!CubeMesh)
	{
		return;
	}

	SpawnMesh(
		ObjectTag,
		CubeMesh,
		Location,
		Rotation,
		FVector(
			DimensionsCm.X / BasicCubeSizeCm,
			DimensionsCm.Y / BasicCubeSizeCm,
			DimensionsCm.Z / BasicCubeSizeCm),
		Material);
}

void Ahe_grenade_gameGameMode::SpawnMesh(
	const FName ObjectTag,
	UStaticMesh* Mesh,
	const FVector& Location,
	const FRotator& Rotation,
	const FVector& Scale,
	UMaterialInterface* Material)
{
	UWorld* World = GetWorld();
	if (!World || !Mesh)
	{
		return;
	}

	const FTransform SpawnTransform(Rotation, Location, Scale);
	AArenaObstacle* Obstacle = World->SpawnActorDeferred<AArenaObstacle>(
		AArenaObstacle::StaticClass(),
		SpawnTransform,
		nullptr,
		nullptr,
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
	if (!Obstacle)
	{
		return;
	}

	Obstacle->Tags.AddUnique(CurriculumArenaTag);
	Obstacle->Tags.AddUnique(ObjectTag);
	Obstacle->ObstacleMesh->SetStaticMesh(Mesh);
	Obstacle->ObstacleMesh->SetMaterial(0, Material);
	Obstacle->ObstacleMesh->SetCollisionProfileName(TEXT("BlockAll"));
	Obstacle->ObstacleMesh->SetGenerateOverlapEvents(false);
	Obstacle->ObstacleMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_Yes;
	UGameplayStatics::FinishSpawningActor(Obstacle, SpawnTransform);
}
