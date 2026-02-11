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
	bool bSprintInputHeld = false;
	bool bCrouchInputHeld = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Crouch", meta = (AllowPrivateAccess = "true", ClampMin = "0.0", Units = "cm"))
	float CrouchCameraDropCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera|Crouch", meta = (AllowPrivateAccess = "true", ClampMin = "1.0"))
	float CrouchCameraInterpSpeed = 14.0f;

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
	virtual void DoAimStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoAimEnd();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoSprintStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoSprintEnd();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoCrouchStart();

	UFUNCTION(BlueprintCallable, Category = "Input")
	virtual void DoCrouchEnd();

	virtual void FellOutOfWorld(const class UDamageType& DmgType) override;

	virtual void SetupPlayerInputComponent(UInputComponent* InputComponent) override;

	void UpdateMovementStates();
	void RefreshCrouchFromInput();
	void UpdateCrouchCamera(float DeltaSeconds);
	UGGMovementComponent* GetGGMovementComponent() const;

	FVector StandingCameraRelativeLocation = FVector::ZeroVector;
	float CurrentCrouchCameraOffsetCm = 0.0f;

public:
	USkeletalMeshComponent* GetFirstPersonMesh() const { return FirstPersonMesh; }
	UCameraComponent* GetFirstPersonCameraComponent() const { return FirstPersonCameraComponent; }
	UGrenadeThrowerComponent* GetGrenadeThrowerComponent() const { return GrenadeThrowerComponent; }
	UGrenadeTrajectoryComponent* GetGrenadeTrajectoryComponent() const { return GrenadeTrajectoryComponent; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsAimModeActive() const { return bAimModeActive; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsGrenadeStateGreen() const;
};
