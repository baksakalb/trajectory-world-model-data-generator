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
	float GroundAcceleration = 30000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Ground", meta = (ClampMin = "0.0"))
	float GroundFrictionAmount = 7.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Ground", meta = (ClampMin = "0.0", Units = "cm/s"))
	float GroundBrakingDeceleration = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float AirAcceleration = 12000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AirSpeedCapCmPerSec = 1500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float NonHopAirSpeedCapRunSpeedScalar = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s^2"))
	float AirOppositeInputBrakeDecelerationCmPerSec2 = 2600.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AirInputKickStartSpeedCmPerSec = 340.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", Units = "cm/s"))
	float AirInputKickStartThresholdCmPerSec = 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Air", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float AirInputAccelerationScale = 0.18f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Jump", meta = (ClampMin = "0.0", Units = "cm/s"))
	float JumpVelocityCmPerSec = 550.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Jump", meta = (ClampMin = "0.1"))
	float JumpGravityScale = 2.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.01", Units = "s"))
	float CrouchHopLandingQualifyWindowSeconds = 0.45f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.01", Units = "s"))
	float CrouchHopPostLandJumpWindowSeconds = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CrouchHopMinSpeedCmPerSec = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "1.0"))
	float CrouchHopBoostSpeedScalar = 1.16f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CrouchHopBoostAdditiveCmPerSec = 170.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CrouchHopSpeedCapCmPerSec = 2100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float CrouchHopRedirectStrength = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Steering", meta = (ClampMin = "0.0"))
	float SteerResponsivenessGround = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Steering", meta = (ClampMin = "0.0"))
	float SteerResponsivenessAir = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Steering", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float LookOnlySteerMultiplier = 0.6f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Steering", meta = (ClampMin = "0.0", Units = "cm/s"))
	float ReverseInputBleedCmPerSec = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.0", Units = "cm/s"))
	float CrouchHopChainAdditiveIncrementCmPerSec = 100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.01", Units = "s"))
	float CrouchHopChainWindowSeconds = 1.2f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Crouch Hop", meta = (ClampMin = "0.01", Units = "s"))
	float CrouchHopFeedbackDurationSeconds = 0.3f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.0", Units = "cm/s"))
	float WalkSpeedCmPerSec = 1100.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float AimWalkSpeedScalar = 0.7f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.1", ClampMax = "1.0"))
	float CrouchSpeedScalar = 0.8f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Movement|Tuning", meta = (ClampMin = "0.0", Units = "cm/s"))
	float BunnyHopSpeedCapCmPerSec = 1900.0f;

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void NotifyCrouchPressed();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void NotifyCrouchReleased();

	UFUNCTION(BlueprintCallable, Category = "Movement")
	bool TryConsumeCrouchJumpBoost();

	UFUNCTION(BlueprintCallable, Category = "Movement|Crouch Hop")
	void CancelCrouchHopChain();

	UFUNCTION(BlueprintPure, Category = "Movement|Crouch Hop")
	bool IsCrouchHopChainActive() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsCrouchHopFeedbackActive() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	int32 GetCrouchHopChainCount() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	float GetCurrentHorizontalSpeedCmPerSec() const;

	UFUNCTION(BlueprintCallable, Category = "Movement")
	void SetAimMode(bool bEnableAimMode);

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsAimModeActive() const { return bAimMode; }

	virtual float GetMaxSpeed() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual FVector GetFallingLateralAcceleration(float DeltaTime) override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;
	virtual void OnMovementModeChanged(EMovementMode PreviousMovementMode, uint8 PreviousCustomMode = 0) override;

private:
	bool bAimMode = false;
	bool bShiftHeld = false;
	float LastShiftPressTimeSeconds = -1.0f;
	float LastShiftReleaseTimeSeconds = -1.0f;
	float LastLandingTimeSeconds = -1.0f;
	float BoostJumpWindowEndTimeSeconds = -1.0f;
	float LastCrouchHopTimeSeconds = -1.0f;
	int32 CrouchHopChainCount = 0;

	bool IsWithinWindow(float CurrentTimeSeconds, float EventTimeSeconds, float WindowSeconds) const;
	void ArmCrouchHopWindow(float CurrentTimeSeconds);
	FVector GetFacingDirection2D() const;
	FVector ComputeDesiredSteerDirection2D(bool bHasInput, const FVector& InputDir) const;
	void ApplySteeringAndReverseBleed(FVector& HorizontalVelocity, const FVector& DesiredDir, bool bHasInput, bool bIsAir, float DeltaTime) const;
	FVector ResolveBoostDirection(const FVector& HorizontalVelocity) const;
};
