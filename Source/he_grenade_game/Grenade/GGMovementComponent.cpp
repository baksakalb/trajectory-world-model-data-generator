#include "Grenade/GGMovementComponent.h"

UGGMovementComponent::UGGMovementComponent()
{
	AirControl = 0.0f;
	BrakingFrictionFactor = 0.0f;
	BrakingDecelerationWalking = GroundBrakingDeceleration;
	MaxAcceleration = GroundAcceleration;
	MaxWalkSpeed = WalkSpeedCmPerSec;
	MaxWalkSpeedCrouched = WalkSpeedCmPerSec * CrouchSpeedScalar;
}

void UGGMovementComponent::SetSprinting(bool bEnableSprint)
{
	bWantsSprint = bEnableSprint;
}

void UGGMovementComponent::SetAimMode(bool bEnableAimMode)
{
	bAimMode = bEnableAimMode;
	if (bAimMode)
	{
		bWantsSprint = false;
	}
}

bool UGGMovementComponent::IsSprintAllowed() const
{
	return !bAimMode && IsMovingOnGround() && !IsCrouching();
}

float UGGMovementComponent::GetMaxSpeed() const
{
	float MaxSpeed = WalkSpeedCmPerSec;
	if (bWantsSprint && IsSprintAllowed())
	{
		MaxSpeed = SprintSpeedCmPerSec;
	}

	if (bAimMode)
	{
		MaxSpeed *= AimWalkSpeedScalar;
	}

	if (IsCrouching())
	{
		MaxSpeed *= CrouchSpeedScalar;
	}

	return FMath::Max(0.0f, MaxSpeed);
}

bool UGGMovementComponent::CanCrouchInCurrentState() const
{
	if (!CanEverCrouch())
	{
		return false;
	}

	// Allow crouch while grounded and while in-air (falling), matching requested control behavior.
	return MovementMode != MOVE_None;
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
			const float Control = bHasAcceleration ? FMath::Max(InitialSpeed, GroundBrakingDeceleration) : InitialSpeed;
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

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
		return;
	}

	if (IsFalling())
	{
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

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
	}
}
