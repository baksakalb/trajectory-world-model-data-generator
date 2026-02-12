#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreakableTile.generated.h"

class UStaticMeshComponent;
class UMaterialInstanceDynamic;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBreakableTileBroken, ABreakableTile*, Tile);

UCLASS(BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API ABreakableTile : public AActor
{
	GENERATED_BODY()

public:
	ABreakableTile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	bool bStartBroken = false;

	UPROPERTY(BlueprintAssignable, Category = "Tile")
	FOnBreakableTileBroken OnTileBroken;

	UFUNCTION(BlueprintCallable, Category = "Tile")
	void BreakTile();

	UFUNCTION(BlueprintCallable, Category = "Tile")
	void ResetTile();

	UFUNCTION(BlueprintPure, Category = "Tile")
	bool IsBroken() const { return bBroken; }

	UFUNCTION(BlueprintCallable, Category = "Tile|Trajectory")
	void SetTrajectoryHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintPure, Category = "Tile|Trajectory")
	bool IsTrajectoryHighlighted() const { return bTrajectoryHighlighted; }

protected:
	virtual void BeginPlay() override;

private:
	void InitializeTileMaterial();
	void ApplyTrajectoryHighlightState();

	UPROPERTY(EditAnywhere, Category = "Tile|Trajectory")
	FLinearColor TrajectoryHighlightColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

	UPROPERTY(EditAnywhere, Category = "Tile|Trajectory", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TrajectoryHighlightOpacity = 0.35f;

	UPROPERTY(VisibleAnywhere, Category = "Tile")
	bool bBroken = false;

	UPROPERTY(VisibleAnywhere, Category = "Tile|Trajectory")
	bool bTrajectoryHighlighted = false;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> TileMaterialMID;

	FLinearColor DefaultBaseColor = FLinearColor(0.0f, 0.75f, 1.0f, 1.0f);
	float DefaultOpacity = 0.35f;
};
