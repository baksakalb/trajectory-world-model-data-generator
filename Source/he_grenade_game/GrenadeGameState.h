// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "GrenadeGameState.generated.h"

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

private:
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	EGGMatchPhase GrenadeMatchPhase = EGGMatchPhase::Lobby;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	int32 MatchStateRevision = 0;
};
