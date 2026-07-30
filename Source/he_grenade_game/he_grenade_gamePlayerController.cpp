// Copyright Epic Games, Inc. All Rights Reserved.

#include "he_grenade_gamePlayerController.h"

#include "Grenade/GrenadeHUD.h"
#include "he_grenade_gameCameraManager.h"

Ahe_grenade_gamePlayerController::Ahe_grenade_gamePlayerController()
{
	PlayerCameraManagerClass = Ahe_grenade_gameCameraManager::StaticClass();
}

void Ahe_grenade_gamePlayerController::BeginPlay()
{
	Super::BeginPlay();

	ClientSetHUD(AGrenadeHUD::StaticClass());

	if (IsLocalPlayerController())
	{
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());
	}
}

void Ahe_grenade_gamePlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// Curriculum V1 deliberately installs no mapping context. The possessed
	// character reads only W/A/S/D and arrow-key state.
}

bool Ahe_grenade_gamePlayerController::ShouldUseTouchControls() const
{
	return false;
}
