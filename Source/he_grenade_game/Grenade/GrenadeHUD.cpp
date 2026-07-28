#include "Grenade/GrenadeHUD.h"

#include "Engine/Canvas.h"
#include "Engine/Engine.h"
#include "Engine/Font.h"
#include "CanvasItem.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Grenade/GGMovementComponent.h"
#include "Grenade/GrenadeTrajectoryComponent.h"
#include "he_grenade_gameCharacter.h"
#include "he_grenade_gameGameMode.h"
#include "GrenadeGameState.h"

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

	if (const UGrenadeTrajectoryComponent* Trajectory = Character->GetGrenadeTrajectoryComponent())
	{
		Trajectory->DrawTrajectoryOverlay(Canvas, PC);
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;

	if (!Character->IsAimModeActive())
	{
		const FLinearColor CrosshairColor = Character->IsGrenadeStateGreen() ? AvailableColor : CooldownColor;

		DrawLine(CenterX - CrosshairSize, CenterY, CenterX + CrosshairSize, CenterY, CrosshairColor, CrosshairThickness);
		DrawLine(CenterX, CenterY - CrosshairSize, CenterX, CenterY + CrosshairSize, CrosshairColor, CrosshairThickness);
	}

	float HorizontalSpeedCmPerSec = FVector(Character->GetVelocity().X, Character->GetVelocity().Y, 0.0f).Size();
	bool bHopActive = false;
	int32 HopChainCount = 0;
	if (const UGGMovementComponent* GGMovement = Cast<UGGMovementComponent>(Character->GetCharacterMovement()))
	{
		HorizontalSpeedCmPerSec = GGMovement->GetCurrentHorizontalSpeedCmPerSec();
		bHopActive = GGMovement->IsCrouchHopFeedbackActive();
		HopChainCount = GGMovement->GetCrouchHopChainCount();
	}

	UFont* HudFont = GEngine ? GEngine->GetSmallFont() : nullptr;
	if (!HudFont)
	{
		return;
	}

	const FVector2D StatusOrigin(22.0f, Canvas->ClipY - 76.0f);
	const FLinearColor HopColor = bHopActive ? HopActiveColor : HopInactiveColor;
	const FString HopText = bHopActive
		? FString::Printf(TEXT("HOP x%d"), FMath::Max(1, HopChainCount))
		: TEXT("HOP");
	FCanvasTextItem HopTextItem(StatusOrigin, FText::FromString(HopText), HudFont, HopColor);
	HopTextItem.Scale = FVector2D(StatusTextScale, StatusTextScale);
	HopTextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(HopTextItem);

	const FString SpeedText = FString::Printf(TEXT("Speed %.0f"), HorizontalSpeedCmPerSec);
	FCanvasTextItem SpeedTextItem(
		FVector2D(StatusOrigin.X + 120.0f, StatusOrigin.Y),
		FText::FromString(SpeedText),
		HudFont,
		SpeedTextColor);
	SpeedTextItem.Scale = FVector2D(StatusTextScale, StatusTextScale);
	SpeedTextItem.EnableShadow(FLinearColor::Black);
	Canvas->DrawItem(SpeedTextItem);

	const AGrenadeGameState* GrenadeGameState = GetWorld() ? GetWorld()->GetGameState<AGrenadeGameState>() : nullptr;
	if (GrenadeGameState && GrenadeGameState->IsFloorCollapseActive())
	{
		float SpeedTextWidth = 0.0f;
		float SpeedTextHeight = 0.0f;
		Canvas->StrLen(HudFont, SpeedText, SpeedTextWidth, SpeedTextHeight);

		const int32 SecondsRemaining =
			FMath::Max(0, FMath::CeilToInt(GrenadeGameState->GetFloorCollapseTimeRemaining()));
		const FString TileTimerText = FString::Printf(TEXT("Tiles %d"), SecondsRemaining);
		const FLinearColor TimerColor = FLinearColor::LerpUsingHSV(
			SpeedTextColor,
			TileTimerWarningColor,
			GrenadeGameState->GetFloorCollapseProgress());
		FCanvasTextItem TileTimerTextItem(
			FVector2D(
				StatusOrigin.X + 120.0f + (SpeedTextWidth * StatusTextScale) + 28.0f,
				StatusOrigin.Y),
			FText::FromString(TileTimerText),
			HudFont,
			TimerColor);
		TileTimerTextItem.Scale = FVector2D(StatusTextScale, StatusTextScale);
		TileTimerTextItem.EnableShadow(FLinearColor::Black);
		Canvas->DrawItem(TileTimerTextItem);
	}
}
