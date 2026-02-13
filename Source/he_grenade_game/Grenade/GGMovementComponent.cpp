#include "Grenade/GGMovementComponent.h"

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
		StopSlide();
	}
}

void UGGMovementComponent::TryStartSlide()
{
	if (bIsSliding || bAimMode || !IsMovingOnGround())
	{
		return;
	}

	const FVector HorizontalVelocity(Velocity.X, Velocity.Y, 0.0f);
	const float SpeedCmPerSec = HorizontalVelocity.Size();
	const bool bWasSprintingOrFast = bWantsSprint || SpeedCmPerSec >= (SprintSpeedCmPerSec * 0.9f);
	if (!bWasSprintingOrFast)
	{
		return;
	}

	if (SpeedCmPerSec < SlideEnterMinSpeedCmPerSec)
	{
		return;
	}

	bIsSliding = true;
	bWantsSprint = false;
	SlideDirection = HorizontalVelocity.GetSafeNormal2D();
	if (SlideDirection.IsNearlyZero())
	{
		SlideDirection = UpdatedComponent ? UpdatedComponent->GetForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
	}

	const float SlideStartSpeed = FMath::Max(0.0f, SprintSpeedCmPerSec);
	const FVector SlideStartVelocity = SlideDirection * SlideStartSpeed;
	Velocity.X = SlideStartVelocity.X;
	Velocity.Y = SlideStartVelocity.Y;
}

void UGGMovementComponent::StopSlide()
{
	bIsSliding = false;
	SlideDirection = FVector::ZeroVector;
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
		StopSlide();
		Super::CalcVelocity(DeltaTime, Friction, bFluid, BrakingDeceleration);
		return;
	}

	const FVector AccelDirection = Acceleration.GetSafeNormal2D();
	const bool bHasAcceleration = !AccelDirection.IsNearlyZero();

	FVector HorizontalVelocity = FVector(Velocity.X, Velocity.Y, 0.0f);
	const float MaxSpeed = GetMaxSpeed();

	if (IsMovingOnGround())
	{
		if (bIsSliding)
		{
			const float InitialSpeed = HorizontalVelocity.Size();
			const bool bMaintainingSlidePosture = IsCrouching() || bWantsToCrouch;
			if (InitialSpeed <= KINDA_SMALL_NUMBER || !bMaintainingSlidePosture)
			{
				StopSlide();
			}
			else
			{
				const float SpeedDrop = (SlideBrakingDecelerationCmPerSec2 + (InitialSpeed * SlideFrictionAmount)) * DeltaTime;
				const float NewSpeed = FMath::Max(InitialSpeed - SpeedDrop, 0.0f);

				if (InitialSpeed > KINDA_SMALL_NUMBER)
				{
					SlideDirection = HorizontalVelocity.GetSafeNormal2D();
				}
				if (SlideDirection.IsNearlyZero())
				{
					SlideDirection = UpdatedComponent ? UpdatedComponent->GetForwardVector().GetSafeNormal2D() : FVector::ForwardVector;
				}

				if (bHasAcceleration)
				{
					const float SteeringAlpha = FMath::Clamp(SlideSteeringResponsiveness * DeltaTime, 0.0f, 1.0f);
					SlideDirection = FMath::Lerp(SlideDirection, AccelDirection, SteeringAlpha).GetSafeNormal2D();
				}

				HorizontalVelocity = SlideDirection * NewSpeed;

				if (NewSpeed <= SlideStopSpeedCmPerSec)
				{
					HorizontalVelocity = FVector::ZeroVector;
					StopSlide();
				}

				Velocity.X = HorizontalVelocity.X;
				Velocity.Y = HorizontalVelocity.Y;
				return;
			}
		}

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

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
		return;
	}

	if (IsFalling())
	{
		if (bIsSliding)
		{
			StopSlide();
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

		Velocity.X = HorizontalVelocity.X;
		Velocity.Y = HorizontalVelocity.Y;
	}
}
