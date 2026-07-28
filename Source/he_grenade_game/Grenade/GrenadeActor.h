#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grenade/GrenadeSim.h"
#include "GrenadeActor.generated.h"

class ABreakableTile;
class UStaticMeshComponent;
class USphereComponent;

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

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Explosion", meta = (ClampMin = "0.0", Units = "cm"))
	float ExplosionRadiusCm = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Debug")
	bool bDrawDebugPath = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Grenade|Simulation")
	FGrenadeSimConfig SimulationConfig;

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void InitializeGrenade(const FVector& StartPosition, const FVector& InitialVelocity, float FuseSeconds, const FGrenadeSimConfig& InSimulationConfig, AActor* InOwnerActor);

	UFUNCTION(BlueprintCallable, Category = "Grenade")
	void ForceExplode();

	static void ApplyInstantKillBlast(UWorld* World, const FVector& Origin, float RadiusCm, AActor* DamageCauser, AActor* InstigatorActor);

protected:
	virtual void OnConstruction(const FTransform& Transform) override;
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;

private:
	void ApplyVisualMaterial();
	ABreakableTile* ResolveBreakableTile(const FHitResult& Hit) const;
	void SimulateFixedStep(float StepSeconds);
	void ExplodeNow();
	void HandleBrokenTile(ABreakableTile* Tile);

	FGrenadeSimState SimState;
	float FixedStepAccumulator = 0.0f;
	bool bInitialized = false;
	bool bExploded = false;

	TWeakObjectPtr<AActor> OwningActor;
};
