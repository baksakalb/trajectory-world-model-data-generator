#include "Grenade/Breakables/BreakableTile.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
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

	SetDestructionWarningAlpha(0.0f);
	SetTrajectoryHighlighted(false);

	// CharacterMovement caches the component it is standing on. In network play,
	// an autonomous proxy can otherwise remain based on this now-non-colliding
	// tile until a later server correction, which looks like levitation.
	if (UWorld* World = GetWorld())
	{
		for (TActorIterator<ACharacter> It(World); It; ++It)
		{
			ACharacter* Character = *It;
			UObject* MovementBaseObject = Character ? Character->GetMovementBaseObject() : nullptr;
			UPrimitiveComponent* MovementBase = Cast<UPrimitiveComponent>(MovementBaseObject);
			if (MovementBase && MovementBase->GetOwner() == this)
			{
				Character->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
				if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
				{
					Movement->SetMovementMode(MOVE_Falling);
				}
			}
		}
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
	bTrajectoryHighlighted = false;
	DestructionWarningAlpha = 0.0f;
	SetActorHiddenInGame(false);
	SetActorEnableCollision(true);

	if (TileMesh)
	{
		TileMesh->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		TileMesh->SetVisibility(true, true);
	}

	ApplyTrajectoryHighlightState();
}

void ABreakableTile::SetDestructionWarningAlpha(const float WarningAlpha)
{
	const float SafeAlpha = bBroken ? 0.0f : FMath::Clamp(WarningAlpha, 0.0f, 1.0f);
	if (FMath::IsNearlyEqual(DestructionWarningAlpha, SafeAlpha, 0.001f))
	{
		return;
	}

	DestructionWarningAlpha = SafeAlpha;
	UpdateDestructionWarningMaterial();
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
	DestructionWarningMaterial = nullptr;
	DestructionWarningAlpha = 0.0f;
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
	if (bTrajectoryHighlighted && TrajectoryHighlightMaterial)
	{
		return TrajectoryHighlightMaterial.Get();
	}

	if (DestructionWarningAlpha > KINDA_SMALL_NUMBER && DestructionWarningMaterial)
	{
		return DestructionWarningMaterial.Get();
	}

	return BaseTileMaterial.Get();
}

void ABreakableTile::UpdateDestructionWarningMaterial()
{
	if (DestructionWarningAlpha <= KINDA_SMALL_NUMBER
		|| !BaseTileMaterial
		|| !TrajectoryHighlightMaterial)
	{
		return;
	}

	if (!DestructionWarningMaterial)
	{
		DestructionWarningMaterial = UMaterialInstanceDynamic::Create(BaseTileMaterial, this);
	}
	if (!DestructionWarningMaterial)
	{
		return;
	}

	static const FName VectorParameterNames[] =
	{
		TEXT("BaseColor"),
		TEXT("EmissiveColor")
	};
	for (const FName ParameterName : VectorParameterNames)
	{
		FLinearColor BaseValue;
		FLinearColor WarningValue;
		const FMaterialParameterInfo ParameterInfo(ParameterName);
		if (BaseTileMaterial->GetVectorParameterValue(ParameterInfo, BaseValue)
			&& TrajectoryHighlightMaterial->GetVectorParameterValue(ParameterInfo, WarningValue))
		{
			DestructionWarningMaterial->SetVectorParameterValue(
				ParameterName,
				FMath::Lerp(BaseValue, WarningValue, DestructionWarningAlpha));
		}
	}

	static const FName ScalarParameterNames[] =
	{
		TEXT("Metallic"),
		TEXT("Roughness"),
		TEXT("Opacity"),
		TEXT("EmissiveStrength")
	};
	for (const FName ParameterName : ScalarParameterNames)
	{
		float BaseValue = 0.0f;
		float WarningValue = 0.0f;
		const FMaterialParameterInfo ParameterInfo(ParameterName);
		if (BaseTileMaterial->GetScalarParameterValue(ParameterInfo, BaseValue)
			&& TrajectoryHighlightMaterial->GetScalarParameterValue(ParameterInfo, WarningValue))
		{
			DestructionWarningMaterial->SetScalarParameterValue(
				ParameterName,
				FMath::Lerp(BaseValue, WarningValue, DestructionWarningAlpha));
		}
	}
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
