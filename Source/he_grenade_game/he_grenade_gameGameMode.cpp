// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameGameMode.h"

#include "Components/StaticMeshComponent.h"
#include "EngineUtils.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/WorldSettings.h"
#include "Grenade/ArenaObstacle.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "Grenade/Breakables/BreakableTileGrid.h"
#include "Grenade/GrenadeHUD.h"
#include "he_grenade_game.h"
#include "Materials/MaterialInterface.h"

namespace
{
	constexpr float CubeSizeCm = 100.0f;
	constexpr float CubeHalfSizeCm = CubeSizeCm * 0.5f;
	constexpr int32 MinArenaTiles = 8;
	const FName GeneratedArenaTag(TEXT("GeneratedArena"));

	float RandomRange(FRandomStream& RandomStream, const float MinValue, const float MaxValue)
	{
		const float SafeMin = FMath::Min(MinValue, MaxValue);
		const float SafeMax = FMath::Max(MinValue, MaxValue);
		return RandomStream.FRandRange(SafeMin, SafeMax);
	}
}

Ahe_grenade_gameGameMode::Ahe_grenade_gameGameMode()
{
	HUDClass = AGrenadeHUD::StaticClass();
	BreakableTileGridClass = ABreakableTileGrid::StaticClass();
	// Keep the complete 2000 cm-deep arena pit above world zero. This avoids
	// below-surface rendering and atmospheric differences at the void bottom.
	BreakableTileGridTransform = FTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, 2606.0f),
		FVector::OneVector);
}

void Ahe_grenade_gameGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!bSpawnBreakableGridOnBeginPlay || !GetWorld())
	{
		return;
	}

	if (!bArenaGeneratedThisMatch)
	{
		GenerateProceduralArena();
	}
}

void Ahe_grenade_gameGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer)
	{
		return;
	}

	if (!bArenaGeneratedThisMatch && bSpawnBreakableGridOnBeginPlay)
	{
		GenerateProceduralArena();
	}

	if (bHasGeneratedSpawnTransforms)
	{
		const int32 SpawnSide = GetOrAssignSpawnSide(NewPlayer);
		const int32 SafeSide = FMath::Clamp(SpawnSide, 0, 1);
		RestartPlayerAtTransform(NewPlayer, GeneratedSpawnTransforms[SafeSide]);
		return;
	}

	Super::RestartPlayer(NewPlayer);
}

void Ahe_grenade_gameGameMode::GenerateProceduralArena()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ClearExistingArenaActors();
	ActiveBreakableGrid = nullptr;
	SpawnSideByController.Reset();
	NextSpawnSide = 0;
	bHasGeneratedSpawnTransforms = false;
	bArenaGeneratedThisMatch = false;

	const int32 EffectiveMinTilesX = FMath::Max(MinArenaTiles, FMath::Min(MinTilesX, MaxTilesX));
	const int32 EffectiveMaxTilesX = FMath::Max(MinArenaTiles, FMath::Max(MinTilesX, MaxTilesX));
	const int32 EffectiveMinTilesY = FMath::Max(MinArenaTiles, FMath::Min(MinTilesY, MaxTilesY));
	const int32 EffectiveMaxTilesY = FMath::Max(MinArenaTiles, FMath::Max(MinTilesY, MaxTilesY));

	const int32 EffectiveSeed = (ArenaSeed >= 0) ? ArenaSeed : FMath::Rand();
	LastGeneratedArenaSeed = EffectiveSeed;
	FRandomStream RandomStream(EffectiveSeed);

	const int32 TilesX = RandomStream.RandRange(EffectiveMinTilesX, EffectiveMaxTilesX);
	const int32 TilesY = RandomStream.RandRange(EffectiveMinTilesY, EffectiveMaxTilesY);

	const float EffectiveMinTileSize = FMath::Max(100.0f, FMath::Min(MinTileSizeCm, MaxTileSizeCm));
	const float EffectiveMaxTileSize = FMath::Max(100.0f, FMath::Max(MinTileSizeCm, MaxTileSizeCm));
	const float TileSizeCm = RandomStream.FRandRange(EffectiveMinTileSize, EffectiveMaxTileSize);
	const float TilePitchCm = TileSizeCm + FMath::Max(0.0f, TileSpacingCm);
	const float EffectiveTileThicknessScale = FMath::Max(0.01f, TileThicknessScale);
	const FVector ArenaCenter = BreakableTileGridTransform.GetLocation();

	const FVector GridLocalOriginOffset(
		-((TilesX - 1) * TilePitchCm * 0.5f),
		-((TilesY - 1) * TilePitchCm * 0.5f),
		0.0f);

	UClass* GridClass = BreakableTileGridClass ? BreakableTileGridClass.Get() : ABreakableTileGrid::StaticClass();
	if (!GridClass)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Arena generation aborted: no breakable grid class."));
		return;
	}

	const FTransform GridTransform(FRotator::ZeroRotator, ArenaCenter, FVector::OneVector);
	ABreakableTileGrid* SpawnedGrid = World->SpawnActor<ABreakableTileGrid>(GridClass, GridTransform);
	if (!SpawnedGrid)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Arena generation aborted: failed to spawn breakable grid actor."));
		return;
	}

	SpawnedGrid->Tags.AddUnique(GeneratedArenaTag);
	SpawnedGrid->bSpawnOnBeginPlay = false;
	SpawnedGrid->TilesX = TilesX;
	SpawnedGrid->TilesY = TilesY;
	SpawnedGrid->TileSizeCm = TileSizeCm;
	SpawnedGrid->TileSpacingCm = FMath::Max(0.0f, TileSpacingCm);
	SpawnedGrid->TileThicknessScale = EffectiveTileThicknessScale;
	SpawnedGrid->GridLocalOriginOffset = GridLocalOriginOffset;
	SpawnedGrid->BuildGrid();

	ActiveBreakableGrid = SpawnedGrid;

	// V2 rule: floor always starts fully intact.
	for (ABreakableTile* FloorTile : SpawnedGrid->GetSpawnedTiles())
	{
		if (!IsValid(FloorTile))
		{
			continue;
		}

		FloorTile->Tags.AddUnique(GeneratedArenaTag);
		FloorTile->bStartBroken = false;
		FloorTile->SetVisualMaterials(FloorTileMaterial, TrajectoryHighlightMaterial);
		FloorTile->ResetTile();
	}

	const float TileHalfHeightCm = CubeHalfSizeCm * SpawnedGrid->TileThicknessScale;
	const float TileTopSurfaceZ = SpawnedGrid->GetActorLocation().Z + TileHalfHeightCm;
	const float HalfExtentX = ((TilesX - 1) * TilePitchCm * 0.5f) + (TileSizeCm * 0.5f);
	const float HalfExtentY = ((TilesY - 1) * TilePitchCm * 0.5f) + (TileSizeCm * 0.5f);

	if (bSpawnVoidBackdrop)
	{
		SpawnVoidBackdrop(ArenaCenter, HalfExtentX, HalfExtentY, TileTopSurfaceZ);
	}

	if (bSpawnArenaWalls)
	{
		SpawnArenaWalls(ArenaCenter, HalfExtentX, HalfExtentY, TileTopSurfaceZ);
	}

	UClass* ShapeTileClass = SpawnedGrid->TileClass ? SpawnedGrid->TileClass.Get() : ABreakableTile::StaticClass();
	const int32 SpawnedShapeActorCount = bSpawnLearningShapeObstacles
		? SpawnSymmetricLearningShapes(
			SpawnedGrid,
			ShapeTileClass,
			TileSizeCm,
			TilePitchCm,
			TileTopSurfaceZ,
			TilesX,
			TilesY,
			RandomStream)
		: 0;

	CacheSpawnTransforms(ArenaCenter, HalfExtentX, TileTopSurfaceZ);

	if (AWorldSettings* WS = World->GetWorldSettings())
	{
		WS->KillZ = TileTopSurfaceZ - FMath::Max(200.0f, KillZDropCm);
	}

	bArenaGeneratedThisMatch = true;

	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Generated learning arena. Seed=%d Tiles=(%d x %d) TileSize=%.1f LearningShapes=%d"),
		LastGeneratedArenaSeed,
		TilesX,
		TilesY,
		TileSizeCm,
		SpawnedShapeActorCount);
}

void Ahe_grenade_gameGameMode::ClearExistingArenaActors() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	TArray<AActor*> ActorsToDestroy;

	for (TActorIterator<ABreakableTileGrid> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			ActorsToDestroy.Add(*It);
		}
	}

	for (TActorIterator<ABreakableTile> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			ActorsToDestroy.Add(*It);
		}
	}

	for (TActorIterator<AArenaObstacle> It(World); It; ++It)
	{
		if (IsValid(*It))
		{
			ActorsToDestroy.Add(*It);
		}
	}

	for (AActor* Actor : ActorsToDestroy)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
}

void Ahe_grenade_gameGameMode::SpawnArenaWalls(const FVector& ArenaCenter, const float HalfExtentX, const float HalfExtentY, const float TileTopSurfaceZ)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float SafeHalfExtentX = FMath::Max(100.0f, HalfExtentX);
	const float SafeHalfExtentY = FMath::Max(100.0f, HalfExtentY);
	const float WallThicknessCm = FMath::Max(5.0f, ArenaWallThicknessCm);
	const float WallHalfThicknessCm = WallThicknessCm * 0.5f;
	const float WallTopZ = TileTopSurfaceZ + FMath::Max(100.0f, ArenaWallHeightCm);
	const float WallBottomZ = bSpawnVoidBackdrop
		? TileTopSurfaceZ - FMath::Max(500.0f, VoidBackdropDepthCm)
		: TileTopSurfaceZ;
	const float TotalWallHeightCm = WallTopZ - WallBottomZ;
	const float WallCenterZ = (WallTopZ + WallBottomZ) * 0.5f;

	const FVector ScaleYWalls(
		((SafeHalfExtentX + WallHalfThicknessCm) * 2.0f) / CubeSizeCm,
		WallThicknessCm / CubeSizeCm,
		TotalWallHeightCm / CubeSizeCm);

	const FVector ScaleXWalls(
		WallThicknessCm / CubeSizeCm,
		((SafeHalfExtentY + WallHalfThicknessCm) * 2.0f) / CubeSizeCm,
		TotalWallHeightCm / CubeSizeCm);

	const FVector WallLocations[] =
	{
		FVector(ArenaCenter.X, ArenaCenter.Y + SafeHalfExtentY + WallHalfThicknessCm, WallCenterZ),
		FVector(ArenaCenter.X, ArenaCenter.Y - SafeHalfExtentY - WallHalfThicknessCm, WallCenterZ),
		FVector(ArenaCenter.X + SafeHalfExtentX + WallHalfThicknessCm, ArenaCenter.Y, WallCenterZ),
		FVector(ArenaCenter.X - SafeHalfExtentX - WallHalfThicknessCm, ArenaCenter.Y, WallCenterZ)
	};

	const FVector WallScales[] =
	{
		ScaleYWalls,
		ScaleYWalls,
		ScaleXWalls,
		ScaleXWalls
	};

	for (int32 WallIndex = 0; WallIndex < UE_ARRAY_COUNT(WallLocations); ++WallIndex)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, WallLocations[WallIndex], WallScales[WallIndex]);
		if (AArenaObstacle* Wall = World->SpawnActor<AArenaObstacle>(AArenaObstacle::StaticClass(), SpawnTransform))
		{
			Wall->Tags.AddUnique(GeneratedArenaTag);
			if (Wall->ObstacleMesh)
			{
				Wall->ObstacleMesh->SetMaterial(0, ArenaWallMaterial);
			}
		}
	}
}

void Ahe_grenade_gameGameMode::SpawnVoidBackdrop(
	const FVector& ArenaCenter,
	const float HalfExtentX,
	const float HalfExtentY,
	const float TileTopSurfaceZ)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (!VoidBackdropMaterial)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Void backdrop material is not assigned in the game mode UI."));
		return;
	}

	constexpr float BackdropThicknessCm = 20.0f;
	const float BackdropTopZ = TileTopSurfaceZ - FMath::Max(500.0f, VoidBackdropDepthCm);
	const FVector BackdropLocation(
		ArenaCenter.X,
		ArenaCenter.Y,
		BackdropTopZ - (BackdropThicknessCm * 0.5f));
	const FVector BackdropScale(
		(FMath::Max(100.0f, HalfExtentX) * 2.0f) / CubeSizeCm,
		(FMath::Max(100.0f, HalfExtentY) * 2.0f) / CubeSizeCm,
		BackdropThicknessCm / CubeSizeCm);

	const FTransform SpawnTransform(FRotator::ZeroRotator, BackdropLocation, BackdropScale);
	AArenaObstacle* Backdrop = World->SpawnActor<AArenaObstacle>(AArenaObstacle::StaticClass(), SpawnTransform);
	if (!Backdrop)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Failed to spawn void backdrop."));
		return;
	}

	Backdrop->Tags.AddUnique(GeneratedArenaTag);
	Backdrop->Tags.AddUnique(TEXT("ArenaVoidBackdrop"));
	Backdrop->SetActorEnableCollision(false);
	if (Backdrop->ObstacleMesh)
	{
		// Keep the material asset as the single source of truth so edits made in
		// Unreal's Material Editor are reflected on the generated backdrop.
		Backdrop->ObstacleMesh->SetMaterial(0, VoidBackdropMaterial);
		Backdrop->ObstacleMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		Backdrop->ObstacleMesh->SetCollisionResponseToAllChannels(ECR_Ignore);
		Backdrop->ObstacleMesh->SetGenerateOverlapEvents(false);
		Backdrop->ObstacleMesh->SetCastShadow(false);
		Backdrop->ObstacleMesh->SetEmissiveLightSource(false);
		Backdrop->ObstacleMesh->SetAffectDynamicIndirectLighting(false);
		Backdrop->ObstacleMesh->SetAffectDistanceFieldLighting(false);
		Backdrop->ObstacleMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	}
}

int32 Ahe_grenade_gameGameMode::SpawnSymmetricLearningShapes(
	ABreakableTileGrid* Grid,
	UClass* TileClass,
	const float CellSizeCm,
	const float CellPitchCm,
	const float TileTopSurfaceZ,
	const int32 TilesX,
	const int32 TilesY,
	FRandomStream& RandomStream)
{
	UWorld* World = GetWorld();
	if (!World || !Grid || !TileClass || TilesX < 8 || TilesY < 8)
	{
		return 0;
	}

	UStaticMesh* CubeMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UStaticMesh* SphereMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (!CubeMesh || !SphereMesh)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Learning shape generation skipped: basic shape meshes are unavailable."));
		return 0;
	}

	enum class ELearningShape : uint8
	{
		Rectangle,
		Triangle,
		Sphere,
		Hoop
	};

	const FVector GridStart = Grid->GetActorLocation()
		+ Grid->GetActorTransform().TransformVectorNoScale(Grid->GridLocalOriginOffset);
	const FVector XAxis = Grid->GetActorForwardVector();
	const FVector YAxis = Grid->GetActorRightVector();
	const float SafeCellSizeCm = FMath::Max(100.0f, CellSizeCm);
	const float SafeCellPitchCm = FMath::Max(SafeCellSizeCm, CellPitchCm);
	const float ShapeOuterSizeCm = SafeCellSizeCm * FMath::Clamp(LearningShapeSizeCells, 0.25f, 0.95f);
	const int32 LeftX = FMath::Clamp(
		FMath::Max(SpawnPadClearanceX + 2, TilesX / 4),
		2,
		FMath::Max(2, (TilesX / 2) - 2));
	const int32 RightX = (TilesX - 1) - LeftX;

	auto GetShapeMaterial = [&](const ELearningShape ShapeType) -> UMaterialInterface*
	{
		switch (ShapeType)
		{
		case ELearningShape::Rectangle:
			return RectangleShapeMaterial;
		case ELearningShape::Triangle:
			return TriangleShapeMaterial;
		case ELearningShape::Sphere:
			return SphereShapeMaterial;
		default:
			return HoopShapeMaterial;
		}
	};

	auto GetShapeTag = [](const ELearningShape ShapeType)
	{
		switch (ShapeType)
		{
		case ELearningShape::Rectangle:
			return FName(TEXT("LearningShape_Rectangle"));
		case ELearningShape::Triangle:
			return FName(TEXT("LearningShape_Triangle"));
		case ELearningShape::Sphere:
			return FName(TEXT("LearningShape_Sphere"));
		default:
			return FName(TEXT("LearningShape_Hoop"));
		}
	};

	auto AddCubePiece = [&](ABreakableTile* ShapeActor, const FVector& LocationCm, const FRotator& Rotation, const FVector& DimensionsCm)
	{
		return ShapeActor->AddCompositeShapePiece(
			CubeMesh,
			LocationCm,
			Rotation,
			FVector(
				DimensionsCm.X / CubeSizeCm,
				DimensionsCm.Y / CubeSizeCm,
				DimensionsCm.Z / CubeSizeCm));
	};

	auto BuildShapeGeometry = [&](ABreakableTile* ShapeActor, const ELearningShape ShapeType)
	{
		if (!ShapeActor)
		{
			return;
		}

		const float DepthCm = ShapeOuterSizeCm * 0.30f;
		const float BarThicknessCm = ShapeOuterSizeCm * 0.12f;

		switch (ShapeType)
		{
		case ELearningShape::Rectangle:
			AddCubePiece(
				ShapeActor,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				FVector(DepthCm, ShapeOuterSizeCm * 0.82f, ShapeOuterSizeCm * 0.62f));
			break;

		case ELearningShape::Triangle:
		{
			const float TriangleWidthCm = ShapeOuterSizeCm * 0.88f;
			const float TriangleHeightCm = ShapeOuterSizeCm * 0.82f;
			const float SideDeltaYCm = TriangleWidthCm * 0.5f;
			const float SideLengthCm = FMath::Sqrt(FMath::Square(SideDeltaYCm) + FMath::Square(TriangleHeightCm));
			const float SideRollDegrees = FMath::RadiansToDegrees(FMath::Atan2(TriangleHeightCm, SideDeltaYCm));
			const float BottomZ = (-TriangleHeightCm * 0.5f) + (BarThicknessCm * 0.5f);

			AddCubePiece(
				ShapeActor,
				FVector(0.0f, 0.0f, BottomZ),
				FRotator::ZeroRotator,
				FVector(DepthCm, TriangleWidthCm, BarThicknessCm));
			AddCubePiece(
				ShapeActor,
				FVector(0.0f, -TriangleWidthCm * 0.25f, 0.0f),
				FRotator(0.0f, 0.0f, -SideRollDegrees),
				FVector(DepthCm, SideLengthCm, BarThicknessCm));
			AddCubePiece(
				ShapeActor,
				FVector(0.0f, TriangleWidthCm * 0.25f, 0.0f),
				FRotator(0.0f, 0.0f, SideRollDegrees),
				FVector(DepthCm, SideLengthCm, BarThicknessCm));
			break;
		}

		case ELearningShape::Sphere:
		{
			const float SphereDiameterCm = ShapeOuterSizeCm * 0.72f;
			ShapeActor->AddCompositeShapePiece(
				SphereMesh,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				FVector(SphereDiameterCm / CubeSizeCm));
			break;
		}

		case ELearningShape::Hoop:
		{
			constexpr int32 HoopSegmentCount = 12;
			const float HoopRadiusCm = ShapeOuterSizeCm * 0.34f;
			const float SegmentLengthCm =
				(2.0f * HoopRadiusCm * FMath::Sin(PI / static_cast<float>(HoopSegmentCount))) * 1.18f;

			for (int32 SegmentIndex = 0; SegmentIndex < HoopSegmentCount; ++SegmentIndex)
			{
				const float AngleRadians =
					(2.0f * PI * static_cast<float>(SegmentIndex)) / static_cast<float>(HoopSegmentCount);
				const float AngleDegrees = FMath::RadiansToDegrees(AngleRadians);
				const FVector SegmentCenter(
					0.0f,
					HoopRadiusCm * FMath::Cos(AngleRadians),
					HoopRadiusCm * FMath::Sin(AngleRadians));

				AddCubePiece(
					ShapeActor,
					SegmentCenter,
					FRotator(0.0f, 0.0f, AngleDegrees + 90.0f),
					FVector(DepthCm * 0.82f, SegmentLengthCm, BarThicknessCm));
			}
			break;
		}
		}
	};

	auto SpawnShapeAtCell = [&](const ELearningShape ShapeType, const int32 X, const int32 Y) -> ABreakableTile*
	{
		const FVector FloorCenter = GridStart
			+ (XAxis * (SafeCellPitchCm * static_cast<float>(X)))
			+ (YAxis * (SafeCellPitchCm * static_cast<float>(Y)));
		const FVector ShapeCenter(FloorCenter.X, FloorCenter.Y, TileTopSurfaceZ + (ShapeOuterSizeCm * 0.5f) + 3.0f);

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		SpawnParams.ObjectFlags = RF_Transactional;

		ABreakableTile* ShapeActor = World->SpawnActor<ABreakableTile>(
			TileClass,
			ShapeCenter,
			Grid->GetActorRotation(),
			SpawnParams);
		if (!ShapeActor)
		{
			return nullptr;
		}

		ShapeActor->Tags.AddUnique(GeneratedArenaTag);
		ShapeActor->Tags.AddUnique(TEXT("LearningShape"));
		ShapeActor->Tags.AddUnique(GetShapeTag(ShapeType));
		ShapeActor->bStartBroken = false;
		ShapeActor->bBreakOnGrenadeImpact = true;
		ShapeActor->bBounceGrenadeBeforeBreaking = true;
		ShapeActor->BeginCompositeShape();
		ShapeActor->SetVisualMaterials(GetShapeMaterial(ShapeType), TrajectoryHighlightMaterial);
		BuildShapeGeometry(ShapeActor, ShapeType);
		ShapeActor->ResetTile();
		return ShapeActor;
	};

	const ELearningShape ShapeTypes[] =
	{
		ELearningShape::Rectangle,
		ELearningShape::Triangle,
		ELearningShape::Sphere,
		ELearningShape::Hoop
	};

	int32 SpawnedActorCount = 0;
	for (int32 ShapeIndex = 0; ShapeIndex < UE_ARRAY_COUNT(ShapeTypes); ++ShapeIndex)
	{
		const int32 RowIndex = FMath::Clamp(
			FMath::RoundToInt((static_cast<float>(ShapeIndex + 1) * static_cast<float>(TilesY - 1)) / 5.0f),
			1,
			TilesY - 2);

		// A seed only decides which end of the row receives the first spawn call; layout stays mirrored and stable.
		const bool bSpawnRightFirst = RandomStream.RandRange(0, 1) == 1;
		const int32 FirstX = bSpawnRightFirst ? RightX : LeftX;
		const int32 SecondX = bSpawnRightFirst ? LeftX : RightX;

		if (SpawnShapeAtCell(ShapeTypes[ShapeIndex], FirstX, RowIndex))
		{
			++SpawnedActorCount;
		}
		if (SpawnShapeAtCell(ShapeTypes[ShapeIndex], SecondX, RowIndex))
		{
			++SpawnedActorCount;
		}
	}

	return SpawnedActorCount;
}

int32 Ahe_grenade_gameGameMode::SpawnMirroredLabyrinthPanels(
	ABreakableTileGrid* Grid,
	UClass* TileClass,
	const float CellSizeCm,
	const float CellPitchCm,
	const float TileTopSurfaceZ,
	const int32 TilesX,
	const int32 TilesY,
	FRandomStream& RandomStream)
{
	UWorld* World = GetWorld();
	if (!World || !Grid || !TileClass)
	{
		return 0;
	}

	const int32 MirrorHalfColumns = TilesX / 2;
	if (MirrorHalfColumns <= 2 || TilesY <= 4)
	{
		return 0;
	}

	UStaticMesh* PanelMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));

	const FVector GridStartRaw = Grid->GetActorLocation() + Grid->GetActorTransform().TransformVectorNoScale(Grid->GridLocalOriginOffset);
	const FVector GridTopStart(GridStartRaw.X, GridStartRaw.Y, TileTopSurfaceZ);
	const FVector XAxis = Grid->GetActorForwardVector();
	const FVector YAxis = Grid->GetActorRightVector();
	const FVector ZAxis = Grid->GetActorUpVector();
	const float FloorTileHalfHeightCm = CubeHalfSizeCm * FMath::Max(0.01f, Grid->TileThicknessScale);
	const float SafeCellSizeCm = FMath::Max(50.0f, CellSizeCm);
	const float SafeCellPitchCm = FMath::Max(SafeCellSizeCm, CellPitchCm);
	const float PanelThicknessCm = FMath::Clamp(LabyrinthPanelThicknessRatio, 0.05f, 0.80f) * SafeCellSizeCm;
	const float PanelSinkDepthCm = FMath::Max(8.0f, FloorTileHalfHeightCm + 2.0f);
	const float MinPanelHeightCells = FMath::Max(0.25f, FMath::Min(LabyrinthMinPanelHeightCells, LabyrinthMaxPanelHeightCells));
	const float MaxPanelHeightCells = FMath::Max(MinPanelHeightCells, LabyrinthMaxPanelHeightCells);
	const int32 MaxRunLength = FMath::Max(1, FMath::Min(LabyrinthMaxPanelRunLength, FMath::Max(1, MirrorHalfColumns)));
	const float Density = FMath::Clamp(TargetNonFloorOccupancy, 0.05f, 0.75f);
	const float ConnectorChance = FMath::Clamp(LabyrinthConnectorChance, 0.0f, 1.0f);
	const int32 LeftMaxVerticalEdgeX = (TilesX % 2 == 0)
		? FMath::Max(1, MirrorHalfColumns - 1)
		: FMath::Max(1, MirrorHalfColumns);

	auto GridEdgePoint = [&](const float GridX, const float GridY) -> FVector
	{
		return GridTopStart
			+ (XAxis * (SafeCellPitchCm * GridX))
			+ (YAxis * (SafeCellPitchCm * GridY));
	};

	const int32 CellCount = TilesX * TilesY;
	TArray<uint8> Reserved;
	Reserved.Init(0, CellCount);

	const auto IsInBounds = [TilesX, TilesY](const int32 X, const int32 Y)
	{
		return X >= 0 && X < TilesX && Y >= 0 && Y < TilesY;
	};

	const auto CellIndex = [TilesX](const int32 X, const int32 Y)
	{
		return (Y * TilesX) + X;
	};

	const auto MarkReserved = [&Reserved, &IsInBounds, &CellIndex](const int32 X, const int32 Y)
	{
		if (IsInBounds(X, Y))
		{
			Reserved[CellIndex(X, Y)] = 1;
		}
	};

	const auto IsCellReserved = [&Reserved, &IsInBounds, &CellIndex](const int32 X, const int32 Y)
	{
		if (!IsInBounds(X, Y))
		{
			return true;
		}
		return Reserved[CellIndex(X, Y)] != 0;
	};

	const int32 SpawnCenterY = TilesY / 2;
	const int32 LeftSpawnX = FMath::Clamp(1, 0, TilesX - 1);
	const int32 RightSpawnX = FMath::Clamp(TilesX - 2, 0, TilesX - 1);

	for (int32 OffsetY = -SpawnPadClearanceY; OffsetY <= SpawnPadClearanceY; ++OffsetY)
	{
		for (int32 OffsetX = -SpawnPadClearanceX; OffsetX <= SpawnPadClearanceX; ++OffsetX)
		{
			MarkReserved(LeftSpawnX + OffsetX, SpawnCenterY + OffsetY);
			MarkReserved(RightSpawnX + OffsetX, SpawnCenterY + OffsetY);
		}
	}

	const int32 VerticalEdgeStride = TilesX + 1;
	TArray<uint8> VerticalOccupied;
	VerticalOccupied.Init(0, VerticalEdgeStride * TilesY);

	const int32 HorizontalEdgeStride = TilesX;
	TArray<uint8> HorizontalOccupied;
	HorizontalOccupied.Init(0, HorizontalEdgeStride * (TilesY + 1));

	const auto VerticalEdgeIndex = [VerticalEdgeStride](const int32 EdgeX, const int32 SegmentY)
	{
		return (SegmentY * VerticalEdgeStride) + EdgeX;
	};

	const auto HorizontalEdgeIndex = [HorizontalEdgeStride](const int32 SegmentX, const int32 EdgeY)
	{
		return (EdgeY * HorizontalEdgeStride) + SegmentX;
	};

	const auto IsVerticalEdgeBlocked = [&VerticalOccupied, &VerticalEdgeIndex, &IsCellReserved, TilesX, TilesY](const int32 EdgeX, const int32 SegmentY)
	{
		if (EdgeX <= 0 || EdgeX >= TilesX || SegmentY < 0 || SegmentY >= TilesY)
		{
			return true;
		}

		if (IsCellReserved(EdgeX - 1, SegmentY) || IsCellReserved(EdgeX, SegmentY))
		{
			return true;
		}

		return VerticalOccupied[VerticalEdgeIndex(EdgeX, SegmentY)] != 0;
	};

	const auto IsHorizontalEdgeBlocked = [&HorizontalOccupied, &HorizontalEdgeIndex, &IsCellReserved, TilesX, TilesY](const int32 SegmentX, const int32 EdgeY)
	{
		if (SegmentX < 0 || SegmentX >= TilesX || EdgeY <= 0 || EdgeY >= TilesY)
		{
			return true;
		}

		if (IsCellReserved(SegmentX, EdgeY - 1) || IsCellReserved(SegmentX, EdgeY))
		{
			return true;
		}

		return HorizontalOccupied[HorizontalEdgeIndex(SegmentX, EdgeY)] != 0;
	};

	const auto MarkVerticalEdgeOccupied = [&VerticalOccupied, &VerticalEdgeIndex, TilesX, TilesY](const int32 EdgeX, const int32 SegmentY)
	{
		if (EdgeX > 0 && EdgeX < TilesX && SegmentY >= 0 && SegmentY < TilesY)
		{
			VerticalOccupied[VerticalEdgeIndex(EdgeX, SegmentY)] = 1;
		}
	};

	const auto MarkHorizontalEdgeOccupied = [&HorizontalOccupied, &HorizontalEdgeIndex, TilesX, TilesY](const int32 SegmentX, const int32 EdgeY)
	{
		if (SegmentX >= 0 && SegmentX < TilesX && EdgeY > 0 && EdgeY < TilesY)
		{
			HorizontalOccupied[HorizontalEdgeIndex(SegmentX, EdgeY)] = 1;
		}
	};

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags = RF_Transactional;

	int32 SpawnedActorCount = 0;

	auto SpawnPanelActor = [&](const float CenterGridX, const float CenterGridY, const int32 RunLength, const bool bAlongX, const float HeightCells) -> ABreakableTile*
	{
		const float PanelHeightCm = FMath::Max(SafeCellSizeCm * 0.25f, HeightCells * SafeCellSizeCm);
		const float PanelTotalHeightCm = PanelHeightCm + PanelSinkDepthCm;
		const float PanelLengthCm = FMath::Max(SafeCellSizeCm * 0.50f, ((FMath::Max(1, RunLength) - 1) * SafeCellPitchCm) + (SafeCellSizeCm * 0.96f));
		const FVector PanelCenter = GridEdgePoint(CenterGridX, CenterGridY) + (ZAxis * ((PanelTotalHeightCm * 0.5f) - PanelSinkDepthCm));
		const FVector PanelScale = bAlongX
			? FVector(PanelLengthCm / CubeSizeCm, PanelThicknessCm / CubeSizeCm, PanelTotalHeightCm / CubeSizeCm)
			: FVector(PanelThicknessCm / CubeSizeCm, PanelLengthCm / CubeSizeCm, PanelTotalHeightCm / CubeSizeCm);

		const FTransform PanelTransform(Grid->GetActorRotation(), PanelCenter, PanelScale);
		ABreakableTile* Panel = World->SpawnActor<ABreakableTile>(TileClass, PanelTransform, SpawnParams);
		if (!Panel)
		{
			return nullptr;
		}

		Panel->Tags.AddUnique(GeneratedArenaTag);
		Panel->bStartBroken = false;
		Panel->bBreakOnGrenadeImpact = false;
		if (Panel->TileMesh)
		{
			Panel->TileMesh->SetMobility(EComponentMobility::Movable);
		}
		Panel->SetMeshAndTransformStyle(PanelMesh, FRotator::ZeroRotator, FVector::OneVector);
		Panel->SetVisualMaterials(LabyrinthPanelMaterial, TrajectoryHighlightMaterial);
		Panel->ResetTile();
		if (Panel->TileMesh)
		{
			Panel->TileMesh->SetCollisionProfileName(TEXT("BlockAll"));
			Panel->TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Panel->TileMesh->SetCollisionResponseToAllChannels(ECR_Block);
			Panel->TileMesh->SetGenerateOverlapEvents(false);
			Panel->TileMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
		}
		Panel->SetActorEnableCollision(true);
		return Panel;
	};

	auto CanPlaceMirroredHorizontalRun = [&](const int32 StartX, const int32 EdgeY, const int32 RunLength)
	{
		for (int32 Step = 0; Step < RunLength; ++Step)
		{
			const int32 LeftX = StartX + Step;
			const int32 RightX = (TilesX - 1) - LeftX;
			if (IsHorizontalEdgeBlocked(LeftX, EdgeY) || IsHorizontalEdgeBlocked(RightX, EdgeY))
			{
				return false;
			}
		}
		return true;
	};

	auto CanPlaceMirroredVerticalRun = [&](const int32 EdgeX, const int32 StartY, const int32 RunLength)
	{
		for (int32 Step = 0; Step < RunLength; ++Step)
		{
			const int32 Y = StartY + Step;
			const int32 RightEdgeX = TilesX - EdgeX;
			if (IsVerticalEdgeBlocked(EdgeX, Y) || IsVerticalEdgeBlocked(RightEdgeX, Y))
			{
				return false;
			}
		}
		return true;
	};

	auto MarkMirroredHorizontalRun = [&](const int32 StartX, const int32 EdgeY, const int32 RunLength)
	{
		for (int32 Step = 0; Step < RunLength; ++Step)
		{
			const int32 LeftX = StartX + Step;
			const int32 RightX = (TilesX - 1) - LeftX;
			MarkHorizontalEdgeOccupied(LeftX, EdgeY);
			MarkHorizontalEdgeOccupied(RightX, EdgeY);
		}
	};

	auto MarkMirroredVerticalRun = [&](const int32 EdgeX, const int32 StartY, const int32 RunLength)
	{
		const int32 RightEdgeX = TilesX - EdgeX;
		for (int32 Step = 0; Step < RunLength; ++Step)
		{
			const int32 Y = StartY + Step;
			MarkVerticalEdgeOccupied(EdgeX, Y);
			MarkVerticalEdgeOccupied(RightEdgeX, Y);
		}
	};

	auto TrySpawnMirroredHorizontalRun = [&](const int32 StartX, const int32 EdgeY, const int32 RequestedRunLength)
	{
		const int32 MaxLengthByBounds = MirrorHalfColumns - StartX;
		const int32 RunLength = FMath::Clamp(RequestedRunLength, 1, FMath::Max(1, MaxLengthByBounds));
		if (StartX < 0 || StartX >= MirrorHalfColumns || EdgeY <= 0 || EdgeY >= TilesY || !CanPlaceMirroredHorizontalRun(StartX, EdgeY, RunLength))
		{
			return false;
		}

		const float HeightCells = RandomRange(RandomStream, MinPanelHeightCells, MaxPanelHeightCells);
		const float LeftCenterX = StartX + ((RunLength - 1) * 0.5f);
		const float CenterY = static_cast<float>(EdgeY) - 0.5f;
		const float RightCenterX = (TilesX - 1) - LeftCenterX;

		ABreakableTile* LeftPanel = SpawnPanelActor(LeftCenterX, CenterY, RunLength, true, HeightCells);
		ABreakableTile* RightPanel = SpawnPanelActor(RightCenterX, CenterY, RunLength, true, HeightCells);
		if (!LeftPanel || !RightPanel)
		{
			if (IsValid(LeftPanel))
			{
				LeftPanel->Destroy();
			}
			if (IsValid(RightPanel))
			{
				RightPanel->Destroy();
			}
			return false;
		}

		MarkMirroredHorizontalRun(StartX, EdgeY, RunLength);
		SpawnedActorCount += 2;
		return true;
	};

	auto TrySpawnMirroredVerticalRun = [&](const int32 EdgeX, const int32 StartY, const int32 RequestedRunLength)
	{
		const int32 MaxLengthByBounds = TilesY - StartY;
		const int32 RunLength = FMath::Clamp(RequestedRunLength, 1, FMath::Max(1, MaxLengthByBounds));
		if (EdgeX <= 0 || EdgeX > LeftMaxVerticalEdgeX || StartY < 0 || StartY >= TilesY || !CanPlaceMirroredVerticalRun(EdgeX, StartY, RunLength))
		{
			return false;
		}

		const int32 RightEdgeX = TilesX - EdgeX;
		if (RightEdgeX <= EdgeX)
		{
			return false;
		}

		const float HeightCells = RandomRange(RandomStream, MinPanelHeightCells, MaxPanelHeightCells);
		const float LeftCenterX = static_cast<float>(EdgeX) - 0.5f;
		const float RightCenterX = static_cast<float>(RightEdgeX) - 0.5f;
		const float CenterY = StartY + ((RunLength - 1) * 0.5f);

		ABreakableTile* LeftPanel = SpawnPanelActor(LeftCenterX, CenterY, RunLength, false, HeightCells);
		ABreakableTile* RightPanel = SpawnPanelActor(RightCenterX, CenterY, RunLength, false, HeightCells);
		if (!LeftPanel || !RightPanel)
		{
			if (IsValid(LeftPanel))
			{
				LeftPanel->Destroy();
			}
			if (IsValid(RightPanel))
			{
				RightPanel->Destroy();
			}
			return false;
		}

		MarkMirroredVerticalRun(EdgeX, StartY, RunLength);
		SpawnedActorCount += 2;
		return true;
	};

	for (int32 EdgeY = 1; EdgeY < TilesY; EdgeY += 2)
	{
		const int32 GapLength = RandomStream.RandRange(1, 2);
		const int32 MaxGapStart = FMath::Max(0, MirrorHalfColumns - GapLength);
		const int32 GapStart = RandomStream.RandRange(0, MaxGapStart);

		if (GapStart > 0 && RandomStream.FRand() <= Density + 0.55f)
		{
			TrySpawnMirroredHorizontalRun(0, EdgeY, FMath::Min(GapStart, MaxRunLength));
		}

		const int32 AfterGapStart = GapStart + GapLength;
		if (AfterGapStart < MirrorHalfColumns && RandomStream.FRand() <= Density + 0.55f)
		{
			TrySpawnMirroredHorizontalRun(AfterGapStart, EdgeY, FMath::Min(MirrorHalfColumns - AfterGapStart, MaxRunLength));
		}
	}

	for (int32 EdgeX = 1 + RandomStream.RandRange(0, 1); EdgeX <= LeftMaxVerticalEdgeX; EdgeX += RandomStream.RandRange(2, 3))
	{
		int32 Y = RandomStream.RandRange(0, 1);
		while (Y < TilesY)
		{
			const int32 RunLength = RandomStream.RandRange(2, FMath::Min(4, FMath::Max(2, TilesY - Y)));
			if (RandomStream.FRand() <= ConnectorChance)
			{
				TrySpawnMirroredVerticalRun(EdgeX, Y, RunLength);
			}
			Y += FMath::Max(1, RunLength) + RandomStream.RandRange(1, 2);
		}
	}

	return SpawnedActorCount;
}

void Ahe_grenade_gameGameMode::CacheSpawnTransforms(const FVector& ArenaCenter, const float HalfExtentX, const float TileTopSurfaceZ)
{
	const float SafeHalfExtentX = FMath::Max(200.0f, HalfExtentX);
	const float MaxSpawnOffsetCm = FMath::Max(150.0f, SafeHalfExtentX - 120.0f);
	const float SpawnOffsetCm = FMath::Clamp(SafeHalfExtentX * FMath::Clamp(SpawnSideOffsetRatio, 0.10f, 0.95f), 150.0f, MaxSpawnOffsetCm);
	const float SpawnZ = TileTopSurfaceZ + FMath::Max(0.0f, SpawnHeightOffsetCm);

	GeneratedSpawnTransforms[0] = FTransform(
		FRotator(0.0f, 0.0f, 0.0f),
		FVector(ArenaCenter.X - SpawnOffsetCm, ArenaCenter.Y, SpawnZ),
		FVector::OneVector);

	GeneratedSpawnTransforms[1] = FTransform(
		FRotator(0.0f, 180.0f, 0.0f),
		FVector(ArenaCenter.X + SpawnOffsetCm, ArenaCenter.Y, SpawnZ),
		FVector::OneVector);

	bHasGeneratedSpawnTransforms = true;
}

int32 Ahe_grenade_gameGameMode::GetOrAssignSpawnSide(AController* Controller)
{
	if (!Controller)
	{
		return 0;
	}

	for (auto It = SpawnSideByController.CreateIterator(); It; ++It)
	{
		if (!It.Key().IsValid())
		{
			It.RemoveCurrent();
		}
	}

	const TWeakObjectPtr<AController> Key(Controller);
	if (const int32* ExistingSide = SpawnSideByController.Find(Key))
	{
		return *ExistingSide;
	}

	const int32 AssignedSide = NextSpawnSide % 2;
	NextSpawnSide = (NextSpawnSide + 1) % 2;
	SpawnSideByController.Add(Key, AssignedSide);
	return AssignedSide;
}
