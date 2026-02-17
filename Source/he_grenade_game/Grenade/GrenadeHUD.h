#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "GrenadeHUD.generated.h"

UCLASS(BlueprintType)
class HE_GRENADE_GAME_API AGrenadeHUD : public AHUD
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "1.0"))
	float CrosshairSize = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair", meta = (ClampMin = "0.1"))
	float CrosshairThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor AvailableColor = FLinearColor(0.1f, 1.0f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Crosshair")
	FLinearColor CooldownColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FLinearColor HopActiveColor = FLinearColor(0.15f, 1.0f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FLinearColor HopInactiveColor = FLinearColor(0.25f, 0.25f, 0.25f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status")
	FLinearColor SpeedTextColor = FLinearColor(0.95f, 0.95f, 0.95f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Status", meta = (ClampMin = "0.5", ClampMax = "3.0"))
	float StatusTextScale = 1.1f;

	virtual void DrawHUD() override;
};
