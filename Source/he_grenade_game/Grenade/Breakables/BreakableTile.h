#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreakableTile.generated.h"

class UStaticMeshComponent;

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

protected:
	virtual void BeginPlay() override;

private:
	UPROPERTY(VisibleAnywhere, Category = "Tile")
	bool bBroken = false;
};
