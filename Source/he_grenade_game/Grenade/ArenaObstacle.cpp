#include "Grenade/ArenaObstacle.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "UObject/ConstructorHelpers.h"

AArenaObstacle::AArenaObstacle()
{
	PrimaryActorTick.bCanEverTick = false;

	ObstacleMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("ObstacleMesh"));
	SetRootComponent(ObstacleMesh);

	ObstacleMesh->SetMobility(EComponentMobility::Static);
	ObstacleMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ObstacleMesh->SetGenerateOverlapEvents(false);
	ObstacleMesh->SetVisibility(true, true);
	SetActorHiddenInGame(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> CubeMesh(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (CubeMesh.Succeeded())
	{
		ObstacleMesh->SetStaticMesh(CubeMesh.Object);
	}
}
