#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "BreakableTile.generated.h"

class UStaticMeshComponent;
class UMaterialInterface;
class UStaticMesh;

/** Server-generation scaffold captured into the replicated stable-ID arena snapshot. */
UCLASS(BlueprintType, Blueprintable)
class HE_GRENADE_GAME_API ABreakableTile : public AActor
{
	GENERATED_BODY()

public:
	ABreakableTile();

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Tile")
	TObjectPtr<UStaticMeshComponent> TileMesh;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Breakable")
	bool bBreakOnGrenadeImpact = true;

	/** When true, the tile applies one normal surface bounce and then disappears immediately. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Breakable")
	bool bBounceGrenadeBeforeBreaking = false;

	UFUNCTION(BlueprintPure, Category = "Tile|Breakable")
	bool CanBreakOnGrenadeImpact() const { return bBreakOnGrenadeImpact; }

	UFUNCTION(BlueprintPure, Category = "Tile|Breakable")
	bool ShouldBounceGrenadeBeforeBreaking() const { return bBounceGrenadeBeforeBreaking; }

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

private:
	void ApplyBaseMaterial();

	UPROPERTY(Transient)
	TArray<TObjectPtr<UStaticMeshComponent>> CompositeShapeMeshes;

	/** Normal appearance. Edit the referenced material asset or replace it in the UI. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Tile|Materials", meta = (AllowPrivateAccess = "true"))
	TObjectPtr<UMaterialInterface> BaseTileMaterial;

};
