#include "Grenade/GrenadeTrajectoryComponent.h"

#include "CanvasItem.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
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

	FVector ComputeRenderTangent(const TArray<FVector>& Points, int32 PointIndex)
	{
		const int32 PointCount = Points.Num();
		if (PointCount < 2 || !Points.IsValidIndex(PointIndex))
		{
			return FVector::ZeroVector;
		}

		if (PointIndex > 0 && PointIndex + 1 < PointCount)
		{
			return (Points[PointIndex + 1] - Points[PointIndex - 1]) * 0.5f;
		}

		if (PointCount >= 3)
		{
			if (PointIndex == 0)
			{
				return ((Points[1] - Points[0]) * 2.0f) - ((Points[2] - Points[0]) * 0.5f);
			}

			return ((Points[PointCount - 1] - Points[PointCount - 2]) * 2.0f)
				- ((Points[PointCount - 1] - Points[PointCount - 3]) * 0.5f);
		}

		return Points[1] - Points[0];
	}

	void BuildSmoothedRenderPath(
		const TArray<FVector>& SimulationPoints,
		const TArray<uint8>& HardCornerFlags,
		float MaxSegmentLengthCm,
		int32 MinSmoothSubsteps,
		TArray<FVector>& OutRenderPoints)
	{
		OutRenderPoints.Reset();
		if (SimulationPoints.IsEmpty())
		{
			return;
		}

		OutRenderPoints.Reserve(
			SimulationPoints.Num() * FMath::Max(1, MinSmoothSubsteps));
		OutRenderPoints.Add(SimulationPoints[0]);

		const float SafeMaxSegmentLengthCm = FMath::Max(1.0f, MaxSegmentLengthCm);
		const int32 SafeMinSmoothSubsteps = FMath::Clamp(MinSmoothSubsteps, 1, 4);

		auto IsHardCorner = [&HardCornerFlags](int32 PointIndex)
			{
				return HardCornerFlags.IsValidIndex(PointIndex) && HardCornerFlags[PointIndex] != 0;
			};

		for (int32 SegmentIndex = 0; SegmentIndex + 1 < SimulationPoints.Num(); ++SegmentIndex)
		{
			const FVector& SegmentStart = SimulationPoints[SegmentIndex];
			const FVector& SegmentEnd = SimulationPoints[SegmentIndex + 1];
			const float ChordLengthCm = FVector::Distance(SegmentStart, SegmentEnd);
			if (ChordLengthCm <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			// A collision can change velocity inside a fixed simulation step. Keep that local area linear
			// so visual smoothing never rounds a bounce through collision geometry.
			const bool bCanSmooth =
				SimulationPoints.Num() >= 3
				&& !IsHardCorner(SegmentIndex)
				&& !IsHardCorner(SegmentIndex + 1)
				&& (SegmentIndex == 0 || !IsHardCorner(SegmentIndex - 1))
				&& (SegmentIndex + 2 >= SimulationPoints.Num() || !IsHardCorner(SegmentIndex + 2));

			const int32 LengthSubsteps =
				FMath::Max(1, FMath::CeilToInt(ChordLengthCm / SafeMaxSegmentLengthCm));
			const int32 SubstepCount = bCanSmooth
				? FMath::Max(LengthSubsteps, SafeMinSmoothSubsteps)
				: LengthSubsteps;

			const FVector StartTangent = bCanSmooth
				? ComputeRenderTangent(SimulationPoints, SegmentIndex)
				: FVector::ZeroVector;
			const FVector EndTangent = bCanSmooth
				? ComputeRenderTangent(SimulationPoints, SegmentIndex + 1)
				: FVector::ZeroVector;

			for (int32 SubstepIndex = 1; SubstepIndex <= SubstepCount; ++SubstepIndex)
			{
				const float Alpha =
					static_cast<float>(SubstepIndex) / static_cast<float>(SubstepCount);
				const FVector RenderPoint = bCanSmooth
					? FMath::CubicInterp(SegmentStart, StartTangent, SegmentEnd, EndTangent, Alpha)
					: FMath::Lerp(SegmentStart, SegmentEnd, Alpha);

				if (!OutRenderPoints.Last().Equals(RenderPoint, 0.01f))
				{
					OutRenderPoints.Add(RenderPoint);
				}
			}
		}
	}

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
	ClearTrajectoryVisual();
	Super::EndPlay(EndPlayReason);
}

void UGrenadeTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTrajectoryEnabled || !bAimModeActive || CVarGGTrajectoryEnabled.GetValueOnGameThread() == 0)
	{
		ClearHighlightedTiles();
		ClearTrajectoryVisual();
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
		ClearTrajectoryVisual();
	}
}

void UGrenadeTrajectoryComponent::DrawTrajectoryOverlay(
	UCanvas* Canvas,
	APlayerController* PlayerController) const
{
	if (!Canvas
		|| !PlayerController
		|| (CachedTrajectoryRuns.IsEmpty() && !bHasCachedExplosionTip))
	{
		return;
	}

	const float SafeLineThickness = FMath::Max(0.5f, LineThickness);
	for (const TArray<FVector>& Run : CachedTrajectoryRuns)
	{
		if (Run.Num() < 2)
		{
			continue;
		}

		FVector2D PreviousScreenPoint = FVector2D::ZeroVector;
		bool bPreviousProjected = PlayerController->ProjectWorldLocationToScreen(
			Run[0],
			PreviousScreenPoint,
			true);

		for (int32 PointIndex = 1; PointIndex < Run.Num(); ++PointIndex)
		{
			FVector2D ScreenPoint = FVector2D::ZeroVector;
			const bool bProjected = PlayerController->ProjectWorldLocationToScreen(
				Run[PointIndex],
				ScreenPoint,
				true);

			if (bPreviousProjected && bProjected)
			{
				FCanvasLineItem LineItem(PreviousScreenPoint, ScreenPoint);
				LineItem.SetColor(CachedTrajectoryColor);
				LineItem.LineThickness = SafeLineThickness;
				Canvas->DrawItem(LineItem);
			}

			PreviousScreenPoint = ScreenPoint;
			bPreviousProjected = bProjected;
		}
	}

	if (bHasCachedExplosionTip)
	{
		FVector2D TipCenter = FVector2D::ZeroVector;
		FVector2D TipRadiusPoint = FVector2D::ZeroVector;
		FVector ViewLocation = FVector::ZeroVector;
		FRotator ViewRotation = FRotator::ZeroRotator;
		PlayerController->GetPlayerViewPoint(ViewLocation, ViewRotation);
		const FVector ViewRight = FRotationMatrix(ViewRotation).GetUnitAxis(EAxis::Y);

		const bool bCenterProjected = PlayerController->ProjectWorldLocationToScreen(
			CachedExplosionTip,
			TipCenter,
			true);
		const bool bRadiusProjected = PlayerController->ProjectWorldLocationToScreen(
			CachedExplosionTip + (ViewRight * FMath::Max(1.0f, ExplosionTipSizeCm)),
			TipRadiusPoint,
			true);

		if (bCenterProjected && bRadiusProjected)
		{
			constexpr int32 TipSideCount = 16;
			const float TipRadiusPixels = FMath::Max(
				2.0f,
				FVector2D::Distance(TipCenter, TipRadiusPoint));
			FVector2D PreviousTipPoint =
				TipCenter + FVector2D(TipRadiusPixels, 0.0f);

			for (int32 SideIndex = 1; SideIndex <= TipSideCount; ++SideIndex)
			{
				const float AngleRadians =
					(2.0f * PI * static_cast<float>(SideIndex))
					/ static_cast<float>(TipSideCount);
				const FVector2D TipPoint =
					TipCenter
					+ FVector2D(
						FMath::Cos(AngleRadians) * TipRadiusPixels,
						FMath::Sin(AngleRadians) * TipRadiusPixels);

				FCanvasLineItem TipLineItem(PreviousTipPoint, TipPoint);
				TipLineItem.SetColor(ExplosionTipColor);
				TipLineItem.LineThickness = FMath::Max(1.0f, SafeLineThickness);
				Canvas->DrawItem(TipLineItem);
				PreviousTipPoint = TipPoint;
			}
		}
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

void UGrenadeTrajectoryComponent::ClearTrajectoryVisual()
{
	CachedTrajectoryRuns.Reset();
	bHasCachedExplosionTip = false;
	CachedExplosionTip = FVector::ZeroVector;
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

	const APawn* OwnerPawn = Cast<APawn>(OwnerActor);
	const APlayerController* PlayerController = OwnerPawn
		? Cast<APlayerController>(OwnerPawn->GetController())
		: nullptr;

	if (const Ahe_grenade_gameCharacter* Character = Cast<Ahe_grenade_gameCharacter>(OwnerActor))
	{
		if (const UCameraComponent* FirstPersonCamera = Character->GetFirstPersonCameraComponent())
		{
			OutViewLocation = FirstPersonCamera->GetComponentLocation();
			return true;
		}
	}

	FRotator ViewRotation = FRotator::ZeroRotator;
	if (PlayerController)
	{
		PlayerController->GetPlayerViewPoint(OutViewLocation, ViewRotation);
	}
	else
	{
		OwnerActor->GetActorEyesViewPoint(OutViewLocation, ViewRotation);
	}

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
	const float VisibilityToleranceSq = FMath::Square(FMath::Max(0.0f, VisibilityToleranceCm));
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
		ClearTrajectoryVisual();
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	if (!Thrower->BuildLaunchParams(LaunchParams))
	{
		ClearHighlightedTiles();
		ClearTrajectoryVisual();
		return;
	}

	const FGrenadeSimConfig& SimConfig = Thrower->GetSimulationConfig();
	FGrenadeSimState SimState;
	FGrenadeSim::InitializeState(SimState, LaunchParams.SpawnLocation, LaunchParams.InitialVelocity, LaunchParams.FuseSeconds);
	TSet<TWeakObjectPtr<ABreakableTile>> HitTilesThisFrame;

	TArray<FVector> TrajectoryPoints;
	TrajectoryPoints.Reserve(MaxSimulationSteps + 1);
	TrajectoryPoints.Add(SimState.Position);
	TArray<uint8> HardCornerFlags;
	HardCornerFlags.Reserve(MaxSimulationSteps + 1);
	HardCornerFlags.Add(0);

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
			HardCornerFlags.Add(StepResult.bHadHit ? 1 : 0);
		}
		else if (StepResult.bHadHit && !HardCornerFlags.IsEmpty())
		{
			HardCornerFlags.Last() = 1;
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

	TArray<FVector> RenderPoints;
	BuildSmoothedRenderPath(
		TrajectoryPoints,
		HardCornerFlags,
		MaxRenderSegmentLengthCm,
		MinRenderSubstepsPerSimulationStep,
		RenderPoints);

	CachedTrajectoryColor = Thrower->IsStateGreen() ? AvailableColor : CooldownColor;
	CachedTrajectoryRuns.Reset();
	bHasCachedExplosionTip = false;

	FVector ViewLocation = FVector::ZeroVector;
	const bool bHasViewLocation = ResolveViewLocation(ViewLocation);
	const bool bUseVisibilityFilter = bHideNonVisibleSegments && bHasViewLocation;

	float FloorZ = 0.0f;
	const bool bUseFloorFilter = bHideBelowFloorSegments && ResolveFloorZ(FloorZ);

	bool bHasVisibleEndpoint = false;
	FVector VisibleEndpoint = FVector::ZeroVector;
	CachedTrajectoryRuns.Reserve(1);
	TArray<FVector>* ActiveTrajectoryRun = nullptr;

	for (int32 Index = 0; Index < RenderPoints.Num() - 1; ++Index)
	{
		FVector DrawStart = RenderPoints[Index];
		FVector DrawEnd = RenderPoints[Index + 1];
		bool bSegmentClippedToFloor = false;

		if (bUseFloorFilter)
		{
			if (DrawStart.Z < FloorZ)
			{
				break;
			}

			if (DrawEnd.Z < FloorZ)
			{
				const float ZDelta = DrawEnd.Z - DrawStart.Z;
				if (FMath::Abs(ZDelta) > KINDA_SMALL_NUMBER)
				{
					const float FloorT =
						FMath::Clamp((FloorZ - DrawStart.Z) / ZDelta, 0.0f, 1.0f);
					DrawEnd = FMath::Lerp(DrawStart, DrawEnd, FloorT);
					bSegmentClippedToFloor = true;
				}
				else
				{
					break;
				}
			}
		}

		const FVector MidPoint = (DrawStart + DrawEnd) * 0.5f;
		const bool bVisible = !bUseVisibilityFilter
			|| IsPointVisibleFromView(ViewLocation, MidPoint, OwnerActor, World);

		if (bVisible && !DrawStart.Equals(DrawEnd, 0.01f))
		{
			if (!ActiveTrajectoryRun)
			{
				CachedTrajectoryRuns.Emplace();
				ActiveTrajectoryRun = &CachedTrajectoryRuns.Last();
				ActiveTrajectoryRun->Reserve(RenderPoints.Num());
				ActiveTrajectoryRun->Add(DrawStart);
			}
			else if (!ActiveTrajectoryRun->Last().Equals(DrawStart, 0.01f))
			{
				ActiveTrajectoryRun->Add(DrawStart);
			}

			ActiveTrajectoryRun->Add(DrawEnd);
			bHasVisibleEndpoint = true;
			VisibleEndpoint = DrawEnd;
		}
		else
		{
			ActiveTrajectoryRun = nullptr;
		}

		if (bSegmentClippedToFloor)
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
			bHasCachedExplosionTip = true;
			CachedExplosionTip = CandidateEndpoint;
		}
	}
}
