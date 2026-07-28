#pragma once

#include "CoreMinimal.h"
#include "GrenadeSim.generated.h"

/**
 * Deterministic grenade sim tuning shared by runtime grenade and trajectory prediction.
 */
USTRUCT(BlueprintType)
struct FGrenadeSimConfig
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "0.001", Units = "s"))
	float FixedStepSeconds = 1.0f / 120.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (Units = "cm/s^2"))
	float GravityZ = -980.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "1.0", Units = "cm"))
	float RadiusCm = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BounceRestitution = 0.35f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float TangentialDamping = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "0.0", Units = "cm/s"))
	float StopSpeedCmPerSec = 45.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Rest", meta = (ClampMin = "0.0", Units = "cm/s"))
	float RestSpeedCmPerSec = 55.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Rest", meta = (ClampMin = "0.0", Units = "cm"))
	float SupportProbeDistanceCm = 6.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Rest", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float SupportMinNormalZ = 0.55f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Rest", meta = (ClampMin = "1", ClampMax = "8"))
	int32 SupportRequiredConsecutiveSteps = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "1", ClampMax = "64"))
	int32 MaxBounces = 8;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation", meta = (ClampMin = "10.0", Units = "cm"))
	float MaxTraceDistanceCm = 100000.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Breakable", meta = (ClampMin = "0.0", ClampMax = "0.95"))
	float BreakableVelocityDamping = 0.95f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation|Breakable", meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float BreakableNormalDeflection = 0.25f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation")
	TEnumAsByte<ECollisionChannel> TraceChannel = ECC_Visibility;
};

USTRUCT(BlueprintType)
struct FGrenadeSimState
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	FVector Position = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	FVector Velocity = FVector::ZeroVector;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation", meta = (Units = "s"))
	float RemainingFuseSeconds = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	int32 BounceCount = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bMotionStopped = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	int32 ConsecutiveSupportedSteps = 0;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bExploded = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation", meta = (Units = "cm"))
	float TraveledDistanceCm = 0.0f;

	TSet<int32> VirtualBrokenArenaObjectIds;
};

struct FGrenadeArenaHit
{
	int32 ArenaObjectId = INDEX_NONE;
	bool bBreakable = false;
	bool bBounceBeforeBreaking = false;

	bool IsValidBreakable() const
	{
		return ArenaObjectId != INDEX_NONE && bBreakable;
	}
};

USTRUCT(BlueprintType)
struct FGrenadeSimStepResult
{
	GENERATED_BODY()

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bHadHit = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bBounced = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bBrokeTile = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bStoppedThisStep = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bExplodedThisStep = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	bool bExceededDistance = false;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	FHitResult Hit;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Simulation")
	int32 BrokenArenaObjectId = INDEX_NONE;
};

/**
 * Single-source deterministic grenade stepping utility.
 */
class FGrenadeSim
{
public:
	using FResolveArenaHit = TFunction<FGrenadeArenaHit(const FHitResult&)>;
	using FAppendIgnoredArenaObject = TFunction<void(int32, FCollisionQueryParams&)>;

	static void InitializeState(FGrenadeSimState& OutState, const FVector& StartPosition, const FVector& StartVelocity, float FuseSeconds);

	static FGrenadeSimStepResult Step(
		UWorld* World,
		const FGrenadeSimConfig& Config,
		FGrenadeSimState& InOutState,
		float StepDt,
		AActor* PrimaryIgnoredActor,
		const FResolveArenaHit& ResolveArenaHit,
		const FAppendIgnoredArenaObject& AppendIgnoredArenaObject);
};
