#include "Grenade/GrenadeHUD.h"

#include "Engine/Canvas.h"
#include "he_grenade_gameCharacter.h"

void AGrenadeHUD::DrawHUD()
{
	Super::DrawHUD();

	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		return;
	}

	Ahe_grenade_gameCharacter* Character = Cast<Ahe_grenade_gameCharacter>(PC->GetPawn());
	if (!Character)
	{
		return;
	}

	if (Character->IsAimModeActive())
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	const FLinearColor CrosshairColor = Character->IsGrenadeStateGreen() ? AvailableColor : CooldownColor;

	DrawLine(CenterX - CrosshairSize, CenterY, CenterX + CrosshairSize, CenterY, CrosshairColor, CrosshairThickness);
	DrawLine(CenterX, CenterY - CrosshairSize, CenterX, CenterY + CrosshairSize, CrosshairColor, CrosshairThickness);
}
