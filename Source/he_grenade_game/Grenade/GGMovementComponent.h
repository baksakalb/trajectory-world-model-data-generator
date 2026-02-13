#pragma once

#include "CoreMinimal.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GGMovementComponent.generated.h"

/**
 * Quake-like movement profile tuned for sharp accel, air strafe, and bhop retention.
 */
UCLASS(ClassGroup = (Movement), BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API UGGMovementComponent : public UCharacterMovementComponent
{
	GENERATED_BODY()

public:
	UGGMovementComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Ground", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float GroundAcceleration = 24000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Ground", meta = (ClampMin = "0.0"))
	float GroundFrictionAmount = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Ground", meta = (ClampMin = "0.0", Units = "cm/s"))
	float GroundBrakingDeceleration = 2200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float AirAcceleration = 7000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AirSpeedCapCmPerSec = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float JumpVelocityCmPerSec = 570.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Jump", meta = (ClampMin = "0.1"))
	float JumpGravityScale = 1.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Slide", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlideEnterMinSpeedCmPerSec = 550.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Slide", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float SlideBrakingDecelerationCmPerSec2 = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideFrictionAmount = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Slide", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SlideStopSpeedCmPerSec = 140.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Slide", meta = (ClampMin = "0.0"))
	float SlideSteeringResponsiveness = 1.5f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkSpeedCmPerSec = 700.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.0", Units = "cm/s"))
	float SprintSpeedCmPerSec = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AimWalkSpeedScalar = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float CrouchSpeedScalar = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.0", Units = "cm/s"))
	float BunnyHopSpeedCapCmPerSec = 1900.0f;

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetSprinting(bool bEnableSprint);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetAimMode(bool bEnableAimMode);

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void TryStartSlide();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void StopSlide();

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprintAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprinting() const { return bWantsSprint; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsAimModeActive() const { return bAimMode; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSliding() const { return bIsSliding; }

	virtual float GetMaxSpeed() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

private:
	bool bWantsSprint = false;
	bool bAimMode = false;
	bool bIsSliding = false;
	FVector SlideDirection = FVector::ZeroVector;
};
