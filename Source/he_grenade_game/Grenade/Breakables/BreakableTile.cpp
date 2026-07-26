#include "Grenade/Breakables/BreakableTile.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
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

	Tags.AddUnique(TEXT("BreakableTile"));
}

void ABreakableTile::BeginPlay()
{
	Super::BeginPlay();
	ApplyTrajectoryHighlightState();

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

void ABreakableTile::SetMeshAndTransformStyle(UStaticMesh* InStaticMesh, FRotator InMeshRelativeRotation, FVector InMeshRelativeScale)
{
	if (!TileMesh)
	{
		return;
	}

	if (InStaticMesh)
	{
		TileMesh->SetStaticMesh(InStaticMesh);
	}

	if (BaseTileMaterial)
	{
		ApplyTrajectoryHighlightState();
	}

	const FVector SafeRelativeScale(
		FMath::Max(0.01f, InMeshRelativeScale.X),
		FMath::Max(0.01f, InMeshRelativeScale.Y),
		FMath::Max(0.01f, InMeshRelativeScale.Z));

	TileMesh->SetRelativeRotation(InMeshRelativeRotation);
	TileMesh->SetRelativeScale3D(SafeRelativeScale);

	ApplyTrajectoryHighlightState();
}

void ABreakableTile::SetVisualMaterials(
	UMaterialInterface* InNormalMaterial,
	UMaterialInterface* InTrajectoryHighlightMaterial)
{
	BaseTileMaterial = InNormalMaterial;
	TrajectoryHighlightMaterial = InTrajectoryHighlightMaterial;
	ApplyTrajectoryHighlightState();
}

void ABreakableTile::BeginCompositeShape()
{
	for (UStaticMeshComponent* ShapeMesh : CompositeShapeMeshes)
	{
		if (IsValid(ShapeMesh))
		{
			ShapeMesh->DestroyComponent();
		}
	}
	CompositeShapeMeshes.Reset();

	if (TileMesh)
	{
		TileMesh->SetMobility(EComponentMobility::Movable);
		TileMesh->SetStaticMesh(nullptr);
		TileMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	}

}

UStaticMeshComponent* ABreakableTile::AddCompositeShapePiece(
	UStaticMesh* InStaticMesh,
	const FVector& RelativeLocation,
	const FRotator& RelativeRotation,
	const FVector& RelativeScale)
{
	if (!TileMesh || !InStaticMesh)
	{
		return nullptr;
	}

	UStaticMeshComponent* ShapeMesh = NewObject<UStaticMeshComponent>(this);
	if (!ShapeMesh)
	{
		return nullptr;
	}

	AddInstanceComponent(ShapeMesh);
	ShapeMesh->SetupAttachment(TileMesh);
	ShapeMesh->SetMobility(EComponentMobility::Movable);
	ShapeMesh->SetStaticMesh(InStaticMesh);
	ShapeMesh->SetRelativeLocation(RelativeLocation);
	ShapeMesh->SetRelativeRotation(RelativeRotation);
	ShapeMesh->SetRelativeScale3D(FVector(
		FMath::Max(0.01f, RelativeScale.X),
		FMath::Max(0.01f, RelativeScale.Y),
		FMath::Max(0.01f, RelativeScale.Z)));
	ShapeMesh->SetCollisionProfileName(TEXT("BlockAll"));
	ShapeMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	ShapeMesh->SetCollisionResponseToAllChannels(ECR_Block);
	ShapeMesh->SetGenerateOverlapEvents(false);
	ShapeMesh->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_No;
	ShapeMesh->RegisterComponent();

	CompositeShapeMeshes.Add(ShapeMesh);
	ShapeMesh->SetMaterial(0, GetActiveVisualMaterial());
	return ShapeMesh;
}

UMaterialInterface* ABreakableTile::GetActiveVisualMaterial() const
{
	return bTrajectoryHighlighted && TrajectoryHighlightMaterial
		? TrajectoryHighlightMaterial.Get()
		: BaseTileMaterial.Get();
}

void ABreakableTile::ApplyTrajectoryHighlightState()
{
	UMaterialInterface* ActiveMaterial = GetActiveVisualMaterial();

	if (TileMesh && TileMesh->GetStaticMesh())
	{
		TileMesh->SetMaterial(0, ActiveMaterial);
	}

	for (UStaticMeshComponent* ShapeMesh : CompositeShapeMeshes)
	{
		if (IsValid(ShapeMesh))
		{
			ShapeMesh->SetMaterial(0, ActiveMaterial);
		}
	}
}
