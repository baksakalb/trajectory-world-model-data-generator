#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"
#include "Components/ActorComponent.h"
#include "GrenadeTrajectoryComponent.generated.h"

class AGrenadeGameState;
class APlayerController;
class UCanvas;
class UGrenadeThrowerComponent;
struct FGrenadeArenaHit;

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

	/** Approximate on-screen width of the trajectory ribbon. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Rendering", meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float LineThickness = 2.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "0.0", Units = "s"))
	float DrawDurationSeconds = 0.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	TEnumAsByte<ESceneDepthPriorityGroup> DepthPriority = SDPG_World;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory", meta = (ClampMin = "1.0", Units = "cm"))
	float MaxRenderSegmentLengthCm = 20.0f;

	/** Minimum visual subdivisions between simulation samples. This smooths only the rendered curve. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Rendering", meta = (ClampMin = "1", ClampMax = "4"))
	int32 MinRenderSubstepsPerSimulationStep = 2;

	/**
	 * Optional extra CPU occlusion filter. World-depth rendering already hides the curve behind opaque geometry;
	 * leave this disabled unless an unusual translucent occluder needs collision-based hiding.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Visibility")
	bool bHideNonVisibleSegments = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Visibility")
	bool bHideBelowFloorSegments = true;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Visibility")
	TEnumAsByte<ECollisionChannel> VisibilityTraceChannel = ECC_Visibility;

	/** Allows an endpoint resting on collision geometry to remain visible when the optional CPU filter is enabled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Visibility", meta = (ClampMin = "0.0", ClampMax = "100.0", Units = "cm"))
	float VisibilityToleranceCm = 10.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FLinearColor AvailableColor = FLinearColor(0.1f, 1.0f, 0.1f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory")
	FLinearColor CooldownColor = FLinearColor(1.0f, 0.15f, 0.15f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Explosion")
	FLinearColor ExplosionTipColor = FLinearColor(1.0f, 0.5f, 0.0f, 1.0f);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Trajectory|Explosion", meta = (ClampMin = "1.0", Units = "cm"))
	float ExplosionTipSizeCm = 6.0f;

	UFUNCTION(BlueprintCallable, Category = "Trajectory")
	void SetAimModeActive(bool bActive);

	UFUNCTION(BlueprintPure, Category = "Trajectory")
	bool IsAimModeActive() const { return bAimModeActive; }

	/** Draws the cached trajectory after the 3D scene so it is unaffected by TAA and motion blur. */
	void DrawTrajectoryOverlay(UCanvas* Canvas, APlayerController* PlayerController) const;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	void ClearHighlightedTiles();
	void ClearTrajectoryVisual();
	void SyncHighlightedTiles(const TSet<int32>& DesiredArenaObjectIds);
	FGrenadeArenaHit ResolveArenaHit(const FHitResult& Hit) const;
	void AppendIgnoredArenaObject(int32 ArenaObjectId, FCollisionQueryParams& QueryParams) const;
	AGrenadeGameState* GetArenaGameState() const;
	void DrawPredictedPath();
	bool ResolveViewLocation(FVector& OutViewLocation) const;
	bool ResolveFloorZ(float& OutFloorZ);
	bool IsPointVisibleFromView(const FVector& ViewLocation, const FVector& Point, AActor* OwnerActor, UWorld* World) const;

	TWeakObjectPtr<UGrenadeThrowerComponent> ThrowerComponent;
	TSet<int32> HighlightedArenaObjectIds;
	TArray<TArray<FVector>> CachedTrajectoryRuns;
	FLinearColor CachedTrajectoryColor = FLinearColor::White;
	bool bHasCachedExplosionTip = false;
	FVector CachedExplosionTip = FVector::ZeroVector;
	bool bAimModeActive = false;
	bool bHasLastKnownFloorZ = false;
	float LastKnownFloorZ = 0.0f;
};
