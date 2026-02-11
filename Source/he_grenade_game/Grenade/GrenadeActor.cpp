#include "Grenade/GrenadeActor.h"

#include "DrawDebugHelpers.h"
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/World.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/Pawn.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarGGGrenadeDebugPath(
		TEXT("gg.Grenade.DebugPath"),
		0,
		TEXT("Draw runtime grenade path segments. 0=off, 1=on"),
		ECVF_Default);
}

AGrenadeActor::AGrenadeActor()
{
	PrimaryActorTick.bCanEverTick = true;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	SetRootComponent(CollisionComponent);
	CollisionComponent->SetSphereRadius(8.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionComponent);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshAsset.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMeshAsset.Object);
		VisualMesh->SetRelativeScale3D(FVector(0.16f));
	}
}

void AGrenadeActor::BeginPlay()
{
	Super::BeginPlay();

	if (!bInitialized)
	{
		FGrenadeSim::InitializeState(SimState, GetActorLocation(), GetActorForwardVector() * 1400.0f, 2.5f);
		bInitialized = true;
	}
}

void AGrenadeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (!bInitialized || bExploded)
	{
		return;
	}

	FixedStepAccumulator += FMath::Max(0.0f, DeltaSeconds);
	const float FixedStep = FMath::Max(0.001f, SimulationConfig.FixedStepSeconds);

	while (FixedStepAccumulator >= FixedStep && !bExploded)
	{
		SimulateFixedStep(FixedStep);
		FixedStepAccumulator -= FixedStep;
	}
}

void AGrenadeActor::InitializeGrenade(const FVector& StartPosition, const FVector& InitialVelocity, float FuseSeconds, const FGrenadeSimConfig& InSimulationConfig, AActor* InOwnerActor)
{
	SimulationConfig = InSimulationConfig;
	CollisionComponent->SetSphereRadius(SimulationConfig.RadiusCm);

	SetActorLocation(StartPosition);
	OwningActor = InOwnerActor;

	FGrenadeSim::InitializeState(SimState, StartPosition, InitialVelocity, FuseSeconds);
	FixedStepAccumulator = 0.0f;
	bExploded = false;
	bInitialized = true;
}

void AGrenadeActor::ForceExplode()
{
	if (!bExploded)
	{
		ExplodeNow();
	}
}

void AGrenadeActor::ApplyInstantKillBlast(UWorld* World, const FVector& Origin, float RadiusCm, AActor* DamageCauser, AActor* InstigatorActor)
{
	if (!World || RadiusCm <= 0.0f)
	{
		return;
	}

	TArray<FOverlapResult> Overlaps;
	FCollisionObjectQueryParams ObjectQuery;
	ObjectQuery.AddObjectTypesToQuery(ECC_Pawn);

	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeInstantKillBlast), false);
	if (DamageCauser)
	{
		QueryParams.AddIgnoredActor(DamageCauser);
	}

	const bool bAnyOverlap = World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeSphere(RadiusCm),
		QueryParams);

	if (!bAnyOverlap)
	{
		return;
	}

	TSet<TWeakObjectPtr<APawn>> UniquePawns;
	for (const FOverlapResult& Overlap : Overlaps)
	{
		if (APawn* Pawn = Cast<APawn>(Overlap.GetActor()))
		{
			UniquePawns.Add(Pawn);
		}
	}

	for (const TWeakObjectPtr<APawn>& PawnPtr : UniquePawns)
	{
		APawn* Pawn = PawnPtr.Get();
		if (!Pawn)
		{
			continue;
		}

		AController* Controller = Pawn->GetController();
		Pawn->Destroy();

		if (Controller)
		{
			if (AGameModeBase* GameMode = World->GetAuthGameMode())
			{
				GameMode->RestartPlayer(Controller);
			}
		}
	}
}

ABreakableTile* AGrenadeActor::ResolveBreakableTile(const FHitResult& Hit) const
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor && Hit.GetComponent())
	{
		HitActor = Hit.GetComponent()->GetOwner();
	}

	ABreakableTile* Tile = Cast<ABreakableTile>(HitActor);
	if (!Tile || Tile->IsBroken())
	{
		return nullptr;
	}

	return Tile;
}

void AGrenadeActor::SimulateFixedStep(float StepSeconds)
{
	const FVector PreviousLocation = SimState.Position;

	const FGrenadeSimStepResult StepResult = FGrenadeSim::Step(
		GetWorld(),
		SimulationConfig,
		SimState,
		StepSeconds,
		OwningActor.Get(),
		[this](const FHitResult& Hit) { return ResolveBreakableTile(Hit); });

	SetActorLocation(SimState.Position);

	if (StepResult.bBrokeTile)
	{
		if (ABreakableTile* Tile = StepResult.BrokenTile.Get())
		{
			HandleBrokenTile(Tile);
		}
	}

	if (bDrawDebugPath || CVarGGGrenadeDebugPath.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), PreviousLocation, SimState.Position, FColor::Yellow, false, 2.0f, 0, 1.5f);
	}

	if (SimState.bExploded || StepResult.bExplodedThisStep)
	{
		ExplodeNow();
	}
}

void AGrenadeActor::ExplodeNow()
{
	if (bExploded)
	{
		return;
	}

	bExploded = true;
	ApplyInstantKillBlast(GetWorld(), GetActorLocation(), ExplosionRadiusCm, this, OwningActor.Get());
	Destroy();
}

void AGrenadeActor::HandleBrokenTile(ABreakableTile* Tile)
{
	if (Tile)
	{
		Tile->BreakTile();
	}
}
