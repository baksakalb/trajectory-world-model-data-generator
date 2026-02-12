// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameGameMode.h"

#include "EngineUtils.h"
#include "Engine/World.h"
#include "GameFramework/WorldSettings.h"
#include "Grenade/ArenaObstacle.h"
#include "Grenade/Breakables/BreakableTileGrid.h"
#include "Grenade/GrenadeHUD.h"
#include "he_grenade_game.h"

Ahe_grenade_gameGameMode::Ahe_grenade_gameGameMode()
{
	HUDClass = AGrenadeHUD::StaticClass();
	BreakableTileGridClass = ABreakableTileGrid::StaticClass();
	BreakableTileGridTransform = FTransform(FRotator::ZeroRotator, FVector(0.0f, 0.0f, 6.0f), FVector::OneVector);
}

void Ahe_grenade_gameGameMode::BeginPlay()
{
	Super::BeginPlay();

	if (!GetWorld())
	{
		return;
	}

	// --- Breakable tile grid ---
	if (!bSpawnBreakableGridOnBeginPlay)
	{
		bSpawnBreakableGridOnBeginPlay = true;
	}

	bool bGridExists = false;
	for (TActorIterator<ABreakableTileGrid> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It))
		{
			UE_LOG(Loghe_grenade_game, Log, TEXT("Breakable grid already exists; skipping auto-spawn."));
			bGridExists = true;
			break;
		}
	}

	if (!bGridExists)
	{
		UClass* GridClass = BreakableTileGridClass ? BreakableTileGridClass.Get() : ABreakableTileGrid::StaticClass();
		if (GridClass)
		{
			if (ABreakableTileGrid* SpawnedGrid = GetWorld()->SpawnActor<ABreakableTileGrid>(GridClass, BreakableTileGridTransform))
			{
				UE_LOG(Loghe_grenade_game, Log, TEXT("Auto-spawned breakable grid actor: %s"), *SpawnedGrid->GetName());
			}
		}
	}

	// --- Kill Z: falling through broken tiles = death ---
	if (AWorldSettings* WS = GetWorld()->GetWorldSettings())
	{
		WS->KillZ = -500.0f;
	}

	// --- Arena walls & obstacles ---
	SpawnArenaWalls();
	SpawnArenaObstacles();
}

void Ahe_grenade_gameGameMode::SpawnArenaWalls()
{
	// Grid spans -1000..+1000 in XY, tile top at Z=11.
	// Spawn 4 invisible collision walls around the perimeter.
	// Cube mesh is 100cm; scale to wall dimensions.
	struct FWallDef
	{
		FVector Location;
		FVector Scale;
	};

	const float WallZ = 261.0f; // center of 500cm wall starting at tile top (Z=11)
	const FWallDef Walls[] =
	{
		{ FVector(0.0f,  1005.0f, WallZ), FVector(20.2f, 0.1f, 5.0f) }, // +Y
		{ FVector(0.0f, -1005.0f, WallZ), FVector(20.2f, 0.1f, 5.0f) }, // -Y
		{ FVector( 1005.0f, 0.0f, WallZ), FVector(0.1f, 20.2f, 5.0f) }, // +X
		{ FVector(-1005.0f, 0.0f, WallZ), FVector(0.1f, 20.2f, 5.0f) }, // -X
	};

	for (const FWallDef& W : Walls)
	{
		const FTransform SpawnTransform(FRotator::ZeroRotator, W.Location, W.Scale);
		if (AArenaObstacle* Wall = GetWorld()->SpawnActor<AArenaObstacle>(AArenaObstacle::StaticClass(), SpawnTransform))
		{
			Wall->SetActorHiddenInGame(true);
		}
	}
}

void Ahe_grenade_gameGameMode::SpawnArenaObstacles()
{
	// Tile top surface at Z=11. Cube mesh is 100cm centered.
	// For an obstacle with Z-scale S, bottom sits at spawn_Z - 50*S,
	// so spawn_Z = 11 + 50*S to place bottom on tile surface.
	struct FObstacleDef
	{
		FVector Location;
		FRotator Rotation;
		FVector Scale;
	};

	const FObstacleDef Obstacles[] =
	{
		// Large cover block
		{ FVector(400.0f, 400.0f, 11.0f + 75.0f),   FRotator::ZeroRotator, FVector(1.5f, 1.5f, 1.5f) },
		// Low wide cover
		{ FVector(-500.0f, 200.0f, 11.0f + 40.0f),   FRotator::ZeroRotator, FVector(2.0f, 1.0f, 0.8f) },
		// Tall pillar
		{ FVector(0.0f, -500.0f, 11.0f + 150.0f),    FRotator::ZeroRotator, FVector(0.7f, 0.7f, 3.0f) },
		// Ramp
		{ FVector(-300.0f, -400.0f, 11.0f + 50.0f),  FRotator(20.0f, 0.0f, 0.0f), FVector(2.5f, 1.5f, 0.15f) },
		// Small step platform
		{ FVector(600.0f, -300.0f, 11.0f + 25.0f),   FRotator::ZeroRotator, FVector(1.5f, 1.5f, 0.5f) },
		// Medium cover block
		{ FVector(-700.0f, 500.0f, 11.0f + 100.0f),  FRotator::ZeroRotator, FVector(1.0f, 2.0f, 2.0f) },
	};

	for (const FObstacleDef& O : Obstacles)
	{
		const FTransform SpawnTransform(O.Rotation, O.Location, O.Scale);
		GetWorld()->SpawnActor<AArenaObstacle>(AArenaObstacle::StaticClass(), SpawnTransform);
	}
}
