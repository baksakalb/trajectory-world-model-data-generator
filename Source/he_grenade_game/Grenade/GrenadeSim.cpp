#include "Grenade/GrenadeSim.h"

#include "Grenade/Breakables/BreakableTile.h"
#include "Engine/World.h"

void FGrenadeSim::InitializeState(FGrenadeSimState& OutState, const FVector& StartPosition, const FVector& StartVelocity, float FuseSeconds)
{
	OutState.Position = StartPosition;
	OutState.Velocity = StartVelocity;
	OutState.RemainingFuseSeconds = FMath::Max(0.0f, FuseSeconds);
	OutState.BounceCount = 0;
	OutState.bMotionStopped = false;
	OutState.bExploded = false;
	OutState.TraveledDistanceCm = 0.0f;
	OutState.VirtualBrokenTiles.Reset();
}

FGrenadeSimStepResult FGrenadeSim::Step(
	UWorld* World,
	const FGrenadeSimConfig& Config,
	FGrenadeSimState& InOutState,
	float StepDt,
	AActor* PrimaryIgnoredActor,
	const FResolveBreakableTile& ResolveBreakableTile)
{
	FGrenadeSimStepResult Result;

	if (!World || InOutState.bExploded)
	{
		return Result;
	}

	const float ClampedStepDt = FMath::Max(0.0f, StepDt);
	if (ClampedStepDt <= SMALL_NUMBER)
	{
		return Result;
	}

	const FVector GravityVector(0.0f, 0.0f, Config.GravityZ);
	float RemainingStepTime = ClampedStepDt;

	constexpr int32 MaxCollisionIterations = 8;
	for (int32 Iteration = 0; Iteration < MaxCollisionIterations && RemainingStepTime > KINDA_SMALL_NUMBER; ++Iteration)
	{
		if (InOutState.bMotionStopped)
		{
			break;
		}

		if (Config.MaxBounces > 0 && InOutState.BounceCount >= Config.MaxBounces)
		{
			InOutState.bMotionStopped = true;
			Result.bStoppedThisStep = true;
			break;
		}

		const FVector StartPosition = InOutState.Position;
		const FVector EndVelocity = InOutState.Velocity + (GravityVector * RemainingStepTime);
		FVector MovementDelta = (InOutState.Velocity + EndVelocity) * 0.5f * RemainingStepTime;
		float EffectiveStepTime = RemainingStepTime;

		const float SegmentDistance = MovementDelta.Size();
		if (Config.MaxTraceDistanceCm > 0.0f && SegmentDistance > SMALL_NUMBER)
		{
			const float RemainingDistanceBudget = Config.MaxTraceDistanceCm - InOutState.TraveledDistanceCm;
			if (RemainingDistanceBudget <= KINDA_SMALL_NUMBER)
			{
				InOutState.bMotionStopped = true;
				Result.bStoppedThisStep = true;
				Result.bExceededDistance = true;
				break;
			}

			if (SegmentDistance > RemainingDistanceBudget)
			{
				const float Scale = FMath::Clamp(RemainingDistanceBudget / SegmentDistance, 0.0f, 1.0f);
				MovementDelta *= Scale;
				EffectiveStepTime *= Scale;
				Result.bExceededDistance = true;
			}
		}

		if (MovementDelta.IsNearlyZero())
		{
			InOutState.Velocity = EndVelocity;
			RemainingStepTime = 0.0f;
			break;
		}

		const FVector EndPosition = StartPosition + MovementDelta;

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeSimSweep), false);
		if (PrimaryIgnoredActor)
		{
			QueryParams.AddIgnoredActor(PrimaryIgnoredActor);
		}

		for (const TWeakObjectPtr<ABreakableTile>& TilePtr : InOutState.VirtualBrokenTiles)
		{
			if (ABreakableTile* Tile = TilePtr.Get())
			{
				QueryParams.AddIgnoredActor(Tile);
			}
		}

		FHitResult Hit;
		const bool bBlockingHit = World->SweepSingleByChannel(
			Hit,
			StartPosition,
			EndPosition,
			FQuat::Identity,
			Config.TraceChannel,
			FCollisionShape::MakeSphere(Config.RadiusCm),
			QueryParams);

		if (!bBlockingHit || !Hit.bBlockingHit)
		{
			InOutState.Position = EndPosition;
			InOutState.Velocity = EndVelocity;
			InOutState.TraveledDistanceCm += MovementDelta.Size();
			RemainingStepTime -= EffectiveStepTime;
			continue;
		}

		Result.bHadHit = true;
		Result.Hit = Hit;

		const float TimeAlpha = FMath::Clamp(Hit.Time, 0.0f, 1.0f);
		float TimeUsed = EffectiveStepTime * TimeAlpha;
		if (TimeUsed <= KINDA_SMALL_NUMBER)
		{
			TimeUsed = FMath::Min(EffectiveStepTime, 0.001f);
		}

		const FVector ImpactCenter = Hit.Location;
		const FVector MoveToImpact = ImpactCenter - StartPosition;
		const FVector VelocityAtImpact = InOutState.Velocity + (GravityVector * TimeUsed);
		InOutState.Position = ImpactCenter;
		InOutState.TraveledDistanceCm += MoveToImpact.Size();

		ABreakableTile* ResolvedTile = ResolveBreakableTile ? ResolveBreakableTile(Hit) : nullptr;
		if (ResolvedTile && !InOutState.VirtualBrokenTiles.Contains(ResolvedTile))
		{
			InOutState.VirtualBrokenTiles.Add(ResolvedTile);
			Result.bBrokeTile = true;
			Result.BrokenTile = ResolvedTile;

			FVector HitNormal = Hit.ImpactNormal.IsNearlyZero() ? Hit.Normal.GetSafeNormal() : Hit.ImpactNormal.GetSafeNormal();
			if (HitNormal.IsNearlyZero())
			{
				HitNormal = (StartPosition - EndPosition).GetSafeNormal();
				if (HitNormal.IsNearlyZero())
				{
					HitNormal = FVector::UpVector;
				}
			}

			const float RetainedSpeedFactor = FMath::Clamp(1.0f - Config.BreakableVelocityDamping, 0.0f, 1.0f);
			FVector PostBreakVelocity = VelocityAtImpact * RetainedSpeedFactor;

			const float IncomingNormalSpeed = FVector::DotProduct(VelocityAtImpact, HitNormal);
			if (IncomingNormalSpeed < 0.0f)
			{
				PostBreakVelocity += HitNormal * ((-IncomingNormalSpeed) * FMath::Clamp(Config.BreakableNormalDeflection, 0.0f, 1.0f));
			}

			FVector ForwardDirection = PostBreakVelocity.GetSafeNormal();
			if (ForwardDirection.IsNearlyZero())
			{
				ForwardDirection = VelocityAtImpact.GetSafeNormal();
			}
			if (ForwardDirection.IsNearlyZero())
			{
				ForwardDirection = (EndPosition - StartPosition).GetSafeNormal();
			}
			if (ForwardDirection.IsNearlyZero())
			{
				ForwardDirection = FVector::ForwardVector;
			}

			const float ForwardPushOut = FMath::Max(2.0f, Config.RadiusCm * 0.4f);
			InOutState.Position = ImpactCenter + (ForwardDirection * ForwardPushOut);
			InOutState.Velocity = PostBreakVelocity;

			if (PostBreakVelocity.SizeSquared() <= FMath::Square(Config.StopSpeedCmPerSec))
			{
				InOutState.Velocity = FVector::ZeroVector;
				InOutState.bMotionStopped = true;
				Result.bStoppedThisStep = true;
				RemainingStepTime = 0.0f;
				break;
			}

			RemainingStepTime -= TimeUsed;
			continue;
		}

		FVector HitNormal = Hit.ImpactNormal.IsNearlyZero() ? Hit.Normal.GetSafeNormal() : Hit.ImpactNormal.GetSafeNormal();
		if (HitNormal.IsNearlyZero())
		{
			HitNormal = (StartPosition - EndPosition).GetSafeNormal();
			if (HitNormal.IsNearlyZero())
			{
				HitNormal = FVector::UpVector;
			}
		}
		const float NormalComponent = FVector::DotProduct(VelocityAtImpact, HitNormal);
		const FVector VelocityNormal = HitNormal * NormalComponent;
		const FVector VelocityTangent = VelocityAtImpact - VelocityNormal;

		FVector BouncedVelocity = (VelocityTangent * FMath::Clamp(1.0f - Config.TangentialDamping, 0.0f, 1.0f))
			- (VelocityNormal * Config.BounceRestitution);

		InOutState.BounceCount += 1;
		InOutState.Velocity = BouncedVelocity;
		const float SurfacePushOut = FMath::Max(1.5f, Config.RadiusCm * 0.15f);
		InOutState.Position = ImpactCenter + (HitNormal * SurfacePushOut);
		Result.bBounced = true;

		if (BouncedVelocity.SizeSquared() <= FMath::Square(Config.StopSpeedCmPerSec))
		{
			InOutState.Velocity = FVector::ZeroVector;
			InOutState.bMotionStopped = true;
			Result.bStoppedThisStep = true;
			RemainingStepTime = 0.0f;
			break;
		}

		RemainingStepTime -= TimeUsed;
	}

	InOutState.RemainingFuseSeconds = FMath::Max(0.0f, InOutState.RemainingFuseSeconds - ClampedStepDt);
	if (InOutState.RemainingFuseSeconds <= KINDA_SMALL_NUMBER)
	{
		InOutState.bExploded = true;
		Result.bExplodedThisStep = true;
	}

	return Result;
}
