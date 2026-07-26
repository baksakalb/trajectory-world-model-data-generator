// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UObject/SoftObjectPath.h"
#include "he_grenade_gameGameMode.generated.h"

class AController;
class ABreakableTileGrid;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FArenaShapeRule
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FSoftObjectPath MeshPath;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "0.01"))
	float Weight = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1"))
	int32 FootprintX = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1"))
	int32 FootprintY = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape", meta = (ClampMin = "1"))
	int32 FootprintZ = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	bool bAllowHorizontal = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	bool bAllowVertical = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector ScaleMin = FVector(0.85f, 0.85f, 0.85f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Shape")
	FVector ScaleMax = FVector(1.20f, 1.20f, 1.20f);
};

/**
 * Base game mode used by first-person gameplay variant.
 */
UCLASS(abstract)
class Ahe_grenade_gameGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	Ahe_grenade_gameGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena")
	bool bSpawnBreakableGridOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena")
	TSubclassOf<ABreakableTileGrid> BreakableTileGridClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena")
	FTransform BreakableTileGridTransform;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Random", meta = (ClampMin = "-1"))
	int32 ArenaSeed = -1;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Arena|Random")
	int32 LastGeneratedArenaSeed = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "8"))
	int32 MinTilesX = 14;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "8"))
	int32 MaxTilesX = 22;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "8"))
	int32 MinTilesY = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "8"))
	int32 MaxTilesY = 16;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "100.0", Units = "cm"))
	float MinTileSizeCm = 180.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "100.0", Units = "cm"))
	float MaxTileSizeCm = 260.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "0.0", Units = "cm"))
	float TileSpacingCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "0.01"))
	float TileThicknessScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "2"))
	int32 GridHeightLayers = 5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Layout", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float TargetNonFloorOccupancy = 0.28f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Labyrinth", meta = (ClampMin = "0.05", ClampMax = "0.80"))
	float LabyrinthPanelThicknessRatio = 0.08f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Labyrinth", meta = (ClampMin = "0.25"))
	float LabyrinthMinPanelHeightCells = 2.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Labyrinth", meta = (ClampMin = "0.25"))
	float LabyrinthMaxPanelHeightCells = 3.4f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Labyrinth", meta = (ClampMin = "1"))
	int32 LabyrinthMaxPanelRunLength = 7;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Labyrinth", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LabyrinthConnectorChance = 0.42f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Learning Shapes")
	bool bSpawnLearningShapeObstacles = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Learning Shapes", meta = (ClampMin = "0.25", ClampMax = "0.95"))
	float LearningShapeSizeCells = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Spawning", meta = (ClampMin = "0"))
	int32 SpawnPadClearanceX = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Spawning", meta = (ClampMin = "0"))
	int32 SpawnPadClearanceY = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Spawning", meta = (ClampMin = "0"))
	int32 SpawnPadClearanceZ = 2;

	/** Every visual slot is selected in BP_FirstPersonGameMode Class Defaults.
	 *  Colors, opacity, and emission are edited on the referenced material instances. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> FloorTileMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> LabyrinthPanelMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> TrajectoryHighlightMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> RectangleShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> TriangleShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> SphereShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> HoopShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> ArenaWallMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> VoidBackdropMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Materials")
	TObjectPtr<UMaterialInterface> GrenadeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Visual")
	bool bSpawnVoidBackdrop = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Visual", meta = (ClampMin = "500.0", Units = "cm"))
	float VoidBackdropDepthCm = 2000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Shapes|Deprecated", meta = (DeprecatedProperty, DeprecationMessage = "Labyrinth generator uses vertical rectangular panels instead of random shape rules."))
	TArray<FArenaShapeRule> ShapeRules;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Floor|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator no longer carves mirrored floor holes."))
	int32 MinMirroredHolePairs = 12;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Floor|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator no longer carves mirrored floor holes."))
	int32 MaxMirroredHolePairs = 42;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|BreakablePlatforms|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator uses shape rules instead of legacy breakable platform pairs."))
	int32 MinBreakablePlatformPairs = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|BreakablePlatforms|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator uses shape rules instead of legacy breakable platform pairs."))
	int32 MaxBreakablePlatformPairs = 4;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|BreakablePlatforms|Deprecated", meta = (ClampMin = "0.0", Units = "cm", DeprecatedProperty, DeprecationMessage = "V2 generator uses 3D tile layers instead of legacy platform offsets."))
	float MinBreakablePlatformBottomOffsetCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|BreakablePlatforms|Deprecated", meta = (ClampMin = "0.0", Units = "cm", DeprecatedProperty, DeprecationMessage = "V2 generator uses 3D tile layers instead of legacy platform offsets."))
	float MaxBreakablePlatformBottomOffsetCm = 85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|NonBreakable|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator no longer spawns legacy non-breakable panel pairs."))
	int32 MinBouncePanelPairs = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|NonBreakable|Deprecated", meta = (ClampMin = "0", DeprecatedProperty, DeprecationMessage = "V2 generator no longer spawns legacy non-breakable panel pairs."))
	int32 MaxBouncePanelPairs = 6;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|NonBreakable|Deprecated", meta = (ClampMin = "0.0", Units = "cm", DeprecatedProperty, DeprecationMessage = "V2 generator no longer spawns legacy non-breakable panel pairs."))
	float NonBreakableMinBottomOffsetCm = 280.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Walls")
	bool bSpawnArenaWalls = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Walls", meta = (ClampMin = "100.0", Units = "cm"))
	float ArenaWallHeightCm = 550.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Walls", meta = (ClampMin = "5.0", Units = "cm"))
	float ArenaWallThicknessCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Spawning", meta = (ClampMin = "0.10", ClampMax = "0.95"))
	float SpawnSideOffsetRatio = 0.72f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Spawning", meta = (ClampMin = "0.0", Units = "cm"))
	float SpawnHeightOffsetCm = 96.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Arena|Rules", meta = (ClampMin = "200.0", Units = "cm"))
	float KillZDropCm = 700.0f;

protected:
	virtual void BeginPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	void GenerateProceduralArena();
	void ClearExistingArenaActors() const;
	void SpawnArenaWalls(const FVector& ArenaCenter, float HalfExtentX, float HalfExtentY, float TileTopSurfaceZ);
	void SpawnVoidBackdrop(const FVector& ArenaCenter, float HalfExtentX, float HalfExtentY, float TileTopSurfaceZ);
	int32 SpawnMirroredLabyrinthPanels(
		ABreakableTileGrid* Grid,
		UClass* TileClass,
		float CellSizeCm,
		float CellPitchCm,
		float TileTopSurfaceZ,
		int32 TilesX,
		int32 TilesY,
		FRandomStream& RandomStream);
	int32 SpawnSymmetricLearningShapes(
		ABreakableTileGrid* Grid,
		UClass* TileClass,
		float CellSizeCm,
		float CellPitchCm,
		float TileTopSurfaceZ,
		int32 TilesX,
		int32 TilesY,
		FRandomStream& RandomStream);
	void CacheSpawnTransforms(const FVector& ArenaCenter, float HalfExtentX, float TileTopSurfaceZ);
	int32 GetOrAssignSpawnSide(AController* Controller);

	UPROPERTY(Transient)
	TObjectPtr<ABreakableTileGrid> ActiveBreakableGrid;

	TMap<TWeakObjectPtr<AController>, int32> SpawnSideByController;
	FTransform GeneratedSpawnTransforms[2];
	bool bHasGeneratedSpawnTransforms = false;
	bool bArenaGeneratedThisMatch = false;
	int32 NextSpawnSide = 0;
};
