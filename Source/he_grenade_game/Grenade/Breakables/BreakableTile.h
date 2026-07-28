#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreakableTile.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UStaticMesh;

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnBreakableTileBroken, ABreakableTile*, Tile);

UCLASS(BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API ABreakableTile : public AActor
{
	GENERATED_BODY()

public:
	ABreakableTile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile")
	bool bStartBroken = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Breakable")
	bool bBreakOnGrenadeImpact = true;

	/** When true, the tile applies one normal surface bounce and then disappears immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Breakable")
	bool bBounceGrenadeBeforeBreaking = false;

	UPROPERTY(BlueprintAssignable, Category = "Tile")
	FOnBreakableTileBroken OnTileBroken;

	UFUNCTION(BlueprintCallable, Category = "Tile")
	void BreakTile();

	UFUNCTION(BlueprintCallable, Category = "Tile")
	void ResetTile();

	UFUNCTION(BlueprintPure, Category = "Tile")
	bool IsBroken() const { return bBroken; }

	UFUNCTION(BlueprintPure, Category = "Tile|Breakable")
	bool CanBreakOnGrenadeImpact() const { return bBreakOnGrenadeImpact; }

	UFUNCTION(BlueprintPure, Category = "Tile|Breakable")
	bool ShouldBounceGrenadeBeforeBreaking() const { return bBounceGrenadeBeforeBreaking; }

	UFUNCTION(BlueprintCallable, Category = "Tile|Trajectory")
	void SetTrajectoryHighlighted(bool bHighlighted);

	UFUNCTION(BlueprintPure, Category = "Tile|Trajectory")
	bool IsTrajectoryHighlighted() const { return bTrajectoryHighlighted; }

	/** Blends the normal tile appearance toward the trajectory-warning material. */
	UFUNCTION(BlueprintCallable, Category = "Tile|Warning")
	void SetDestructionWarningAlpha(float WarningAlpha);

	UFUNCTION(BlueprintPure, Category = "Tile|Warning")
	float GetDestructionWarningAlpha() const { return DestructionWarningAlpha; }

	UFUNCTION(BlueprintCallable, Category = "Tile|Style")
	void SetMeshAndTransformStyle(UStaticMesh* InStaticMesh, FRotator InMeshRelativeRotation, FVector InMeshRelativeScale);

	UFUNCTION(BlueprintCallable, Category = "Tile|Style")
	void SetVisualMaterials(UMaterialInterface* InNormalMaterial, UMaterialInterface* InTrajectoryHighlightMaterial);

	/** Turns the root mesh into an invisible anchor so several mesh pieces can form one breakable shape. */
	void BeginCompositeShape();

	/** Adds one collidable mesh piece to a composite breakable shape. Dimensions are controlled by relative scale. */
	UStaticMeshComponent* AddCompositeShapePiece(
		UStaticMesh* InStaticMesh,
		const FVector& RelativeLocation,
		const FRotator& RelativeRotation,
		const FVector& RelativeScale);

protected:
	virtual void BeginPlay() override;

private:
	void ApplyTrajectoryHighlightState();
	void UpdateDestructionWarningMaterial();
	UMaterialInterface* GetActiveVisualMaterial() const;

	UPROPERTY(VisibleAnywhere, Category = "Tile")
	bool bBroken = false;

	UPROPERTY(VisibleAnywhere, Category = "Tile|Trajectory")
	bool bTrajectoryHighlighted = false;

	UPROPERTY(VisibleAnywhere, Category = "Tile|Warning")
	float DestructionWarningAlpha = 0.0f;

	UPROPERTY(Transient)
	TObjectPtr<UMaterialInstanceDynamic> DestructionWarningMaterial;

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CompositeShapeMeshes;

	/** Normal appearance. Edit the referenced material asset or replace it in the UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> BaseTileMaterial;

	/** Appearance while the predicted trajectory touches this object. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> TrajectoryHighlightMaterial;
};
