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

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprintAllowed() const;

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsSprinting() const { return bWantsSprint; }

	UFUNCTION(BlueprintPure, Category = "Movement")
	bool IsAimModeActive() const { return bAimMode; }

	virtual float GetMaxSpeed() const override;
	virtual bool CanCrouchInCurrentState() const override;
	virtual void CalcVelocity(float DeltaTime, float Friction, bool bFluid, float BrakingDeceleration) override;

private:
	bool bWantsSprint = false;
	bool bAimMode = false;
};
