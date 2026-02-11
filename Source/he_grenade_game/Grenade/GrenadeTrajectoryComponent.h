#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GrenadeTrajectoryComponent.generated.h"

class ABreakableTile;
class UGrenadeThrowerComponent;

UCLASS(ClassGroup = (Grenade), BlueprintType, Blueprintable, meta = (BlueprintSpawnableComponent))
class HE_GRENADE_GAME_API UGrenadeTrajectoryComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UGrenadeTrajectoryComponent();

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	bool bTrajectoryEnabled = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "4", ClampMax = "4096"))
	int32 MaxSimulationSteps = 512;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float LineThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "0.0", Units = "s"))
	float DrawDurationSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	TEnumAsByte<ESceneDepthPriorityGroup> DepthPriority = SDPG_Foreground;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRenderSegmentLengthCm = 20.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FLinearColor AvailableColor = FLinearColor(0.1f, 1.0f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FLinearColor CooldownColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void SetAimModeActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Trajectory")
	bool IsAimModeActive() const { return bAimModeActive; }

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	ABreakableTile* ResolveBreakableTile(const FHitResult& Hit) const;
	void DrawPredictedPath();

	TWeakObjectPtr<UGrenadeThrowerComponent> ThrowerComponent;
	bool bAimModeActive = false;
};
