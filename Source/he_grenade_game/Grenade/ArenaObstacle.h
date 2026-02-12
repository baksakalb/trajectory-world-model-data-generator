#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "ArenaObstacle.generated.h"

class UStaticMeshComponent;

UCLASS()
class HE_GRENADE_GAME_API AArenaObstacle : public AActor
{
	GENERATED_BODY()

public:
	AArenaObstacle();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Obstacle")
	TObjectPtr<UStaticMeshComponent> ObstacleMesh;
};
