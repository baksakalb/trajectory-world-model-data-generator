#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreakableTileGrid.generated.h"

class ABreakableTile;

UCLASS(BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API ABreakableTileGrid : public AActor
{
	GENERATED_BODY()

public:
	ABreakableTileGrid();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	bool bSpawnOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 TilesX = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	int32 TilesY = 10;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (Units = "cm", ClampMin = "1.0"))
	float TileSizeCm = 250.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (ClampMin = "0.01"))
	float TileThicknessScale = 0.15f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid", meta = (Units = "cm"))
	float TileSpacingCm = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	FVector GridLocalOriginOffset = FVector(-1125.0f, -1125.0f, 0.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grid")
	TSubclassOf<ABreakableTile> TileClass;

	UFUNCTION(BlueprintCallable, Category = "Grid")
	void BuildGrid();

	UFUNCTION(BlueprintCallable, Category = "Grid")
	void ClearGrid();

	UFUNCTION(BlueprintPure, Category = "Grid")
	ABreakableTile* GetTileAtIndex(int32 X, int32 Y) const;

	const TArray<TObjectPtr<ABreakableTile>>& GetSpawnedTiles() const { return SpawnedTiles; }

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Grid")
	TArray<TObjectPtr<ABreakableTile>> SpawnedTiles;
};
