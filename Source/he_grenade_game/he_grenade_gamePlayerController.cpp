// Copyright Epic Games, Inc. All Rights Reserved.


#include "he_grenade_gamePlayerController.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "InputMappingContext.h"
#include "he_grenade_gameCameraManager.h"
#include "Grenade/GrenadeHUD.h"
#include "GrenadePlayerState.h"
#include "Blueprint/UserWidget.h"
#include "he_grenade_game.h"
#include "Widgets/Input/SVirtualJoystick.h"

Ahe_grenade_gamePlayerController::Ahe_grenade_gamePlayerController()
{
	// set the player camera manager class
	PlayerCameraManagerClass = Ahe_grenade_gameCameraManager::StaticClass();
}

void Ahe_grenade_gamePlayerController::ConfirmArenaLayout(const int32 LayoutRevision, const int64 LayoutChecksum)
{
	if (IsLocalController())
	{
		ServerConfirmArenaLayout(LayoutRevision, LayoutChecksum);
	}
}

void Ahe_grenade_gamePlayerController::ServerConfirmArenaLayout_Implementation(
	const int32 LayoutRevision,
	const int64 LayoutChecksum)
{
	if (AGrenadePlayerState* GrenadePlayerState = GetPlayerState<AGrenadePlayerState>())
	{
		GrenadePlayerState->SetArenaReady(LayoutRevision, LayoutChecksum);
		UE_LOG(
			Loghe_grenade_game,
			Log,
			TEXT("Server accepted arena readiness from %s: revision %d checksum %lld."),
			*GetNameSafe(this),
			LayoutRevision,
			LayoutChecksum);
	}
}

void Ahe_grenade_gamePlayerController::BeginPlay()
{
	Super::BeginPlay();

	// Force gameplay HUD so BP game mode overrides do not hide grenade crosshair logic.
	ClientSetHUD(AGrenadeHUD::StaticClass());

	
	// only spawn touch controls on local player controllers
	if (ShouldUseTouchControls() && IsLocalPlayerController())
	{
		// spawn the mobile controls widget
		MobileControlsWidget = CreateWidget<UUserWidget>(this, MobileControlsWidgetClass);

		if (MobileControlsWidget)
		{
			// add the controls to the player screen
			MobileControlsWidget->AddToPlayerScreen(0);

		} else {

			UE_LOG(Loghe_grenade_game, Error, TEXT("Could not spawn mobile controls widget."));

		}

	}
}

void Ahe_grenade_gamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// only add IMCs for local player controllers
	if (IsLocalPlayerController())
	{
		// Add Input Mapping Context
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(GetLocalPlayer()))
		{
			for (UInputMappingContext* CurrentContext : DefaultMappingContexts)
			{
				Subsystem->AddMappingContext(CurrentContext, 0);
			}

			// only add these IMCs if we're not using mobile touch input
			if (!ShouldUseTouchControls())
			{
				for (UInputMappingContext* CurrentContext : MobileExcludedMappingContexts)
				{
					Subsystem->AddMappingContext(CurrentContext, 0);
				}
			}
		}
	}
	
}

bool Ahe_grenade_gamePlayerController::ShouldUseTouchControls() const
{
	// are we on a mobile platform? Should we force touch?
	return SVirtualJoystick::ShouldDisplayTouchInterface() || bForceTouchControls;
}
