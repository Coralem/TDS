#include "TDSCoverComponent.h"

#include "Components/CapsuleComponent.h"
#include "DrawDebugHelpers.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

namespace
{
	FVector Flatten(const FVector& V)
	{
		return FVector(V.X, V.Y, 0.f).GetSafeNormal();
	}
}

UTDSCoverComponent::UTDSCoverComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = true;
}

void UTDSCoverComponent::BeginPlay()
{
	Super::BeginPlay();

	// Run after the movement component so the wall correction applies to this frame's movement.
	if (ACharacter* Character = GetCharacter())
	{
		if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
		{
			PrimaryComponentTick.AddPrerequisite(Movement, Movement->PrimaryComponentTick);
		}
	}
}

ACharacter* UTDSCoverComponent::GetCharacter() const
{
	return Cast<ACharacter>(GetOwner());
}

float UTDSCoverComponent::GetCapsuleRadius() const
{
	const ACharacter* Character = GetCharacter();
	return Character ? Character->GetCapsuleComponent()->GetScaledCapsuleRadius() : 34.f;
}

FVector UTDSCoverComponent::GetTraceOrigin() const
{
	const ACharacter* Character = GetCharacter();
	if (!Character)
	{
		return FVector::ZeroVector;
	}
	const FVector Location = Character->GetActorLocation();
	const float FeetZ = Location.Z - Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	return FVector(Location.X, Location.Y, FeetZ + TraceHeightAboveFeet);
}

FVector UTDSCoverComponent::GetWallRight() const
{
	// Facing the wall means facing -CoverNormal; right of that is Normal x Up.
	return FVector::CrossProduct(CoverNormal, FVector::UpVector).GetSafeNormal();
}

bool UTDSCoverComponent::TraceWall(const FVector& Origin, const FVector& Dir, float Distance, FHitResult& OutHit) const
{
	const FVector FlatDir = Flatten(Dir);
	if (FlatDir.IsNearlyZero())
	{
		return false;
	}

	const FVector End = Origin + FlatDir * Distance;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TDSCoverTrace), false, GetOwner());
	const bool bHit = GetWorld()->LineTraceSingleByChannel(OutHit, Origin, End, ECC_Visibility, Params);
	const bool bIsWall = bHit && !OutHit.bStartPenetrating && FMath::Abs(OutHit.ImpactNormal.Z) <= MaxWallNormalZ;

	if (bDrawDebug)
	{
		DrawDebugLine(GetWorld(), Origin, bHit ? OutHit.ImpactPoint : End, bIsWall ? FColor::Green : FColor::Red, false, 0.f, 0, 1.f);
	}
	return bIsWall;
}

bool UTDSCoverComponent::HasWallAhead(float Side) const
{
	const float Radius = GetCapsuleRadius();
	const FVector Origin = GetTraceOrigin();
	const FVector ProbeStart = Origin + GetWallRight() * Side * (Radius + EdgeLookAhead);

	// Something between us and the probe point (an inside corner): not an edge, movement is just blocked.
	FHitResult BlockHit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(TDSCoverEdge), false, GetOwner());
	if (GetWorld()->LineTraceSingleByChannel(BlockHit, Origin, ProbeStart, ECC_Visibility, Params))
	{
		return true;
	}

	// The wall is held at Radius + WallGap; allow WallTolerance for curvature before calling it an edge.
	FHitResult WallHit;
	return TraceWall(ProbeStart, -CoverNormal, Radius + WallGap + WallTolerance, WallHit);
}

bool UTDSCoverComponent::ToggleCover()
{
	if (bInCover)
	{
		ExitCover();
		return false;
	}
	return TryEnterCover();
}

bool UTDSCoverComponent::TryEnterCover()
{
	ACharacter* Character = GetCharacter();
	if (!Character || bInCover || Character->GetCharacterMovement()->IsFalling())
	{
		return false;
	}

	const FVector Origin = GetTraceOrigin();
	const FVector ActorForward = Character->GetActorForwardVector();
	const FVector ActorRight = Character->GetActorRightVector();
	const FVector ViewForward = FRotator(0.f, Character->GetControlRotation().Yaw, 0.f).Vector();

	// Facing the wall is the common case; then where the camera looks; then walls beside the character.
	const FVector Directions[] = { ActorForward, ViewForward, ActorRight, -ActorRight };
	for (const FVector& Dir : Directions)
	{
		FHitResult Hit;
		if (TraceWall(Origin, Dir, SearchDistance, Hit))
		{
			EnterCover(Hit);
			return true;
		}
	}

	if (bShowMessages && GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 1.5f, FColor::Cyan, TEXT("Cover: no wall within reach"));
	}
	return false;
}

void UTDSCoverComponent::EnterCover(const FHitResult& WallHit)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	bWasCrouchedBeforeCover = Character->bIsCrouched;
	bOrientRotationToMovementBeforeCover = Movement->bOrientRotationToMovement;
	Movement->bOrientRotationToMovement = false;

	CoverNormal = Flatten(WallHit.ImpactNormal);
	bInCover = true;
	bAtEdge = false;

	// Face along the wall toward wherever the camera is looking.
	const FVector ViewForward = FRotator(0.f, Character->GetControlRotation().Yaw, 0.f).Vector();
	bFacingRight = FVector::DotProduct(ViewForward, GetWallRight()) >= 0.f;

	FVector Target = WallHit.ImpactPoint + CoverNormal * (GetCapsuleRadius() + WallGap);
	Target.Z = Character->GetActorLocation().Z;
	TargetRotation = (-CoverNormal).Rotation();
	Character->SetActorLocationAndRotation(Target, TargetRotation, true);
	Movement->StopMovementImmediately();

	Character->Crouch();
}

void UTDSCoverComponent::ExitCover()
{
	if (!bInCover)
	{
		return;
	}
	bInCover = false;
	bAtEdge = false;

	if (ACharacter* Character = GetCharacter())
	{
		Character->GetCharacterMovement()->bOrientRotationToMovement = bOrientRotationToMovementBeforeCover;
		if (!bWasCrouchedBeforeCover)
		{
			Character->UnCrouch();
		}
	}
}

void UTDSCoverComponent::HandleMoveInput(float RightInput, float ForwardInput)
{
	ACharacter* Character = GetCharacter();
	if (!bInCover || !Character)
	{
		return;
	}

	const FRotator ViewYaw(0.f, Character->GetControlRotation().Yaw, 0.f);
	const FVector Input = ViewYaw.RotateVector(FVector(ForwardInput, RightInput, 0.f));
	const FVector WallRight = GetWallRight();
	const float Lateral = FMath::Clamp(FVector::DotProduct(Input, WallRight), -1.f, 1.f);
	const float Away = FVector::DotProduct(Input, CoverNormal);

	if (FMath::Abs(Lateral) > LateralDeadZone)
	{
		bFacingRight = Lateral > 0.f;
		const float Side = bFacingRight ? 1.f : -1.f;
		bAtEdge = !HasWallAhead(Side);
		if (!bAtEdge)
		{
			Character->AddMovementInput(WallRight * Side, FMath::Abs(Lateral));
		}
	}
	else if (Away > PullAwayToExit)
	{
		ExitCover();
	}
}

void UTDSCoverComponent::HoldAgainstWall(const FHitResult& WallHit, float DeltaTime)
{
	ACharacter* Character = GetCharacter();
	UCharacterMovementComponent* Movement = Character->GetCharacterMovement();

	CoverNormal = Flatten(WallHit.ImpactNormal);
	TargetRotation = (-CoverNormal).Rotation();
	const FRotator NewRotation = FMath::RInterpTo(Character->GetActorRotation(), TargetRotation, DeltaTime, RotationInterpSpeed);

	// Keep a constant distance: moving tangentially along a convex wall otherwise drifts away from it.
	FVector Desired = WallHit.ImpactPoint + CoverNormal * (GetCapsuleRadius() + WallGap);
	FVector Delta = Desired - Character->GetActorLocation();
	Delta.Z = 0.f;

	FHitResult MoveHit;
	Movement->SafeMoveUpdatedComponent(Delta.SizeSquared() > 0.01f ? Delta : FVector::ZeroVector, NewRotation.Quaternion(), true, MoveHit);
}

void UTDSCoverComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	ACharacter* Character = GetCharacter();
	if (!bInCover || !Character)
	{
		return;
	}
	if (Character->GetCharacterMovement()->IsFalling())
	{
		ExitCover();
		return;
	}

	const FVector Origin = GetTraceOrigin();
	const float HoldDistance = GetCapsuleRadius() + WallGap + WallTolerance;
	const float Side = bFacingRight ? 1.f : -1.f;

	FHitResult Hit;
	if (TraceWall(Origin, -CoverNormal, HoldDistance, Hit)
		// The wall curved away or ended under us: follow it around onto a wall beside us, facing side first.
		|| TraceWall(Origin, GetWallRight() * Side, HoldDistance, Hit)
		|| TraceWall(Origin, GetWallRight() * -Side, HoldDistance, Hit))
	{
		HoldAgainstWall(Hit, DeltaTime);
		bAtEdge = !HasWallAhead(Side);
	}
	else
	{
		ExitCover();
	}
}

void UTDSCoverComponent::GetCoverAnimState(const AActor* Actor, bool& bIsInCover, bool& bIsFacingRight, bool& bIsAtEdge)
{
	const UTDSCoverComponent* Cover = Actor ? Actor->FindComponentByClass<UTDSCoverComponent>() : nullptr;
	bIsInCover = Cover && Cover->bInCover;
	bIsFacingRight = Cover ? Cover->bFacingRight : true;
	bIsAtEdge = Cover && Cover->bAtEdge;
}
