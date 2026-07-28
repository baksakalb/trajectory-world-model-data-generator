// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "UObject/SoftObjectPath.h"
#include "GrenadeGameState.generated.h"

class AActor;
class ABreakableTileGrid;

UENUM(BlueprintType)
enum class EGGMatchPhase : uint8
{
	Lobby,
	ArenaSync,
	Countdown,
	InProgress,
	Reconnecting,
	PostMatch,
	ReturningToMenu
};

USTRUCT()
struct FReplicatedArenaGridLayout
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	int32 TilesX = 0;

	UPROPERTY()
	int32 TilesY = 0;

	UPROPERTY()
	float TileSizeCm = 0.0f;

	UPROPERTY()
	float TileSpacingCm = 0.0f;

	UPROPERTY()
	float TileThicknessScale = 0.0f;

	UPROPERTY()
	FVector GridLocalOriginOffset = FVector::ZeroVector;

	UPROPERTY()
	FSoftObjectPath FloorMaterialPath;

	UPROPERTY()
	FSoftObjectPath TrajectoryMaterialPath;
};

USTRUCT()
struct FReplicatedArenaActorLayout
{
	GENERATED_BODY()

	UPROPERTY()
	FTransform Transform;

	UPROPERTY()
	int32 FirstComponentIndex = 0;

	UPROPERTY()
	int32 ComponentCount = 0;

	UPROPERTY()
	bool bBreakableTile = false;

	UPROPERTY()
	bool bBreakOnGrenadeImpact = false;

	UPROPERTY()
	bool bBounceBeforeBreaking = false;
};

USTRUCT()
struct FReplicatedArenaMeshLayout
{
	GENERATED_BODY()

	UPROPERTY()
	FSoftObjectPath MeshPath;

	UPROPERTY()
	FSoftObjectPath MaterialPath;

	UPROPERTY()
	FVector RelativeLocation = FVector::ZeroVector;

	UPROPERTY()
	FRotator RelativeRotation = FRotator::ZeroRotator;

	UPROPERTY()
	FVector RelativeScale = FVector::OneVector;

	UPROPERTY()
	FName CollisionProfile = NAME_None;

	UPROPERTY()
	bool bVisible = true;
};

/**
 * Replicated match state shared by the listen server and every client.
 * Timed phases use Unreal's synchronized server clock instead of replicating
 * a countdown value every frame.
 */
UCLASS()
class HE_GRENADE_GAME_API AGrenadeGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AGrenadeGameState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	EGGMatchPhase GetGrenadeMatchPhase() const { return GrenadeMatchPhase; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	float GetPhaseEndServerTime() const { return PhaseEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	float GetPhaseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	int32 GetMatchStateRevision() const { return MatchStateRevision; }

	/** Server-only phase transition. A non-positive duration creates an untimed phase. */
	void SetGrenadeMatchPhase(EGGMatchPhase NewPhase, float DurationSeconds = 0.0f);

	/** Server-only: captures the completed generated arena, not its random seed. */
	void PublishGeneratedArena(
		ABreakableTileGrid* Grid,
		class UMaterialInterface* FloorMaterial,
		class UMaterialInterface* TrajectoryMaterial);

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	int32 GetArenaLayoutRevision() const { return ArenaLayoutRevision; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	int64 GetArenaLayoutChecksum() const { return ArenaLayoutChecksum; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	bool HasAppliedArenaLayout() const { return AppliedArenaLayoutRevision == ArenaLayoutRevision && ArenaLayoutRevision > 0; }

private:
	UFUNCTION()
	void OnRep_ArenaLayout();

	void BuildClientArenaReplica();
	void ClearClientArenaReplica();
	int64 CalculateArenaLayoutChecksum() const;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	EGGMatchPhase GrenadeMatchPhase = EGGMatchPhase::Lobby;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	int32 MatchStateRevision = 0;

	UPROPERTY(Replicated)
	FReplicatedArenaGridLayout ArenaGridLayout;

	UPROPERTY(Replicated)
	TArray<FReplicatedArenaActorLayout> ArenaActors;

	UPROPERTY(Replicated)
	TArray<FReplicatedArenaMeshLayout> ArenaMeshes;

	UPROPERTY(Replicated)
	int64 ArenaLayoutChecksum = 0;

	/** Kept after every layout payload field so its notification observes a complete snapshot. */
	UPROPERTY(ReplicatedUsing = OnRep_ArenaLayout)
	int32 ArenaLayoutRevision = 0;

	UPROPERTY(Transient)
	TArray<TObjectPtr<AActor>> ClientArenaActors;

	int32 AppliedArenaLayoutRevision = INDEX_NONE;
};
