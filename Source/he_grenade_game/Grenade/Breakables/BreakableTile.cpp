#include "Grenade/Breakables/BreakableTile.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	const FName BaseColorParameterName(TEXT("BaseColor"));
	const FName OpacityParameterName(TEXT("Opacity"));
}

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
	InitializeTileMaterial();

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

	SetTrajectoryHighlighted(false);

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
	bTrajectoryHighlighted = false;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	if (TileMesh)
	{
		TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TileMesh->SetVisibility(true, true);
	}

	ApplyTrajectoryHighlightState();
}

void ABreakableTile::SetTrajectoryHighlighted(bool bHighlighted)
{
	const bool bShouldHighlight = bHighlighted && !bBroken;
	if (bTrajectoryHighlighted == bShouldHighlight)
	{
		return;
	}

	bTrajectoryHighlighted = bShouldHighlight;
	ApplyTrajectoryHighlightState();
}

void ABreakableTile::InitializeTileMaterial()
{
	if (!TileMesh)
	{
		return;
	}

	UMaterialInterface* CurrentMaterial = TileMesh->GetMaterial(0);
	if (!CurrentMaterial)
	{
		return;
	}

	TileMaterialMID = UMaterialInstanceDynamic::Create(CurrentMaterial, this);
	if (!TileMaterialMID)
	{
		return;
	}

	TileMesh->SetMaterial(0, TileMaterialMID);

	const FLinearColor MaterialBaseColor = TileMaterialMID->K2_GetVectorParameterValue(BaseColorParameterName);
	if (MaterialBaseColor.R > KINDA_SMALL_NUMBER
		|| MaterialBaseColor.G > KINDA_SMALL_NUMBER
		|| MaterialBaseColor.B > KINDA_SMALL_NUMBER
		|| MaterialBaseColor.A > KINDA_SMALL_NUMBER)
	{
		DefaultBaseColor = MaterialBaseColor;
	}

	const float MaterialOpacity = TileMaterialMID->K2_GetScalarParameterValue(OpacityParameterName);
	if (MaterialOpacity > KINDA_SMALL_NUMBER)
	{
		DefaultOpacity = MaterialOpacity;
	}

	ApplyTrajectoryHighlightState();
}

void ABreakableTile::ApplyTrajectoryHighlightState()
{
	if (!TileMaterialMID)
	{
		return;
	}

	const FLinearColor BaseColor = bTrajectoryHighlighted ? TrajectoryHighlightColor : DefaultBaseColor;
	const float Opacity = bTrajectoryHighlighted ? TrajectoryHighlightOpacity : DefaultOpacity;

	TileMaterialMID->SetVectorParameterValue(BaseColorParameterName, BaseColor);
	TileMaterialMID->SetScalarParameterValue(OpacityParameterName, FMath::Clamp(Opacity, 0.0f, 1.0f));
}
