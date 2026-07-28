// Copyright Epic Games, Inc. All Rights Reserved.

#include "GrenadeGameState.h"

#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Grenade/ArenaObstacle.h"
#include "Grenade/Breakables/BreakableTile.h"
#include "Grenade/Breakables/BreakableTileGrid.h"
#include "he_grenade_game.h"
#include "he_grenade_gamePlayerController.h"
#include "Materials/MaterialInterface.h"
#include "Net/UnrealNetwork.h"

namespace
{
	const FName ReplicatedArenaGeneratedTag(TEXT("GeneratedArena"));
	const FName ReplicatedArenaClientReplicaTag(TEXT("ClientArenaReplica"));

	uint32 HashFloat(const float Value)
	{
		// Replicated transforms may differ by harmless sub-millimetre float
		// roundoff, so checksum the gameplay-significant 0.1 cm/degree value.
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
		return HashCombine(HashCombine(HashVector(Value.GetLocation()), HashRotator(Value.Rotator())), HashVector(Value.GetScale3D()));
	}
}

AGrenadeGameState::AGrenadeGameState()
{
	bReplicates = true;
	PrimaryActorTick.bCanEverTick = true;
}

void AGrenadeGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AGrenadeGameState, GrenadeMatchPhase);
	DOREPLIFETIME(AGrenadeGameState, PhaseEndServerTime);
	DOREPLIFETIME(AGrenadeGameState, MatchStateRevision);
	DOREPLIFETIME(AGrenadeGameState, ArenaGridLayout);
	DOREPLIFETIME(AGrenadeGameState, ArenaActors);
	DOREPLIFETIME(AGrenadeGameState, ArenaMeshes);
	DOREPLIFETIME(AGrenadeGameState, ArenaLayoutRevision);
	DOREPLIFETIME(AGrenadeGameState, ArenaLayoutChecksum);
	DOREPLIFETIME(AGrenadeGameState, ReplicatedFloorBrokenStates);
	DOREPLIFETIME(AGrenadeGameState, ReplicatedActorBrokenStates);
	DOREPLIFETIME(AGrenadeGameState, bReplicatedFloorCollapseActive);
	DOREPLIFETIME(AGrenadeGameState, ReplicatedFloorCollapseRing);
	DOREPLIFETIME(AGrenadeGameState, FloorCollapseEndServerTime);
	DOREPLIFETIME(AGrenadeGameState, FloorCollapseDuration);
}

void AGrenadeGameState::Tick(const float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (HasAuthority())
	{
		BreakStateRefreshAccumulator += DeltaSeconds;
		if (BreakStateRefreshAccumulator >= 0.1f)
		{
			BreakStateRefreshAccumulator = 0.0f;
			RefreshAuthoritativeBreakStates();
		}
	}
	else
	{
		ApplyReplicatedArenaState();
	}
}

void AGrenadeGameState::PublishGeneratedArena(
	ABreakableTileGrid* Grid,
	UMaterialInterface* FloorMaterial,
	UMaterialInterface* TrajectoryMaterial)
{
	if (!HasAuthority() || !Grid || !GetWorld())
	{
		return;
	}

	ArenaGridLayout.Transform = Grid->GetActorTransform();
	ArenaGridLayout.TilesX = Grid->TilesX;
	ArenaGridLayout.TilesY = Grid->TilesY;
	ArenaGridLayout.TileSizeCm = Grid->TileSizeCm;
	ArenaGridLayout.TileSpacingCm = Grid->TileSpacingCm;
	ArenaGridLayout.TileThicknessScale = Grid->TileThicknessScale;
	ArenaGridLayout.GridLocalOriginOffset = Grid->GridLocalOriginOffset;
	ArenaGridLayout.FloorMaterialPath = FloorMaterial ? FSoftObjectPath(FloorMaterial) : FSoftObjectPath();
	ArenaGridLayout.TrajectoryMaterialPath = TrajectoryMaterial ? FSoftObjectPath(TrajectoryMaterial) : FSoftObjectPath();

	TSet<const AActor*> FloorTiles;
	ServerFloorTiles.Reset();
	for (const ABreakableTile* Tile : Grid->GetSpawnedTiles())
	{
		FloorTiles.Add(Tile);
		ServerFloorTiles.Add(const_cast<ABreakableTile*>(Tile));
	}

	ArenaActors.Reset();
	ArenaMeshes.Reset();
	ServerLayoutBreakables.Reset();

	int32 StableActorIndex = 0;
	for (TActorIterator<AActor> It(GetWorld()); It; ++It)
	{
		AActor* Actor = *It;
		if (!IsValid(Actor)
			|| Actor == Grid
			|| FloorTiles.Contains(Actor)
			|| !Actor->ActorHasTag(ReplicatedArenaGeneratedTag))
		{
			continue;
		}

		TInlineComponentArray<UStaticMeshComponent*> MeshComponents;
		Actor->GetComponents(MeshComponents);
		if (MeshComponents.IsEmpty())
		{
			continue;
		}

		Actor->Rename(
			*FString::Printf(TEXT("ArenaObject_%d"), StableActorIndex),
			nullptr,
			REN_DontCreateRedirectors);
		Actor->SetNetAddressable();
		for (int32 ComponentIndex = 0; ComponentIndex < MeshComponents.Num(); ++ComponentIndex)
		{
			if (UStaticMeshComponent* Component = MeshComponents[ComponentIndex])
			{
				Component->Rename(
					*FString::Printf(TEXT("ArenaMesh_%d"), ComponentIndex),
					Actor,
					REN_DontCreateRedirectors);
				Component->SetNetAddressable();
			}
		}

		FReplicatedArenaActorLayout& ActorLayout = ArenaActors.AddDefaulted_GetRef();
		ActorLayout.Transform = Actor->GetActorTransform();
		ActorLayout.FirstComponentIndex = ArenaMeshes.Num();
		ActorLayout.ComponentCount = MeshComponents.Num();

		if (const ABreakableTile* Breakable = Cast<ABreakableTile>(Actor))
		{
			ActorLayout.bBreakableTile = true;
			ActorLayout.bBreakOnGrenadeImpact = Breakable->CanBreakOnGrenadeImpact();
			ActorLayout.bBounceBeforeBreaking = Breakable->ShouldBounceGrenadeBeforeBreaking();
			ServerLayoutBreakables.Add(const_cast<ABreakableTile*>(Breakable));
		}
		else
		{
			ServerLayoutBreakables.Add(nullptr);
		}

		for (const UStaticMeshComponent* Component : MeshComponents)
		{
			FReplicatedArenaMeshLayout& MeshLayout = ArenaMeshes.AddDefaulted_GetRef();
			MeshLayout.MeshPath = Component && Component->GetStaticMesh()
				? FSoftObjectPath(Component->GetStaticMesh())
				: FSoftObjectPath();
			MeshLayout.MaterialPath = Component && Component->GetMaterial(0)
				? FSoftObjectPath(Component->GetMaterial(0))
				: FSoftObjectPath();
			if (Component)
			{
				MeshLayout.RelativeLocation = Component->GetRelativeLocation();
				MeshLayout.RelativeRotation = Component->GetRelativeRotation();
				MeshLayout.RelativeScale = Component->GetRelativeScale3D();
				MeshLayout.CollisionProfile = Component->GetCollisionProfileName();
				MeshLayout.bVisible = Component->IsVisible();
			}
		}
		++StableActorIndex;
	}

	++ArenaLayoutRevision;
	ReplicatedFloorBrokenStates.Init(0, ServerFloorTiles.Num());
	ReplicatedActorBrokenStates.Init(0, ServerLayoutBreakables.Num());
	ArenaLayoutChecksum = CalculateArenaLayoutChecksum();
	SetGrenadeMatchPhase(EGGMatchPhase::ArenaSync);
	ForceNetUpdate();
	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Published arena layout revision %d checksum %lld: grid %dx%d, actors %d, meshes %d."),
		ArenaLayoutRevision,
		ArenaLayoutChecksum,
		ArenaGridLayout.TilesX,
		ArenaGridLayout.TilesY,
		ArenaActors.Num(),
		ArenaMeshes.Num());
}

void AGrenadeGameState::OnRep_ArenaLayout()
{
	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Arena layout notification: revision %d checksum %lld, actors %d, meshes %d."),
		ArenaLayoutRevision,
		ArenaLayoutChecksum,
		ArenaActors.Num(),
		ArenaMeshes.Num());
	if (ArenaLayoutRevision > 0 && ArenaLayoutChecksum != 0)
	{
		BuildClientArenaReplica();
	}
}

void AGrenadeGameState::ClearClientArenaReplica()
{
	if (GetWorld())
	{
		TArray<AActor*> TaggedActors;
		for (TActorIterator<AActor> It(GetWorld()); It; ++It)
		{
			if (It->ActorHasTag(ReplicatedArenaClientReplicaTag))
			{
				TaggedActors.Add(*It);
			}
		}
		for (AActor* Actor : TaggedActors)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	for (AActor* Actor : ClientArenaActors)
	{
		if (IsValid(Actor))
		{
			Actor->Destroy();
		}
	}
	ClientArenaActors.Reset();
	ClientFloorTiles.Reset();
	ClientLayoutBreakables.Reset();
}

void AGrenadeGameState::BuildClientArenaReplica()
{
	if (HasAuthority()
		|| AppliedArenaLayoutRevision == ArenaLayoutRevision
		|| ArenaGridLayout.TilesX <= 0
		|| ArenaGridLayout.TilesY <= 0
		|| !GetWorld())
	{
		return;
	}

	ClearClientArenaReplica();

	ABreakableTileGrid* Grid = GetWorld()->SpawnActorDeferred<ABreakableTileGrid>(
		ABreakableTileGrid::StaticClass(),
		ArenaGridLayout.Transform);
	if (!Grid)
	{
		return;
	}

	Grid->Tags.Add(ReplicatedArenaClientReplicaTag);
	Grid->bSpawnOnBeginPlay = false;
	Grid->TilesX = ArenaGridLayout.TilesX;
	Grid->TilesY = ArenaGridLayout.TilesY;
	Grid->TileSizeCm = ArenaGridLayout.TileSizeCm;
	Grid->TileSpacingCm = ArenaGridLayout.TileSpacingCm;
	Grid->TileThicknessScale = ArenaGridLayout.TileThicknessScale;
	Grid->GridLocalOriginOffset = ArenaGridLayout.GridLocalOriginOffset;
	Grid->FinishSpawning(ArenaGridLayout.Transform);
	Grid->BuildGrid();
	ClientArenaActors.Add(Grid);

	UMaterialInterface* FloorMaterial = Cast<UMaterialInterface>(ArenaGridLayout.FloorMaterialPath.TryLoad());
	UMaterialInterface* TrajectoryMaterial = Cast<UMaterialInterface>(ArenaGridLayout.TrajectoryMaterialPath.TryLoad());
	for (ABreakableTile* FloorTile : Grid->GetSpawnedTiles())
	{
		if (FloorTile)
		{
			FloorTile->Tags.Add(ReplicatedArenaClientReplicaTag);
			FloorTile->SetVisualMaterials(FloorMaterial, TrajectoryMaterial);
			FloorTile->ResetTile();
			ClientFloorTiles.Add(FloorTile);
		}
	}

	ClientLayoutBreakables.Reserve(ArenaActors.Num());
	for (int32 ActorIndex = 0; ActorIndex < ArenaActors.Num(); ++ActorIndex)
	{
		const FReplicatedArenaActorLayout& ActorLayout = ArenaActors[ActorIndex];
		FActorSpawnParameters SpawnParams;
		SpawnParams.Name = FName(*FString::Printf(TEXT("ArenaObject_%d"), ActorIndex));
		SpawnParams.NameMode = FActorSpawnParameters::ESpawnActorNameMode::Requested;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		AActor* SpawnedActor = ActorLayout.bBreakableTile
			? static_cast<AActor*>(GetWorld()->SpawnActor<ABreakableTile>(ABreakableTile::StaticClass(), FTransform::Identity, SpawnParams))
			: static_cast<AActor*>(GetWorld()->SpawnActor<AArenaObstacle>(AArenaObstacle::StaticClass(), FTransform::Identity, SpawnParams));
		if (!SpawnedActor)
		{
			continue;
		}

		SpawnedActor->Tags.Add(ReplicatedArenaClientReplicaTag);
		SpawnedActor->SetNetAddressable();
		ClientArenaActors.Add(SpawnedActor);

		ABreakableTile* Breakable = Cast<ABreakableTile>(SpawnedActor);
		ClientLayoutBreakables.Add(Breakable);
		AArenaObstacle* Obstacle = Cast<AArenaObstacle>(SpawnedActor);
		UStaticMeshComponent* RootMesh = Breakable ? Breakable->TileMesh.Get() : (Obstacle ? Obstacle->ObstacleMesh.Get() : nullptr);
		if (RootMesh)
		{
			RootMesh->SetMobility(EComponentMobility::Movable);
		}
		SpawnedActor->SetActorTransform(ActorLayout.Transform);
		if (Breakable)
		{
			Breakable->bBreakOnGrenadeImpact = ActorLayout.bBreakOnGrenadeImpact;
			Breakable->bBounceGrenadeBeforeBreaking = ActorLayout.bBounceBeforeBreaking;
		}

		for (int32 LocalIndex = 0; LocalIndex < ActorLayout.ComponentCount; ++LocalIndex)
		{
			const int32 MeshIndex = ActorLayout.FirstComponentIndex + LocalIndex;
			if (!ArenaMeshes.IsValidIndex(MeshIndex))
			{
				continue;
			}

			const FReplicatedArenaMeshLayout& MeshLayout = ArenaMeshes[MeshIndex];
			UStaticMesh* Mesh = Cast<UStaticMesh>(MeshLayout.MeshPath.TryLoad());
			UMaterialInterface* Material = Cast<UMaterialInterface>(MeshLayout.MaterialPath.TryLoad());
			UStaticMeshComponent* Component = nullptr;

			if (LocalIndex == 0)
			{
				Component = Breakable ? Breakable->TileMesh.Get() : (Obstacle ? Obstacle->ObstacleMesh.Get() : nullptr);
			}
			else if (Breakable)
			{
				Component = Breakable->AddCompositeShapePiece(
					Mesh,
					MeshLayout.RelativeLocation,
					MeshLayout.RelativeRotation,
					MeshLayout.RelativeScale);
			}

			if (!Component)
			{
				continue;
			}

			Component->Rename(
				*FString::Printf(TEXT("ArenaMesh_%d"), LocalIndex),
				SpawnedActor,
				REN_DontCreateRedirectors);
			Component->SetNetAddressable();
			Component->SetStaticMesh(Mesh);
			Component->SetRelativeLocation(MeshLayout.RelativeLocation);
			Component->SetRelativeRotation(MeshLayout.RelativeRotation);
			Component->SetRelativeScale3D(MeshLayout.RelativeScale);
			Component->SetCollisionProfileName(MeshLayout.CollisionProfile);
			Component->SetVisibility(MeshLayout.bVisible, true);
			if (Material)
			{
				Component->SetMaterial(0, Material);
			}
		}

		if (Breakable && ActorLayout.ComponentCount > 0)
		{
			UMaterialInterface* NormalMaterial = nullptr;
			for (int32 LocalIndex = 0; LocalIndex < ActorLayout.ComponentCount && !NormalMaterial; ++LocalIndex)
			{
				const int32 MeshIndex = ActorLayout.FirstComponentIndex + LocalIndex;
				if (ArenaMeshes.IsValidIndex(MeshIndex) && ArenaMeshes[MeshIndex].MaterialPath.IsValid())
				{
					NormalMaterial = Cast<UMaterialInterface>(ArenaMeshes[MeshIndex].MaterialPath.TryLoad());
				}
			}
			Breakable->SetVisualMaterials(NormalMaterial, TrajectoryMaterial);
		}

		if (RootMesh)
		{
			RootMesh->SetMobility(EComponentMobility::Static);
		}
	}

	const int64 LocalChecksum = CalculateArenaLayoutChecksum();
	if (LocalChecksum != ArenaLayoutChecksum)
	{
		UE_LOG(
			Loghe_grenade_game,
			Error,
			TEXT("Client arena checksum mismatch: expected %lld, calculated %lld."),
			ArenaLayoutChecksum,
			LocalChecksum);
		ClearClientArenaReplica();
		return;
	}

	AppliedArenaLayoutRevision = ArenaLayoutRevision;
	ApplyReplicatedArenaState();
	UE_LOG(
		Loghe_grenade_game,
		Log,
		TEXT("Client applied arena layout revision %d checksum %lld: actors %d, meshes %d."),
		ArenaLayoutRevision,
		LocalChecksum,
		ArenaActors.Num(),
		ArenaMeshes.Num());
	if (Ahe_grenade_gamePlayerController* LocalController =
		Cast<Ahe_grenade_gamePlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		LocalController->ConfirmArenaLayout(ArenaLayoutRevision, LocalChecksum);
	}
}

void AGrenadeGameState::RefreshAuthoritativeBreakStates()
{
	bool bChanged = false;
	for (int32 Index = 0; Index < ServerFloorTiles.Num(); ++Index)
	{
		const uint8 NewState = ServerFloorTiles[Index].IsValid() && ServerFloorTiles[Index]->IsBroken() ? 1 : 0;
		if (ReplicatedFloorBrokenStates.IsValidIndex(Index) && ReplicatedFloorBrokenStates[Index] != NewState)
		{
			ReplicatedFloorBrokenStates[Index] = NewState;
			bChanged = true;
		}
	}
	for (int32 Index = 0; Index < ServerLayoutBreakables.Num(); ++Index)
	{
		const uint8 NewState = ServerLayoutBreakables[Index].IsValid() && ServerLayoutBreakables[Index]->IsBroken() ? 1 : 0;
		if (ReplicatedActorBrokenStates.IsValidIndex(Index) && ReplicatedActorBrokenStates[Index] != NewState)
		{
			ReplicatedActorBrokenStates[Index] = NewState;
			bChanged = true;
		}
	}
	if (bChanged)
	{
		ForceNetUpdate();
	}
}

void AGrenadeGameState::OnRep_ArenaState()
{
	ApplyReplicatedArenaState();
}

void AGrenadeGameState::ApplyReplicatedArenaState()
{
	if (HasAuthority() || AppliedArenaLayoutRevision != ArenaLayoutRevision)
	{
		return;
	}

	const float WarningAlpha = GetFloorCollapseProgress();
	const int32 MaxX = ArenaGridLayout.TilesX - 1;
	const int32 MaxY = ArenaGridLayout.TilesY - 1;
	for (int32 Index = 0; Index < ClientFloorTiles.Num(); ++Index)
	{
		ABreakableTile* Tile = ClientFloorTiles[Index];
		if (!Tile)
		{
			continue;
		}
		const bool bBroken = ReplicatedFloorBrokenStates.IsValidIndex(Index) && ReplicatedFloorBrokenStates[Index] != 0;
		if (bBroken)
		{
			if (!Tile->IsBroken())
			{
				Tile->BreakTile();
			}
			continue;
		}
		const int32 X = ArenaGridLayout.TilesX > 0 ? Index % ArenaGridLayout.TilesX : 0;
		const int32 Y = ArenaGridLayout.TilesX > 0 ? Index / ArenaGridLayout.TilesX : 0;
		const int32 Ring = FMath::Min(FMath::Min(X, MaxX - X), FMath::Min(Y, MaxY - Y));
		Tile->SetDestructionWarningAlpha(
			bReplicatedFloorCollapseActive && Ring == ReplicatedFloorCollapseRing ? WarningAlpha : 0.0f);
	}

	for (int32 Index = 0; Index < ClientLayoutBreakables.Num(); ++Index)
	{
		ABreakableTile* Tile = ClientLayoutBreakables[Index];
		if (Tile && ReplicatedActorBrokenStates.IsValidIndex(Index) && ReplicatedActorBrokenStates[Index] != 0 && !Tile->IsBroken())
		{
			Tile->BreakTile();
		}
	}
}

void AGrenadeGameState::SetFloorCollapseState(const bool bActive, const int32 RingIndex, const float DurationSeconds)
{
	if (!HasAuthority())
	{
		return;
	}
	bReplicatedFloorCollapseActive = bActive;
	ReplicatedFloorCollapseRing = bActive ? RingIndex : INDEX_NONE;
	FloorCollapseEndServerTime = bActive && DurationSeconds > 0.0f
		? GetServerWorldTimeSeconds() + DurationSeconds
		: 0.0f;
	FloorCollapseDuration = bActive ? FMath::Max(0.0f, DurationSeconds) : 0.0f;
	ForceNetUpdate();
}

float AGrenadeGameState::GetFloorCollapseTimeRemaining() const
{
	return FloorCollapseEndServerTime > 0.0f
		? FMath::Max(0.0f, FloorCollapseEndServerTime - GetServerWorldTimeSeconds())
		: 0.0f;
}

float AGrenadeGameState::GetFloorCollapseProgress() const
{
	if (!bReplicatedFloorCollapseActive || FloorCollapseEndServerTime <= 0.0f)
	{
		return 1.0f;
	}
	return FMath::Clamp(
		1.0f - (GetFloorCollapseTimeRemaining() / FMath::Max(0.01f, FloorCollapseDuration)),
		0.0f,
		1.0f);
}

int64 AGrenadeGameState::CalculateArenaLayoutChecksum() const
{
	uint32 Hash = HashCombine(GetTypeHash(ArenaGridLayout.TilesX), GetTypeHash(ArenaGridLayout.TilesY));
	Hash = HashCombine(Hash, HashTransform(ArenaGridLayout.Transform));
	Hash = HashCombine(Hash, HashFloat(ArenaGridLayout.TileSizeCm));
	Hash = HashCombine(Hash, HashFloat(ArenaGridLayout.TileSpacingCm));
	Hash = HashCombine(Hash, HashFloat(ArenaGridLayout.TileThicknessScale));
	Hash = HashCombine(Hash, HashVector(ArenaGridLayout.GridLocalOriginOffset));

	for (const FReplicatedArenaActorLayout& Actor : ArenaActors)
	{
		Hash = HashCombine(Hash, HashTransform(Actor.Transform));
		Hash = HashCombine(Hash, GetTypeHash(Actor.FirstComponentIndex));
		Hash = HashCombine(Hash, GetTypeHash(Actor.ComponentCount));
		Hash = HashCombine(Hash, GetTypeHash(Actor.bBreakableTile));
		Hash = HashCombine(Hash, GetTypeHash(Actor.bBreakOnGrenadeImpact));
		Hash = HashCombine(Hash, GetTypeHash(Actor.bBounceBeforeBreaking));
	}

	for (const FReplicatedArenaMeshLayout& Mesh : ArenaMeshes)
	{
		Hash = HashCombine(Hash, GetTypeHash(Mesh.MeshPath.ToString()));
		Hash = HashCombine(Hash, GetTypeHash(Mesh.MaterialPath.ToString()));
		Hash = HashCombine(Hash, HashVector(Mesh.RelativeLocation));
		Hash = HashCombine(Hash, HashRotator(Mesh.RelativeRotation));
		Hash = HashCombine(Hash, HashVector(Mesh.RelativeScale));
		Hash = HashCombine(Hash, GetTypeHash(Mesh.CollisionProfile));
		Hash = HashCombine(Hash, GetTypeHash(Mesh.bVisible));
	}

	return static_cast<int64>(Hash);
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
