#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Grenade/GrenadeSim.h"
#include "GrenadeThrowerComponent.generated.h"

class AGrenadeActor;

UENUM(BlueprintType)
enum class EGrenadeThrowState : uint8
{
	Ready UMETA(DisplayName = "Ready"),
	Cooldown UMETA(DisplayName = "Cooldown")
};

USTRUCT(BlueprintType)
struct FGrenadeLaunchParams
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	FVector SpawnLocation = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	FVector InitialVelocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade", meta = (Units = "s"))
	float FuseSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	bool bCanThrowNow = false;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnGrenadeStateChanged, EGrenadeThrowState, NewState);

UCLASS(ClassGroup = (Grenade), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class HE_GRENADE_GAME_API UGrenadeThrowerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrenadeThrowerComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Classes")
	TSubclassOf<AGrenadeActor> GrenadeClass;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Lifetime", meta = (ClampMin = "0.1", Units = "s"))
	float FuseSeconds = 3.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Timing", meta = (ClampMin = "0.0", Units = "s"))
	float ReloadCooldownSeconds = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Charge", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MinThrowSpeedCmPerSec = 450.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Charge", meta = (ClampMin = "0.0", Units = "cm/s"))
	float MaxThrowSpeedCmPerSec = 1400.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Charge", meta = (ClampMin = "0.01", Units = "s"))
	float ChargeDurationSeconds = 0.75f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw", meta = (ClampMin = "0.0", ClampMax = "1.5"))
	float ThrowInheritVelocityFactor = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw", meta = (ClampMin = "100.0", Units = "cm"))
	float TrajectoryTraceDistanceCm = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw", meta = (Units = "cm"))
	FVector ThrowSpawnOffset = FVector(30.0f, 10.0f, -10.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Crouch")
	bool bEnableCrouchThrowAdjustment = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Crouch", meta = (ClampMin = "-45.0", ClampMax = "45.0", Units = "deg"))
	float CrouchThrowPitchOffsetDegrees = -8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Crouch", meta = (ClampMin = "0.0", Units = "cm"))
	float CrouchThrowSpawnDropCm = 18.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Arc Raise")
	bool bEnableControlArcRaise = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Throw|Arc Raise", meta = (ClampMin = "0.0", ClampMax = "60.0", Units = "deg"))
	float ControlArcRaiseMaxPitchOffsetDegrees = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation")
	FGrenadeSimConfig SimulationConfig;

	UPROPERTY(BlueprintAssignable, Category = "Grenade")
	FOnGrenadeStateChanged OnGrenadeStateChanged;

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void OnThrowPressed();

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void OnThrowReleased();

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void SetAimModeActive(bool bActiveAimMode);

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void SetControlArcRaiseInputActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsAimModeActive() const { return bAimModeActive; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	float GetCurrentChargeAlpha() const;

	UFUNCTION(BlueprintPure, Category = "Grenade")
	float GetCurrentThrowSpeedCmPerSec() const;

	UFUNCTION(BlueprintPure, Category = "Grenade")
	float GetRemainingFuseSeconds() const;

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsStateGreen() const;

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsThrowAvailableNow() const { return ThrowState == EGrenadeThrowState::Ready; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	EGrenadeThrowState GetThrowState() const { return ThrowState; }

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	bool BuildLaunchParams(FGrenadeLaunchParams& OutLaunchParams) const;

	UFUNCTION(BlueprintPure, Category = "Grenade")
	const FGrenadeSimConfig& GetSimulationConfig() const { return SimulationConfig; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void TryThrowGrenade(const FGrenadeLaunchParams* LaunchParamsOverride = nullptr);
	void EnterCooldown();
	void ExitCooldown();
	void SetThrowState(EGrenadeThrowState NewState);
	void DetonateInHand();
	float GetHeldDurationSeconds() const;
	float GetControlArcRaiseHeldDurationSeconds() const;
	float GetControlArcRaiseAlpha() const;
	bool IsControlArcRaiseContextActive() const;

	bool BuildCurrentLaunchParams(FGrenadeLaunchParams& OutLaunchParams) const;
	bool ComputeLaunchTransform(FVector& OutSpawnLocation, FVector& OutInitialVelocity, float ThrowSpeedCmPerSec) const;

	FTimerHandle CooldownTimerHandle;

	EGrenadeThrowState ThrowState = EGrenadeThrowState::Ready;
	bool bThrowInputHeld = false;
	bool bAimModeActive = false;
	bool bControlArcRaiseInputHeld = false;
	bool bDetonatedInHandThisHold = false;
	bool bHasHeldLaunchSnapshot = false;
	float HoldStartWorldTimeSeconds = 0.0f;
	float ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	float LastHeldDurationSeconds = 0.0f;
	FGrenadeLaunchParams HeldLaunchSnapshot;
};
