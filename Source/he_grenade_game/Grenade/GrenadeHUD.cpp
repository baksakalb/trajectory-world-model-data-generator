#include "Grenade/GrenadeHUD.h"

#include "Engine/Canvas.h"

void AGrenadeHUD::DrawHUD()
{
	Super::DrawHUD();

	if (!Canvas)
	{
		return;
	}

	const float CenterX = Canvas->ClipX * 0.5f;
	const float CenterY = Canvas->ClipY * 0.5f;
	const float HalfSize = FMath::Max(1.0f, CrosshairHalfSizePixels);
	const float Thickness = FMath::Max(0.5f, CrosshairThicknessPixels);

	DrawLine(
		CenterX - HalfSize,
		CenterY,
		CenterX + HalfSize,
		CenterY,
		NeutralCrosshairColor,
		Thickness);
	DrawLine(
		CenterX,
		CenterY - HalfSize,
		CenterX,
		CenterY + HalfSize,
		NeutralCrosshairColor,
		Thickness);
}
