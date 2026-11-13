// Cover system for third-person characters: enter/exit low cover against walls, slide along them,
// stop and peek at edges, and wrap around inside corners.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "TDSCoverComponent.generated.h"

class ACharacter;

/**
 * Attach to an ACharacter. Input code calls ToggleCover() and, while IsInCover(), routes movement
 * input to HandleMoveInput() instead of AddMovementInput. Anim blueprints read GetCoverAnimState().
 *
 * While in cover the character faces the wall (the low-cover animation set is authored that way) and
 * is kept at a constant distance from it every tick, so curved walls cannot make it drift off.
 */
UCLASS(ClassGroup = (TDS), meta = (BlueprintSpawnableComponent))
class TDS_API UTDSCoverComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UTDSCoverComponent();

	// --- Tuning ------------------------------------------------------------------------------

	/** How far from the capsule centre to look for a wall when entering cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", Units = "cm"))
	float SearchDistance = 150.f;

	/** Height above the character's feet at which walls are traced. Walls lower than this are not cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", Units = "cm"))
	float TraceHeightAboveFeet = 50.f;

	/** Gap kept between the capsule and the wall while in cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", Units = "cm"))
	float WallGap = 2.f;

	/** How far past the capsule edge to look for more wall before treating it as a cover edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", Units = "cm"))
	float EdgeLookAhead = 10.f;

	/** Extra trace depth beyond the expected wall distance before the wall counts as lost. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", Units = "cm"))
	float WallTolerance = 30.f;

	/** Surfaces whose normal has a larger |Z| than this are floors/ramps, not cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float MaxWallNormalZ = 0.3f;

	/** Input pointing away from the wall beyond this (0..1) leaves cover. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float PullAwayToExit = 0.7f;

	/** Sideways input below this (0..1) is ignored. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0", ClampMax = "1"))
	float LateralDeadZone = 0.2f;

	/** How fast the character turns to follow the wall (curves, corners). */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover", meta = (ClampMin = "0"))
	float RotationInterpSpeed = 12.f;

	/** Show an on-screen hint when cover is requested but no wall is in reach. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Debug")
	bool bShowMessages = true;

	/** Draw the cover traces. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Cover|Debug")
	bool bDrawDebug = false;

	// --- API ---------------------------------------------------------------------------------

	/** Enter cover if out of it (returns true on success), leave it if in it (returns false). */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	bool ToggleCover();

	/** Look for a wall ahead, toward the camera, then to either side, and enter cover on the first one. */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	bool TryEnterCover();

	UFUNCTION(BlueprintCallable, Category = "Cover")
	void ExitCover();

	/** Feed the raw 2D move input (X = right, Y = forward, camera relative) while in cover. */
	UFUNCTION(BlueprintCallable, Category = "Cover")
	void HandleMoveInput(float RightInput, float ForwardInput);

	UFUNCTION(BlueprintPure, Category = "Cover")
	bool IsInCover() const { return bInCover; }

	/** Everything the anim blueprint needs, in one node. Safe to call with any actor. */
	UFUNCTION(BlueprintPure, Category = "Cover", meta = (DisplayName = "Get Cover Anim State"))
	static void GetCoverAnimState(const AActor* Actor, bool& bIsInCover, bool& bIsFacingRight, bool& bIsAtEdge);

	/** Wall normal (pointing away from the wall, horizontal) of the current cover. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover|State")
	FVector CoverNormal = FVector::ZeroVector;

	UPROPERTY(BlueprintReadOnly, Category = "Cover|State")
	bool bInCover = false;

	/** True when the character is moving/facing toward its right along the wall. */
	UPROPERTY(BlueprintReadOnly, Category = "Cover|State")
	bool bFacingRight = true;

	/** True when the wall ends just past the character in the facing direction (plays the peek pose). */
	UPROPERTY(BlueprintReadOnly, Category = "Cover|State")
	bool bAtEdge = false;

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;

private:
	ACharacter* GetCharacter() const;
	float GetCapsuleRadius() const;
	/** Point at the capsule centre's XY, TraceHeightAboveFeet above the feet. */
	FVector GetTraceOrigin() const;
	/** Direction along the wall toward the character's right when facing the wall. */
	FVector GetWallRight() const;

	/** Horizontal trace from Origin along Dir; true only for a hit on a wall-like surface. */
	bool TraceWall(const FVector& Origin, const FVector& Dir, float Distance, FHitResult& OutHit) const;
	/** Is there wall beside the character in the given direction (+1 right, -1 left)? */
	bool HasWallAhead(float Side) const;

	void EnterCover(const FHitResult& WallHit);
	/** Re-pin the capsule WallGap away from the wall and turn toward it. */
	void HoldAgainstWall(const FHitResult& WallHit, float DeltaTime);

	bool bWasCrouchedBeforeCover = false;
	bool bOrientRotationToMovementBeforeCover = true;
	FRotator TargetRotation = FRotator::ZeroRotator;
};
