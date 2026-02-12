#include "Grenade/Breakables/BreakableTile.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "UObject/ConstructorHelpers.h"

ABreakableTile::ABreakableTile()
{
	PrimaryActorTick.bCanEverTick = false;

	TileMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("TileMesh"));
	SetRootComponent(TileMesh);

	TileMesh->SetMobility(EComponentMobility::Static);
	TileMesh->SetCollisionProfileName(TEXT("BlockAll"));
	TileMesh->SetGenerateOverlapEvents(false);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> TileMeshAsset(TEXT("/Engine/BasicShapes/Cube.Cube"));
	if (TileMeshAsset.Succeeded())
	{
		TileMesh->SetStaticMesh(TileMeshAsset.Object);
	}

	static ConstructorHelpers::FObjectFinder<UMaterialInterface> GlassMat(TEXT("/Game/Materials/M_GlassTile.M_GlassTile"));
	if (GlassMat.Succeeded())
	{
		TileMesh->SetMaterial(0, GlassMat.Object);
	}

	Tags.AddUnique(TEXT("BreakableTile"));
}

void ABreakableTile::BeginPlay()
{
	Super::BeginPlay();

	if (bStartBroken)
	{
		BreakTile();
	}
}

void ABreakableTile::BreakTile()
{
	if (bBroken)
	{
		return;
	}

	bBroken = true;
	SetActorHiddenInGame(true);
	SetActorEnableCollision(false);

	if (TileMesh)
	{
		TileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		TileMesh->SetVisibility(false, true);
	}

	OnTileBroken.Broadcast(this);
}

void ABreakableTile::ResetTile()
{
	bBroken = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	if (TileMesh)
	{
		TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TileMesh->SetVisibility(true, true);
	}
}
