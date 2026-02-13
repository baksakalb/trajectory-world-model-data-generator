#include "Grenade/GrenadeSim.h"

#include "Grenade/Breakables/BreakableTile.h"
#include "Engine/World.h"

namespace
{
	bool HasStableSupport(
		UWorld* World,
		const FGrenadeSimConfig& Config,
		const FGrenadeSimState& State,
		AActor* PrimaryIgnoredActor)
	{
		if (!World)
		{
			return false;
		}

		const float ProbeDistanceCm = FMath::Max(0.0f, Config.SupportProbeDistanceCm);
		if (ProbeDistanceCm <= KINDA_SMALL_NUMBER)
		{
			return false;
		}

		FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeSimSupportProbe), false);
		if (PrimaryIgnoredActor)
		{
			QueryParams.AddIgnoredActor(PrimaryIgnoredActor);
		}

		for (const TWeakObjectPtr<ABreakableTile>& TilePtr : State.VirtualBrokenTiles)
		{
			if (ABreakableTile* Tile = TilePtr.Get())
			{
				QueryParams.AddIgnoredActor(Tile);
			}
		}

		const FVector ProbeStart = State.Position;
		const FVector ProbeEnd = ProbeStart - FVector(0.0f, 0.0f, ProbeDistanceCm);

		FHitResult SupportHit;
		const bool bHitSupport = World->SweepSingleByChannel(
			SupportHit,
			ProbeStart,
			ProbeEnd,
			FQuat::Identity,
			Config.TraceChannel,
			FCollisionShape::MakeSphere(Config.RadiusCm),
			QueryParams);

		if (!bHitSupport || !SupportHit.bBlockingHit)
		{
			return false;
		}

		const FVector SupportNormal = SupportHit.ImpactNormal.IsNearlyZero()
			? SupportHit.Normal.GetSafeNormal()
			: SupportHit.ImpactNormal.GetSafeNormal();

		if (SupportNormal.IsNearlyZero())
		{
			return false;
		}

		return SupportNormal.Z >= FMath::Clamp(Config.SupportMinNormalZ, 0.0f, 1.0f);
	}
}

void FGrenadeSim::InitializeState(FGrenadeSimState& OutState, const FVector& StartPosition, const FVector& StartVelocity, float FuseSeconds)
{
	OutState.Position = StartPosition;
	OutState.Velocity = StartVelocity;
	OutState.RemainingFuseSeconds = FMath::Max(0.0f, FuseSeconds);
	OutState.BounceCount = 0;
	OutState.bMotionStopped = false;
	OutState.ConsecutiveSupportedSteps = 0;
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
	const float RestSpeedSq = FMath::Square(FMath::Max(0.0f, Config.RestSpeedCmPerSec));
	const int32 RequiredSupportedSteps = FMath::Max(1, Config.SupportRequiredConsecutiveSteps);
	float RemainingStepTime = ClampedStepDt;

	auto TryEnterRestState = [&](const FVector& CandidateVelocity) -> bool
		{
			if (CandidateVelocity.SizeSquared() > RestSpeedSq)
			{
				InOutState.ConsecutiveSupportedSteps = 0;
				return false;
			}

			if (!HasStableSupport(World, Config, InOutState, PrimaryIgnoredActor))
			{
				InOutState.ConsecutiveSupportedSteps = 0;
				return false;
			}

			InOutState.ConsecutiveSupportedSteps = FMath::Min(InOutState.ConsecutiveSupportedSteps + 1, RequiredSupportedSteps);
			if (InOutState.ConsecutiveSupportedSteps < RequiredSupportedSteps)
			{
				return false;
			}

			InOutState.Velocity = FVector::ZeroVector;
			InOutState.bMotionStopped = true;
			Result.bStoppedThisStep = true;
			RemainingStepTime = 0.0f;
			return true;
		};

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
			TryEnterRestState(EndVelocity);
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

			if (TryEnterRestState(EndVelocity))
			{
				break;
			}

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

			const float BreakDeflection = FMath::Clamp(Config.BreakableNormalDeflection, 0.0f, 1.0f);
			const float PostBreakNormalSpeed = FVector::DotProduct(PostBreakVelocity, HitNormal);
			if (PostBreakNormalSpeed < 0.0f)
			{
				PostBreakVelocity += HitNormal * ((-PostBreakNormalSpeed) * BreakDeflection);
			}

			// Breaking an upward-facing floor tile should not create an upward rebound.
			const float ImpactNormalSpeed = FVector::DotProduct(VelocityAtImpact, HitNormal);
			if (HitNormal.Z > 0.5f && ImpactNormalSpeed < 0.0f)
			{
				const float OutwardNormalSpeed = FVector::DotProduct(PostBreakVelocity, HitNormal);
				if (OutwardNormalSpeed > 0.0f)
				{
					PostBreakVelocity -= HitNormal * OutwardNormalSpeed;
				}
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

			const float ForwardPushOut = FMath::Max(1.5f, Config.RadiusCm * 0.2f);
			InOutState.Position = ImpactCenter + (ForwardDirection * ForwardPushOut);
			InOutState.Velocity = PostBreakVelocity;

			if (TryEnterRestState(PostBreakVelocity))
			{
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

		if (TryEnterRestState(BouncedVelocity))
		{
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
