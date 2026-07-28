#include "Grenade/GrenadeActor.h"

#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/ProjectileMovementComponent.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "GrenadeGameState.h"
#include "he_grenade_gameGameMode.h"
#include "Net/UnrealNetwork.h"
#include "UObject/ConstructorHelpers.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarGGGrenadeDebugPath(
		TEXT("gg.Grenade.DebugPath"),
		0,
		TEXT("Draw runtime grenade path segments. 0=off, 1=on"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarGGGrenadeActorThrowLockDebug(
		TEXT("gg.Grenade.DebugActorThrowLock"),
		0,
		TEXT("Logs grenade initialization and reconciliation. 0=off, 1=on"),
		ECVF_Default);
}

AGrenadeActor::AGrenadeActor()
{
	PrimaryActorTick.bCanEverTick = true;
	bReplicates = true;
	SetReplicateMovement(true);
	SetNetUpdateFrequency(20.0f);
	SetMinNetUpdateFrequency(10.0f);
	NetPriority = 1.4f;

	CollisionComponent = CreateDefaultSubobject<USphereComponent>(TEXT("CollisionComponent"));
	SetRootComponent(CollisionComponent);
	CollisionComponent->SetSphereRadius(8.0f);
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	CollisionComponent->SetCollisionResponseToAllChannels(ECR_Ignore);

	VisualMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("VisualMesh"));
	VisualMesh->SetupAttachment(CollisionComponent);
	VisualMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	NetworkInterpolation = CreateDefaultSubobject<UProjectileMovementComponent>(TEXT("NetworkInterpolation"));
	NetworkInterpolation->SetUpdatedComponent(CollisionComponent);
	NetworkInterpolation->SetInterpolatedComponent(VisualMesh);
	NetworkInterpolation->bSimulationEnabled = false;
	NetworkInterpolation->bInterpMovement = true;
	NetworkInterpolation->bInterpRotation = true;
	NetworkInterpolation->InterpLocationTime = 0.11f;
	NetworkInterpolation->InterpRotationTime = 0.08f;
	NetworkInterpolation->InterpLocationMaxLagDistance = 250.0f;
	NetworkInterpolation->InterpLocationSnapToTargetDistance = 800.0f;

	static ConstructorHelpers::FObjectFinder<UStaticMesh> SphereMeshAsset(TEXT("/Engine/BasicShapes/Sphere.Sphere"));
	if (SphereMeshAsset.Succeeded())
	{
		VisualMesh->SetStaticMesh(SphereMeshAsset.Object);
		VisualMesh->SetRelativeScale3D(FVector(0.16f));
	}
}

void AGrenadeActor::OnConstruction(const FTransform& Transform)
{
	Super::OnConstruction(Transform);
	ApplyVisualMaterial();
}

void AGrenadeActor::BeginPlay()
{
	Super::BeginPlay();
	ApplyVisualMaterial();

	if (!HasAuthority() && !bCosmeticPrediction)
	{
		TryBeginOwnerReconciliation();
	}
}

void AGrenadeActor::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (AGrenadeActor* Target = ReconciliationTarget.Get())
	{
		if (Target->VisualMesh && !Target->bAuthorityExploded)
		{
			Target->VisualMesh->SetVisibility(true, true);
		}
	}
	if (!HasAuthority() && !bCosmeticPrediction && ThrowId != 0)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("GRENADE_INTERPOLATION_SUMMARY ThrowId=%u Targets=%d MaxTargetDeltaCm=%.2f MaxVisualFrameStepCm=%.2f"),
			ThrowId,
			NetworkInterpolationTargetCount,
			MaximumNetworkTargetDeltaCm,
			MaximumNetworkVisualFrameStepCm);
	}
	Super::EndPlay(EndPlayReason);
}

void AGrenadeActor::ApplyVisualMaterial()
{
	if (!VisualMesh)
	{
		return;
	}

	UMaterialInterface* SelectedMaterial = nullptr;
	if (const UWorld* World = GetWorld())
	{
		if (const Ahe_grenade_gameGameMode* GameMode = World->GetAuthGameMode<Ahe_grenade_gameGameMode>())
		{
			SelectedMaterial = GameMode->GrenadeMaterial;
		}
		if (!SelectedMaterial)
		{
			if (const AGrenadeGameState* ArenaState =
				World->GetGameState<AGrenadeGameState>())
			{
				const FSoftObjectPath& MaterialPath =
					ArenaState->GetGrenadeMaterialPath();
				SelectedMaterial = Cast<UMaterialInterface>(MaterialPath.ResolveObject());
				if (!SelectedMaterial && MaterialPath.IsValid())
				{
					SelectedMaterial =
						Cast<UMaterialInterface>(MaterialPath.TryLoad());
				}
			}
		}
	}
	if (SelectedMaterial)
	{
		VisualMesh->SetMaterial(0, SelectedMaterial);
	}
}

void AGrenadeActor::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bCosmeticPrediction && ReconciliationTarget.IsValid())
	{
		TickPredictionReconciliation(DeltaSeconds);
		return;
	}

	if (!HasAuthority() && !bCosmeticPrediction)
	{
		RecordNetworkVisualSample();
		if (!bOwnerReconciliationAttempted)
		{
			TryBeginOwnerReconciliation();
		}
		return;
	}

	if (!bInitialized || bAuthorityExploded)
	{
		return;
	}

	FixedStepAccumulator += FMath::Max(0.0f, DeltaSeconds);
	const float FixedStep = FMath::Max(0.001f, SimulationConfig.FixedStepSeconds);
	while (FixedStepAccumulator >= FixedStep && !bAuthorityExploded)
	{
		SimulateFixedStep(FixedStep);
		FixedStepAccumulator -= FixedStep;
	}
}

void AGrenadeActor::RecordNetworkVisualSample()
{
	if (!VisualMesh)
	{
		return;
	}

	const FVector VisualLocation = VisualMesh->GetComponentLocation();
	if (bHasNetworkVisualSample)
	{
		MaximumNetworkVisualFrameStepCm = FMath::Max(
			MaximumNetworkVisualFrameStepCm,
			FVector::Distance(LastNetworkVisualLocation, VisualLocation));
	}
	LastNetworkVisualLocation = VisualLocation;
	bHasNetworkVisualSample = true;
}

void AGrenadeActor::InitializeAuthoritativeGrenade(
	uint32 InThrowId,
	const FVector& StartPosition,
	const FVector& InitialVelocity,
	float FuseSeconds,
	const FGrenadeSimConfig& InSimulationConfig,
	AActor* InOwnerActor)
{
	if (!HasAuthority() || GetNetMode() == NM_Client)
	{
		return;
	}

	ThrowId = InThrowId;
	bCosmeticPrediction = false;
	SimulationConfig = InSimulationConfig;
	CollisionComponent->SetSphereRadius(SimulationConfig.RadiusCm);
	SetActorLocation(StartPosition);
	OwningActor = InOwnerActor;
	SetOwner(InOwnerActor);

	FGrenadeSim::InitializeState(SimState, StartPosition, InitialVelocity, FuseSeconds);
	NetworkInterpolation->Velocity = InitialVelocity;
	FuseEndServerWorldTimeSeconds = GetWorld()->GetGameState()
		? GetWorld()->GetGameState()->GetServerWorldTimeSeconds() + FuseSeconds
		: GetWorld()->GetTimeSeconds() + FuseSeconds;
	FixedStepAccumulator = 0.0f;
	bAuthorityExploded = false;
	bInitialized = true;
	ForceNetUpdate();

	if (CVarGGGrenadeActorThrowLockDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("GRENADE_AUTH_SPAWN ThrowId=%u Spawn=%s Velocity=%s Fuse=%.3f Owner=%s"),
			ThrowId,
			*StartPosition.ToCompactString(),
			*InitialVelocity.ToCompactString(),
			FuseSeconds,
			*GetNameSafe(InOwnerActor));
	}
}

void AGrenadeActor::InitializePredictedVisual(
	uint32 InThrowId,
	const FVector& StartPosition,
	const FVector& InitialVelocity,
	float FuseSeconds,
	const FGrenadeSimConfig& InSimulationConfig,
	AActor* InOwnerActor)
{
	SetReplicates(false);
	SetReplicateMovement(false);
	ThrowId = InThrowId;
	bCosmeticPrediction = true;
	SimulationConfig = InSimulationConfig;
	CollisionComponent->SetSphereRadius(SimulationConfig.RadiusCm);
	SetActorLocation(StartPosition);
	OwningActor = InOwnerActor;
	FGrenadeSim::InitializeState(SimState, StartPosition, InitialVelocity, FuseSeconds);
	NetworkInterpolation->Velocity = InitialVelocity;
	FixedStepAccumulator = 0.0f;
	bAuthorityExploded = false;
	bInitialized = true;
}

void AGrenadeActor::BeginPredictionReconciliation(AGrenadeActor* AuthoritativeGrenade)
{
	if (!bCosmeticPrediction || !AuthoritativeGrenade || AuthoritativeGrenade == this)
	{
		return;
	}

	ReconciliationTarget = AuthoritativeGrenade;
	ReconciliationElapsedSeconds = 0.0f;
	bInitialized = false;
	if (AuthoritativeGrenade->VisualMesh)
	{
		AuthoritativeGrenade->VisualMesh->SetVisibility(false, true);
	}
	UE_LOG(
		LogTemp,
		Display,
		TEXT("GRENADE_RECONCILE_START ThrowId=%u InitialErrorCm=%.2f"),
		ThrowId,
		FVector::Distance(GetActorLocation(), AuthoritativeGrenade->GetActorLocation()));
}

void AGrenadeActor::CancelPredictedVisual()
{
	if (bCosmeticPrediction)
	{
		Destroy();
	}
}

FVector AGrenadeActor::GetVelocity() const
{
	if (bInitialized && (HasAuthority() || bCosmeticPrediction))
	{
		return SimState.Velocity;
	}
	return NetworkInterpolation ? NetworkInterpolation->Velocity : Super::GetVelocity();
}

void AGrenadeActor::ForceExplode()
{
	if (HasAuthority() && GetNetMode() != NM_Client && !bAuthorityExploded)
	{
		ExplodeNow();
	}
}

void AGrenadeActor::ApplyInstantKillBlast(
	UWorld* World,
	const FVector& Origin,
	float RadiusCm,
	AActor* DamageCauser,
	AActor* InstigatorActor)
{
	if (!World || World->GetNetMode() == NM_Client || RadiusCm <= 0.0f)
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

	if (!World->OverlapMultiByObjectType(
		Overlaps,
		Origin,
		FQuat::Identity,
		ObjectQuery,
		FCollisionShape::MakeSphere(RadiusCm),
		QueryParams))
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
		if (Ahe_grenade_gameGameMode* GameMode = World->GetAuthGameMode<Ahe_grenade_gameGameMode>())
		{
			GameMode->EliminatePlayer(Pawn->GetController());
		}
	}
}

FGrenadeArenaHit AGrenadeActor::ResolveArenaHit(const FHitResult& Hit) const
{
	FGrenadeArenaHit Result;
	if (const AGrenadeGameState* ArenaState = GetArenaGameState())
	{
		ArenaState->ResolveArenaHit(
			Hit,
			Result.ArenaObjectId,
			Result.bBreakable,
			Result.bBounceBeforeBreaking);
	}
	return Result;
}

void AGrenadeActor::AppendIgnoredArenaObject(
	int32 ArenaObjectId,
	FCollisionQueryParams& QueryParams) const
{
	if (const AGrenadeGameState* ArenaState = GetArenaGameState())
	{
		ArenaState->AppendArenaObjectIgnoredComponents(ArenaObjectId, QueryParams);
	}
}

AGrenadeGameState* AGrenadeActor::GetArenaGameState() const
{
	UWorld* World = GetWorld();
	return World ? World->GetGameState<AGrenadeGameState>() : nullptr;
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
		[this](const FHitResult& Hit) { return ResolveArenaHit(Hit); },
		[this](int32 ArenaObjectId, FCollisionQueryParams& QueryParams)
		{
			AppendIgnoredArenaObject(ArenaObjectId, QueryParams);
		});

	SetActorLocation(SimState.Position, false, nullptr, ETeleportType::TeleportPhysics);
	NetworkInterpolation->Velocity = SimState.Velocity;

	if (HasAuthority() && !bCosmeticPrediction)
	{
		if (StepResult.bBounced)
		{
			const FGrenadeArenaHit BounceArenaHit = ResolveArenaHit(StepResult.Hit);
			PublishAuthorityEvent(
				EGrenadeAuthorityEventType::Bounce,
				StepResult.Hit.ImpactPoint,
				StepResult.Hit.ImpactNormal,
				BounceArenaHit.ArenaObjectId);
			EArenaObjectType ArenaObjectType = EArenaObjectType::StaticObstacle;
			const bool bHasArenaType = GetArenaGameState()
				&& GetArenaGameState()->GetArenaObjectType(
					BounceArenaHit.ArenaObjectId,
					ArenaObjectType);
			UE_LOG(
				LogTemp,
				Display,
				TEXT("GRENADE_BOUNCE ThrowId=%u ArenaId=%d ArenaType=%d"),
				ThrowId,
				BounceArenaHit.ArenaObjectId,
				bHasArenaType ? static_cast<int32>(ArenaObjectType) : INDEX_NONE);
		}

		if (StepResult.bBrokeTile && StepResult.BrokenArenaObjectId != INDEX_NONE)
		{
			if (AGrenadeGameState* ArenaState = GetArenaGameState())
			{
				if (ArenaState->DestroyArenaObject(
					StepResult.BrokenArenaObjectId,
					EArenaDestructionCause::Grenade))
				{
					PublishAuthorityEvent(
						EGrenadeAuthorityEventType::ArenaDestroyed,
						StepResult.Hit.ImpactPoint,
						StepResult.Hit.ImpactNormal,
						StepResult.BrokenArenaObjectId);
					UE_LOG(
						LogTemp,
						Log,
						TEXT("GRENADE_ARENA_DESTROY ThrowId=%u ArenaId=%d StateRevision=%d"),
						ThrowId,
						StepResult.BrokenArenaObjectId,
						ArenaState->GetArenaStateRevision());
				}
			}
		}
	}

	if (bDrawDebugPath || CVarGGGrenadeDebugPath.GetValueOnGameThread() != 0)
	{
		DrawDebugLine(GetWorld(), PreviousLocation, SimState.Position, FColor::Yellow, false, 2.0f, 0, 1.5f);
	}

	if (SimState.bExploded || StepResult.bExplodedThisStep)
	{
		if (bCosmeticPrediction)
		{
			Destroy();
		}
		else
		{
			ExplodeNow();
		}
	}
}

void AGrenadeActor::ExplodeNow()
{
	if (!HasAuthority() || GetNetMode() == NM_Client || bAuthorityExploded)
	{
		return;
	}

	bAuthorityExploded = true;
	PublishAuthorityEvent(EGrenadeAuthorityEventType::Exploded, GetActorLocation());
	ApplyInstantKillBlast(GetWorld(), GetActorLocation(), ExplosionRadiusCm, this, OwningActor.Get());
	CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	VisualMesh->SetVisibility(false, true);
	ForceNetUpdate();
	SetLifeSpan(0.75f);
}

void AGrenadeActor::PublishAuthorityEvent(
	EGrenadeAuthorityEventType Type,
	const FVector& Location,
	const FVector& Normal,
	int32 ArenaObjectId)
{
	if (!HasAuthority())
	{
		return;
	}

	FReplicatedGrenadeEvent& Event = AuthorityEvents.AddDefaulted_GetRef();
	Event.Revision = AuthorityEvents.Num() == 1
		? 1
		: static_cast<uint16>(AuthorityEvents[AuthorityEvents.Num() - 2].Revision + 1);
	Event.Type = Type;
	Event.Location = Location;
	Event.Normal = Normal;
	Event.ArenaObjectId = ArenaObjectId;
	Event.ServerWorldTimeSeconds = GetWorld() && GetWorld()->GetGameState()
		? GetWorld()->GetGameState()->GetServerWorldTimeSeconds()
		: 0.0f;
	ForceNetUpdate();
}

void AGrenadeActor::TryBeginOwnerReconciliation()
{
	if (HasAuthority() || bCosmeticPrediction || ThrowId == 0)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasLocalNetOwner())
	{
		return;
	}

	bOwnerReconciliationAttempted = true;
	if (UGrenadeThrowerComponent* Thrower = OwnerActor->FindComponentByClass<UGrenadeThrowerComponent>())
	{
		Thrower->ReconcilePredictedGrenade(ThrowId, this);
	}
}

void AGrenadeActor::TickPredictionReconciliation(float DeltaSeconds)
{
	AGrenadeActor* Target = ReconciliationTarget.Get();
	if (!Target)
	{
		Destroy();
		return;
	}

	ReconciliationElapsedSeconds += FMath::Max(0.0f, DeltaSeconds);
	const FVector TargetLocation = Target->VisualMesh
		? Target->VisualMesh->GetComponentLocation()
		: Target->GetActorLocation();
	const FVector NewLocation = FMath::VInterpTo(
		GetActorLocation(),
		TargetLocation,
		DeltaSeconds,
		18.0f);
	SetActorLocation(NewLocation, false, nullptr, ETeleportType::TeleportPhysics);

	if (ReconciliationElapsedSeconds >= 0.18f
		|| FVector::DistSquared(NewLocation, TargetLocation) <= FMath::Square(2.0f))
	{
		if (Target->VisualMesh && !Target->bAuthorityExploded)
		{
			Target->VisualMesh->SetVisibility(true, true);
		}
		ReconciliationTarget.Reset();
		UE_LOG(
			LogTemp,
			Display,
			TEXT("GRENADE_RECONCILE_COMPLETE ThrowId=%u DurationMs=%.1f"),
			ThrowId,
			ReconciliationElapsedSeconds * 1000.0f);
		Destroy();
	}
}

void AGrenadeActor::PostNetReceiveLocationAndRotation()
{
	if (HasAuthority() || bCosmeticPrediction || !NetworkInterpolation)
	{
		Super::PostNetReceiveLocationAndRotation();
		return;
	}

	const FRepMovement& RepMovement = GetReplicatedMovement();
	const FVector NewLocation = FRepMovement::RebaseOntoLocalOrigin(RepMovement.Location, this);
	++NetworkInterpolationTargetCount;
	MaximumNetworkTargetDeltaCm = FMath::Max(
		MaximumNetworkTargetDeltaCm,
		FVector::Distance(GetActorLocation(), NewLocation));
	NetworkInterpolation->MoveInterpolationTarget(NewLocation, RepMovement.Rotation);
}

void AGrenadeActor::PostNetReceiveVelocity(const FVector& NewVelocity)
{
	if (NetworkInterpolation)
	{
		NetworkInterpolation->Velocity = NewVelocity;
		NetworkInterpolation->UpdateComponentVelocity();
	}
}

void AGrenadeActor::OnRep_ThrowIdentity()
{
	TryBeginOwnerReconciliation();
}

void AGrenadeActor::OnRep_AuthorityEvents()
{
	for (const FReplicatedGrenadeEvent& Event : AuthorityEvents)
	{
		if (Event.Revision <= LastProcessedAuthorityEventRevision)
		{
			continue;
		}

		LastProcessedAuthorityEventRevision = Event.Revision;
		UE_LOG(
			LogTemp,
			Display,
			TEXT("GRENADE_EVENT ThrowId=%u Revision=%u Type=%d ArenaId=%d"),
			ThrowId,
			Event.Revision,
			static_cast<int32>(Event.Type),
			Event.ArenaObjectId);
	}
}

void AGrenadeActor::OnRep_AuthorityExploded()
{
	if (bAuthorityExploded)
	{
		CollisionComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		VisualMesh->SetVisibility(false, true);
	}
}

void AGrenadeActor::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME(AGrenadeActor, ThrowId);
	DOREPLIFETIME(AGrenadeActor, FuseEndServerWorldTimeSeconds);
	DOREPLIFETIME(AGrenadeActor, AuthorityEvents);
	DOREPLIFETIME(AGrenadeActor, bAuthorityExploded);
}
