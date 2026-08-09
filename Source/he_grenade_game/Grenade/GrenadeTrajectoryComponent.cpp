#include "Grenade/GrenadeTrajectoryComponent.h"

#include "CanvasItem.h"
#include "Camera/CameraComponent.h"
#include "Engine/Canvas.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "GlobalRenderResources.h"
#include "Grenade/GrenadeSim.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "he_grenade_gameCharacter.h"

namespace
{
	constexpr TCHAR TrajectoryRendererBuildId[] = TEXT("single-ribbon-v3");

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
		const int32 SafeMinSmoothSubsteps = FMath::Clamp(MinSmoothSubsteps, 1, 8);

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

	void AddCanvasTriangle(
		TArray<FCanvasUVTri>& Triangles,
		const FVector2D& A,
		const FVector2D& B,
		const FVector2D& C,
		const FLinearColor& ColorA,
		const FLinearColor& ColorB,
		const FLinearColor& ColorC)
	{
		FCanvasUVTri& Triangle = Triangles.Emplace_GetRef();
		Triangle.V0_Pos = A;
		Triangle.V1_Pos = B;
		Triangle.V2_Pos = C;
		Triangle.V0_UV = FVector2D::ZeroVector;
		Triangle.V1_UV = FVector2D::ZeroVector;
		Triangle.V2_UV = FVector2D::ZeroVector;
		Triangle.V0_Color = ColorA;
		Triangle.V1_Color = ColorB;
		Triangle.V2_Color = ColorC;
	}

	void DrawContinuousScreenRibbon(
		UCanvas* Canvas,
		const TArray<FVector2D>& InputPoints,
		float Thickness,
		const FLinearColor& Color)
	{
		if (!Canvas || InputPoints.Num() < 2)
		{
			return;
		}

		TArray<FVector2D> Points;
		Points.Reserve(InputPoints.Num());
		for (const FVector2D& Point : InputPoints)
		{
			if (Points.IsEmpty() || FVector2D::Distance(Points.Last(), Point) >= 0.1f)
			{
				Points.Add(Point);
			}
		}

		if (Points.Num() < 2)
		{
			return;
		}

		const float HalfWidth = FMath::Max(0.25f, Thickness * 0.5f);
		const float FeatherWidth = 1.0f;
		const FLinearColor TransparentColor(Color.R, Color.G, Color.B, 0.0f);

		TArray<FVector2D> InnerLeft;
		TArray<FVector2D> InnerRight;
		TArray<FVector2D> OuterLeft;
		TArray<FVector2D> OuterRight;
		InnerLeft.SetNumUninitialized(Points.Num());
		InnerRight.SetNumUninitialized(Points.Num());
		OuterLeft.SetNumUninitialized(Points.Num());
		OuterRight.SetNumUninitialized(Points.Num());

		for (int32 PointIndex = 0; PointIndex < Points.Num(); ++PointIndex)
		{
			const FVector2D PreviousDirection = PointIndex > 0
				? (Points[PointIndex] - Points[PointIndex - 1]).GetSafeNormal()
				: (Points[1] - Points[0]).GetSafeNormal();
			const FVector2D NextDirection = PointIndex + 1 < Points.Num()
				? (Points[PointIndex + 1] - Points[PointIndex]).GetSafeNormal()
				: (Points.Last() - Points[Points.Num() - 2]).GetSafeNormal();

			const FVector2D PreviousNormal(-PreviousDirection.Y, PreviousDirection.X);
			const FVector2D NextNormal(-NextDirection.Y, NextDirection.X);
			FVector2D JoinNormal = (PreviousNormal + NextNormal).GetSafeNormal();
			if (JoinNormal.IsNearlyZero())
			{
				JoinNormal = NextNormal;
			}

			const float JoinDenominator = FMath::Max(
				0.25f,
				FMath::Abs(FVector2D::DotProduct(JoinNormal, NextNormal)));
			const float InnerJoinLength = FMath::Min(HalfWidth / JoinDenominator, HalfWidth * 2.0f);
			const float OuterHalfWidth = HalfWidth + FeatherWidth;
			const float OuterJoinLength = FMath::Min(
				OuterHalfWidth / JoinDenominator,
				OuterHalfWidth * 2.0f);

			InnerLeft[PointIndex] = Points[PointIndex] + (JoinNormal * InnerJoinLength);
			InnerRight[PointIndex] = Points[PointIndex] - (JoinNormal * InnerJoinLength);
			OuterLeft[PointIndex] = Points[PointIndex] + (JoinNormal * OuterJoinLength);
			OuterRight[PointIndex] = Points[PointIndex] - (JoinNormal * OuterJoinLength);
		}

		TArray<FCanvasUVTri> Triangles;
		Triangles.Reserve(((Points.Num() - 1) * 6) + 52);

		for (int32 SegmentIndex = 0; SegmentIndex + 1 < Points.Num(); ++SegmentIndex)
		{
			const int32 NextIndex = SegmentIndex + 1;

			// Solid center: every section shares its edge vertices with the next section.
			AddCanvasTriangle(
				Triangles,
				InnerLeft[SegmentIndex],
				InnerRight[SegmentIndex],
				InnerLeft[NextIndex],
				Color,
				Color,
				Color);
			AddCanvasTriangle(
				Triangles,
				InnerRight[SegmentIndex],
				InnerRight[NextIndex],
				InnerLeft[NextIndex],
				Color,
				Color,
				Color);

			// One-pixel transparent fringe provides anti-aliased outer edges.
			AddCanvasTriangle(
				Triangles,
				OuterLeft[SegmentIndex],
				InnerLeft[SegmentIndex],
				OuterLeft[NextIndex],
				TransparentColor,
				Color,
				TransparentColor);
			AddCanvasTriangle(
				Triangles,
				InnerLeft[SegmentIndex],
				InnerLeft[NextIndex],
				OuterLeft[NextIndex],
				Color,
				Color,
				TransparentColor);
			AddCanvasTriangle(
				Triangles,
				InnerRight[SegmentIndex],
				OuterRight[SegmentIndex],
				InnerRight[NextIndex],
				Color,
				TransparentColor,
				Color);
			AddCanvasTriangle(
				Triangles,
				OuterRight[SegmentIndex],
				OuterRight[NextIndex],
				InnerRight[NextIndex],
				TransparentColor,
				TransparentColor,
				Color);
		}

		constexpr int32 CapSides = 16;
		auto AddRoundCap = [&](const FVector2D& Center)
			{
				FVector2D PreviousInner = Center + FVector2D(HalfWidth, 0.0f);
				FVector2D PreviousOuter = Center + FVector2D(HalfWidth + FeatherWidth, 0.0f);
				for (int32 SideIndex = 1; SideIndex <= CapSides; ++SideIndex)
				{
					const float Angle = (2.0f * PI * SideIndex) / CapSides;
					const FVector2D Direction(FMath::Cos(Angle), FMath::Sin(Angle));
					const FVector2D NextInner = Center + (Direction * HalfWidth);
					const FVector2D NextOuter = Center + (Direction * (HalfWidth + FeatherWidth));

					AddCanvasTriangle(
						Triangles,
						Center,
						PreviousInner,
						NextInner,
						Color,
						Color,
						Color);
					AddCanvasTriangle(
						Triangles,
						PreviousInner,
						PreviousOuter,
						NextInner,
						Color,
						TransparentColor,
						Color);
					AddCanvasTriangle(
						Triangles,
						PreviousOuter,
						NextOuter,
						NextInner,
						TransparentColor,
						TransparentColor,
						Color);

					PreviousInner = NextInner;
					PreviousOuter = NextOuter;
				}
			};

		AddRoundCap(Points[0]);
		AddRoundCap(Points.Last());

		FCanvasTriangleItem RibbonItem(Triangles, GWhiteTexture);
		RibbonItem.BlendMode = SE_BLEND_Translucent;
		Canvas->DrawItem(RibbonItem);
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

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Grenade trajectory renderer active: %s (compiled %s %s)"),
		TrajectoryRendererBuildId,
		TEXT(__DATE__),
		TEXT(__TIME__));

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

		TArray<FVector2D> ProjectedRun;
		ProjectedRun.Reserve(Run.Num());
		for (const FVector& WorldPoint : Run)
		{
			FVector2D ScreenPoint = FVector2D::ZeroVector;
			const bool bProjected = PlayerController->ProjectWorldLocationToScreen(
				WorldPoint,
				ScreenPoint,
				true);

			if (bProjected)
			{
				ProjectedRun.Add(ScreenPoint);
			}
			else if (ProjectedRun.Num() >= 2)
			{
				DrawContinuousScreenRibbon(
					Canvas,
					ProjectedRun,
					SafeLineThickness,
					CachedTrajectoryColor);
				ProjectedRun.Reset();
			}
		}

		if (ProjectedRun.Num() >= 2)
		{
			DrawContinuousScreenRibbon(
				Canvas,
				ProjectedRun,
				SafeLineThickness,
				CachedTrajectoryColor);
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
		FMath::Min(MaxRenderSegmentLengthCm, 8.0f),
		FMath::Max(MinRenderSubstepsPerSimulationStep, 4),
		RenderPoints);

	// V2 uses the crosshair—not the trajectory—as the availability indicator.
	CachedTrajectoryColor = AvailableColor;
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
