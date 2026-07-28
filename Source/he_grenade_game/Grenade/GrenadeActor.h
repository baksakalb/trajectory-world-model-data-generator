#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grenade/GrenadeSim.h"
#include "GrenadeActor.generated.h"

class AGrenadeGameState;
class UProjectileMovementComponent;
class UStaticMeshComponent;
class USphereComponent;

UENUM(BlueprintType)
enum class EGrenadeAuthorityEventType : uint8
{
	Bounce,
	ArenaDestroyed,
	Exploded
};

USTRUCT(BlueprintType)
struct FReplicatedGrenadeEvent
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 Revision = 0;

	UPROPERTY()
	EGrenadeAuthorityEventType Type = EGrenadeAuthorityEventType::Bounce;

	UPROPERTY()
	FVector_NetQuantize10 Location = FVector::ZeroVector;

	UPROPERTY()
	FVector_NetQuantizeNormal Normal = FVector::UpVector;

	UPROPERTY()
	int32 ArenaObjectId = INDEX_NONE;

	UPROPERTY()
	float ServerWorldTimeSeconds = 0.0f;
};

/**
 * The server owns the only gameplay grenade simulation. A locally spawned instance may run
 * the same simulation as a collision-less visual prediction, but can never mutate gameplay.
 */
UCLASS(BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API AGrenadeActor : public AActor
{
	GENERATED_BODY()

public:
	AGrenadeActor();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<USphereComponent> CollisionComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade")
	TObjectPtr<UStaticMeshComponent> VisualMesh;

	/** Uses the engine's documented projectile interpolation path on simulated proxies. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Grenade|Networking")
	TObjectPtr<UProjectileMovementComponent> NetworkInterpolation;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Explosion", meta = (ClampMin = "0.0", Units = "cm"))
	float ExplosionRadiusCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Debug")
	bool bDrawDebugPath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation")
	FGrenadeSimConfig SimulationConfig;

	void InitializeAuthoritativeGrenade(
		uint32 InThrowId,
		const FVector& StartPosition,
		const FVector& InitialVelocity,
		float FuseSeconds,
		const FGrenadeSimConfig& InSimulationConfig,
		AActor* InOwnerActor);

	void InitializePredictedVisual(
		uint32 InThrowId,
		const FVector& StartPosition,
		const FVector& InitialVelocity,
		float FuseSeconds,
		const FGrenadeSimConfig& InSimulationConfig,
		AActor* InOwnerActor);

	void BeginPredictionReconciliation(AGrenadeActor* AuthoritativeGrenade);
	void CancelPredictedVisual();

	uint32 GetThrowId() const { return ThrowId; }

	UFUNCTION(BlueprintPure, Category = "Grenade")
	bool IsCosmeticPrediction() const { return bCosmeticPrediction; }

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void ForceExplode();

	static void ApplyInstantKillBlast(
		UWorld* World,
		const FVector& Origin,
		float RadiusCm,
		AActor* DamageCauser,
		AActor* InstigatorActor);

	virtual FVector GetVelocity() const override;

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void PostNetReceiveLocationAndRotation() override;
	virtual void PostNetReceiveVelocity(const FVector& NewVelocity) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	void ApplyVisualMaterial();
	FGrenadeArenaHit ResolveArenaHit(const FHitResult& Hit) const;
	void AppendIgnoredArenaObject(int32 ArenaObjectId, FCollisionQueryParams& QueryParams) const;
	AGrenadeGameState* GetArenaGameState() const;
	void SimulateFixedStep(float StepSeconds);
	void ExplodeNow();
	void PublishAuthorityEvent(
		EGrenadeAuthorityEventType Type,
		const FVector& Location,
		const FVector& Normal = FVector::UpVector,
		int32 ArenaObjectId = INDEX_NONE);
	void TryBeginOwnerReconciliation();
	void TickPredictionReconciliation(float DeltaSeconds);
	void RecordNetworkVisualSample();

	UFUNCTION()
	void OnRep_ThrowIdentity();

	UFUNCTION()
	void OnRep_AuthorityEvents();

	UFUNCTION()
	void OnRep_AuthorityExploded();

	UPROPERTY(ReplicatedUsing = OnRep_ThrowIdentity)
	uint32 ThrowId = 0;

	UPROPERTY(Replicated)
	float FuseEndServerWorldTimeSeconds = 0.0f;

	UPROPERTY(ReplicatedUsing = OnRep_AuthorityEvents)
	TArray<FReplicatedGrenadeEvent> AuthorityEvents;

	UPROPERTY(ReplicatedUsing = OnRep_AuthorityExploded)
	bool bAuthorityExploded = false;

	FGrenadeSimState SimState;
	float FixedStepAccumulator = 0.0f;
	bool bInitialized = false;
	bool bCosmeticPrediction = false;
	bool bOwnerReconciliationAttempted = false;
	uint16 LastProcessedAuthorityEventRevision = 0;
	int32 NetworkInterpolationTargetCount = 0;
	float MaximumNetworkTargetDeltaCm = 0.0f;
	bool bHasNetworkVisualSample = false;
	FVector LastNetworkVisualLocation = FVector::ZeroVector;
	float MaximumNetworkVisualFrameStepCm = 0.0f;

	TWeakObjectPtr<AActor> OwningActor;
	TWeakObjectPtr<AGrenadeActor> ReconciliationTarget;
	float ReconciliationElapsedSeconds = 0.0f;
};
