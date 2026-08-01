// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "Logging/LogMacros.h"
#include "he_grenade_gameCharacter.generated.h"

class UInputComponent;
class USkeletalMeshComponent;
class UCameraComponent;
class UInputAction;
class UGGMovementComponent;
class UGrenadeThrowerComponent;
class UGrenadeTrajectoryComponent;
struct FInputActionValue;

DECLARE_LOG_CATEGORY_EXTERN(LogTemplateCharacter, Log, All);

/**
 * Base first-person character used by project variants.
 */
UCLASS(abstract)
class Ahe_grenade_gameCharacter : public ACharacter
{
	GENERATED_BODY()

	/** Pawn mesh: first person view (arms; seen only by self) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	USkeletalMeshComponent* FirstPersonMesh;

	/** First person camera */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	UCameraComponent* FirstPersonCameraComponent;

	/** Grenade throw state machine component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGrenadeThrowerComponent> GrenadeThrowerComponent;

	/** Trajectory prediction component */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UGrenadeTrajectoryComponent> GrenadeTrajectoryComponent;

protected:
	/** Jump Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* JumpAction;

	/** Move Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MoveAction;

	/** Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* LookAction;

	/** Mouse Look Input Action */
	UPROPERTY(EditAnywhere, Category = "Input")
	UInputAction* MouseLookAction;

	bool bAimModeActive = false;
	bool bCrouchInputHeld = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Crouch", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	float CrouchCameraDropCm = 30.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Crouch", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float CrouchCameraInterpSpeed = 14.0f;

	/** Deterministic keyboard-camera rates used by Curriculum V1. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Camera", meta = (ClampMin = "1.0", Units = "deg/s"))
	float ArrowKeyYawRateDegreesPerSecond = 90.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Camera", meta = (ClampMin = "1.0", Units = "deg/s"))
	float ArrowKeyPitchRateDegreesPerSecond = 75.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Camera", meta = (ClampMin = "-89.0", ClampMax = "0.0", Units = "deg"))
	float MinimumCameraPitchDegrees = -85.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Camera", meta = (ClampMin = "0.0", ClampMax = "89.0", Units = "deg"))
	float MaximumCameraPitchDegrees = 85.0f;

public:
	Ahe_grenade_gameCharacter(const FObjectInitializer& ObjectInitializer = FObjectInitializer::Get());

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

	/** Called from Input Actions for movement input */
	void MoveInput(const FInputActionValue& Value);

	/** Called from Input Actions for looking input */
	void LookInput(const FInputActionValue& Value);

	/** Handles aim inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAim(float Yaw, float Pitch);

	/** Handles move inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoMove(float Right, float Forward);

	/** Handles jump start inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpStart();

	/** Handles jump end inputs from either controls or UI interfaces */
	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoJumpEnd();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoThrowPressed();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoThrowReleased();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoTrajectoryHeightStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoTrajectoryHeightEnd();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAimStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAimEnd();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoCrouchStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoCrouchEnd();

	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	void ProcessCurriculumMovementInput(float DeltaSeconds);
	void RefreshCrouchFromInput();
	void UpdateCrouchCamera(float DeltaSeconds);
	UGGMovementComponent* GetGGMovementComponent() const;

	FVector StandingMeshRelativeLocation = FVector::ZeroVector;
	float CurrentCrouchCameraOffsetCm = 0.0f;

public:
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }
	UGrenadeThrowerComponent* GetGrenadeThrowerComponent() const { return GrenadeThrowerComponent; }
	UGrenadeTrajectoryComponent* GetGrenadeTrajectoryComponent() const { return GrenadeTrajectoryComponent; }

	/**
	 * Enables deterministic externally supplied curriculum actions. The mask uses
	 * the canonical [W,A,S,D,Up,Down,Left,Right,Q,E] bit order.
	 */
	void SetCurriculumActionOverride(bool bEnabled, uint16 ActionMask);

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsAimModeActive() const { return bAimModeActive; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsGrenadeStateGreen() const;

private:
	bool bCurriculumActionOverrideEnabled = false;
	uint16 CurriculumActionOverrideMask = 0;
};
