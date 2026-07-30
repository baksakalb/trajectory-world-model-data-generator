#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GrenadeHUD.generated.h"

/**
 * Minimal Curriculum V1 HUD: one permanent neutral crosshair and no status UI.
 * The historical class name is retained so existing Blueprint references remain
 * valid when opening the curriculum project.
 */
UCLASS(BlueprintType)
class HE_GRENADE_GAME_API AGrenadeHUD : public AHUD
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Crosshair", meta = (ClampMin = "1.0"))
	float CrosshairHalfSizePixels = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Crosshair", meta = (ClampMin = "0.5"))
	float CrosshairThicknessPixels = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Crosshair")
	FLinearColor NeutralCrosshairColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.0f);

	virtual void DrawHUD() override;
};
