// Copyright Epic Games, Inc. All Rights Reserved.

#include "GrenadeGameState.h"

#include "Net/UnrealNetwork.h"

AGrenadeGameState::AGrenadeGameState()
{
	bReplicates = true;
}

void AGrenadeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrenadeGameState, GrenadeMatchPhase);
	DOREPLIFETIME(AGrenadeGameState, PhaseEndServerTime);
	DOREPLIFETIME(AGrenadeGameState, MatchStateRevision);
}

float AGrenadeGameState::GetPhaseTimeRemaining() const
{
	if (PhaseEndServerTime <= 0.0f)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds());
}

void AGrenadeGameState::SetGrenadeMatchPhase(const EGGMatchPhase NewPhase, const float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}

	GrenadeMatchPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f
		? GetServerWorldTimeSeconds() + DurationSeconds
		: 0.0f;
	++MatchStateRevision;
	ForceNetUpdate();
}
