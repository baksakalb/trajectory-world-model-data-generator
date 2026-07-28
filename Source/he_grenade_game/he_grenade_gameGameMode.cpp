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
#include "GrenadeGameState.h"
#include "GrenadePlayerState.h"
#include "he_grenade_game.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

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
	PrimaryActorTick.bCanEverTick = true;
	HUDClass = AGrenadeHUD::StaticClass();
	GameStateClass = AGrenadeGameState::StaticClass();
	PlayerStateClass = AGrenadePlayerState::StaticClass();
	BreakableTileGridClass = ABreakableTileGrid::StaticClass();

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> DefaultGlassPanelMaterial(
		TEXT("/Game/Materials/M_GlassTile.M_GlassTile"));
	if (DefaultGlassPanelMaterial.Succeeded())
	{
		GlassPanelMaterial = DefaultGlassPanelMaterial.Object;
	}

	// Keep the complete 2000 cm-deep arena pit above world zero. This avoids
	// below-surface rendering and atmospheric differences at the void bottom.
	BreakableTileGridTransform = FTransform(
		FRotator::ZeroRotator,
		FVector(0.0f, 0.0f, 2606.0f),
		FVector::OneVector);
}

void Ahe_grenade_gameGameMode::PostLogin(APlayerController* NewPlayer)
{
	Super::PostLogin(NewPlayer);
	GetOrAssignSpawnSide(NewPlayer);
}

void Ahe_grenade_gameGameMode::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bFloorCollapseActive || !ActiveBreakableGrid)
	{
		return;
	}

	FloorCollapseTimeRemaining -= FMath::Max(0.0f, DeltaSeconds);
	if (FloorCollapseTimeRemaining > 0.0f)
	{
		UpdateFloorRingWarning();
		return;
	}

	CollapseCurrentFloorRing();
	++CurrentFloorCollapseRing;
	if (CurrentFloorCollapseRing > MaximumFloorCollapseRing)
	{
		bFloorCollapseActive = false;
		FloorCollapseTimeRemaining = 0.0f;
		CurrentFloorRingTiles.Reset();
		return;
	}

	FloorCollapseTimeRemaining = FMath::Max(1.0f, FloorRingCollapseIntervalSeconds);
	CacheCurrentFloorRingTiles();
	UpdateFloorRingWarning();
}

float Ahe_grenade_gameGameMode::GetFloorCollapseProgress() const
{
	if (!bFloorCollapseActive)
	{
		return 1.0f;
	}

	const float SafeInterval = FMath::Max(1.0f, FloorRingCollapseIntervalSeconds);
	return 1.0f - FMath::Clamp(FloorCollapseTimeRemaining / SafeInterval, 0.0f, 1.0f);
}

void Ahe_grenade_gameGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (AGrenadeGameState* GrenadeGameState = GetGameState<AGrenadeGameState>())
	{
		GrenadeGameState->SetGrenadeMatchPhase(EGGMatchPhase::Lobby);
	}

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
	bFloorCollapseActive = false;
	CurrentFloorRingTiles.Reset();
	LearningObjectFootprints.Reset();
	CurrentFloorCollapseRing = 0;
	MaximumFloorCollapseRing = INDEX_NONE;
	FloorCollapseTimeRemaining = 0.0f;

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

	const int32 SpawnedGlassPanelCount = bSpawnGlassPanels
		? SpawnRandomGlassPanels(
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
	InitializeFloorRingCollapse();

	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Generated learning arena. Seed=%d Tiles=(%d x %d) TileSize=%.1f LearningShapes=%d GlassPanels=%d"),
		LastGeneratedArenaSeed,
		TilesX,
		TilesY,
		TileSizeCm,
		SpawnedShapeActorCount,
		SpawnedGlassPanelCount);
}

void Ahe_grenade_gameGameMode::InitializeFloorRingCollapse()
{
	bFloorCollapseActive = false;
	CurrentFloorRingTiles.Reset();
	CurrentFloorCollapseRing = 0;
	FloorCollapseTimeRemaining = 0.0f;

	if (!bEnableFloorRingCollapse || !ActiveBreakableGrid)
	{
		return;
	}

	MaximumFloorCollapseRing =
		(FMath::Min(ActiveBreakableGrid->TilesX, ActiveBreakableGrid->TilesY) - 1) / 2;
	if (MaximumFloorCollapseRing < 0)
	{
		return;
	}

	FloorCollapseTimeRemaining = FMath::Max(1.0f, FloorRingCollapseIntervalSeconds);
	bFloorCollapseActive = true;
	CacheCurrentFloorRingTiles();
	UpdateFloorRingWarning();

	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Floor ring collapse armed. Rings=%d Interval=%.1fs FirstRingTiles=%d"),
		MaximumFloorCollapseRing + 1,
		FloorCollapseTimeRemaining,
		CurrentFloorRingTiles.Num());
}

void Ahe_grenade_gameGameMode::CacheCurrentFloorRingTiles()
{
	CurrentFloorRingTiles.Reset();
	if (!ActiveBreakableGrid)
	{
		return;
	}

	const int32 MaxX = ActiveBreakableGrid->TilesX - 1;
	const int32 MaxY = ActiveBreakableGrid->TilesY - 1;
	for (int32 Y = 0; Y <= MaxY; ++Y)
	{
		for (int32 X = 0; X <= MaxX; ++X)
		{
			const int32 RingIndex = FMath::Min(
				FMath::Min(X, MaxX - X),
				FMath::Min(Y, MaxY - Y));
			if (RingIndex == CurrentFloorCollapseRing)
			{
				CurrentFloorRingTiles.Add(ActiveBreakableGrid->GetTileAtIndex(X, Y));
			}
		}
	}
}

void Ahe_grenade_gameGameMode::UpdateFloorRingWarning()
{
	const float WarningAlpha = GetFloorCollapseProgress();
	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : CurrentFloorRingTiles)
	{
		if (ABreakableTile* Tile = TilePtr.Get())
		{
			Tile->SetDestructionWarningAlpha(WarningAlpha);
		}
	}
}

void Ahe_grenade_gameGameMode::CollapseCurrentFloorRing()
{
	int32 BrokenTileCount = 0;
	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : CurrentFloorRingTiles)
	{
		if (ABreakableTile* Tile = TilePtr.Get(); Tile && !Tile->IsBroken())
		{
			Tile->SetDestructionWarningAlpha(1.0f);
			Tile->BreakTile();
			++BrokenTileCount;
		}
	}

	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Collapsed floor ring %d. BrokenTiles=%d"),
		CurrentFloorCollapseRing,
		BrokenTileCount);
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
	UStaticMesh* HoopMesh = LoadObject<UStaticMesh>(
		nullptr,
		TEXT("/Game/Meshes/SM_BreakableHoop.SM_BreakableHoop"));
	if (!CubeMesh || !SphereMesh || !HoopMesh)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Learning shape generation skipped: required shape meshes are unavailable."));
		return 0;
	}

	enum class ELearningShape : uint8
	{
		Rectangle,
		Triangle,
		Sphere,
		Hoop,
		Ramp
	};

	const FVector GridStart = Grid->GetActorLocation()
		+ Grid->GetActorTransform().TransformVectorNoScale(Grid->GridLocalOriginOffset);
	const FVector XAxis = Grid->GetActorForwardVector();
	const FVector YAxis = Grid->GetActorRightVector();
	const float SafeCellSizeCm = FMath::Max(100.0f, CellSizeCm);
	const float SafeCellPitchCm = FMath::Max(SafeCellSizeCm, CellPitchCm);
	const float ShapeOuterSizeCm = SafeCellSizeCm * FMath::Clamp(LearningShapeSizeCells, 0.25f, 0.95f);

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
		case ELearningShape::Hoop:
			return HoopShapeMaterial;
		default:
			return RampShapeMaterial
				? RampShapeMaterial.Get()
				: (RectangleShapeMaterial ? RectangleShapeMaterial.Get() : FloorTileMaterial.Get());
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
		case ELearningShape::Hoop:
			return FName(TEXT("LearningShape_Hoop"));
		default:
			return FName(TEXT("LearningShape_Ramp"));
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

	auto BuildShapeGeometry =
		[&](ABreakableTile* ShapeActor, const ELearningShape ShapeType, const FIntPoint& RampDirection)
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
			// The imported torus is 82 cm across and centered at the origin.
			// Its static-mesh asset owns decomposed convex collision, preserving
			// the open center while rendering as one continuous surface.
			ShapeActor->AddCompositeShapePiece(
				HoopMesh,
				FVector::ZeroVector,
				FRotator::ZeroRotator,
				FVector(ShapeOuterSizeCm / 100.0f));
			break;
		}

		case ELearningShape::Ramp:
		{
			const float RampRunCm = SafeCellSizeCm * FMath::Max(0.5f, RampLengthCells);
			const float RampHeightCm = FMath::Max(100.0f, GlassPanelHeightCm);
			const float RampThicknessCm =
				SafeCellSizeCm * FMath::Clamp(RampThicknessRatio, 0.02f, 0.30f);

			// Solve for a rotated slab whose world-aligned bounds are exactly the
			// requested run and height, including the slab's own thickness.
			float RampAngleRadians = FMath::Atan2(RampHeightCm, RampRunCm);
			float CenterlineRunCm = RampRunCm;
			float CenterlineRiseCm = RampHeightCm;
			for (int32 Iteration = 0; Iteration < 4; ++Iteration)
			{
				CenterlineRunCm = FMath::Max(
					1.0f,
					RampRunCm - (RampThicknessCm * FMath::Sin(RampAngleRadians)));
				CenterlineRiseCm = FMath::Max(
					1.0f,
					RampHeightCm - (RampThicknessCm * FMath::Cos(RampAngleRadians)));
				RampAngleRadians = FMath::Atan2(CenterlineRiseCm, CenterlineRunCm);
			}

			const float RampSlabLengthCm =
				FMath::Sqrt(FMath::Square(CenterlineRunCm) + FMath::Square(CenterlineRiseCm));
			const float RampAngleDegrees = FMath::RadiansToDegrees(RampAngleRadians);
			const bool bAlongX = RampDirection.X != 0;
			const float DirectionSign = bAlongX
				? static_cast<float>(RampDirection.X)
				: static_cast<float>(RampDirection.Y);
			const FRotator RampRotation = bAlongX
				? FRotator(RampAngleDegrees * DirectionSign, 0.0f, 0.0f)
				: FRotator(0.0f, 0.0f, RampAngleDegrees * DirectionSign);
			const FVector RampDimensions = bAlongX
				? FVector(RampSlabLengthCm, SafeCellSizeCm, RampThicknessCm)
				: FVector(SafeCellSizeCm, RampSlabLengthCm, RampThicknessCm);

			AddCubePiece(
				ShapeActor,
				FVector::ZeroVector,
				RampRotation,
				RampDimensions);
			break;
		}
		}
	};

	auto SpawnShapeAtCell = [&](
		const ELearningShape ShapeType,
		const int32 X,
		const int32 Y,
		const FIntPoint& RampDirection) -> ABreakableTile*
	{
		const FVector FloorCenter = GridStart
			+ (XAxis * (SafeCellPitchCm * static_cast<float>(X)))
			+ (YAxis * (SafeCellPitchCm * static_cast<float>(Y)));
		const float ShapeCenterHeightCm = ShapeType == ELearningShape::Ramp
			? (FMath::Max(100.0f, GlassPanelHeightCm) * 0.5f)
			: ((ShapeOuterSizeCm * 0.5f) + 3.0f);
		const FVector ShapeCenter(
			FloorCenter.X,
			FloorCenter.Y,
			TileTopSurfaceZ + ShapeCenterHeightCm);

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
		if (ShapeType == ELearningShape::Ramp)
		{
			ShapeActor->Tags.AddUnique(
				RampDirection.X != 0
					? FName(TEXT("Ramp_AlongX"))
					: FName(TEXT("Ramp_AlongY")));
			ShapeActor->Tags.AddUnique(
				(RampDirection.X + RampDirection.Y) > 0
					? FName(TEXT("Ramp_RisesPositive"))
					: FName(TEXT("Ramp_RisesNegative")));
		}
		ShapeActor->bStartBroken = false;
		ShapeActor->bBreakOnGrenadeImpact = true;
		ShapeActor->bBounceGrenadeBeforeBreaking = true;
		ShapeActor->BeginCompositeShape();
		ShapeActor->SetVisualMaterials(GetShapeMaterial(ShapeType), TrajectoryHighlightMaterial);
		BuildShapeGeometry(ShapeActor, ShapeType, RampDirection);
		ShapeActor->ResetTile();
		return ShapeActor;
	};

	const ELearningShape NonRampShapeTypes[] =
	{
		ELearningShape::Rectangle,
		ELearningShape::Triangle,
		ELearningShape::Sphere,
		ELearningShape::Hoop
	};

	const int32 SpawnCenterY = TilesY / 2;
	const int32 LeftSpawnX = FMath::Clamp(1, 0, TilesX - 1);
	const int32 RightSpawnX = FMath::Clamp(TilesX - 2, 0, TilesX - 1);
	auto IsSpawnClearanceCell = [&](const int32 X, const int32 Y)
	{
		const bool bWithinY = FMath::Abs(Y - SpawnCenterY) <= SpawnPadClearanceY;
		const bool bWithinLeftX = FMath::Abs(X - LeftSpawnX) <= SpawnPadClearanceX;
		const bool bWithinRightX = FMath::Abs(X - RightSpawnX) <= SpawnPadClearanceX;
		return bWithinY && (bWithinLeftX || bWithinRightX);
	};

	auto BuildSymmetricOrbit = [&](const int32 X, const int32 Y)
	{
		const int32 MirroredX = (TilesX - 1) - X;
		const int32 MirroredY = (TilesY - 1) - Y;
		TArray<FIntPoint> Orbit;
		Orbit.Reserve(4);
		Orbit.AddUnique(FIntPoint(X, Y));
		Orbit.AddUnique(FIntPoint(MirroredX, Y));
		Orbit.AddUnique(FIntPoint(X, MirroredY));
		Orbit.AddUnique(FIntPoint(MirroredX, MirroredY));
		return Orbit;
	};

	const float CellHalfExtentGridUnits = (SafeCellSizeCm / SafeCellPitchCm) * 0.5f;
	const float StandardShapeHalfExtentGridUnits = (ShapeOuterSizeCm / SafeCellPitchCm) * 0.5f;
	const float RampReachGridUnits =
		(FMath::Max(0.5f, RampLengthCells) * SafeCellSizeCm) / SafeCellPitchCm;

	auto MakeShapeFootprint = [&](const ELearningShape ShapeType, const FIntPoint& Cell, const FIntPoint& RampDirection)
	{
		FArenaObjectFootprint Footprint;
		const FVector2D CellCenter(static_cast<float>(Cell.X), static_cast<float>(Cell.Y));
		if (ShapeType != ELearningShape::Ramp)
		{
			const FVector2D Extent(StandardShapeHalfExtentGridUnits);
			Footprint.Min = CellCenter - Extent;
			Footprint.Max = CellCenter + Extent;
			return Footprint;
		}

		if (RampDirection.X != 0)
		{
			Footprint.Min = FVector2D(
				CellCenter.X - (RampReachGridUnits * 0.5f),
				CellCenter.Y - CellHalfExtentGridUnits);
			Footprint.Max = FVector2D(
				CellCenter.X + (RampReachGridUnits * 0.5f),
				CellCenter.Y + CellHalfExtentGridUnits);
		}
		else
		{
			Footprint.Min = FVector2D(
				CellCenter.X - CellHalfExtentGridUnits,
				CellCenter.Y - (RampReachGridUnits * 0.5f));
			Footprint.Max = FVector2D(
				CellCenter.X + CellHalfExtentGridUnits,
				CellCenter.Y + (RampReachGridUnits * 0.5f));
		}
		return Footprint;
	};

	auto FootprintsOverlap = [](const FArenaObjectFootprint& A, const FArenaObjectFootprint& B)
	{
		constexpr float ContactTolerance = 0.005f;
		return A.Max.X > (B.Min.X + ContactTolerance)
			&& B.Max.X > (A.Min.X + ContactTolerance)
			&& A.Max.Y > (B.Min.Y + ContactTolerance)
			&& B.Max.Y > (A.Min.Y + ContactTolerance);
	};

	auto FootprintFitsArena = [&](const FArenaObjectFootprint& Footprint)
	{
		return Footprint.Min.X >= -CellHalfExtentGridUnits
			&& Footprint.Min.Y >= -CellHalfExtentGridUnits
			&& Footprint.Max.X <= static_cast<float>(TilesX - 1) + CellHalfExtentGridUnits
			&& Footprint.Max.Y <= static_cast<float>(TilesY - 1) + CellHalfExtentGridUnits;
	};

	TArray<FIntPoint> CandidateCells;
	const int32 LastSourceColumn = (TilesX - 1) / 2;
	const int32 LastSourceRow = (TilesY - 1) / 2;
	for (int32 Y = 1; Y <= LastSourceRow; ++Y)
	{
		for (int32 X = 1; X <= LastSourceColumn; ++X)
		{
			const TArray<FIntPoint> Orbit = BuildSymmetricOrbit(X, Y);
			if (Orbit.Num() == 1)
			{
				// A directional ramp cannot occupy the exact arena center while
				// remaining symmetric across both axes.
				continue;
			}

			bool bOrbitIsClear = true;
			for (const FIntPoint& Cell : Orbit)
			{
				if (IsSpawnClearanceCell(Cell.X, Cell.Y))
				{
					bOrbitIsClear = false;
					break;
				}
			}
			if (bOrbitIsClear)
			{
				CandidateCells.Add(FIntPoint(X, Y));
			}
		}
	}

	for (int32 Index = CandidateCells.Num() - 1; Index > 0; --Index)
	{
		const int32 SwapIndex = RandomStream.RandRange(0, Index);
		CandidateCells.Swap(Index, SwapIndex);
	}

	int32 SpawnedActorCount = 0;
	auto SpawnShapeOrbit = [&](const ELearningShape ShapeType, const FIntPoint& SourceCell) -> bool
	{
		const bool bOnXCenterline = SourceCell.X == ((TilesX - 1) - SourceCell.X);
		const bool bOnYCenterline = SourceCell.Y == ((TilesY - 1) - SourceCell.Y);
		const bool bRampAlongX = bOnYCenterline
			? true
			: (bOnXCenterline ? false : (RandomStream.RandRange(0, 1) == 0));
		const int32 RampDirectionSign = RandomStream.RandRange(0, 1) == 0 ? -1 : 1;
		const FIntPoint SourceRampDirection = bRampAlongX
			? FIntPoint(RampDirectionSign, 0)
			: FIntPoint(0, RampDirectionSign);

		const TArray<FIntPoint> Orbit = BuildSymmetricOrbit(SourceCell.X, SourceCell.Y);
		TArray<FIntPoint> RampDirections;
		TArray<FArenaObjectFootprint> ProposedFootprints;
		RampDirections.Reserve(Orbit.Num());
		ProposedFootprints.Reserve(Orbit.Num());

		for (const FIntPoint& Cell : Orbit)
		{
			FIntPoint MirroredRampDirection = SourceRampDirection;
			if (Cell.X != SourceCell.X)
			{
				MirroredRampDirection.X *= -1;
			}
			if (Cell.Y != SourceCell.Y)
			{
				MirroredRampDirection.Y *= -1;
			}

			const FArenaObjectFootprint Footprint =
				MakeShapeFootprint(ShapeType, Cell, MirroredRampDirection);
			if (!FootprintFitsArena(Footprint))
			{
				return false;
			}
			for (const FArenaObjectFootprint& ExistingFootprint : LearningObjectFootprints)
			{
				if (FootprintsOverlap(Footprint, ExistingFootprint))
				{
					return false;
				}
			}
			for (const FArenaObjectFootprint& ProposedFootprint : ProposedFootprints)
			{
				if (FootprintsOverlap(Footprint, ProposedFootprint))
				{
					return false;
				}
			}

			RampDirections.Add(MirroredRampDirection);
			ProposedFootprints.Add(Footprint);
		}

		bool bSpawnedAny = false;
		for (int32 OrbitIndex = 0; OrbitIndex < Orbit.Num(); ++OrbitIndex)
		{
			const FIntPoint& Cell = Orbit[OrbitIndex];
			const FIntPoint& RampDirection = RampDirections[OrbitIndex];
			if (SpawnShapeAtCell(ShapeType, Cell.X, Cell.Y, RampDirection))
			{
				++SpawnedActorCount;
				bSpawnedAny = true;
				LearningObjectFootprints.Add(ProposedFootprints[OrbitIndex]);
			}
		}
		return bSpawnedAny;
	};

	TSet<int32> UsedCandidateIndices;
	int32 GuaranteedTypeCount = 0;
	auto TrySpawnGuaranteedType = [&](const ELearningShape ShapeType)
	{
		for (int32 CandidateIndex = 0; CandidateIndex < CandidateCells.Num(); ++CandidateIndex)
		{
			if (UsedCandidateIndices.Contains(CandidateIndex))
			{
				continue;
			}
			if (SpawnShapeOrbit(ShapeType, CandidateCells[CandidateIndex]))
			{
				UsedCandidateIndices.Add(CandidateIndex);
				++GuaranteedTypeCount;
				return true;
			}
		}
		return false;
	};

	// Large directional footprints are hardest to place. Reserve their valid space
	// first, then let smaller objects fill around them.
	TrySpawnGuaranteedType(ELearningShape::Ramp);
	const float AdditionalRampChance = FMath::Clamp(RampSpawnChance, 0.0f, 1.0f);
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCells.Num(); ++CandidateIndex)
	{
		if (!UsedCandidateIndices.Contains(CandidateIndex)
			&& RandomStream.FRand() <= AdditionalRampChance
			&& SpawnShapeOrbit(ELearningShape::Ramp, CandidateCells[CandidateIndex]))
		{
			UsedCandidateIndices.Add(CandidateIndex);
		}
	}

	for (const ELearningShape ShapeType : NonRampShapeTypes)
	{
		TrySpawnGuaranteedType(ShapeType);
	}

	const float AdditionalShapeChance = FMath::Clamp(LearningShapeSpawnChance, 0.0f, 1.0f);
	for (int32 CandidateIndex = 0; CandidateIndex < CandidateCells.Num(); ++CandidateIndex)
	{
		if (!UsedCandidateIndices.Contains(CandidateIndex)
			&& RandomStream.FRand() <= AdditionalShapeChance)
		{
			const ELearningShape ShapeType =
				NonRampShapeTypes[RandomStream.RandRange(0, UE_ARRAY_COUNT(NonRampShapeTypes) - 1)];
			if (SpawnShapeOrbit(ShapeType, CandidateCells[CandidateIndex]))
			{
				UsedCandidateIndices.Add(CandidateIndex);
			}
		}
	}

	constexpr int32 RequiredShapeTypeCount = UE_ARRAY_COUNT(NonRampShapeTypes) + 1;
	if (GuaranteedTypeCount < RequiredShapeTypeCount)
	{
		UE_LOG(
			Loghe_grenade_game,
			Warning,
			TEXT("Only %d learning-shape types could be guaranteed because the arena has too few clear symmetric cells."),
			GuaranteedTypeCount);
	}

	return SpawnedActorCount;
}

int32 Ahe_grenade_gameGameMode::SpawnRandomGlassPanels(
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
	if (!World || !Grid || !TileClass || TilesX < 1 || TilesY < 1)
	{
		return 0;
	}

	UStaticMesh* PanelMesh = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
	UMaterialInterface* PanelMaterial = GlassPanelMaterial
		? GlassPanelMaterial.Get()
		: LoadObject<UMaterialInterface>(nullptr, TEXT("/Game/Materials/M_GlassTile.M_GlassTile"));
	if (!PanelMesh || !PanelMaterial)
	{
		UE_LOG(
			Loghe_grenade_game,
			Warning,
			TEXT("Glass panel generation skipped: required cube mesh or glass material is unavailable."));
		return 0;
	}

	const float SafeCellSizeCm = FMath::Max(100.0f, CellSizeCm);
	const float SafeCellPitchCm = FMath::Max(SafeCellSizeCm, CellPitchCm);
	const float PanelLengthCm = SafeCellSizeCm;
	const float PanelThicknessCm =
		SafeCellSizeCm * FMath::Clamp(GlassPanelThicknessRatio, 0.02f, 0.50f);
	const float PanelHeight = FMath::Max(100.0f, GlassPanelHeightCm);
	const float SpawnChance = FMath::Clamp(GlassPanelSpawnChance, 0.0f, 1.0f);

	const FVector GridStart = Grid->GetActorLocation()
		+ Grid->GetActorTransform().TransformVectorNoScale(Grid->GridLocalOriginOffset);
	const FVector XAxis = Grid->GetActorForwardVector();
	const FVector YAxis = Grid->GetActorRightVector();
	const FVector ZAxis = Grid->GetActorUpVector();

	const int32 SpawnCenterY = TilesY / 2;
	const int32 LeftSpawnX = FMath::Clamp(1, 0, TilesX - 1);
	const int32 RightSpawnX = FMath::Clamp(TilesX - 2, 0, TilesX - 1);

	auto IsSpawnClearanceCell = [&](const int32 X, const int32 Y)
	{
		const bool bWithinY = FMath::Abs(Y - SpawnCenterY) <= SpawnPadClearanceY;
		const bool bWithinLeftX = FMath::Abs(X - LeftSpawnX) <= SpawnPadClearanceX;
		const bool bWithinRightX = FMath::Abs(X - RightSpawnX) <= SpawnPadClearanceX;
		return bWithinY && (bWithinLeftX || bWithinRightX);
	};

	const float PanelHalfLengthGridUnits = (PanelLengthCm / SafeCellPitchCm) * 0.5f;
	const float PanelHalfThicknessGridUnits = (PanelThicknessCm / SafeCellPitchCm) * 0.5f;
	auto MakePanelFootprint = [&](const int32 X, const int32 Y, const bool bAlongX)
	{
		const FVector2D Center(static_cast<float>(X), static_cast<float>(Y));
		const FVector2D Extent = bAlongX
			? FVector2D(PanelHalfLengthGridUnits, PanelHalfThicknessGridUnits)
			: FVector2D(PanelHalfThicknessGridUnits, PanelHalfLengthGridUnits);
		FArenaObjectFootprint Footprint;
		Footprint.Min = Center - Extent;
		Footprint.Max = Center + Extent;
		return Footprint;
	};

	auto OverlapsLearningObject = [&](const FArenaObjectFootprint& PanelFootprint)
	{
		constexpr float ContactTolerance = 0.005f;
		for (const FArenaObjectFootprint& ObjectFootprint : LearningObjectFootprints)
		{
			if (PanelFootprint.Max.X > (ObjectFootprint.Min.X + ContactTolerance)
				&& ObjectFootprint.Max.X > (PanelFootprint.Min.X + ContactTolerance)
				&& PanelFootprint.Max.Y > (ObjectFootprint.Min.Y + ContactTolerance)
				&& ObjectFootprint.Max.Y > (PanelFootprint.Min.Y + ContactTolerance))
			{
				return true;
			}
		}
		return false;
	};

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	SpawnParams.ObjectFlags = RF_Transactional;

	int32 SpawnedPanelCount = 0;
	auto SpawnPanelAtCell = [&](const int32 X, const int32 Y, const bool bAlongX) -> bool
	{
		const FVector FloorCenter = GridStart
			+ (XAxis * (SafeCellPitchCm * static_cast<float>(X)))
			+ (YAxis * (SafeCellPitchCm * static_cast<float>(Y)));
		const FVector PanelCenter(
			FloorCenter.X,
			FloorCenter.Y,
			TileTopSurfaceZ + (PanelHeight * 0.5f));
		const FVector PanelScale = bAlongX
			? FVector(
				PanelLengthCm / CubeSizeCm,
				PanelThicknessCm / CubeSizeCm,
				PanelHeight / CubeSizeCm)
			: FVector(
				PanelThicknessCm / CubeSizeCm,
				PanelLengthCm / CubeSizeCm,
				PanelHeight / CubeSizeCm);

		const FTransform PanelTransform(
			Grid->GetActorRotation(),
			PanelCenter,
			FVector::OneVector);
		ABreakableTile* Panel = World->SpawnActor<ABreakableTile>(
			TileClass,
			PanelTransform,
			SpawnParams);
		if (!Panel)
		{
			return false;
		}

		Panel->Tags.AddUnique(GeneratedArenaTag);
		Panel->Tags.AddUnique(TEXT("GlassPanel"));
		Panel->Tags.AddUnique(
			bAlongX
				? FName(TEXT("GlassPanel_AlongX"))
				: FName(TEXT("GlassPanel_AlongY")));
		Panel->bStartBroken = false;
		Panel->bBreakOnGrenadeImpact = true;
		Panel->bBounceGrenadeBeforeBreaking = true;
		if (Panel->TileMesh)
		{
			Panel->TileMesh->SetMobility(EComponentMobility::Movable);
		}
		Panel->SetMeshAndTransformStyle(
			PanelMesh,
			FRotator::ZeroRotator,
			PanelScale);
		Panel->SetVisualMaterials(PanelMaterial, TrajectoryHighlightMaterial);
		Panel->ResetTile();

		if (Panel->TileMesh)
		{
			Panel->TileMesh->SetCollisionProfileName(TEXT("BlockAll"));
			Panel->TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Panel->TileMesh->SetCollisionResponseToAllChannels(ECR_Block);
			Panel->TileMesh->SetGenerateOverlapEvents(false);
			Panel->TileMesh->SetCastShadow(false);
			Panel->TileMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
		}
		Panel->SetActorEnableCollision(true);
		return true;
	};

	// Generate one quadrant, then reflect each accepted placement across both arena
	// centerlines. The same chance and orientation therefore produce four-way fairness.
	TSet<FIntPoint> OccupiedPanelCells;
	const int32 LastSourceColumn = (TilesX - 1) / 2;
	const int32 LastSourceRow = (TilesY - 1) / 2;
	for (int32 Y = 0; Y <= LastSourceRow; ++Y)
	{
		for (int32 X = 0; X <= LastSourceColumn; ++X)
		{
			const int32 MirroredX = (TilesX - 1) - X;
			const int32 MirroredY = (TilesY - 1) - Y;
			TArray<FIntPoint> Orbit;
			Orbit.Reserve(4);
			Orbit.AddUnique(FIntPoint(X, Y));
			Orbit.AddUnique(FIntPoint(MirroredX, Y));
			Orbit.AddUnique(FIntPoint(X, MirroredY));
			Orbit.AddUnique(FIntPoint(MirroredX, MirroredY));

			const bool bAlongX = RandomStream.RandRange(0, 1) == 0;
			bool bOrbitIsClear = true;
			for (const FIntPoint& Cell : Orbit)
			{
				if (IsSpawnClearanceCell(Cell.X, Cell.Y)
					|| OccupiedPanelCells.Contains(Cell)
					|| OverlapsLearningObject(MakePanelFootprint(Cell.X, Cell.Y, bAlongX)))
				{
					bOrbitIsClear = false;
					break;
				}
			}

			if (!bOrbitIsClear || RandomStream.FRand() > SpawnChance)
			{
				continue;
			}

			for (const FIntPoint& Cell : Orbit)
			{
				if (SpawnPanelAtCell(Cell.X, Cell.Y, bAlongX))
				{
					++SpawnedPanelCount;
					OccupiedPanelCells.Add(Cell);
				}
			}
		}
	}

	return SpawnedPanelCount;
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

	if (AGrenadePlayerState* GrenadePlayerState = Controller->GetPlayerState<AGrenadePlayerState>())
	{
		GrenadePlayerState->SetAssignedSide(
			AssignedSide == 0 ? EGGPlayerSide::Left : EGGPlayerSide::Right);
		GrenadePlayerState->ClearArenaReady();
	}

	return AssignedSide;
}
