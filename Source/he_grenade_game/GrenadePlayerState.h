// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "GrenadePlayerState.generated.h"

UENUM(BlueprintType)
enum class EGGPlayerSide : uint8
{
	Unassigned,
	Left,
	Right
};

/** Replicated identity, side assignment, readiness, and score for one player. */
UCLASS()
class HE_GRENADE_GAME_API AGrenadePlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	AGrenadePlayerState();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Grenade|Player")
	EGGPlayerSide GetAssignedSide() const { return AssignedSide; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Player")
	bool IsArenaReady() const { return bArenaReady; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Player")
	int32 GetReadyLayoutRevision() const { return ReadyLayoutRevision; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Player")
	int64 GetReadyLayoutChecksum() const { return ReadyLayoutChecksum; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Player")
	const FString& GetEOSProductUserId() const { return EOSProductUserId; }

	void SetAssignedSide(EGGPlayerSide NewSide);
	void SetArenaReady(int32 LayoutRevision, int64 LayoutChecksum);
	void ClearArenaReady();
	void SetEOSProductUserId(const FString& NewProductUserId);

private:
	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Player", meta = (AllowPrivateAccess = "true"))
	EGGPlayerSide AssignedSide = EGGPlayerSide::Unassigned;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Player", meta = (AllowPrivateAccess = "true"))
	bool bArenaReady = false;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Player", meta = (AllowPrivateAccess = "true"))
	int32 ReadyLayoutRevision = INDEX_NONE;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Player", meta = (AllowPrivateAccess = "true"))
	int64 ReadyLayoutChecksum = 0;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Player", meta = (AllowPrivateAccess = "true"))
	FString EOSProductUserId;
};
