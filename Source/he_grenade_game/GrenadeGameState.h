// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "Net/Serialization/FastArraySerializer.h"
#include "UObject/SoftObjectPath.h"
#include "GrenadeGameState.generated.h"

class AActor;
class ABreakableTile;
class ABreakableTileGrid;
class UMaterialInstanceDynamic;
class UMaterialInterface;
class USceneComponent;
class UStaticMeshComponent;

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

UENUM(BlueprintType)
enum class EArenaObjectType : uint8
{
	FloorTile,
	BreakableObstacle,
	StaticObstacle
};

UENUM(BlueprintType)
enum class EArenaObjectState : uint8
{
	Intact,
	Destroyed
};

UENUM()
enum class EArenaDestructionCause : uint8
{
	None,
	Grenade,
	FloorCollapse,
	Test
};

USTRUCT()
struct FArenaSnapshotHeader
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 SchemaVersion = 1;

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
	FVector ArenaOrigin = FVector::ZeroVector;

	UPROPERTY()
	FSoftObjectPath TrajectoryMaterialPath;

	UPROPERTY()
	FSoftObjectPath GrenadeMaterialPath;

	UPROPERTY()
	int32 ObjectCount = 0;

	UPROPERTY()
	int32 ComponentRecordCount = 0;

	UPROPERTY()
	int32 DestructibleCount = 0;
};

USTRUCT()
struct FArenaAssetDefinition
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 AssetId = 0;

	UPROPERTY()
	FSoftObjectPath MeshPath;

	UPROPERTY()
	FSoftObjectPath MaterialPath;

	UPROPERTY()
	FName CollisionProfile = NAME_None;
};

USTRUCT()
struct FArenaComponentLayout
{
	GENERATED_BODY()

	UPROPERTY()
	uint16 AssetId = 0;

	UPROPERTY()
	FTransform RelativeTransform = FTransform::Identity;

	UPROPERTY()
	bool bVisible = true;
};

USTRUCT()
struct FArenaObjectLayout
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ArenaId = INDEX_NONE;

	UPROPERTY()
	EArenaObjectType Type = EArenaObjectType::StaticObstacle;

	/** Single-component objects store the component world transform here. */
	UPROPERTY()
	FTransform Transform = FTransform::Identity;

	UPROPERTY()
	int16 GridX = -1;

	UPROPERTY()
	int16 GridY = -1;

	UPROPERTY()
	int16 CollapseRing = -1;

	/** Non-zero for the common single-component representation. */
	UPROPERTY()
	uint16 PrimaryAssetId = 0;

	/** Composite objects refer to a contiguous range of component records. */
	UPROPERTY()
	int32 FirstComponentIndex = 0;

	UPROPERTY()
	int16 ComponentCount = 0;

	UPROPERTY()
	bool bDestructible = false;

	UPROPERTY()
	bool bBreakOnGrenadeImpact = false;

	UPROPERTY()
	bool bBounceBeforeBreaking = false;
};

USTRUCT()
struct FReplicatedFloorCollapseState
{
	GENERATED_BODY()

	UPROPERTY()
	bool bActive = false;

	UPROPERTY()
	int32 RingIndex = INDEX_NONE;

	UPROPERTY()
	float EndServerTime = 0.0f;

	UPROPERTY()
	float Duration = 0.0f;

	UPROPERTY()
	int32 Revision = 0;
};

USTRUCT()
struct FArenaMutableStateItem : public FFastArraySerializerItem
{
	GENERATED_BODY()

	UPROPERTY()
	int32 ArenaId = INDEX_NONE;

	UPROPERTY()
	EArenaObjectState State = EArenaObjectState::Intact;

	UPROPERTY()
	int32 StateRevision = 0;

	UPROPERTY()
	EArenaDestructionCause Cause = EArenaDestructionCause::None;
};

class AGrenadeGameState;

USTRUCT()
struct FArenaMutableStateArray : public FFastArraySerializer
{
	GENERATED_BODY()

	UPROPERTY()
	TArray<FArenaMutableStateItem> Items;

	bool NetDeltaSerialize(FNetDeltaSerializeInfo& DeltaParms)
	{
		return FFastArraySerializer::FastArrayDeltaSerialize<FArenaMutableStateItem, FArenaMutableStateArray>(
			Items,
			DeltaParms,
			*this);
	}

	void SetOwner(AGrenadeGameState* InOwner) { Owner = InOwner; }
	void PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32 FinalSize);
	void PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32 FinalSize);
	void PostReplicatedReceive(const FFastArraySerializer::FPostReplicatedReceiveParameters& Parameters);

private:
	AGrenadeGameState* Owner = nullptr;
};

template<>
struct TStructOpsTypeTraits<FArenaMutableStateArray> : public TStructOpsTypeTraitsBase2<FArenaMutableStateArray>
{
	enum
	{
		WithNetDeltaSerializer = true
	};
};

/**
 * Replicated match state and the one arena protocol shared by server and clients.
 * The server owns all mutation. Runtime components are deterministic mirrors of
 * the explicit snapshot and are owned by this same replicated actor everywhere.
 */
UCLASS()
class HE_GRENADE_GAME_API AGrenadeGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AGrenadeGameState();

	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	EGGMatchPhase GetGrenadeMatchPhase() const { return GrenadeMatchPhase; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	float GetPhaseEndServerTime() const { return PhaseEndServerTime; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	float GetPhaseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Grenade|Match")
	int32 GetMatchStateRevision() const { return MatchStateRevision; }

	void SetGrenadeMatchPhase(EGGMatchPhase NewPhase, float DurationSeconds = 0.0f);

	/** Server-only capture and realization of the completed generated arena. */
	void PublishGeneratedArena(
		ABreakableTileGrid* Grid,
		UMaterialInterface* FloorMaterial,
		UMaterialInterface* TrajectoryMaterial,
		UMaterialInterface* GrenadeMaterial);

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	int32 GetArenaLayoutRevision() const { return ArenaLayoutRevision; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	int64 GetArenaLayoutChecksum() const { return ArenaLayoutChecksum; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	int32 GetArenaStateRevision() const { return ArenaStateRevision; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena")
	bool HasAppliedArenaLayout() const;

	bool HasCompleteArenaState() const;
	int32 GetArenaDestructibleCount() const { return ArenaHeader.DestructibleCount; }
	int32 GetArenaTilesX() const { return ArenaHeader.TilesX; }
	int32 GetArenaTilesY() const { return ArenaHeader.TilesY; }
	const FSoftObjectPath& GetGrenadeMaterialPath() const { return ArenaHeader.GrenadeMaterialPath; }

	bool IsArenaObjectDestroyed(int32 ArenaId) const;
	bool ResolveArenaHit(
		const FHitResult& Hit,
		int32& OutArenaId,
		bool& bOutBreakable,
		bool& bOutBounceBeforeBreaking) const;
	void AppendArenaObjectIgnoredComponents(int32 ArenaId, FCollisionQueryParams& QueryParams) const;

	/** Server-only state mutation. */
	bool DestroyArenaObject(int32 ArenaId, EArenaDestructionCause Cause);

	/** Local-only cosmetic highlight used by trajectory preview. */
	void SetLocalTrajectoryHighlight(int32 ArenaId, bool bHighlighted);

	void SetFloorCollapseState(bool bActive, int32 RingIndex, float DurationSeconds);

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena|Floor Collapse")
	bool IsFloorCollapseActive() const { return FloorCollapseState.bActive; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena|Floor Collapse")
	int32 GetFloorCollapseRing() const { return FloorCollapseState.RingIndex; }

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena|Floor Collapse")
	float GetFloorCollapseTimeRemaining() const;

	UFUNCTION(BlueprintPure, Category = "Grenade|Arena|Floor Collapse")
	float GetFloorCollapseProgress() const;

	void GetFloorArenaIdsForRing(int32 RingIndex, TArray<int32>& OutArenaIds) const;
	bool GetArenaObjectType(int32 ArenaId, EArenaObjectType& OutType) const;
	bool FindIntactArenaObjectBounds(
		EArenaObjectType Type,
		int32& OutArenaId,
		FBox& OutWorldBounds,
		int32 ExcludedArenaId = INDEX_NONE) const;

	/** Called by Fast Array receive callbacks. */
	void HandleArenaStateItemsChanged(const TArrayView<int32>& ChangedIndices);
	void HandleArenaStateReceiveComplete();

private:
	static constexpr uint16 CurrentArenaSchemaVersion = 1;

	UFUNCTION()
	void OnRep_ArenaLayout();

	UFUNCTION()
	void OnRep_ArenaStateRevision();

	UFUNCTION()
	void OnRep_FloorCollapseState();

	UFUNCTION()
	void OnRep_MatchPhase();

	void ClearRuntimeArena();
	bool BuildRuntimeArena();
	UStaticMeshComponent* CreateRuntimeComponent(
		int32 ArenaId,
		int32 ComponentIndex,
		const FArenaAssetDefinition& Asset,
		const FTransform& WorldTransform,
		bool bVisible);
	const FArenaAssetDefinition* FindAsset(uint16 AssetId) const;
	const FArenaObjectLayout* FindObjectLayout(int32 ArenaId) const;
	void RebuildLookupMaps();
	void ApplyAllArenaStates();
	void ApplyArenaStateItem(const FArenaMutableStateItem& Item);
	void ApplyArenaObjectPresentation(int32 ArenaId);
	void InvalidateCharacterBasesForObject(int32 ArenaId);
	void UpdateCollapseWarningVisuals();
	void UpdateObjectMaterials(int32 ArenaId);
	void TryConfirmArenaReady();
	int64 CalculateArenaLayoutChecksum() const;
	int32 EstimateArenaSnapshotBytes() const;

	UPROPERTY(VisibleAnywhere, Category = "Arena")
	TObjectPtr<USceneComponent> ArenaRoot;

	UPROPERTY(ReplicatedUsing = OnRep_MatchPhase, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	EGGMatchPhase GrenadeMatchPhase = EGGMatchPhase::Lobby;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	float PhaseEndServerTime = 0.0f;

	UPROPERTY(Replicated, VisibleInstanceOnly, BlueprintReadOnly, Category = "Grenade|Match", meta = (AllowPrivateAccess = "true"))
	int32 MatchStateRevision = 0;

	UPROPERTY(Replicated)
	FArenaSnapshotHeader ArenaHeader;

	UPROPERTY(Replicated)
	TArray<FArenaAssetDefinition> ArenaAssets;

	UPROPERTY(Replicated)
	TArray<FArenaObjectLayout> ArenaObjects;

	UPROPERTY(Replicated)
	TArray<FArenaComponentLayout> ArenaComponents;

	UPROPERTY(Replicated)
	int64 ArenaLayoutChecksum = 0;

	/** Replicated after all snapshot payload fields. */
	UPROPERTY(ReplicatedUsing = OnRep_ArenaLayout)
	int32 ArenaLayoutRevision = 0;

	UPROPERTY(Replicated)
	FArenaMutableStateArray ArenaMutableState;

	UPROPERTY(ReplicatedUsing = OnRep_ArenaStateRevision)
	int32 ArenaStateRevision = 0;

	UPROPERTY(ReplicatedUsing = OnRep_FloorCollapseState)
	FReplicatedFloorCollapseState FloorCollapseState;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> RuntimeComponents;

	TMap<int32, int32> ObjectIndexByArenaId;
	TMap<uint16, int32> AssetIndexById;
	TMap<int32, int32> MutableIndexByArenaId;
	TMap<int32, TArray<TWeakObjectPtr<UStaticMeshComponent>>> ComponentsByArenaId;
	TMap<TWeakObjectPtr<UPrimitiveComponent>, int32> ArenaIdByComponent;
	TMap<TWeakObjectPtr<UStaticMeshComponent>, TWeakObjectPtr<UMaterialInterface>> BaseMaterialByComponent;
	TMap<TWeakObjectPtr<UStaticMeshComponent>, TWeakObjectPtr<UMaterialInstanceDynamic>> WarningMIDByComponent;
	TSet<int32> LocalTrajectoryHighlights;
	TMap<int32, float> LocalWarningAlpha;

	int32 AppliedArenaLayoutRevision = INDEX_NONE;
	int32 LastReadyAttemptLayoutRevision = INDEX_NONE;
	int32 LastReadyAttemptStateRevision = INDEX_NONE;
	float NetworkMetricsAccumulator = 0.0f;
};
