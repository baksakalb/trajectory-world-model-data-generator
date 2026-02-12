#include "Grenade/Breakables/BreakableTileGrid.h"

#include "Grenade/Breakables/BreakableTile.h"
#include "Engine/World.h"

ABreakableTileGrid::ABreakableTileGrid()
{
	PrimaryActorTick.bCanEverTick = false;

	// Center a 10x10 grid (200cm tiles) around the actor origin.
	GridLocalOriginOffset = FVector(-900.0f, -900.0f, 0.0f);
}

void ABreakableTileGrid::BeginPlay()
{
	Super::BeginPlay();

	if (bSpawnOnBeginPlay)
	{
		BuildGrid();
	}
}

void ABreakableTileGrid::BuildGrid()
{
	ClearGrid();

	if (!GetWorld())
	{
		return;
	}

	UClass* SpawnClass = TileClass ? TileClass.Get() : ABreakableTile::StaticClass();
	if (!SpawnClass)
	{
		return;
	}

	const FVector StartLocation = GetActorLocation() + GetActorTransform().TransformVectorNoScale(GridLocalOriginOffset);
	const FVector XAxis = GetActorForwardVector();
	const FVector YAxis = GetActorRightVector();

	const int32 ClampedX = FMath::Max(1, TilesX);
	const int32 ClampedY = FMath::Max(1, TilesY);
	const float Pitch = TileSizeCm + TileSpacingCm;

	SpawnedTiles.Reserve(ClampedX * ClampedY);

	for (int32 Y = 0; Y < ClampedY; ++Y)
	{
		for (int32 X = 0; X < ClampedX; ++X)
		{
			const FVector SpawnLocation = StartLocation + (XAxis * (Pitch * X)) + (YAxis * (Pitch * Y));
			const FVector TileScale(
				FMath::Max(0.01f, TileSizeCm / 100.0f),
				FMath::Max(0.01f, TileSizeCm / 100.0f),
				0.10f);
			const FTransform SpawnTransform(GetActorRotation(), SpawnLocation, TileScale);

			FActorSpawnParameters SpawnParams;
			SpawnParams.Owner = this;
			SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			SpawnParams.ObjectFlags = RF_Transactional;

			ABreakableTile* SpawnedTile = GetWorld()->SpawnActor<ABreakableTile>(SpawnClass, SpawnTransform, SpawnParams);
			if (SpawnedTile)
			{
				SpawnedTiles.Add(SpawnedTile);
			}
		}
	}
}

void ABreakableTileGrid::ClearGrid()
{
	for (ABreakableTile* Tile : SpawnedTiles)
	{
		if (IsValid(Tile))
		{
			Tile->Destroy();
		}
	}
	SpawnedTiles.Reset();
}

ABreakableTile* ABreakableTileGrid::GetTileAtIndex(int32 X, int32 Y) const
{
	if (X < 0 || Y < 0 || X >= TilesX || Y >= TilesY)
	{
		return nullptr;
	}

	const int32 Index = (Y * TilesX) + X;
	return SpawnedTiles.IsValidIndex(Index) ? SpawnedTiles[Index] : nullptr;
}
