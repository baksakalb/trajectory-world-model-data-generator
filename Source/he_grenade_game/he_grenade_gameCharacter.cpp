// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/InputComponent.h"
#include "EnhancedInputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameModeBase.h"
#include "GameFramework/PlayerController.h"
#include "InputActionValue.h"
#include "InputCoreTypes.h"
#include "Grenade/GGMovementComponent.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "Grenade/GrenadeTrajectoryComponent.h"
#include "GrenadeGameState.h"
#include "GrenadePlayerState.h"
#include "he_grenade_game.h"
#include "he_grenade_gameGameMode.h"

Ahe_grenade_gameCharacter::Ahe_grenade_gameCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGGMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	bReplicates = true;

	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Grenade systems
	GrenadeThrowerComponent = CreateDefaultSubobject<UGrenadeThrowerComponent>(TEXT("GrenadeThrowerComponent"));
	GrenadeTrajectoryComponent = CreateDefaultSubobject<UGrenadeTrajectoryComponent>(TEXT("GrenadeTrajectoryComponent"));

	// Configure character visuals
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetCapsuleComponent()->SetCapsuleSize(34.0f, 96.0f);

	// Crouch support
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	GetCharacterMovement()->SetCrouchedHalfHeight(88.0f);

	// Keep jump taps consistent and non-floaty by disabling hold-to-extend behavior.
	JumpMaxHoldTime = 0.0f;
}

void Ahe_grenade_gameCharacter::BeginPlay()
{
	Super::BeginPlay();

	// Runtime hardening in case Blueprint defaults override constructor-time crouch flags.
	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->GetNavAgentPropertiesRef().bCanCrouch = true;

		if (const UGGMovementComponent* GGMovement = Cast<UGGMovementComponent>(Movement))
		{
			Movement->JumpZVelocity = GGMovement->JumpVelocityCmPerSec;
			Movement->GravityScale = GGMovement->JumpGravityScale;
		}
	}

	if (FirstPersonMesh)
	{
		StandingMeshRelativeLocation = FirstPersonMesh->GetRelativeLocation();
		CurrentCrouchCameraOffsetCm = 0.0f;
	}
}

void Ahe_grenade_gameCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	RefreshCrouchFromInput();
	UpdateCrouchCamera(DeltaSeconds);
}

void Ahe_grenade_gameCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Started, this, &Ahe_grenade_gameCharacter::DoJumpStart);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &Ahe_grenade_gameCharacter::DoJumpEnd);

		// Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &Ahe_grenade_gameCharacter::MoveInput);

		// Looking/Aiming
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &Ahe_grenade_gameCharacter::LookInput);
		EnhancedInputComponent->BindAction(MouseLookAction, ETriggerEvent::Triggered, this, &Ahe_grenade_gameCharacter::LookInput);
	}
	else
	{
		UE_LOG(Loghe_grenade_game, Error, TEXT("'%s' Failed to find an Enhanced Input Component!"), *GetNameSafe(this));
	}

	if (PlayerInputComponent)
	{
		// Action bindings that intentionally avoid requiring additional IA/IMC assets.
		PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Pressed, this, &Ahe_grenade_gameCharacter::DoThrowPressed);
		PlayerInputComponent->BindKey(EKeys::LeftMouseButton, IE_Released, this, &Ahe_grenade_gameCharacter::DoThrowReleased);

		PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Pressed, this, &Ahe_grenade_gameCharacter::DoAimStart);
		PlayerInputComponent->BindKey(EKeys::RightMouseButton, IE_Released, this, &Ahe_grenade_gameCharacter::DoAimEnd);

		PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Pressed, this, &Ahe_grenade_gameCharacter::DoTrajectoryHeightStart);
		PlayerInputComponent->BindKey(EKeys::LeftControl, IE_Released, this, &Ahe_grenade_gameCharacter::DoTrajectoryHeightEnd);

		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Pressed, this, &Ahe_grenade_gameCharacter::DoCrouchStart);
		PlayerInputComponent->BindKey(EKeys::LeftShift, IE_Released, this, &Ahe_grenade_gameCharacter::DoCrouchEnd);
	}
}

void Ahe_grenade_gameCharacter::MoveInput(const FInputActionValue& Value)
{
	const FVector2D MovementVector = Value.Get<FVector2D>();
	DoMove(MovementVector.X, MovementVector.Y);
}

void Ahe_grenade_gameCharacter::LookInput(const FInputActionValue& Value)
{
	const FVector2D LookAxisVector = Value.Get<FVector2D>();
	DoAim(LookAxisVector.X, LookAxisVector.Y);
}

void Ahe_grenade_gameCharacter::DoAim(float Yaw, float Pitch)
{
	if (GetController())
	{
		AddControllerYawInput(Yaw);
		AddControllerPitchInput(Pitch);
	}
}

void Ahe_grenade_gameCharacter::DoMove(float Right, float Forward)
{
	if (!IsGameplayInputAllowed())
	{
		return;
	}

	if (UGGMovementComponent* MovementComponent = GetGGMovementComponent())
	{
		if (MovementComponent->IsCrouchHopChainActive())
		{
			constexpr float HopBreakAxisThreshold = 0.2f;
			const bool bHopBreakInput = FMath::Abs(Right) > HopBreakAxisThreshold || Forward < -HopBreakAxisThreshold;
			if (bHopBreakInput)
			{
				MovementComponent->CancelCrouchHopChain();
			}
		}
	}

	if (GetController())
	{
		AddMovementInput(GetActorRightVector(), Right);
		AddMovementInput(GetActorForwardVector(), Forward);
	}
}

void Ahe_grenade_gameCharacter::DoJumpStart()
{
	if (!IsGameplayInputAllowed())
	{
		return;
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		Movement->bWantsToCrouch = false;
	}

	if (bIsCrouched)
	{
		UnCrouch(false);
	}

	if (UGGMovementComponent* MovementComponent = GetGGMovementComponent())
	{
		MovementComponent->TryConsumeCrouchJumpBoost();
	}

	Jump();
}

void Ahe_grenade_gameCharacter::DoJumpEnd()
{
	StopJumping();
}

void Ahe_grenade_gameCharacter::DoThrowPressed()
{
	if (GrenadeThrowerComponent && IsGameplayInputAllowed())
	{
		GrenadeThrowerComponent->OnThrowPressed();
	}
}

void Ahe_grenade_gameCharacter::DoThrowReleased()
{
	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->OnThrowReleased();
	}
}

void Ahe_grenade_gameCharacter::OnMovementModeChanged(
	const EMovementMode PreviousMovementMode,
	const uint8 PreviousCustomMode)
{
	Super::OnMovementModeChanged(PreviousMovementMode, PreviousCustomMode);

	const UCharacterMovementComponent* Movement = GetCharacterMovement();
	const AGrenadeGameState* GrenadeGameState =
		GetWorld() ? GetWorld()->GetGameState<AGrenadeGameState>() : nullptr;
	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("CHARACTER_MOVEMENT_MODE role=%d character=%s previous=%d current=%d arena_revision=%d"),
		static_cast<int32>(GetLocalRole()),
		*GetNameSafe(this),
		static_cast<int32>(PreviousMovementMode),
		Movement ? static_cast<int32>(Movement->MovementMode) : INDEX_NONE,
		GrenadeGameState ? GrenadeGameState->GetArenaStateRevision() : INDEX_NONE);
}

void Ahe_grenade_gameCharacter::DoTrajectoryHeightStart()
{
	if (!IsGameplayInputAllowed())
	{
		return;
	}

	if (UGGMovementComponent* MovementComponent = GetGGMovementComponent())
	{
		MovementComponent->CancelCrouchHopChain();
	}

	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->SetControlArcRaiseInputActive(true);
	}
}

void Ahe_grenade_gameCharacter::DoTrajectoryHeightEnd()
{
	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->SetControlArcRaiseInputActive(false);
	}
}

void Ahe_grenade_gameCharacter::DoAimStart()
{
	if (!IsGameplayInputAllowed())
	{
		return;
	}

	bAimModeActive = true;

	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->SetAimModeActive(true);
	}
	if (GrenadeTrajectoryComponent)
	{
		GrenadeTrajectoryComponent->SetAimModeActive(true);
	}
}

void Ahe_grenade_gameCharacter::DoAimEnd()
{
	bAimModeActive = false;

	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->SetAimModeActive(false);
	}
	if (GrenadeTrajectoryComponent)
	{
		GrenadeTrajectoryComponent->SetAimModeActive(false);
	}
}

void Ahe_grenade_gameCharacter::DoCrouchStart()
{
	if (!IsGameplayInputAllowed())
	{
		return;
	}

	bCrouchInputHeld = true;

	if (UGGMovementComponent* MovementComponent = GetGGMovementComponent())
	{
		MovementComponent->NotifyCrouchPressed();
	}

	RefreshCrouchFromInput();
}

void Ahe_grenade_gameCharacter::DoCrouchEnd()
{
	bCrouchInputHeld = false;

	if (UGGMovementComponent* MovementComponent = GetGGMovementComponent())
	{
		MovementComponent->NotifyCrouchReleased();
	}

	RefreshCrouchFromInput();
}

void Ahe_grenade_gameCharacter::FellOutOfWorld(const UDamageType& DmgType)
{
	if (!HasAuthority())
	{
		return;
	}

	if (Ahe_grenade_gameGameMode* GameMode =
		GetWorld() ? Cast<Ahe_grenade_gameGameMode>(GetWorld()->GetAuthGameMode()) : nullptr)
	{
		GameMode->EliminatePlayer(GetController());
		return;
	}

	Super::FellOutOfWorld(DmgType);
}

void Ahe_grenade_gameCharacter::RefreshCrouchFromInput()
{
	bool bWantsCrouchInput = IsGameplayInputAllowed() && bCrouchInputHeld;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		bWantsCrouchInput = IsGameplayInputAllowed() && (bWantsCrouchInput
			|| PC->IsInputKeyDown(EKeys::LeftShift));
	}

	if (UCharacterMovementComponent* Movement = GetCharacterMovement())
	{
		const bool bShouldCrouch = bWantsCrouchInput && Movement->IsMovingOnGround();

		Movement->GetNavAgentPropertiesRef().bCanCrouch = true;
		Movement->bWantsToCrouch = bShouldCrouch;

		if (bShouldCrouch)
		{
			Crouch(false);
		}
		else
		{
			UnCrouch(false);
		}
	}
}

void Ahe_grenade_gameCharacter::UpdateCrouchCamera(float DeltaSeconds)
{
	if (!FirstPersonMesh)
	{
		return;
	}

	float TargetOffsetCm = bIsCrouched ? -FMath::Max(0.0f, CrouchCameraDropCm) : 0.0f;
	const float InterpSpeed = FMath::Max(1.0f, CrouchCameraInterpSpeed);

	CurrentCrouchCameraOffsetCm = FMath::FInterpTo(CurrentCrouchCameraOffsetCm, TargetOffsetCm, DeltaSeconds, InterpSpeed);

	// Offset the entire first-person mesh in Z. Since it's attached to GetMesh()
	// (which has at most a yaw rotation), local Z is always world-vertical.
	// This avoids the coordinate-space mismatch of offsetting the camera
	// inside its rotated socket.
	FVector NewLocation = StandingMeshRelativeLocation;
	NewLocation.Z += CurrentCrouchCameraOffsetCm;
	FirstPersonMesh->SetRelativeLocation(NewLocation);
}

UGGMovementComponent* Ahe_grenade_gameCharacter::GetGGMovementComponent() const
{
	return Cast<UGGMovementComponent>(GetCharacterMovement());
}

bool Ahe_grenade_gameCharacter::IsGameplayInputAllowed() const
{
	const UWorld* World = GetWorld();
	const AGrenadeGameState* GrenadeGameState =
		World ? World->GetGameState<AGrenadeGameState>() : nullptr;
	const AGrenadePlayerState* GrenadePlayerState =
		GetPlayerState<AGrenadePlayerState>();
	return GrenadeGameState
		&& GrenadeGameState->GetGrenadeMatchPhase() == EGGMatchPhase::InProgress
		&& GrenadePlayerState
		&& GrenadePlayerState->IsArenaReady()
		&& GrenadePlayerState->IsAlive();
}

bool Ahe_grenade_gameCharacter::IsGrenadeStateGreen() const
{
	return GrenadeThrowerComponent ? GrenadeThrowerComponent->IsStateGreen() : false;
}
