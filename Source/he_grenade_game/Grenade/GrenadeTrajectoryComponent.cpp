#include "Grenade/GrenadeTrajectoryComponent.h"

#include "DrawDebugHelpers.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Grenade/GrenadeSim.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "he_grenade_gameCharacter.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarGGTrajectoryEnabled(
		TEXT("gg.Grenade.DebugTrajectory"),
		1,
		TEXT("Draw grenade trajectory while aiming. 0=off, 1=on"),
		ECVF_Default);
}

UGrenadeTrajectoryComponent::UGrenadeTrajectoryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGrenadeTrajectoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		ThrowerComponent = OwnerActor->FindComponentByClass<UGrenadeThrowerComponent>();
	}
}

void UGrenadeTrajectoryComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ClearHighlightedTiles();
	Super::EndPlay(EndPlayReason);
}

void UGrenadeTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTrajectoryEnabled || !bAimModeActive || CVarGGTrajectoryEnabled.GetValueOnGameThread() == 0)
	{
		ClearHighlightedTiles();
		return;
	}

	DrawPredictedPath();
}

void UGrenadeTrajectoryComponent::SetAimModeActive(bool bActive)
{
	bAimModeActive = bActive;
	if (!bAimModeActive)
	{
		ClearHighlightedTiles();
	}
}

void UGrenadeTrajectoryComponent::ClearHighlightedTiles()
{
	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : HighlightedTiles)
	{
		if (ABreakableTile* Tile = TilePtr.Get())
		{
			Tile->SetTrajectoryHighlighted(false);
		}
	}

	HighlightedTiles.Reset();
}

void UGrenadeTrajectoryComponent::SyncHighlightedTiles(const TSet<TWeakObjectPtr<ABreakableTile>>& DesiredTiles)
{
	TArray<TWeakObjectPtr<ABreakableTile>> TilesToRemove;
	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : HighlightedTiles)
	{
		if (!TilePtr.IsValid() || !DesiredTiles.Contains(TilePtr))
		{
			TilesToRemove.Add(TilePtr);
		}
	}

	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : TilesToRemove)
	{
		if (ABreakableTile* Tile = TilePtr.Get())
		{
			Tile->SetTrajectoryHighlighted(false);
		}
		HighlightedTiles.Remove(TilePtr);
	}

	for (const TWeakObjectPtr<ABreakableTile>& TilePtr : DesiredTiles)
	{
		ABreakableTile* Tile = TilePtr.Get();
		if (!Tile)
		{
			continue;
		}

		if (!HighlightedTiles.Contains(TilePtr))
		{
			Tile->SetTrajectoryHighlighted(true);
			HighlightedTiles.Add(TilePtr);
		}
	}
}

ABreakableTile* UGrenadeTrajectoryComponent::ResolveBreakableTile(const FHitResult& Hit) const
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor && Hit.GetComponent())
	{
		HitActor = Hit.GetComponent()->GetOwner();
	}

	ABreakableTile* Tile = Cast<ABreakableTile>(HitActor);
	if (!Tile || Tile->IsBroken() || !Tile->CanBreakOnGrenadeImpact())
	{
		return nullptr;
	}

	return Tile;
}

bool UGrenadeTrajectoryComponent::ResolveViewLocation(FVector& OutViewLocation) const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return false;
	}

	if (const Ahe_grenade_gameCharacter* Character = Cast<Ahe_grenade_gameCharacter>(OwnerActor))
	{
		if (const UCameraComponent* FirstPersonCamera = Character->GetFirstPersonCameraComponent())
		{
			OutViewLocation = FirstPersonCamera->GetComponentLocation();
			return true;
		}
	}

	if (const APawn* OwnerPawn = Cast<APawn>(OwnerActor))
	{
		if (const AController* Controller = OwnerPawn->GetController())
		{
			FRotator ViewRotation;
			Controller->GetPlayerViewPoint(OutViewLocation, ViewRotation);
			return true;
		}
	}

	FRotator ViewRotation;
	OwnerActor->GetActorEyesViewPoint(OutViewLocation, ViewRotation);
	return true;
}

bool UGrenadeTrajectoryComponent::ResolveFloorZ(float& OutFloorZ)
{
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!World || !OwnerActor)
	{
		return false;
	}

	const FVector OwnerLocation = OwnerActor->GetActorLocation();
	const FVector TraceStart = OwnerLocation + FVector(0.0f, 0.0f, 100.0f);
	const FVector TraceEnd = OwnerLocation - FVector(0.0f, 0.0f, 20000.0f);

	FHitResult FloorHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeTrajectoryFloorProbe), false);
	QueryParams.AddIgnoredActor(OwnerActor);

	const bool bHitFloor = World->LineTraceSingleByChannel(
		FloorHit,
		TraceStart,
		TraceEnd,
		VisibilityTraceChannel,
		QueryParams);

	if (bHitFloor && FloorHit.bBlockingHit)
	{
		OutFloorZ = FloorHit.ImpactPoint.Z;
		LastKnownFloorZ = OutFloorZ;
		bHasLastKnownFloorZ = true;
		return true;
	}

	if (bHasLastKnownFloorZ)
	{
		OutFloorZ = LastKnownFloorZ;
		return true;
	}

	return false;
}

bool UGrenadeTrajectoryComponent::IsPointVisibleFromView(const FVector& ViewLocation, const FVector& Point, AActor* OwnerActor, UWorld* World) const
{
	if (!World)
	{
		return false;
	}

	const FVector ToPoint = Point - ViewLocation;
	if (ToPoint.SizeSquared() <= KINDA_SMALL_NUMBER)
	{
		return true;
	}

	FHitResult VisibilityHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeTrajectoryVisibility), false);
	if (OwnerActor)
	{
		QueryParams.AddIgnoredActor(OwnerActor);
	}

	const bool bBlockingHit = World->LineTraceSingleByChannel(
		VisibilityHit,
		ViewLocation,
		Point,
		VisibilityTraceChannel,
		QueryParams);

	if (!bBlockingHit || !VisibilityHit.bBlockingHit)
	{
		return true;
	}

	const float HitDistanceSq = FVector::DistSquared(ViewLocation, VisibilityHit.ImpactPoint);
	const float PointDistanceSq = FVector::DistSquared(ViewLocation, Point);
	const float VisibilityToleranceSq = FMath::Square(2.0f);
	return HitDistanceSq + VisibilityToleranceSq >= PointDistanceSq;
}

void UGrenadeTrajectoryComponent::DrawPredictedPath()
{
	UGrenadeThrowerComponent* Thrower = ThrowerComponent.Get();
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!Thrower || !World || !OwnerActor)
	{
		ClearHighlightedTiles();
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	if (!Thrower->BuildLaunchParams(LaunchParams))
	{
		ClearHighlightedTiles();
		return;
	}

	const FGrenadeSimConfig& SimConfig = Thrower->GetSimulationConfig();
	FGrenadeSimState SimState;
	FGrenadeSim::InitializeState(SimState, LaunchParams.SpawnLocation, LaunchParams.InitialVelocity, LaunchParams.FuseSeconds);
	TSet<TWeakObjectPtr<ABreakableTile>> HitTilesThisFrame;

	TArray<FVector> TrajectoryPoints;
	TrajectoryPoints.Reserve(MaxSimulationSteps + 1);
	TrajectoryPoints.Add(SimState.Position);

	bool bEndedByExplosion = false;
	FVector ExplosionPoint = SimState.Position;

	const float StepTime = FMath::Max(0.001f, SimConfig.FixedStepSeconds);
	for (int32 StepIndex = 0; StepIndex < MaxSimulationSteps; ++StepIndex)
	{
		const FVector PreviousPosition = SimState.Position;

		const FGrenadeSimStepResult StepResult = FGrenadeSim::Step(
			World,
			SimConfig,
			SimState,
			StepTime,
			OwnerActor,
			[this](const FHitResult& Hit) { return ResolveBreakableTile(Hit); });

		if (!SimState.Position.Equals(PreviousPosition, 0.01f))
		{
			TrajectoryPoints.Add(SimState.Position);
		}

		if (StepResult.bBrokeTile)
		{
			if (ABreakableTile* Tile = StepResult.BrokenTile.Get())
			{
				HitTilesThisFrame.Add(Tile);
			}
		}

		const bool bExplodedThisStep = SimState.bExploded || StepResult.bExplodedThisStep;
		if (SimState.bMotionStopped || StepResult.bStoppedThisStep || bExplodedThisStep)
		{
			if (bExplodedThisStep)
			{
				bEndedByExplosion = true;
				ExplosionPoint = SimState.Position;
			}
			break;
		}
	}

	SyncHighlightedTiles(HitTilesThisFrame);

	const FColor DrawColor = (Thrower->IsStateGreen() ? AvailableColor : CooldownColor).ToFColor(true);
	const FColor ExplosionColor = ExplosionTipColor.ToFColor(true);
	const float SegmentLengthLimitCm = FMath::Max(1.0f, MaxRenderSegmentLengthCm);
	const float DrawDuration = FMath::Max(0.0f, DrawDurationSeconds);
	const uint8 DrawDepthPriority = static_cast<uint8>(DepthPriority);

	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasViewLocation = ResolveViewLocation(ViewLocation);
	const bool bUseVisibilityFilter = bHideNonVisibleSegments && bHasViewLocation;

	float FloorZ = 0.0f;
	const bool bUseFloorFilter = bHideBelowFloorSegments && ResolveFloorZ(FloorZ);

	bool bHasVisibleEndpoint = false;
	FVector VisibleEndpoint = FVector::ZeroVector;
	bool bStoppedByFloor = false;

	for (int32 Index = 0; Index < TrajectoryPoints.Num() - 1; ++Index)
	{
		const FVector SegmentStart = TrajectoryPoints[Index];
		const FVector SegmentEnd = TrajectoryPoints[Index + 1];
		const float SegmentLengthCm = FVector::Distance(SegmentStart, SegmentEnd);
		const int32 SubSegmentCount = FMath::Max(1, FMath::CeilToInt(SegmentLengthCm / SegmentLengthLimitCm));

		FVector DrawStart = SegmentStart;
		for (int32 SubIndex = 1; SubIndex <= SubSegmentCount; ++SubIndex)
		{
			const float Alpha = static_cast<float>(SubIndex) / static_cast<float>(SubSegmentCount);
			const FVector RawDrawEnd = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);

			FVector DrawEnd = RawDrawEnd;
			bool bSegmentClippedToFloor = false;

			if (bUseFloorFilter)
			{
				const bool bStartBelowFloor = DrawStart.Z < FloorZ;
				if (bStartBelowFloor)
				{
					bStoppedByFloor = true;
					break;
				}

				if (DrawEnd.Z < FloorZ)
				{
					const float ZDelta = DrawEnd.Z - DrawStart.Z;
					if (FMath::Abs(ZDelta) > KINDA_SMALL_NUMBER)
					{
						const float FloorT = FMath::Clamp((FloorZ - DrawStart.Z) / ZDelta, 0.0f, 1.0f);
						DrawEnd = FMath::Lerp(DrawStart, DrawEnd, FloorT);
						bSegmentClippedToFloor = true;
					}
					else
					{
						bStoppedByFloor = true;
						break;
					}
				}
			}

			const FVector MidPoint = (DrawStart + DrawEnd) * 0.5f;
			const bool bVisible = !bUseVisibilityFilter
				|| IsPointVisibleFromView(ViewLocation, MidPoint, OwnerActor, World);

			if (bVisible && !DrawStart.Equals(DrawEnd, 0.01f))
			{
				DrawDebugLine(World, DrawStart, DrawEnd, DrawColor, false, DrawDuration, DrawDepthPriority, LineThickness);
				bHasVisibleEndpoint = true;
				VisibleEndpoint = DrawEnd;
			}

			DrawStart = RawDrawEnd;

			if (bSegmentClippedToFloor)
			{
				bStoppedByFloor = true;
				break;
			}
		}

		if (bStoppedByFloor)
		{
			break;
		}
	}

	FVector CandidateEndpoint = VisibleEndpoint;
	bool bHasCandidateEndpoint = bHasVisibleEndpoint;

	if (!bHasCandidateEndpoint && bEndedByExplosion)
	{
		const bool bExplosionAboveFloor = !bUseFloorFilter || ExplosionPoint.Z >= FloorZ;
		const bool bExplosionVisible = !bUseVisibilityFilter
			|| IsPointVisibleFromView(ViewLocation, ExplosionPoint, OwnerActor, World);
		if (bExplosionAboveFloor && bExplosionVisible)
		{
			CandidateEndpoint = ExplosionPoint;
			bHasCandidateEndpoint = true;
		}
	}

	if (bEndedByExplosion && bHasCandidateEndpoint)
	{
		const float EndpointEpsilon = FMath::Max(2.0f, ExplosionTipSizeCm);
		if (FVector::DistSquared(CandidateEndpoint, ExplosionPoint) <= FMath::Square(EndpointEpsilon))
		{
			DrawDebugSphere(
				World,
				CandidateEndpoint,
				FMath::Max(1.0f, ExplosionTipSizeCm),
				10,
				ExplosionColor,
				false,
				DrawDuration,
				DrawDepthPriority,
				1.5f);
		}
	}
}
