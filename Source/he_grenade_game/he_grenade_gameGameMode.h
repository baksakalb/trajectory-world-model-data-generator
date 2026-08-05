// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "he_grenade_gameGameMode.generated.h"

class AArenaObstacle;
class AController;
class UMaterialInterface;
class UStaticMesh;

/**
 * Curriculum V1 game mode.
 *
 * Builds one deterministic square arena at runtime. The complete procedural
 * grenade-game implementation is retained in Git at the
 * curriculum-complete-game-baseline tag.
 */
UCLASS(abstract)
class Ahe_grenade_gameGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	Ahe_grenade_gameGameMode();

	/** Side length of the fixed square arena. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena", meta = (ClampMin = "2000.0", Units = "cm"))
	float ArenaSizeCm = 3200.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena", meta = (ClampMin = "10.0", Units = "cm"))
	float FloorThicknessCm = 40.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena", meta = (ClampMin = "200.0", Units = "cm"))
	float ArenaWallHeightCm = 500.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena", meta = (ClampMin = "5.0", Units = "cm"))
	float ArenaWallThicknessCm = 24.0f;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> FloorTileMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> RectangleShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> TriangleShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> SphereShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> HoopShapeMaterial;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Arena")
	TObjectPtr<UMaterialInterface> RampShapeMaterial;

	/** Retained for source compatibility with the dormant grenade actor. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Curriculum|Future")
	TObjectPtr<UMaterialInterface> GrenadeMaterial;

protected:
	virtual void BeginPlay() override;
	virtual void RestartPlayer(AController* NewPlayer) override;

private:
	void BuildFixedCurriculumArena();
	void BuildFixedCurriculumLighting();
	void SpawnBox(
		FName ObjectTag,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& DimensionsCm,
		UMaterialInterface* Material);
	void SpawnMesh(
		FName ObjectTag,
		UStaticMesh* Mesh,
		const FVector& Location,
		const FRotator& Rotation,
		const FVector& Scale,
		UMaterialInterface* Material);
	FTransform PlayerSpawnTransform = FTransform::Identity;
	bool bFixedArenaBuilt = false;

	UPROPERTY()
	TObjectPtr<UStaticMesh> CubeMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> SphereMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> HoopMesh;

	UPROPERTY()
	TObjectPtr<UStaticMesh> PyramidMesh;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteRectangleMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteFloorMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteWallMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteTriangleMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteSphereMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteHoopMaterial;

	UPROPERTY()
	TObjectPtr<UMaterialInterface> MatteRampMaterial;
};
