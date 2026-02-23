#include "Grenade/GGMovementComponent.h"
#include "Engine/World.h"
#include "GameFramework/Controller.h"
#include "GameFramework/Pawn.h"
#include "Math/RotationMatrix.h"

UGGMovementComponent::UGGMovementComponent()
{
	AirControl = 0.0f;
	BrakingFrictionFactor = 0.0f;
	BrakingDecelerationWalking = GroundBrakingDeceleration;
	MaxAcceleration = GroundAcceleration;
	MaxWalkSpeed = WalkSpeedCmPerSec;
	MaxWalkSpeedCrouched = WalkSpeedCmPerSec * CrouchSpeedScalar;
	JumpZVelocity = JumpVelocityCmPerSec;
	GravityScale = JumpGravityScale;
}

void UGGMovementComponent::NotifyCrouchPressed()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	bShiftHeld = true;
	LastShiftPressTimeSeconds = CurrentTimeSeconds;
}

void UGGMovementComponent::NotifyCrouchReleased()
{
	bShiftHeld = false;
	UWorld* World = GetWorld();
	if (World)
	{
		LastShiftReleaseTimeSeconds = World->GetTimeSeconds();
	}
}

void UGGMovementComponent::CancelCrouchHopChain()
{
	CrouchHopChainCount = 0;
	LastCrouchHopTimeSeconds = -1.0f;
	BoostJumpWindowEndTimeSeconds = -1.0f;
}

bool UGGMovementComponent::IsCrouchHopChainActive() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return CrouchHopChainCount > 0
		&& IsWithinWindow(World->GetTimeSeconds(), LastCrouchHopTimeSeconds, CrouchHopChainWindowSeconds);
}

bool UGGMovementComponent::TryConsumeCrouchJumpBoost()
{
	if (!IsMovingOnGround())
	{
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	const float CurrentTimeSeconds = World->GetTimeSeconds();
	if (BoostJumpWindowEndTimeSeconds < 0.0f || CurrentTimeSeconds > BoostJumpWindowEndTimeSeconds)
	{
		return false;
	}

	const bool bReleasedAfterLanding = LastShiftReleaseTimeSeconds >= LastLandingTimeSeconds;
	if (bShiftHeld || !bReleasedAfterLanding)
	{
		return false;
	}

	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	const float CurrentSpeedCmPerSec = HorizontalVelocity.Size();
	if (CurrentSpeedCmPerSec < CrouchHopMinSpeedCmPerSec)
	{
		BoostJumpWindowEndTimeSeconds = -1.0f;
		return false;
	}

	int32 NextChainCount = 1;
	if (IsWithinWindow(CurrentTimeSeconds, LastCrouchHopTimeSeconds, CrouchHopChainWindowSeconds))
	{
		NextChainCount = CrouchHopChainCount + 1;
	}
	const int32 HopStepIndex = FMath::Max(0, NextChainCount - 1);
	float DesiredBoostSpeedCmPerSec = FMath::Max(0.0f, WalkSpeedCmPerSec)
		+ (FMath::Max(0.0f, CrouchHopChainAdditiveIncrementCmPerSec) * HopStepIndex);

	const float HardCapCmPerSec = FMath::Max(
		0.0f,
		CrouchHopSpeedCapCmPerSec > 0.0f ? CrouchHopSpeedCapCmPerSec : BunnyHopSpeedCapCmPerSec);
	if (HardCapCmPerSec > 0.0f)
	{
		DesiredBoostSpeedCmPerSec = FMath::Min(DesiredBoostSpeedCmPerSec, HardCapCmPerSec);
	}

	// Hop consume can add speed, but should never reduce existing momentum.
	const float TargetSpeedCmPerSec = FMath::Max(CurrentSpeedCmPerSec, DesiredBoostSpeedCmPerSec);
	const float AddedBoostSpeedCmPerSec = FMath::Max(0.0f, TargetSpeedCmPerSec - CurrentSpeedCmPerSec);
	FVector BaseBoostDirection = Acceleration.GetSafeNormal2D();
	if (BaseBoostDirection.IsNearlyZero())
	{
		BaseBoostDirection = ResolveBoostDirection(HorizontalVelocity);
	}
	FVector BoostDirection = BaseBoostDirection.GetSafeNormal2D();
	if (BoostDirection.IsNearlyZero())
	{
		BoostDirection = HorizontalVelocity.GetSafeNormal2D();
	}
	if (BoostDirection.IsNearlyZero())
	{
		BoostDirection = GetFacingDirection2D();
	}
	FVector RedirectedHorizontalVelocity = HorizontalVelocity;
	if (CurrentSpeedCmPerSec > KINDA_SMALL_NUMBER)
	{
		const float RedirectAlpha = FMath::Clamp(CrouchHopRedirectStrength, 0.0f, 1.0f);
		FVector RedirectedDirection = FMath::Lerp(HorizontalVelocity / CurrentSpeedCmPerSec, BoostDirection, RedirectAlpha);
		if (!RedirectedDirection.IsNearlyZero())
		{
			RedirectedHorizontalVelocity = RedirectedDirection.GetSafeNormal2D() * CurrentSpeedCmPerSec;
		}
	}
	FVector BoostedHorizontalVelocity = RedirectedHorizontalVelocity + (BoostDirection * AddedBoostSpeedCmPerSec);
	const float BoostedHorizontalSpeed = BoostedHorizontalVelocity.Size();
	if (HardCapCmPerSec > 0.0f && BoostedHorizontalSpeed > HardCapCmPerSec && BoostedHorizontalSpeed > KINDA_SMALL_NUMBER)
	{
		BoostedHorizontalVelocity *= (HardCapCmPerSec / BoostedHorizontalSpeed);
	}
	Velocity.X = BoostedHorizontalVelocity.X;
	Velocity.Y = BoostedHorizontalVelocity.Y;
	LastCrouchHopTimeSeconds = CurrentTimeSeconds;
	CrouchHopChainCount = NextChainCount;
	BoostJumpWindowEndTimeSeconds = -1.0f;
	return true;
}

bool UGGMovementComponent::IsCrouchHopFeedbackActive() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return false;
	}

	return IsWithinWindow(World->GetTimeSeconds(), LastCrouchHopTimeSeconds, CrouchHopFeedbackDurationSeconds);
}

float UGGMovementComponent::GetCurrentHorizontalSpeedCmPerSec() const
{
	return FVector(Velocity.X, Velocity.Y, 0.0f).Size();
}

int32 UGGMovementComponent::GetCrouchHopChainCount() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	if (!IsWithinWindow(World->GetTimeSeconds(), LastCrouchHopTimeSeconds, CrouchHopChainWindowSeconds))
	{
		return 0;
	}

	return CrouchHopChainCount;
}

void UGGMovementComponent::SetAimMode(bool bEnableAimMode)
{
	bAimMode = bEnableAimMode;
}

float UGGMovementComponent::GetMaxSpeed() const
{
	float MaxSpeedCmPerSec = WalkSpeedCmPerSec;

	if (bAimMode)
	{
		MaxSpeedCmPerSec *= AimWalkSpeedScalar;
	}

	if (IsCrouching())
	{
		MaxSpeedCmPerSec *= CrouchSpeedScalar;
	}

	return FMath::Max(0.0f, MaxSpeedCmPerSec);
}

bool UGGMovementComponent::CanCrouchInCurrentState() const
{
	if (!CanEverCrouch())
	{
		return false;
	}

	return IsMovingOnGround();
}

FVector UGGMovementComponent::GetFallingLateralAcceleration(float /*DeltaTime*/)
{
	// Preserve full lateral input while falling; we intentionally do not scale by AirControl.
	FVector FallAcceleration = ProjectToGravityFloor(Acceleration);
	if (!HasAnimRootMotion() && FallAcceleration.SizeSquared() > 0.0f)
	{
		FallAcceleration = FallAcceleration.GetClampedToMaxSize(GetMaxAcceleration());
	}

	return FallAcceleration;
}

void UGGMovementComponent::CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration)
{
	if (!HasValidData() || HasAnimRootMotion() || DeltaTime < MIN_TICK_TIME)
	{
		return;
	}

	if (MovementMode != MOVE_Walking && MovementMode != MOVE_NavWalking && MovementMode != MOVE_Falling)
	{
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	const FVector InputDirection = Acceleration.GetSafeNormal2D();
	const bool bHasInput = !InputDirection.IsNearlyZero();
	const FVector DesiredDirection = ComputeDesiredSteerDirection2D(bHasInput, InputDirection);

	FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float MaxSpeed = GetMaxSpeed();

	if (IsMovingOnGround())
	{
		const float InitialSpeed = HorizontalVelocity.Size();
		if (InitialSpeed > KINDA_SMALL_NUMBER)
		{
			// Keep steering crisp while input is held; apply stronger stop friction only on release.
			const float Control = bHasInput ? InitialSpeed : FMath::Max(InitialSpeed, GroundBrakingDeceleration);
			const float Drop = Control * GroundFrictionAmount * DeltaTime;
			const float NewSpeed = FMath::Max(InitialSpeed - Drop, 0.0f);
			if (NewSpeed != InitialSpeed)
			{
				HorizontalVelocity *= (NewSpeed / InitialSpeed);
			}
		}

		if (bHasInput)
		{
			const float CurrentSpeedAlongWishDir = FVector::DotProduct(HorizontalVelocity, DesiredDirection);
			const float AdditionalSpeed = MaxSpeed - CurrentSpeedAlongWishDir;
			if (AdditionalSpeed > 0.0f)
			{
				const float AccelStep = GroundAcceleration * DeltaTime;
				HorizontalVelocity += DesiredDirection * FMath::Min(AdditionalSpeed, AccelStep);
			}
		}

		ApplySteeringAndReverseBleed(HorizontalVelocity, DesiredDirection, bHasInput, false, DeltaTime);

		const float HorizontalSpeed = HorizontalVelocity.Size();
		const bool bHopChainGroundActive = IsCrouchHopChainActive();
		const float GroundHopHardCap = bHopChainGroundActive && CrouchHopSpeedCapCmPerSec > 0.0f
			? CrouchHopSpeedCapCmPerSec
			: BunnyHopSpeedCapCmPerSec;
		const float HardCap = FMath::Max(MaxSpeed, GroundHopHardCap);
		if (HardCap > 0.0f && HorizontalSpeed > HardCap && HorizontalSpeed > KINDA_SMALL_NUMBER)
		{
			HorizontalVelocity *= (HardCap / HorizontalSpeed);
		}

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
		return;
	}

	if (IsFalling())
	{
		const bool bHopAirSpeedAllowed = IsCrouchHopChainActive();
		const int32 ActiveHopCount = bHopAirSpeedAllowed ? FMath::Max(1, GetCrouchHopChainCount()) : 0;
		const float NonHopAirSpeedCap = FMath::Max(
			0.0f,
			GetMaxSpeed() * FMath::Clamp(NonHopAirSpeedCapRunSpeedScalar, 0.0f, 1.0f));
		const float HopLadderSpeedCap = FMath::Max(0.0f, WalkSpeedCmPerSec)
			+ (FMath::Max(0.0f, CrouchHopChainAdditiveIncrementCmPerSec) * FMath::Max(0, ActiveHopCount - 1));
		const float HopHardCap = FMath::Max(
			0.0f,
			CrouchHopSpeedCapCmPerSec > 0.0f ? CrouchHopSpeedCapCmPerSec : BunnyHopSpeedCapCmPerSec);
		const float HopAirSpeedCap = HopHardCap > 0.0f ? FMath::Min(HopLadderSpeedCap, HopHardCap) : HopLadderSpeedCap;
		const float ActiveAirSpeedCap = bHopAirSpeedAllowed
			? HopAirSpeedCap
			: NonHopAirSpeedCap;

		if (bHasInput)
		{
			const float CurrentSpeed = HorizontalVelocity.Size();
			if (CurrentSpeed < FMath::Max(0.0f, AirInputKickStartThresholdCmPerSec))
			{
				const float KickStartSpeed = FMath::Clamp(
					FMath::Max(0.0f, AirInputKickStartSpeedCmPerSec),
					0.0f,
					ActiveAirSpeedCap);
				HorizontalVelocity = DesiredDirection * FMath::Max(CurrentSpeed, KickStartSpeed);
			}
		}

		if (bHasInput)
		{
			const float CurrentSpeedAlongWishDir = FVector::DotProduct(HorizontalVelocity, DesiredDirection);
			const float AdditionalSpeed = ActiveAirSpeedCap - CurrentSpeedAlongWishDir;
			if (AdditionalSpeed > 0.0f)
			{
				const float AirAccelScale = FMath::Clamp(AirInputAccelerationScale, 0.0f, 1.0f);
				const float AccelStep = AirAcceleration * AirAccelScale * DeltaTime;
				HorizontalVelocity += DesiredDirection * FMath::Min(AdditionalSpeed, AccelStep);
			}
		}

		ApplySteeringAndReverseBleed(HorizontalVelocity, DesiredDirection, bHasInput, true, DeltaTime);

		const float SpeedAfterSteer = HorizontalVelocity.Size();
		if (ActiveAirSpeedCap > 0.0f && SpeedAfterSteer > ActiveAirSpeedCap && SpeedAfterSteer > KINDA_SMALL_NUMBER)
		{
			HorizontalVelocity *= (ActiveAirSpeedCap / SpeedAfterSteer);
		}

		const float HorizontalSpeed = HorizontalVelocity.Size();
		const float FinalAirHardCap = bHopAirSpeedAllowed && CrouchHopSpeedCapCmPerSec > 0.0f
			? CrouchHopSpeedCapCmPerSec
			: BunnyHopSpeedCapCmPerSec;
		if (FinalAirHardCap > 0.0f && HorizontalSpeed > FinalAirHardCap && HorizontalSpeed > KINDA_SMALL_NUMBER)
		{
			HorizontalVelocity *= (FinalAirHardCap / HorizontalSpeed);
		}

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
	}
}

void UGGMovementComponent::OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	if (PreviousMovementMode == MOVE_Falling && IsMovingOnGround())
	{
		UWorld* World = GetWorld();
		if (World)
		{
			const float CurrentTimeSeconds = World->GetTimeSeconds();
			LastLandingTimeSeconds = CurrentTimeSeconds;
			const bool bPressedRecently = IsWithinWindow(
				CurrentTimeSeconds,
				LastShiftPressTimeSeconds,
				CrouchHopLandingQualifyWindowSeconds);
			if (bPressedRecently)
			{
				ArmCrouchHopWindow(CurrentTimeSeconds);
			}
			else
			{
				BoostJumpWindowEndTimeSeconds = -1.0f;
			}
		}
	}

	if (MovementMode == MOVE_Falling)
	{
		BoostJumpWindowEndTimeSeconds = -1.0f;
	}
}

bool UGGMovementComponent::IsWithinWindow(float CurrentTimeSeconds, float EventTimeSeconds, float WindowSeconds) const
{
	if (EventTimeSeconds < 0.0f)
	{
		return false;
	}

	return (CurrentTimeSeconds - EventTimeSeconds) <= FMath::Max(0.0f, WindowSeconds);
}

void UGGMovementComponent::ArmCrouchHopWindow(float CurrentTimeSeconds)
{
	const float CurrentHorizontalSpeedCmPerSec = FVector(Velocity.X, Velocity.Y, 0.0f).Size();
	if (CurrentHorizontalSpeedCmPerSec < CrouchHopMinSpeedCmPerSec)
	{
		BoostJumpWindowEndTimeSeconds = -1.0f;
		return;
	}

	BoostJumpWindowEndTimeSeconds = CurrentTimeSeconds + FMath::Max(0.01f, CrouchHopPostLandJumpWindowSeconds);
}

FVector UGGMovementComponent::ComputeDesiredSteerDirection2D(bool bHasInput, const FVector& InputDir) const
{
	if (bHasInput && !InputDir.IsNearlyZero())
	{
		// Active key input always defines desired travel direction.
		return InputDir.GetSafeNormal2D();
	}

	FVector FacingDir = GetFacingDirection2D();
	if (FacingDir.IsNearlyZero())
	{
		FacingDir = FVector(Velocity.X, Velocity.Y, 0.0f).GetSafeNormal2D();
	}

	if (!FacingDir.IsNearlyZero())
	{
		return FacingDir.GetSafeNormal2D();
	}

	FVector VelocityDir = FVector(Velocity.X, Velocity.Y, 0.0f).GetSafeNormal2D();
	if (!VelocityDir.IsNearlyZero())
	{
		return VelocityDir;
	}

	if (bHasInput && !InputDir.IsNearlyZero())
	{
		return InputDir;
	}

	return FVector::ForwardVector;
}

void UGGMovementComponent::ApplySteeringAndReverseBleed(
	FVector& HorizontalVelocity,
	const FVector& DesiredDir,
	bool bHasInput,
	bool bIsAir,
	float DeltaTime) const
{
	if (DeltaTime <= 0.0f)
	{
		return;
	}

	const FVector SafeDesiredDir = DesiredDir.GetSafeNormal2D();
	if (SafeDesiredDir.IsNearlyZero())
	{
		return;
	}

	float Speed = HorizontalVelocity.Size();
	FVector CurrentDir = Speed > KINDA_SMALL_NUMBER ? (HorizontalVelocity / Speed) : SafeDesiredDir;

	if (Speed <= KINDA_SMALL_NUMBER)
	{
		HorizontalVelocity = FVector::ZeroVector;
		return;
	}

	float Responsiveness = bIsAir ? SteerResponsivenessAir : SteerResponsivenessGround;
	if (!bHasInput)
	{
		Responsiveness *= FMath::Clamp(LookOnlySteerMultiplier, 0.0f, 1.0f);
		Responsiveness = FMath::Max(0.0f, Responsiveness);

		const float Alpha = FMath::Clamp(1.0f - FMath::Exp(-Responsiveness * DeltaTime), 0.0f, 1.0f);
		FVector SteeredDir = FMath::Lerp(CurrentDir, SafeDesiredDir, Alpha);
		if (SteeredDir.IsNearlyZero())
		{
			SteeredDir = SafeDesiredDir;
		}
		HorizontalVelocity = SteeredDir.GetSafeNormal2D() * Speed;
		return;
	}
	Responsiveness = FMath::Max(0.0f, Responsiveness);

	// Input-active steering: decompose momentum into desired-axis and perpendicular parts.
	// This prevents "press back but drift left" artifacts while keeping smooth air steering.
	float ParallelSpeed = FVector::DotProduct(HorizontalVelocity, SafeDesiredDir);
	FVector PerpendicularVelocity = HorizontalVelocity - (SafeDesiredDir * ParallelSpeed);

	const float PerpendicularDecay = FMath::Exp(-Responsiveness * DeltaTime);
	PerpendicularVelocity *= PerpendicularDecay;

	if (ParallelSpeed < 0.0f)
	{
		float BrakeRate = FMath::Max(0.0f, ReverseInputBleedCmPerSec);
		if (bIsAir)
		{
			BrakeRate += FMath::Max(0.0f, AirOppositeInputBrakeDecelerationCmPerSec2);
		}
		else
		{
			BrakeRate += FMath::Max(0.0f, GroundBrakingDeceleration * 0.5f);
		}
		ParallelSpeed = FMath::Min(0.0f, ParallelSpeed + (BrakeRate * DeltaTime));
	}

	HorizontalVelocity = PerpendicularVelocity + (SafeDesiredDir * ParallelSpeed);
}

FVector UGGMovementComponent::ResolveBoostDirection(const FVector& HorizontalVelocity) const
{
	FVector MoveDirection = GetFacingDirection2D();
	if (!MoveDirection.IsNearlyZero())
	{
		return MoveDirection;
	}

	MoveDirection = Acceleration.GetSafeNormal2D();
	if (!MoveDirection.IsNearlyZero())
	{
		return MoveDirection;
	}

	if (PawnOwner)
	{
		const FVector LastInput = PawnOwner->GetLastMovementInputVector();
		MoveDirection = FVector(LastInput.X, LastInput.Y, 0.0f).GetSafeNormal();
		if (!MoveDirection.IsNearlyZero())
		{
			return MoveDirection;
		}

		if (const AController* Controller = PawnOwner->GetController())
		{
			FRotator YawOnlyControlRotation = Controller->GetControlRotation();
			YawOnlyControlRotation.Pitch = 0.0f;
			YawOnlyControlRotation.Roll = 0.0f;
			MoveDirection = FRotationMatrix(YawOnlyControlRotation).GetScaledAxis(EAxis::X).GetSafeNormal2D();
			if (!MoveDirection.IsNearlyZero())
			{
				return MoveDirection;
			}
		}
	}

	MoveDirection = HorizontalVelocity.GetSafeNormal2D();
	return MoveDirection;
}

FVector UGGMovementComponent::GetFacingDirection2D() const
{
	if (PawnOwner)
	{
		if (const AController* Controller = PawnOwner->GetController())
		{
			FRotator YawOnlyControlRotation = Controller->GetControlRotation();
			YawOnlyControlRotation.Pitch = 0.0f;
			YawOnlyControlRotation.Roll = 0.0f;
			const FVector ControlForward = FRotationMatrix(YawOnlyControlRotation).GetScaledAxis(EAxis::X).GetSafeNormal2D();
			if (!ControlForward.IsNearlyZero())
			{
				return ControlForward;
			}
		}
	}

	if (UpdatedComponent)
	{
		return UpdatedComponent->GetForwardVector().GetSafeNormal2D();
	}

	return FVector::ForwardVector;
}
