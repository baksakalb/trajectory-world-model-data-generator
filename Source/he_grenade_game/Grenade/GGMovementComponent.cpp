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
	const float ChainAdditiveBonus = FMath::Max(0, NextChainCount - 1) * FMath::Max(0.0f, CrouchHopChainAdditiveIncrementCmPerSec);
	const float ScalarBoostSpeed = CurrentSpeedCmPerSec * FMath::Max(1.0f, CrouchHopBoostSpeedScalar);
	const float AdditiveBoostSpeed = CurrentSpeedCmPerSec
		+ FMath::Max(0.0f, CrouchHopBoostAdditiveCmPerSec)
		+ ChainAdditiveBonus;
	float DesiredBoostSpeedCmPerSec = FMath::Max(ScalarBoostSpeed, AdditiveBoostSpeed);

	const float HardCapCmPerSec = FMath::Max(
		0.0f,
		CrouchHopSpeedCapCmPerSec > 0.0f ? CrouchHopSpeedCapCmPerSec : BunnyHopSpeedCapCmPerSec);
	if (HardCapCmPerSec > 0.0f)
	{
		DesiredBoostSpeedCmPerSec = FMath::Min(DesiredBoostSpeedCmPerSec, HardCapCmPerSec);
	}

	// Preserve momentum on hop redirects: even a full reverse redirect keeps at least current speed.
	const float TargetSpeedCmPerSec = FMath::Max(CurrentSpeedCmPerSec, DesiredBoostSpeedCmPerSec);
	const float AddedBoostSpeedCmPerSec = FMath::Max(0.0f, TargetSpeedCmPerSec - CurrentSpeedCmPerSec);
	FVector BaseBoostDirection = Acceleration.GetSafeNormal2D();
	if (BaseBoostDirection.IsNearlyZero())
	{
		BaseBoostDirection = ResolveBoostDirection(HorizontalVelocity);
	}
	const FVector BoostDirection = BlendWithFacingDirection(BaseBoostDirection);
	FVector BoostedHorizontalVelocity = HorizontalVelocity + (BoostDirection * AddedBoostSpeedCmPerSec);
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

	const FVector AccelDirection = Acceleration.GetSafeNormal2D();
	const bool bHasAcceleration = !AccelDirection.IsNearlyZero();

	FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float MaxSpeed = GetMaxSpeed();

	if (IsMovingOnGround())
	{
		const float InitialSpeed = HorizontalVelocity.Size();
		if (InitialSpeed > KINDA_SMALL_NUMBER)
		{
			// Keep steering crisp while input is held; apply stronger stop friction only on release.
			const float Control = bHasAcceleration ? InitialSpeed : FMath::Max(InitialSpeed, GroundBrakingDeceleration);
			const float Drop = Control * GroundFrictionAmount * DeltaTime;
			const float NewSpeed = FMath::Max(InitialSpeed - Drop, 0.0f);
			if (NewSpeed != InitialSpeed)
			{
				HorizontalVelocity *= (NewSpeed / InitialSpeed);
			}
		}

		if (bHasAcceleration)
		{
			const float CurrentSpeedAlongWishDir = FVector::DotProduct(HorizontalVelocity, AccelDirection);
			const float AdditionalSpeed = MaxSpeed - CurrentSpeedAlongWishDir;
			if (AdditionalSpeed > 0.0f)
			{
				const float AccelStep = GroundAcceleration * DeltaTime;
				HorizontalVelocity += AccelDirection * FMath::Min(AdditionalSpeed, AccelStep);
			}
		}

		const float HorizontalSpeed = HorizontalVelocity.Size();
		const float HardCap = FMath::Max(MaxSpeed, BunnyHopSpeedCapCmPerSec);
		if (HorizontalSpeed > HardCap && HorizontalSpeed > KINDA_SMALL_NUMBER)
		{
			HorizontalVelocity *= (HardCap / HorizontalSpeed);
		}

		const float NormalMoveSpeed = FMath::Max(0.0f, WalkSpeedCmPerSec);
		const float CurrentSpeed = HorizontalVelocity.Size();
		if (NormalMoveSpeed > 0.0f && CurrentSpeed > (NormalMoveSpeed + KINDA_SMALL_NUMBER))
		{
			const float ExcessSpeed = CurrentSpeed - NormalMoveSpeed;
			FVector BaseDirection = bHasAcceleration ? AccelDirection : HorizontalVelocity.GetSafeNormal2D();
			if (BaseDirection.IsNearlyZero())
			{
				BaseDirection = GetFacingDirection2D();
			}
			const FVector ExcessDirection = BlendWithFacingDirection(BaseDirection);
			FVector RecombinedVelocity = (BaseDirection * NormalMoveSpeed) + (ExcessDirection * ExcessSpeed);
			const float RecombinedSpeed = RecombinedVelocity.Size();
			if (RecombinedSpeed > CurrentSpeed && RecombinedSpeed > KINDA_SMALL_NUMBER)
			{
				RecombinedVelocity *= (CurrentSpeed / RecombinedSpeed);
			}
			HorizontalVelocity = RecombinedVelocity;
		}

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
		return;
	}

	if (IsFalling())
	{
		if (bShiftHeld)
		{
			const float InitialSpeed = HorizontalVelocity.Size();
			if (InitialSpeed > KINDA_SMALL_NUMBER)
			{
				const float SpeedDrop = FMath::Max(0.0f, AirShiftHoldDragDecelerationCmPerSec2) * DeltaTime;
				const float NewSpeed = FMath::Max(0.0f, InitialSpeed - SpeedDrop);
				if (NewSpeed != InitialSpeed)
				{
					HorizontalVelocity *= (NewSpeed / InitialSpeed);
				}
			}
		}

		if (bHasAcceleration)
		{
			const float CurrentSpeedAlongWishDir = FVector::DotProduct(HorizontalVelocity, AccelDirection);
			const float AdditionalSpeed = AirSpeedCapCmPerSec - CurrentSpeedAlongWishDir;
			if (AdditionalSpeed > 0.0f)
			{
				const float AccelStep = AirAcceleration * DeltaTime;
				HorizontalVelocity += AccelDirection * FMath::Min(AdditionalSpeed, AccelStep);
			}
		}

		const float HorizontalSpeed = HorizontalVelocity.Size();
		if (HorizontalSpeed > BunnyHopSpeedCapCmPerSec && HorizontalSpeed > KINDA_SMALL_NUMBER)
		{
			HorizontalVelocity *= (BunnyHopSpeedCapCmPerSec / HorizontalSpeed);
		}

		const float NormalMoveSpeed = FMath::Max(0.0f, WalkSpeedCmPerSec);
		const float CurrentSpeed = HorizontalVelocity.Size();
		if (NormalMoveSpeed > 0.0f && CurrentSpeed > (NormalMoveSpeed + KINDA_SMALL_NUMBER))
		{
			const float ExcessSpeed = CurrentSpeed - NormalMoveSpeed;
			FVector BaseDirection = bHasAcceleration ? AccelDirection : HorizontalVelocity.GetSafeNormal2D();
			if (BaseDirection.IsNearlyZero())
			{
				BaseDirection = GetFacingDirection2D();
			}
			const FVector ExcessDirection = BlendWithFacingDirection(BaseDirection);
			FVector RecombinedVelocity = (BaseDirection * NormalMoveSpeed) + (ExcessDirection * ExcessSpeed);
			const float RecombinedSpeed = RecombinedVelocity.Size();
			if (RecombinedSpeed > CurrentSpeed && RecombinedSpeed > KINDA_SMALL_NUMBER)
			{
				RecombinedVelocity *= (CurrentSpeed / RecombinedSpeed);
			}
			HorizontalVelocity = RecombinedVelocity;
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
			if (bShiftHeld || bPressedRecently)
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

FVector UGGMovementComponent::BlendWithFacingDirection(const FVector& BaseDirection) const
{
	FVector InputDir = BaseDirection.GetSafeNormal2D();
	FVector FacingDir = GetFacingDirection2D();
	if (FacingDir.IsNearlyZero())
	{
		FacingDir = InputDir;
	}

	if (InputDir.IsNearlyZero())
	{
		return FacingDir;
	}

	const float FacingWeight = FMath::Clamp(CrosshairInfluenceOnMomentum, 0.0f, 1.0f);
	FVector BlendedDirection = (InputDir * (1.0f - FacingWeight)) + (FacingDir * FacingWeight);
	if (BlendedDirection.IsNearlyZero())
	{
		BlendedDirection = InputDir;
	}
	return BlendedDirection.GetSafeNormal2D();
}
