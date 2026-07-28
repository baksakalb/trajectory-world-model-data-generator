// Copyright Epic Games, Inc. All Rights Reserved.

#include "GrenadePlayerState.h"

#include "Net/UnrealNetwork.h"

AGrenadePlayerState::AGrenadePlayerState()
{
	bReplicates = true;
	SetNetUpdateFrequency(10.0f);
}

void AGrenadePlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrenadePlayerState, AssignedSide);
	DOREPLIFETIME(AGrenadePlayerState, bArenaReady);
	DOREPLIFETIME(AGrenadePlayerState, ReadyLayoutRevision);
	DOREPLIFETIME(AGrenadePlayerState, ReadyLayoutChecksum);
	DOREPLIFETIME(AGrenadePlayerState, EOSProductUserId);
}

void AGrenadePlayerState::SetAssignedSide(const EGGPlayerSide NewSide)
{
	if (!HasAuthority())
	{
		return;
	}

	AssignedSide = NewSide;
	ForceNetUpdate();
}

void AGrenadePlayerState::SetArenaReady(const int32 LayoutRevision, const int64 LayoutChecksum)
{
	if (!HasAuthority())
	{
		return;
	}

	ReadyLayoutRevision = LayoutRevision;
	ReadyLayoutChecksum = LayoutChecksum;
	bArenaReady = true;
	ForceNetUpdate();
}

void AGrenadePlayerState::ClearArenaReady()
{
	if (!HasAuthority())
	{
		return;
	}

	bArenaReady = false;
	ReadyLayoutRevision = INDEX_NONE;
	ReadyLayoutChecksum = 0;
	ForceNetUpdate();
}

void AGrenadePlayerState::SetEOSProductUserId(const FString& NewProductUserId)
{
	if (!HasAuthority())
	{
		return;
	}

	EOSProductUserId = NewProductUserId;
	ForceNetUpdate();
}
