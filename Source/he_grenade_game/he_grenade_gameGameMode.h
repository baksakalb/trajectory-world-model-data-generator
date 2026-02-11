// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "he_grenade_gameGameMode.generated.h"

class ABreakableTileGrid;

/**
 * Base game mode used by first-person gameplay variant.
 */
UCLASS(abstract)
class Ahe_grenade_gameGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	Ahe_grenade_gameGameMode();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Tiles")
	bool bSpawnBreakableGridOnBeginPlay = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Tiles")
	TSubclassOf<ABreakableTileGrid> BreakableTileGridClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Tiles")
	FTransform BreakableTileGridTransform;

protected:
	virtual void BeginPlay() override;
};
