// Copyright Epic Games, Inc. All Rights Reserved.

#include "GrenadeGameState.h"

#include "Components/SceneComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/NetDriver.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Grenade/ArenaObstacle.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "Grenade/Breakables/BreakableTileGrid.h"
#include "GrenadePlayerState.h"
#include "he_grenade_game.h"
#include "he_grenade_gamePlayerController.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName GeneratedArenaTag(TEXT("GeneratedArena"));

	uint32 HashFloat(const float Value)
	{
		return GetTypeHash(FMath::RoundToInt(Value * 10.0f));
	}

	uint32 HashVector(const FVector& Value)
	{
		return HashCombine(HashCombine(HashFloat(Value.X), HashFloat(Value.Y)), HashFloat(Value.Z));
	}

	uint32 HashRotator(const FRotator& Value)
	{
		return HashCombine(
			HashCombine(HashFloat(FRotator::NormalizeAxis(Value.Pitch)), HashFloat(FRotator::NormalizeAxis(Value.Yaw))),
			HashFloat(FRotator::NormalizeAxis(Value.Roll)));
	}

	uint32 HashTransform(const FTransform& Value)
	{
		return HashCombine(
			HashCombine(HashVector(Value.GetLocation()), HashRotator(Value.Rotator())),
			HashVector(Value.GetScale3D()));
	}

	FString MakeAssetKey(const UStaticMesh* Mesh, const UMaterialInterface* Material, const FName CollisionProfile)
	{
		return FString::Printf(
			TEXT("%s|%s|%s"),
			*GetPathNameSafe(Mesh),
			*GetPathNameSafe(Material),
			*CollisionProfile.ToString());
	}
}

void FArenaMutableStateArray::PostReplicatedAdd(const TArrayView<int32>& AddedIndices, int32)
{
	if (Owner)
	{
		Owner->HandleArenaStateItemsChanged(AddedIndices);
	}
}

void FArenaMutableStateArray::PostReplicatedChange(const TArrayView<int32>& ChangedIndices, int32)
{
	if (Owner)
	{
		Owner->HandleArenaStateItemsChanged(ChangedIndices);
	}
}

void FArenaMutableStateArray::PostReplicatedReceive(
	const FFastArraySerializer::FPostReplicatedReceiveParameters&)
{
	if (Owner)
	{
		Owner->HandleArenaStateReceiveComplete();
	}
}

AGrenadeGameState::AGrenadeGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;

	ArenaRoot = CreateDefaultSubobject<USceneComponent>(TEXT("ArenaRoot"));
	SetRootComponent(ArenaRoot);
	ArenaMutableState.SetOwner(this);
}

void AGrenadeGameState::BeginPlay()
{
	Super::BeginPlay();
	ArenaMutableState.SetOwner(this);
}

void AGrenadeGameState::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);
	UpdateCollapseWarningVisuals();
	TryConfirmArenaReady();

	if (FParse::Param(FCommandLine::Get(), TEXT("GGNetworkMetrics")))
	{
		NetworkMetricsAccumulator += FMath::Max(0.0f, DeltaSeconds);
		if (NetworkMetricsAccumulator >= 5.0f)
		{
			NetworkMetricsAccumulator = 0.0f;
			if (const UNetDriver* NetDriver = GetWorld() ? GetWorld()->GetNetDriver() : nullptr)
			{
				UE_LOG(
					Loghe_grenade_game,
					Display,
					TEXT("NET_METRICS role=%s in_bps=%u out_bps=%u in_total=%u out_total=%u in_packets=%u out_packets=%u reliable_in=%u reliable_out=%u"),
					HasAuthority() ? TEXT("authority") : TEXT("mirror"),
					NetDriver->InBytesPerSecond,
					NetDriver->OutBytesPerSecond,
					NetDriver->InTotalBytes,
					NetDriver->OutTotalBytes,
					NetDriver->InTotalPackets,
					NetDriver->OutTotalPackets,
					NetDriver->InTotalReliableBunches,
					NetDriver->OutTotalReliableBunches);
			}
		}
	}
}

void AGrenadeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrenadeGameState, GrenadeMatchPhase);
	DOREPLIFETIME(AGrenadeGameState, PhaseEndServerTime);
	DOREPLIFETIME(AGrenadeGameState, MatchStateRevision);
	DOREPLIFETIME(AGrenadeGameState, ArenaHeader);
	DOREPLIFETIME(AGrenadeGameState, ArenaAssets);
	DOREPLIFETIME(AGrenadeGameState, ArenaObjects);
	DOREPLIFETIME(AGrenadeGameState, ArenaComponents);
	DOREPLIFETIME(AGrenadeGameState, ArenaLayoutChecksum);
	DOREPLIFETIME(AGrenadeGameState, ArenaLayoutRevision);
	DOREPLIFETIME(AGrenadeGameState, ArenaMutableState);
	DOREPLIFETIME(AGrenadeGameState, ArenaStateRevision);
	DOREPLIFETIME(AGrenadeGameState, FloorCollapseState);
}

void AGrenadeGameState::PublishGeneratedArena(
	ABreakableTileGrid* Grid,
	UMaterialInterface* FloorMaterial,
	UMaterialInterface* TrajectoryMaterial,
	UMaterialInterface* GrenadeMaterial)
{
	if (!HasAuthority() || GetNetMode() == NM_Client || !Grid || !GetWorld())
	{
		return;
	}

	ClearRuntimeArena();
	ArenaAssets.Reset();
	ArenaObjects.Reset();
	ArenaComponents.Reset();
	ArenaMutableState.Items.Reset();
	ArenaMutableState.MarkArrayDirty();
	ArenaStateRevision = 0;

	ArenaHeader = FArenaSnapshotHeader();
	ArenaHeader.SchemaVersion = CurrentArenaSchemaVersion;
	ArenaHeader.TilesX = Grid->TilesX;
	ArenaHeader.TilesY = Grid->TilesY;
	ArenaHeader.TileSizeCm = Grid->TileSizeCm;
	ArenaHeader.TileSpacingCm = Grid->TileSpacingCm;
	ArenaHeader.TileThicknessScale = Grid->TileThicknessScale;
	ArenaHeader.ArenaOrigin = Grid->GetActorLocation();
	ArenaHeader.TrajectoryMaterialPath = TrajectoryMaterial
		? FSoftObjectPath(TrajectoryMaterial)
		: FSoftObjectPath();
	ArenaHeader.GrenadeMaterialPath = GrenadeMaterial
		? FSoftObjectPath(GrenadeMaterial)
		: FSoftObjectPath();

	TMap<FString, uint16> AssetIdByKey;
	auto FindOrAddAsset = [&](UStaticMeshComponent* Component, UMaterialInterface* MaterialOverride = nullptr) -> uint16
		{
			if (!Component || !Component->GetStaticMesh())
			{
				return 0;
			}

			UMaterialInterface* Material = MaterialOverride ? MaterialOverride : Component->GetMaterial(0);
			const FName CollisionProfile = Component->GetCollisionProfileName();
			const FString Key = MakeAssetKey(Component->GetStaticMesh(), Material, CollisionProfile);
			if (const uint16* ExistingId = AssetIdByKey.Find(Key))
			{
				return *ExistingId;
			}

			if (ArenaAssets.Num() >= MAX_uint16 - 1)
			{
				return 0;
			}

			FArenaAssetDefinition& Asset = ArenaAssets.AddDefaulted_GetRef();
			Asset.AssetId = static_cast<uint16>(ArenaAssets.Num());
			Asset.MeshPath = FSoftObjectPath(Component->GetStaticMesh());
			Asset.MaterialPath = Material ? FSoftObjectPath(Material) : FSoftObjectPath();
			Asset.CollisionProfile = CollisionProfile;
			AssetIdByKey.Add(Key, Asset.AssetId);
			return Asset.AssetId;
		};

	int32 NextArenaId = 1;
	auto AddArenaObject = [&](
		AActor* SourceActor,
		const EArenaObjectType Type,
		const int16 GridX,
		const int16 GridY,
		UMaterialInterface* MaterialOverride) -> int32
		{
			if (!SourceActor)
			{
				return INDEX_NONE;
			}

			TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
			SourceActor->GetComponents(MeshComponents);
			MeshComponents.RemoveAll([](const UStaticMeshComponent* Component)
				{
					return !Component || !Component->GetStaticMesh();
				});
			MeshComponents.Sort([](const UStaticMeshComponent& A, const UStaticMeshComponent& B)
				{
					return A.GetName() < B.GetName();
				});

			if (MeshComponents.IsEmpty())
			{
				return INDEX_NONE;
			}

			FArenaObjectLayout& Object = ArenaObjects.AddDefaulted_GetRef();
			Object.ArenaId = NextArenaId++;
			Object.Type = Type;
			Object.GridX = GridX;
			Object.GridY = GridY;
			Object.CollapseRing = (GridX >= 0 && GridY >= 0)
				? static_cast<int16>(FMath::Min(
					FMath::Min<int32>(GridX, ArenaHeader.TilesX - 1 - GridX),
					FMath::Min<int32>(GridY, ArenaHeader.TilesY - 1 - GridY)))
				: -1;

			if (const ABreakableTile* Breakable = Cast<ABreakableTile>(SourceActor))
			{
				Object.bDestructible = true;
				Object.bBreakOnGrenadeImpact = Breakable->CanBreakOnGrenadeImpact();
				Object.bBounceBeforeBreaking = Breakable->ShouldBounceGrenadeBeforeBreaking();
			}

			if (MeshComponents.Num() == 1)
			{
				Object.Transform = MeshComponents[0]->GetComponentTransform();
				Object.PrimaryAssetId = FindOrAddAsset(MeshComponents[0], MaterialOverride);
			}
			else
			{
				Object.Transform = SourceActor->GetActorTransform();
				Object.FirstComponentIndex = ArenaComponents.Num();
				Object.ComponentCount = static_cast<int16>(
					FMath::Min(MeshComponents.Num(), static_cast<int32>(MAX_int16)));
				for (int32 ComponentIndex = 0; ComponentIndex < Object.ComponentCount; ++ComponentIndex)
				{
					UStaticMeshComponent* Component = MeshComponents[ComponentIndex];
					FArenaComponentLayout& ComponentLayout = ArenaComponents.AddDefaulted_GetRef();
					ComponentLayout.AssetId = FindOrAddAsset(Component, MaterialOverride);
					ComponentLayout.RelativeTransform =
						Component->GetComponentTransform().GetRelativeTransform(Object.Transform);
					ComponentLayout.bVisible = Component->IsVisible();
				}
			}

			return Object.ArenaId;
		};

	const TArray<TObjectPtr<ABreakableTile>>& FloorTiles = Grid->GetSpawnedTiles();
	for (int32 Index = 0; Index < FloorTiles.Num(); ++Index)
	{
		const int16 GridX = static_cast<int16>(Index % FMath::Max(1, Grid->TilesX));
		const int16 GridY = static_cast<int16>(Index / FMath::Max(1, Grid->TilesX));
		AddArenaObject(
			FloorTiles[Index],
			EArenaObjectType::FloorTile,
			GridX,
			GridY,
			FloorMaterial);
	}

	TSet<const AActor*> FloorSet;
	for (const ABreakableTile* FloorTile : FloorTiles)
	{
		FloorSet.Add(FloorTile);
	}

	TArray<AActor*> GeneratedObjects;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (IsValid(Actor)
			&& Actor != Grid
			&& !FloorSet.Contains(Actor)
			&& Actor->ActorHasTag(GeneratedArenaTag))
		{
			GeneratedObjects.Add(Actor);
		}
	}
	GeneratedObjects.Sort([](const AActor& A, const AActor& B)
		{
			const FVector LA = A.GetActorLocation();
			const FVector LB = B.GetActorLocation();
			if (!FMath::IsNearlyEqual(LA.X, LB.X))
			{
				return LA.X < LB.X;
			}
			if (!FMath::IsNearlyEqual(LA.Y, LB.Y))
			{
				return LA.Y < LB.Y;
			}
			if (!FMath::IsNearlyEqual(LA.Z, LB.Z))
			{
				return LA.Z < LB.Z;
			}
			return A.GetName() < B.GetName();
		});

	for (AActor* Actor : GeneratedObjects)
	{
		const bool bBreakable = Actor->IsA<ABreakableTile>();
		AddArenaObject(
			Actor,
			bBreakable ? EArenaObjectType::BreakableObstacle : EArenaObjectType::StaticObstacle,
			-1,
			-1,
			nullptr);
	}

	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		if (!Object.bDestructible)
		{
			continue;
		}

		FArenaMutableStateItem& Item = ArenaMutableState.Items.AddDefaulted_GetRef();
		Item.ArenaId = Object.ArenaId;
		Item.State = EArenaObjectState::Intact;
		Item.StateRevision = 0;
		Item.Cause = EArenaDestructionCause::None;
		ArenaMutableState.MarkItemDirty(Item);
	}

	ArenaHeader.ObjectCount = ArenaObjects.Num();
	ArenaHeader.ComponentRecordCount = ArenaComponents.Num();
	ArenaHeader.DestructibleCount = ArenaMutableState.Items.Num();
	++ArenaLayoutRevision;
	ArenaLayoutChecksum = CalculateArenaLayoutChecksum();

	RebuildLookupMaps();
	if (!BuildRuntimeArena())
	{
		UE_LOG(Loghe_grenade_game, Error, TEXT("Failed to build authoritative runtime arena."));
		return;
	}
	ApplyAllArenaStates();

	SetGrenadeMatchPhase(EGGMatchPhase::ArenaSync);
	ForceNetUpdate();

	UE_LOG(
		Loghe_grenade_game,
		Display,
		TEXT("ARENA_SNAPSHOT authority layout=%d checksum=%lld objects=%d assets=%d components=%d destructibles=%d estimated_bytes=%d"),
		ArenaLayoutRevision,
		ArenaLayoutChecksum,
		ArenaObjects.Num(),
		ArenaAssets.Num(),
		ArenaComponents.Num(),
		ArenaHeader.DestructibleCount,
		EstimateArenaSnapshotBytes());
}

void AGrenadeGameState::OnRep_ArenaLayout()
{
	if (ArenaLayoutRevision <= 0
		|| ArenaHeader.SchemaVersion != CurrentArenaSchemaVersion
		|| ArenaHeader.ObjectCount != ArenaObjects.Num()
		|| ArenaHeader.ComponentRecordCount != ArenaComponents.Num()
		|| CalculateArenaLayoutChecksum() != ArenaLayoutChecksum)
	{
		UE_LOG(
			Loghe_grenade_game,
			Error,
			TEXT("ARENA_SNAPSHOT rejected layout=%d schema=%d checksum=%lld local=%lld objects=%d/%d components=%d/%d"),
			ArenaLayoutRevision,
			ArenaHeader.SchemaVersion,
			ArenaLayoutChecksum,
			CalculateArenaLayoutChecksum(),
			ArenaObjects.Num(),
			ArenaHeader.ObjectCount,
			ArenaComponents.Num(),
			ArenaHeader.ComponentRecordCount);
		return;
	}

	RebuildLookupMaps();
	if (!BuildRuntimeArena())
	{
		return;
	}
	ApplyAllArenaStates();
	LastReadyAttemptLayoutRevision = INDEX_NONE;
	LastReadyAttemptStateRevision = INDEX_NONE;

	UE_LOG(
		Loghe_grenade_game,
		Display,
		TEXT("ARENA_SNAPSHOT mirror layout=%d checksum=%lld objects=%d destructibles=%d estimated_bytes=%d"),
		ArenaLayoutRevision,
		ArenaLayoutChecksum,
		ArenaObjects.Num(),
		ArenaHeader.DestructibleCount,
		EstimateArenaSnapshotBytes());

	TryConfirmArenaReady();
}

void AGrenadeGameState::OnRep_ArenaStateRevision()
{
	TryConfirmArenaReady();
}

void AGrenadeGameState::OnRep_FloorCollapseState()
{
	UpdateCollapseWarningVisuals();
}

void AGrenadeGameState::OnRep_MatchPhase()
{
	UE_LOG(
		Loghe_grenade_game,
		Display,
		TEXT("MATCH_PHASE role=mirror phase=%d revision=%d remaining=%.2f"),
		static_cast<int32>(GrenadeMatchPhase),
		MatchStateRevision,
		GetPhaseTimeRemaining());
}

void AGrenadeGameState::ClearRuntimeArena()
{
	for (UStaticMeshComponent* Component : RuntimeComponents)
	{
		if (IsValid(Component))
		{
			Component->DestroyComponent();
		}
	}

	RuntimeComponents.Reset();
	ComponentsByArenaId.Reset();
	ArenaIdByComponent.Reset();
	BaseMaterialByComponent.Reset();
	WarningMIDByComponent.Reset();
	LocalTrajectoryHighlights.Reset();
	LocalWarningAlpha.Reset();
	AppliedArenaLayoutRevision = INDEX_NONE;
}

bool AGrenadeGameState::BuildRuntimeArena()
{
	if (!GetWorld() || ArenaLayoutRevision <= 0 || ArenaObjects.IsEmpty())
	{
		return false;
	}

	if (AppliedArenaLayoutRevision == ArenaLayoutRevision
		&& RuntimeComponents.Num() > 0)
	{
		return true;
	}

	ClearRuntimeArena();
	RebuildLookupMaps();

	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		if (Object.PrimaryAssetId != 0)
		{
			const FArenaAssetDefinition* Asset = FindAsset(Object.PrimaryAssetId);
			if (!Asset || !CreateRuntimeComponent(Object.ArenaId, 0, *Asset, Object.Transform, true))
			{
				ClearRuntimeArena();
				return false;
			}
			continue;
		}

		for (int32 LocalIndex = 0; LocalIndex < Object.ComponentCount; ++LocalIndex)
		{
			const int32 ComponentRecordIndex = Object.FirstComponentIndex + LocalIndex;
			if (!ArenaComponents.IsValidIndex(ComponentRecordIndex))
			{
				ClearRuntimeArena();
				return false;
			}

			const FArenaComponentLayout& ComponentLayout = ArenaComponents[ComponentRecordIndex];
			const FArenaAssetDefinition* Asset = FindAsset(ComponentLayout.AssetId);
			if (!Asset)
			{
				ClearRuntimeArena();
				return false;
			}

			const FTransform WorldTransform = ComponentLayout.RelativeTransform * Object.Transform;
			if (!CreateRuntimeComponent(
				Object.ArenaId,
				LocalIndex,
				*Asset,
				WorldTransform,
				ComponentLayout.bVisible))
			{
				ClearRuntimeArena();
				return false;
			}
		}
	}

	AppliedArenaLayoutRevision = ArenaLayoutRevision;
	return RuntimeComponents.Num() > 0;
}

UStaticMeshComponent* AGrenadeGameState::CreateRuntimeComponent(
	const int32 ArenaId,
	const int32 ComponentIndex,
	const FArenaAssetDefinition& Asset,
	const FTransform& WorldTransform,
	const bool bVisible)
{
	UStaticMesh* Mesh = Cast<UStaticMesh>(Asset.MeshPath.TryLoad());
	if (!Mesh)
	{
		return nullptr;
	}

	const FName ComponentName(*FString::Printf(TEXT("Arena_%d_Component_%d"), ArenaId, ComponentIndex));
	UStaticMeshComponent* Component = NewObject<UStaticMeshComponent>(this, ComponentName);
	if (!Component)
	{
		return nullptr;
	}

	AddInstanceComponent(Component);
	Component->SetupAttachment(ArenaRoot);
	Component->SetNetAddressable();
	Component->SetMobility(EComponentMobility::Movable);
	Component->SetStaticMesh(Mesh);
	Component->SetWorldTransform(WorldTransform);
	Component->SetCollisionProfileName(Asset.CollisionProfile.IsNone() ? FName(TEXT("BlockAll")) : Asset.CollisionProfile);
	Component->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	Component->SetGenerateOverlapEvents(false);
	Component->SetVisibility(bVisible, true);
	Component->CanCharacterStepUpOn = ECanBeCharacterBase::ECB_Yes;

	UMaterialInterface* Material = Cast<UMaterialInterface>(Asset.MaterialPath.TryLoad());
	if (Material)
	{
		Component->SetMaterial(0, Material);
	}

	Component->RegisterComponent();

	RuntimeComponents.Add(Component);
	ComponentsByArenaId.FindOrAdd(ArenaId).Add(Component);
	ArenaIdByComponent.Add(Component, ArenaId);
	BaseMaterialByComponent.Add(Component, Material);
	return Component;
}

const FArenaAssetDefinition* AGrenadeGameState::FindAsset(const uint16 AssetId) const
{
	if (const int32* Index = AssetIndexById.Find(AssetId))
	{
		return ArenaAssets.IsValidIndex(*Index) ? &ArenaAssets[*Index] : nullptr;
	}
	return nullptr;
}

const FArenaObjectLayout* AGrenadeGameState::FindObjectLayout(const int32 ArenaId) const
{
	if (const int32* Index = ObjectIndexByArenaId.Find(ArenaId))
	{
		return ArenaObjects.IsValidIndex(*Index) ? &ArenaObjects[*Index] : nullptr;
	}
	return nullptr;
}

void AGrenadeGameState::RebuildLookupMaps()
{
	ObjectIndexByArenaId.Reset();
	for (int32 Index = 0; Index < ArenaObjects.Num(); ++Index)
	{
		ObjectIndexByArenaId.Add(ArenaObjects[Index].ArenaId, Index);
	}

	AssetIndexById.Reset();
	for (int32 Index = 0; Index < ArenaAssets.Num(); ++Index)
	{
		AssetIndexById.Add(ArenaAssets[Index].AssetId, Index);
	}

	MutableIndexByArenaId.Reset();
	for (int32 Index = 0; Index < ArenaMutableState.Items.Num(); ++Index)
	{
		MutableIndexByArenaId.Add(ArenaMutableState.Items[Index].ArenaId, Index);
	}
}

void AGrenadeGameState::HandleArenaStateItemsChanged(const TArrayView<int32>& ChangedIndices)
{
	for (const int32 Index : ChangedIndices)
	{
		if (ArenaMutableState.Items.IsValidIndex(Index))
		{
			MutableIndexByArenaId.Add(ArenaMutableState.Items[Index].ArenaId, Index);
			ApplyArenaStateItem(ArenaMutableState.Items[Index]);
		}
	}
}

void AGrenadeGameState::HandleArenaStateReceiveComplete()
{
	RebuildLookupMaps();
	TryConfirmArenaReady();
}

void AGrenadeGameState::ApplyAllArenaStates()
{
	RebuildLookupMaps();
	for (const FArenaMutableStateItem& Item : ArenaMutableState.Items)
	{
		ApplyArenaStateItem(Item);
	}
}

void AGrenadeGameState::ApplyArenaStateItem(const FArenaMutableStateItem& Item)
{
	if (AppliedArenaLayoutRevision != ArenaLayoutRevision)
	{
		return;
	}

	ApplyArenaObjectPresentation(Item.ArenaId);
	if (Item.State == EArenaObjectState::Destroyed)
	{
		InvalidateCharacterBasesForObject(Item.ArenaId);
		UE_LOG(
			Loghe_grenade_game,
			Log,
			TEXT("ARENA_STATE role=%s id=%d state=destroyed item_revision=%d global_revision=%d cause=%d"),
			HasAuthority() ? TEXT("authority") : TEXT("mirror"),
			Item.ArenaId,
			Item.StateRevision,
			ArenaStateRevision,
			static_cast<int32>(Item.Cause));
	}
}

void AGrenadeGameState::ApplyArenaObjectPresentation(const int32 ArenaId)
{
	const bool bDestroyed = IsArenaObjectDestroyed(ArenaId);
	if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Components = ComponentsByArenaId.Find(ArenaId))
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& ComponentPtr : *Components)
		{
			if (UStaticMeshComponent* Component = ComponentPtr.Get())
			{
				Component->SetCollisionEnabled(
					bDestroyed ? ECollisionEnabled::NoCollision : ECollisionEnabled::QueryAndPhysics);
				Component->SetVisibility(!bDestroyed, true);
			}
		}
	}

	if (bDestroyed)
	{
		LocalTrajectoryHighlights.Remove(ArenaId);
		LocalWarningAlpha.Remove(ArenaId);
	}
	UpdateObjectMaterials(ArenaId);
}

void AGrenadeGameState::InvalidateCharacterBasesForObject(const int32 ArenaId)
{
	const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Components = ComponentsByArenaId.Find(ArenaId);
	if (!Components || !GetWorld())
	{
		return;
	}

	TSet<const UPrimitiveComponent*> ComponentSet;
	for (const TWeakObjectPtr<UStaticMeshComponent>& Component : *Components)
	{
		if (Component.IsValid())
		{
			ComponentSet.Add(Component.Get());
		}
	}

	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		ACharacter* Character = *It;
		UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		const UPrimitiveComponent* MovementBase = Character
			? Cast<UPrimitiveComponent>(Character->GetMovementBaseObject())
			: nullptr;
		if (Movement && MovementBase && ComponentSet.Contains(MovementBase))
		{
			UE_LOG(
				Loghe_grenade_game,
				Display,
				TEXT("ARENA_BASE_INVALIDATE role=%d id=%d character=%s movement_mode=%d"),
				static_cast<int32>(Character->GetLocalRole()),
				ArenaId,
				*GetNameSafe(Character),
				static_cast<int32>(Movement->MovementMode));
			Movement->SetBase(static_cast<FMovementBaseInterfaceData*>(nullptr));
			Movement->bForceNextFloorCheck = true;
		}
	}
}

bool AGrenadeGameState::DestroyArenaObject(
	const int32 ArenaId,
	const EArenaDestructionCause Cause)
{
	if (!HasAuthority() || GetNetMode() == NM_Client)
	{
		return false;
	}

	const FArenaObjectLayout* Object = FindObjectLayout(ArenaId);
	const int32* MutableIndex = MutableIndexByArenaId.Find(ArenaId);
	if (!Object || !Object->bDestructible || !MutableIndex || !ArenaMutableState.Items.IsValidIndex(*MutableIndex))
	{
		return false;
	}

	FArenaMutableStateItem& Item = ArenaMutableState.Items[*MutableIndex];
	if (Item.State == EArenaObjectState::Destroyed)
	{
		return false;
	}

	Item.State = EArenaObjectState::Destroyed;
	Item.StateRevision = ++ArenaStateRevision;
	Item.Cause = Cause;
	ArenaMutableState.MarkItemDirty(Item);
	ApplyArenaStateItem(Item);
	ForceNetUpdate();
	return true;
}

bool AGrenadeGameState::IsArenaObjectDestroyed(const int32 ArenaId) const
{
	if (const int32* Index = MutableIndexByArenaId.Find(ArenaId))
	{
		return ArenaMutableState.Items.IsValidIndex(*Index)
			&& ArenaMutableState.Items[*Index].State == EArenaObjectState::Destroyed;
	}
	return false;
}

bool AGrenadeGameState::ResolveArenaHit(
	const FHitResult& Hit,
	int32& OutArenaId,
	bool& bOutBreakable,
	bool& bOutBounceBeforeBreaking) const
{
	OutArenaId = INDEX_NONE;
	bOutBreakable = false;
	bOutBounceBeforeBreaking = false;

	const UPrimitiveComponent* Component = Hit.GetComponent();
	if (!Component)
	{
		return false;
	}

	const int32* ArenaId = ArenaIdByComponent.Find(Component);
	if (!ArenaId)
	{
		return false;
	}

	const FArenaObjectLayout* Object = FindObjectLayout(*ArenaId);
	if (!Object)
	{
		return false;
	}

	OutArenaId = *ArenaId;
	bOutBreakable = Object->bDestructible
		&& Object->bBreakOnGrenadeImpact
		&& !IsArenaObjectDestroyed(*ArenaId);
	bOutBounceBeforeBreaking = Object->bBounceBeforeBreaking;
	return true;
}

void AGrenadeGameState::AppendArenaObjectIgnoredComponents(
	const int32 ArenaId,
	FCollisionQueryParams& QueryParams) const
{
	if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Components = ComponentsByArenaId.Find(ArenaId))
	{
		for (const TWeakObjectPtr<UStaticMeshComponent>& Component : *Components)
		{
			if (Component.IsValid())
			{
				QueryParams.AddIgnoredComponent(Component.Get());
			}
		}
	}
}

void AGrenadeGameState::SetLocalTrajectoryHighlight(
	const int32 ArenaId,
	const bool bHighlighted)
{
	const FArenaObjectLayout* Object = FindObjectLayout(ArenaId);
	if (!Object || !Object->bDestructible || IsArenaObjectDestroyed(ArenaId))
	{
		LocalTrajectoryHighlights.Remove(ArenaId);
		UpdateObjectMaterials(ArenaId);
		return;
	}

	if (bHighlighted)
	{
		LocalTrajectoryHighlights.Add(ArenaId);
	}
	else
	{
		LocalTrajectoryHighlights.Remove(ArenaId);
	}
	UpdateObjectMaterials(ArenaId);
}

void AGrenadeGameState::UpdateCollapseWarningVisuals()
{
	if (AppliedArenaLayoutRevision != ArenaLayoutRevision)
	{
		return;
	}

	const float WarningAlpha = FloorCollapseState.bActive ? GetFloorCollapseProgress() : 0.0f;
	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		if (Object.Type != EArenaObjectType::FloorTile || IsArenaObjectDestroyed(Object.ArenaId))
		{
			continue;
		}

		const float NewAlpha =
			FloorCollapseState.bActive && Object.CollapseRing == FloorCollapseState.RingIndex
			? WarningAlpha
			: 0.0f;
		const float OldAlpha = LocalWarningAlpha.FindRef(Object.ArenaId);
		if (!FMath::IsNearlyEqual(OldAlpha, NewAlpha, 0.005f))
		{
			if (NewAlpha <= KINDA_SMALL_NUMBER)
			{
				LocalWarningAlpha.Remove(Object.ArenaId);
			}
			else
			{
				LocalWarningAlpha.Add(Object.ArenaId, NewAlpha);
			}
			UpdateObjectMaterials(Object.ArenaId);
		}
	}
}

void AGrenadeGameState::UpdateObjectMaterials(const int32 ArenaId)
{
	const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Components = ComponentsByArenaId.Find(ArenaId);
	if (!Components)
	{
		return;
	}

	UMaterialInterface* HighlightMaterial =
		Cast<UMaterialInterface>(ArenaHeader.TrajectoryMaterialPath.ResolveObject());
	if (!HighlightMaterial && ArenaHeader.TrajectoryMaterialPath.IsValid())
	{
		HighlightMaterial = Cast<UMaterialInterface>(ArenaHeader.TrajectoryMaterialPath.TryLoad());
	}

	const bool bHighlighted = LocalTrajectoryHighlights.Contains(ArenaId);
	const float WarningAlpha = LocalWarningAlpha.FindRef(ArenaId);
	for (const TWeakObjectPtr<UStaticMeshComponent>& ComponentPtr : *Components)
	{
		UStaticMeshComponent* Component = ComponentPtr.Get();
		if (!Component)
		{
			continue;
		}

		UMaterialInterface* BaseMaterial = BaseMaterialByComponent.FindRef(Component).Get();
		if (bHighlighted && HighlightMaterial)
		{
			Component->SetMaterial(0, HighlightMaterial);
			continue;
		}

		if (WarningAlpha > KINDA_SMALL_NUMBER && BaseMaterial && HighlightMaterial)
		{
			UMaterialInstanceDynamic* MID = WarningMIDByComponent.FindRef(Component).Get();
			if (!MID)
			{
				MID = UMaterialInstanceDynamic::Create(BaseMaterial, this);
				WarningMIDByComponent.Add(Component, MID);
			}
			if (MID)
			{
				static const FName VectorNames[] = { TEXT("BaseColor"), TEXT("EmissiveColor") };
				for (const FName ParameterName : VectorNames)
				{
					FLinearColor BaseValue;
					FLinearColor WarningValue;
					const FMaterialParameterInfo Info(ParameterName);
					if (BaseMaterial->GetVectorParameterValue(Info, BaseValue)
						&& HighlightMaterial->GetVectorParameterValue(Info, WarningValue))
					{
						MID->SetVectorParameterValue(
							ParameterName,
							FMath::Lerp(BaseValue, WarningValue, WarningAlpha));
					}
				}
				Component->SetMaterial(0, MID);
				continue;
			}
		}

		Component->SetMaterial(0, BaseMaterial);
	}
}

void AGrenadeGameState::SetFloorCollapseState(
	const bool bActive,
	const int32 RingIndex,
	const float DurationSeconds)
{
	if (!HasAuthority() || GetNetMode() == NM_Client)
	{
		return;
	}

	FloorCollapseState.bActive = bActive;
	FloorCollapseState.RingIndex = bActive ? RingIndex : INDEX_NONE;
	FloorCollapseState.EndServerTime = bActive && DurationSeconds > 0.0f
		? GetServerWorldTimeSeconds() + DurationSeconds
		: 0.0f;
	FloorCollapseState.Duration = bActive ? FMath::Max(0.0f, DurationSeconds) : 0.0f;
	++FloorCollapseState.Revision;
	UpdateCollapseWarningVisuals();
	ForceNetUpdate();
}

float AGrenadeGameState::GetFloorCollapseTimeRemaining() const
{
	return FloorCollapseState.EndServerTime > 0.0f
		? FMath::Max(0.0f, FloorCollapseState.EndServerTime - GetServerWorldTimeSeconds())
		: 0.0f;
}

float AGrenadeGameState::GetFloorCollapseProgress() const
{
	if (!FloorCollapseState.bActive || FloorCollapseState.EndServerTime <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(
		1.0f - (GetFloorCollapseTimeRemaining() / FMath::Max(0.01f, FloorCollapseState.Duration)),
		0.0f,
		1.0f);
}

void AGrenadeGameState::GetFloorArenaIdsForRing(
	const int32 RingIndex,
	TArray<int32>& OutArenaIds) const
{
	OutArenaIds.Reset();
	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		if (Object.Type == EArenaObjectType::FloorTile && Object.CollapseRing == RingIndex)
		{
			OutArenaIds.Add(Object.ArenaId);
		}
	}
}

bool AGrenadeGameState::GetArenaObjectType(
	const int32 ArenaId,
	EArenaObjectType& OutType) const
{
	if (const FArenaObjectLayout* Object = FindObjectLayout(ArenaId))
	{
		OutType = Object->Type;
		return true;
	}
	return false;
}

bool AGrenadeGameState::FindIntactArenaObjectBounds(
	const EArenaObjectType Type,
	int32& OutArenaId,
	FBox& OutWorldBounds,
	const int32 ExcludedArenaId) const
{
	OutArenaId = INDEX_NONE;
	OutWorldBounds = FBox(ForceInit);

	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		if (Object.Type != Type
			|| Object.ArenaId == ExcludedArenaId
			|| IsArenaObjectDestroyed(Object.ArenaId))
		{
			continue;
		}
		if (Type == EArenaObjectType::BreakableObstacle
			&& !Object.bBounceBeforeBreaking)
		{
			continue;
		}

		FBox CandidateBounds(ForceInit);
		if (const TArray<TWeakObjectPtr<UStaticMeshComponent>>* Components =
			ComponentsByArenaId.Find(Object.ArenaId))
		{
			for (const TWeakObjectPtr<UStaticMeshComponent>& ComponentPtr : *Components)
			{
				if (const UStaticMeshComponent* Component = ComponentPtr.Get())
				{
					CandidateBounds += Component->Bounds.GetBox();
				}
			}
		}
		if (CandidateBounds.IsValid)
		{
			if (Type == EArenaObjectType::StaticObstacle)
			{
				const FVector Extent = CandidateBounds.GetExtent();
				if (Extent.Z <= 100.0f || FMath::Min(Extent.X, Extent.Y) >= 100.0f)
				{
					continue;
				}
			}
			OutArenaId = Object.ArenaId;
			OutWorldBounds = CandidateBounds;
			return true;
		}
	}
	return false;
}

bool AGrenadeGameState::HasAppliedArenaLayout() const
{
	return ArenaLayoutRevision > 0
		&& AppliedArenaLayoutRevision == ArenaLayoutRevision
		&& RuntimeComponents.Num() > 0;
}

bool AGrenadeGameState::HasCompleteArenaState() const
{
	if (!HasAppliedArenaLayout()
		|| ArenaMutableState.Items.Num() != ArenaHeader.DestructibleCount)
	{
		return false;
	}

	int32 HighestRevision = 0;
	for (const FArenaMutableStateItem& Item : ArenaMutableState.Items)
	{
		HighestRevision = FMath::Max(HighestRevision, Item.StateRevision);
	}
	return HighestRevision == ArenaStateRevision;
}

void AGrenadeGameState::TryConfirmArenaReady()
{
	if (!HasCompleteArenaState() || !GetWorld())
	{
		return;
	}

	if (Ahe_grenade_gamePlayerController* LocalController =
		Cast<Ahe_grenade_gamePlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		if (!LocalController->IsLocalController())
		{
			return;
		}
		if (const AGrenadePlayerState* PlayerState =
			LocalController->GetPlayerState<AGrenadePlayerState>())
		{
			if (PlayerState->IsArenaReady()
				&& PlayerState->GetReadyLayoutRevision() == ArenaLayoutRevision
				&& PlayerState->GetReadyLayoutChecksum() == ArenaLayoutChecksum)
			{
				return;
			}
		}
		if (LastReadyAttemptLayoutRevision == ArenaLayoutRevision
			&& LastReadyAttemptStateRevision == ArenaStateRevision)
		{
			return;
		}
		LastReadyAttemptLayoutRevision = ArenaLayoutRevision;
		LastReadyAttemptStateRevision = ArenaStateRevision;
		LocalController->ConfirmArenaState(
			ArenaLayoutRevision,
			ArenaLayoutChecksum,
			ArenaStateRevision);
	}
}

int64 AGrenadeGameState::CalculateArenaLayoutChecksum() const
{
	uint32 Hash = GetTypeHash(ArenaHeader.SchemaVersion);
	Hash = HashCombine(Hash, GetTypeHash(ArenaHeader.TilesX));
	Hash = HashCombine(Hash, GetTypeHash(ArenaHeader.TilesY));
	Hash = HashCombine(Hash, HashFloat(ArenaHeader.TileSizeCm));
	Hash = HashCombine(Hash, HashFloat(ArenaHeader.TileSpacingCm));
	Hash = HashCombine(Hash, HashFloat(ArenaHeader.TileThicknessScale));
	Hash = HashCombine(Hash, HashVector(ArenaHeader.ArenaOrigin));
	Hash = HashCombine(Hash, GetTypeHash(ArenaHeader.TrajectoryMaterialPath.ToString()));
	Hash = HashCombine(Hash, GetTypeHash(ArenaHeader.GrenadeMaterialPath.ToString()));

	for (const FArenaAssetDefinition& Asset : ArenaAssets)
	{
		Hash = HashCombine(Hash, GetTypeHash(Asset.AssetId));
		Hash = HashCombine(Hash, GetTypeHash(Asset.MeshPath.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(Asset.MaterialPath.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(Asset.CollisionProfile));
	}

	for (const FArenaObjectLayout& Object : ArenaObjects)
	{
		Hash = HashCombine(Hash, GetTypeHash(Object.ArenaId));
		Hash = HashCombine(Hash, GetTypeHash(static_cast<uint8>(Object.Type)));
		Hash = HashCombine(Hash, HashTransform(Object.Transform));
		Hash = HashCombine(Hash, GetTypeHash(Object.GridX));
		Hash = HashCombine(Hash, GetTypeHash(Object.GridY));
		Hash = HashCombine(Hash, GetTypeHash(Object.CollapseRing));
		Hash = HashCombine(Hash, GetTypeHash(Object.PrimaryAssetId));
		Hash = HashCombine(Hash, GetTypeHash(Object.FirstComponentIndex));
		Hash = HashCombine(Hash, GetTypeHash(Object.ComponentCount));
		Hash = HashCombine(Hash, GetTypeHash(Object.bDestructible));
		Hash = HashCombine(Hash, GetTypeHash(Object.bBreakOnGrenadeImpact));
		Hash = HashCombine(Hash, GetTypeHash(Object.bBounceBeforeBreaking));
	}

	for (const FArenaComponentLayout& Component : ArenaComponents)
	{
		Hash = HashCombine(Hash, GetTypeHash(Component.AssetId));
		Hash = HashCombine(Hash, HashTransform(Component.RelativeTransform));
		Hash = HashCombine(Hash, GetTypeHash(Component.bVisible));
	}

	return static_cast<int64>(Hash);
}

int32 AGrenadeGameState::EstimateArenaSnapshotBytes() const
{
	int32 Bytes = sizeof(FArenaSnapshotHeader)
		+ (ArenaObjects.Num() * sizeof(FArenaObjectLayout))
		+ (ArenaComponents.Num() * sizeof(FArenaComponentLayout));
	for (const FArenaAssetDefinition& Asset : ArenaAssets)
	{
		Bytes += sizeof(uint16) + sizeof(FName);
		Bytes += Asset.MeshPath.ToString().Len() * sizeof(TCHAR);
		Bytes += Asset.MaterialPath.ToString().Len() * sizeof(TCHAR);
	}
	return Bytes;
}

float AGrenadeGameState::GetPhaseTimeRemaining() const
{
	return PhaseEndServerTime > 0.0f
		? FMath::Max(0.0f, PhaseEndServerTime - GetServerWorldTimeSeconds())
		: 0.0f;
}

void AGrenadeGameState::SetGrenadeMatchPhase(
	const EGGMatchPhase NewPhase,
	const float DurationSeconds)
{
	if (!HasAuthority() || GetNetMode() == NM_Client)
	{
		return;
	}

	GrenadeMatchPhase = NewPhase;
	PhaseEndServerTime = DurationSeconds > 0.0f
		? GetServerWorldTimeSeconds() + DurationSeconds
		: 0.0f;
	++MatchStateRevision;
	UE_LOG(
		Loghe_grenade_game,
		Display,
		TEXT("MATCH_PHASE role=authority phase=%d revision=%d duration=%.2f"),
		static_cast<int32>(GrenadeMatchPhase),
		MatchStateRevision,
		FMath::Max(0.0f, DurationSeconds));
	ForceNetUpdate();
}
