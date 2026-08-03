// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gameCharacter.h"

#include "Animation/AnimInstance.h"
#include "Camera/PlayerCameraManager.h"
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
#include "DataGenerator/CurriculumAction.h"
#include "Grenade/GGMovementComponent.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "Grenade/GrenadeTrajectoryComponent.h"
#include "he_grenade_game.h"

Ahe_grenade_gameCharacter::Ahe_grenade_gameCharacter(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer.SetDefaultSubobjectClass<UGGMovementComponent>(ACharacter::CharacterMovementComponentName))
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(55.f, 96.0f);

	// Create the first person mesh that will be viewed only by this character's owner
	FirstPersonMesh = CreateDefaultSubobject<USkeletalMeshComponent>(TEXT("First Person Mesh"));
	FirstPersonMesh->SetupAttachment(GetMesh());
	FirstPersonMesh->SetOnlyOwnerSee(true);
	FirstPersonMesh->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::FirstPerson;
	FirstPersonMesh->SetCollisionProfileName(FName("NoCollision"));
	FirstPersonMesh->SetHiddenInGame(true);
	FirstPersonMesh->SetCastShadow(false);

	// Create the Camera Component
	FirstPersonCameraComponent = CreateDefaultSubobject<UCameraComponent>(TEXT("First Person Camera"));
	FirstPersonCameraComponent->SetupAttachment(FirstPersonMesh, FName("head"));
	FirstPersonCameraComponent->SetRelativeLocationAndRotation(FVector(-2.8f, 5.89f, 0.0f), FRotator(0.0f, 90.0f, -90.0f));
	FirstPersonCameraComponent->bUsePawnControlRotation = true;
	FirstPersonCameraComponent->bEnableFirstPersonFieldOfView = true;
	FirstPersonCameraComponent->bEnableFirstPersonScale = true;
	FirstPersonCameraComponent->FirstPersonFieldOfView = 70.0f;
	FirstPersonCameraComponent->FirstPersonScale = 0.6f;

	// Curriculum V1 contains no grenade or trajectory component instances. Their
	// source remains in the repository for later curriculum branches.
	GrenadeThrowerComponent = nullptr;
	GrenadeTrajectoryComponent = nullptr;

	// Configure character visuals
	GetMesh()->SetOwnerNoSee(true);
	GetMesh()->FirstPersonPrimitiveType = EFirstPersonPrimitiveType::WorldSpaceRepresentation;
	GetMesh()->SetHiddenInGame(true);
	GetMesh()->SetCastShadow(false);
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
		FirstPersonMesh->SetVisibility(false, true);
		FirstPersonMesh->SetHiddenInGame(true, true);
		FirstPersonMesh->SetCastShadow(false);
	}

	if (USkeletalMeshComponent* ThirdPersonMesh = GetMesh())
	{
		ThirdPersonMesh->SetVisibility(false, true);
		ThirdPersonMesh->SetHiddenInGame(true, true);
		ThirdPersonMesh->SetCastShadow(false);
	}

	// Curriculum V1 keeps the complete future-curriculum components in source but
	// makes them inert and invisible at runtime.
	bAimModeActive = false;
	if (GrenadeThrowerComponent)
	{
		GrenadeThrowerComponent->SetAimModeActive(false);
		GrenadeThrowerComponent->SetComponentTickEnabled(false);
	}
	if (GrenadeTrajectoryComponent)
	{
		GrenadeTrajectoryComponent->bTrajectoryEnabled = false;
		GrenadeTrajectoryComponent->SetAimModeActive(false);
		GrenadeTrajectoryComponent->SetComponentTickEnabled(false);
	}
}

void Ahe_grenade_gameCharacter::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	ProcessCurriculumMovementInput(DeltaSeconds);
}

void Ahe_grenade_gameCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// Curriculum V1 reads only the canonical keyboard bits directly each frame.
	// No Enhanced Input action, mouse, jump, crouch, touch, or grenade binding is
	// installed.
}

void Ahe_grenade_gameCharacter::ProcessCurriculumMovementInput(const float DeltaSeconds)
{
	APlayerController* PlayerController = Cast<APlayerController>(GetController());
	if (!PlayerController || !PlayerController->IsLocalPlayerController())
	{
		return;
	}

	uint16 ActionMask = CurriculumActionOverrideMask;
	if (!bCurriculumActionOverrideEnabled)
	{
		ActionMask =
			(PlayerController->IsInputKeyDown(EKeys::W) ? CurriculumAction::W : 0)
			| (PlayerController->IsInputKeyDown(EKeys::A) ? CurriculumAction::A : 0)
			| (PlayerController->IsInputKeyDown(EKeys::S) ? CurriculumAction::S : 0)
			| (PlayerController->IsInputKeyDown(EKeys::D) ? CurriculumAction::D : 0)
			| (PlayerController->IsInputKeyDown(EKeys::Up) ? CurriculumAction::ArrowUp : 0)
			| (PlayerController->IsInputKeyDown(EKeys::Down) ? CurriculumAction::ArrowDown : 0)
			| (PlayerController->IsInputKeyDown(EKeys::Left) ? CurriculumAction::ArrowLeft : 0)
			| (PlayerController->IsInputKeyDown(EKeys::Right) ? CurriculumAction::ArrowRight : 0);
	}

	const float ForwardAxis = CurriculumAction::ForwardAxis(ActionMask);
	const float RightAxis = CurriculumAction::RightAxis(ActionMask);
	const float YawAxis = CurriculumAction::YawAxis(ActionMask);
	const float PitchAxis = CurriculumAction::PitchAxis(ActionMask);

	FRotator ControlRotation = PlayerController->GetControlRotation();
	ControlRotation.Yaw = FRotator::NormalizeAxis(
		ControlRotation.Yaw
		+ (YawAxis * FMath::Max(1.0f, ArrowKeyYawRateDegreesPerSecond) * DeltaSeconds));

	float SafeMinPitch = 0.0f;
	float SafeMaxPitch = 0.0f;
	GetCurriculumCameraPitchLimits(SafeMinPitch, SafeMaxPitch);
	ControlRotation.Pitch = FMath::Clamp(
		FRotator::NormalizeAxis(
			ControlRotation.Pitch
			+ (PitchAxis * GetCurriculumCameraPitchRateDegreesPerSecond() * DeltaSeconds)),
		SafeMinPitch,
		SafeMaxPitch);
	ControlRotation.Roll = 0.0f;
	PlayerController->SetControlRotation(ControlRotation);

	DoMove(RightAxis, ForwardAxis);
}

void Ahe_grenade_gameCharacter::GetCurriculumCameraPitchLimits(
	float& OutMinimum,
	float& OutMaximum) const
{
	OutMinimum = MinimumCameraPitchDegrees;
	OutMaximum = MaximumCameraPitchDegrees;
	if (const APlayerController* PlayerController =
			Cast<APlayerController>(GetController()))
	{
		if (const APlayerCameraManager* CameraManager =
				PlayerController->PlayerCameraManager)
		{
			OutMinimum = CameraManager->ViewPitchMin;
			OutMaximum = CameraManager->ViewPitchMax;
		}
	}
	if (OutMinimum > OutMaximum)
	{
		Swap(OutMinimum, OutMaximum);
	}
}

float Ahe_grenade_gameCharacter::GetCurriculumCameraPitchRateDegreesPerSecond() const
{
	return FMath::Max(1.0f, ArrowKeyPitchRateDegreesPerSecond);
}

void Ahe_grenade_gameCharacter::SetCurriculumActionOverride(
	const bool bEnabled,
	const uint16 ActionMask)
{
	bCurriculumActionOverrideEnabled = bEnabled;
	CurriculumActionOverrideMask =
		bEnabled ? (ActionMask & CurriculumAction::CanonicalMask) : 0;
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
	if (GetController())
	{
		const FRotator YawRotation(0.0f, GetController()->GetControlRotation().Yaw, 0.0f);
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		AddMovementInput(RightDirection, Right);
		AddMovementInput(ForwardDirection, Forward);
	}
}

void Ahe_grenade_gameCharacter::DoJumpStart()
{
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
	if (GrenadeThrowerComponent)
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

void Ahe_grenade_gameCharacter::DoTrajectoryHeightStart()
{
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
	AController* PawnController = GetController();
	Destroy();

	if (PawnController)
	{
		if (AGameModeBase* GameMode = GetWorld() ? GetWorld()->GetAuthGameMode() : nullptr)
		{
			GameMode->RestartPlayer(PawnController);
		}
	}
}

void Ahe_grenade_gameCharacter::RefreshCrouchFromInput()
{
	bool bWantsCrouchInput = bCrouchInputHeld;

	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		bWantsCrouchInput = bWantsCrouchInput
			|| PC->IsInputKeyDown(EKeys::LeftShift);
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

bool Ahe_grenade_gameCharacter::IsGrenadeStateGreen() const
{
	return GrenadeThrowerComponent ? GrenadeThrowerComponent->IsStateGreen() : false;
}
