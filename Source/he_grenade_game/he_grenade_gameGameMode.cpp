// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameGameMode.h"

#include "EngineUtils.h"
#include "Engine/World.h"
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

	// Ensure breakable grid stays enabled even if an older BP default overrides this bool.
	if (!bSpawnBreakableGridOnBeginPlay)
	{
		bSpawnBreakableGridOnBeginPlay = true;
	}

	for (TActorIterator<ABreakableTileGrid> It(GetWorld()); It; ++It)
	{
		if (IsValid(*It))
		{
			// A grid is already present in this level; do not spawn a duplicate.
			UE_LOG(Loghe_grenade_game, Log, TEXT("Breakable grid already exists; skipping auto-spawn."));
			return;
		}
	}

	UClass* GridClass = BreakableTileGridClass ? BreakableTileGridClass.Get() : ABreakableTileGrid::StaticClass();
	if (!GridClass)
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Breakable grid class is null; cannot auto-spawn."));
		return;
	}

	if (ABreakableTileGrid* SpawnedGrid = GetWorld()->SpawnActor<ABreakableTileGrid>(GridClass, BreakableTileGridTransform))
	{
		UE_LOG(Loghe_grenade_game, Log, TEXT("Auto-spawned breakable grid actor: %s"), *SpawnedGrid->GetName());
	}
	else
	{
		UE_LOG(Loghe_grenade_game, Warning, TEXT("Failed to auto-spawn breakable grid actor."));
	}
}
