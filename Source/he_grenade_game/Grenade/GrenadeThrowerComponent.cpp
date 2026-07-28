#include "Grenade/GrenadeThrowerComponent.h"

#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Grenade/GrenadeActor.h"
#include "he_grenade_gameCharacter.h"

namespace
{
	constexpr float MinBreakableVelocityDamping = 0.90f;
	constexpr float MinRestSpeedCmPerSec = 55.0f;
	constexpr float MinCrouchSpawnDropCm = 16.0f;
	constexpr float MinCrouchPitchOffsetDegrees = -7.0f;

	static TAutoConsoleVariable<int32> CVarGGGrenadeStateDebug(
		TEXT("gg.Grenade.DebugState"),
		0,
		TEXT("Logs grenade throw state transitions. 0=off, 1=on"),
		ECVF_Default);

	static TAutoConsoleVariable<int32> CVarGGGrenadeThrowLockDebug(
		TEXT("gg.Grenade.DebugThrowLock"),
		0,
		TEXT("Logs release-snapshot launch params and actual spawned launch params. 0=off, 1=on"),
		ECVF_Default);
}

UGrenadeThrowerComponent::UGrenadeThrowerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bStartWithTickEnabled = false;
}

void UGrenadeThrowerComponent::BeginPlay()
{
	Super::BeginPlay();

	SimulationConfig.BreakableVelocityDamping = FMath::Clamp(
		FMath::Max(SimulationConfig.BreakableVelocityDamping, MinBreakableVelocityDamping),
		0.0f,
		0.95f);
	SimulationConfig.RestSpeedCmPerSec = FMath::Max(SimulationConfig.RestSpeedCmPerSec, MinRestSpeedCmPerSec);
	CrouchThrowPitchOffsetDegrees = FMath::Min(CrouchThrowPitchOffsetDegrees, MinCrouchPitchOffsetDegrees);
	CrouchThrowSpawnDropCm = FMath::Max(CrouchThrowSpawnDropCm, MinCrouchSpawnDropCm);
	ControlArcRaiseMaxPitchOffsetDegrees = FMath::Clamp(ControlArcRaiseMaxPitchOffsetDegrees, 0.0f, 60.0f);

	MinThrowSpeedCmPerSec = FMath::Max(0.0f, MinThrowSpeedCmPerSec);
	MaxThrowSpeedCmPerSec = FMath::Max(MinThrowSpeedCmPerSec, MaxThrowSpeedCmPerSec);
	ChargeDurationSeconds = FMath::Max(0.01f, ChargeDurationSeconds);
	FuseSeconds = FMath::Max(0.1f, FuseSeconds);

	bThrowInputHeld = false;
	bBufferedThrowPress = false;
	bControlArcRaiseInputHeld = false;
	bDetonatedInHandThisHold = false;
	bHasHeldLaunchSnapshot = false;
	HoldStartWorldTimeSeconds = 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	LastHeldDurationSeconds = 0.0f;

	SetThrowState(EGrenadeThrowState::Ready);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("Grenade tuning active: BreakableDamping=%.2f RestSpeed=%.1f MinThrow=%.1f MaxThrow=%.1f Charge=%.2fs Fuse=%.2fs"),
		SimulationConfig.BreakableVelocityDamping,
		SimulationConfig.RestSpeedCmPerSec,
		MinThrowSpeedCmPerSec,
		MaxThrowSpeedCmPerSec,
		ChargeDurationSeconds,
		FuseSeconds);
}

void UGrenadeThrowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	}

	bThrowInputHeld = false;
	bBufferedThrowPress = false;
	bControlArcRaiseInputHeld = false;
	bDetonatedInHandThisHold = false;
	bHasHeldLaunchSnapshot = false;
	HoldStartWorldTimeSeconds = 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	LastHeldDurationSeconds = 0.0f;
	SetComponentTickEnabled(false);
}

void UGrenadeThrowerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bThrowInputHeld)
	{
		return;
	}

	LastHeldDurationSeconds = GetHeldDurationSeconds();

	if (GetRemainingFuseSeconds() <= KINDA_SMALL_NUMBER)
	{
		DetonateInHand();
		return;
	}

	FGrenadeLaunchParams LaunchSnapshot;
	if (BuildCurrentLaunchParams(LaunchSnapshot))
	{
		HeldLaunchSnapshot = LaunchSnapshot;
		bHasHeldLaunchSnapshot = true;
	}
}

void UGrenadeThrowerComponent::OnThrowPressed()
{
	if (bThrowInputHeld)
	{
		return;
	}

	if (ThrowState != EGrenadeThrowState::Ready)
	{
		// Buffer press while cooling down; hold will auto-start once state becomes Ready.
		bBufferedThrowPress = true;
		return;
	}

	BeginThrowHold();
}

void UGrenadeThrowerComponent::BeginThrowHold()
{
	if (ThrowState != EGrenadeThrowState::Ready || bThrowInputHeld)
	{
		return;
	}

	bBufferedThrowPress = false;
	bThrowInputHeld = true;
	bControlArcRaiseInputHeld = false;
	bDetonatedInHandThisHold = false;
	bHasHeldLaunchSnapshot = false;
	LastHeldDurationSeconds = 0.0f;
	HoldStartWorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	SetComponentTickEnabled(true);

	if (GetRemainingFuseSeconds() <= KINDA_SMALL_NUMBER)
	{
		DetonateInHand();
		return;
	}

	// Capture immediately as a fallback for edge cases where release-time sampling fails.
	FGrenadeLaunchParams ImmediateSnapshot;
	if (BuildCurrentLaunchParams(ImmediateSnapshot))
	{
		HeldLaunchSnapshot = ImmediateSnapshot;
		bHasHeldLaunchSnapshot = true;
	}
}

void UGrenadeThrowerComponent::OnThrowReleased()
{
	bBufferedThrowPress = false;

	if (!bThrowInputHeld)
	{
		return;
	}

	LastHeldDurationSeconds = GetHeldDurationSeconds();
	if (GetRemainingFuseSeconds() <= KINDA_SMALL_NUMBER)
	{
		DetonateInHand();
		return;
	}

	FGrenadeLaunchParams ReleaseLaunchParams;
	bool bHasReleaseLaunch = false;
	if (bHasHeldLaunchSnapshot)
	{
		ReleaseLaunchParams = HeldLaunchSnapshot;
		bHasReleaseLaunch = true;
	}
	else
	{
		bHasReleaseLaunch = BuildCurrentLaunchParams(ReleaseLaunchParams);
	}

	TryThrowGrenade(bHasReleaseLaunch ? &ReleaseLaunchParams : nullptr);

	if (CVarGGGrenadeThrowLockDebug.GetValueOnGameThread() != 0 && bHasReleaseLaunch)
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("ThrowReleaseSnapshot T=%.4f Spawn=(%.2f,%.2f,%.2f) Vel=(%.2f,%.2f,%.2f) Fuse=%.3f"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			ReleaseLaunchParams.SpawnLocation.X,
			ReleaseLaunchParams.SpawnLocation.Y,
			ReleaseLaunchParams.SpawnLocation.Z,
			ReleaseLaunchParams.InitialVelocity.X,
			ReleaseLaunchParams.InitialVelocity.Y,
			ReleaseLaunchParams.InitialVelocity.Z,
			ReleaseLaunchParams.FuseSeconds);
	}

	bThrowInputHeld = false;
	bBufferedThrowPress = false;
	bControlArcRaiseInputHeld = false;
	bDetonatedInHandThisHold = false;
	bHasHeldLaunchSnapshot = false;
	HoldStartWorldTimeSeconds = 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	LastHeldDurationSeconds = 0.0f;
	SetComponentTickEnabled(false);
}

void UGrenadeThrowerComponent::SetAimModeActive(bool bActiveAimMode)
{
	bAimModeActive = bActiveAimMode;
	if (!bAimModeActive)
	{
		bControlArcRaiseInputHeld = false;
		ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	}
}

void UGrenadeThrowerComponent::SetControlArcRaiseInputActive(bool bActive)
{
	if (!bEnableControlArcRaise)
	{
		bControlArcRaiseInputHeld = false;
		ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
		return;
	}

	if (!bActive)
	{
		bControlArcRaiseInputHeld = false;
		ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
		return;
	}

	if (!IsControlArcRaiseContextActive())
	{
		return;
	}

	if (!bControlArcRaiseInputHeld)
	{
		bControlArcRaiseInputHeld = true;
		ControlArcRaiseHoldStartWorldTimeSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0f;
	}
}

float UGrenadeThrowerComponent::GetCurrentChargeAlpha() const
{
	if (!bThrowInputHeld)
	{
		return 0.0f;
	}

	return FMath::Clamp(GetHeldDurationSeconds() / FMath::Max(0.01f, ChargeDurationSeconds), 0.0f, 1.0f);
}

float UGrenadeThrowerComponent::GetCurrentThrowSpeedCmPerSec() const
{
	if (!bThrowInputHeld)
	{
		return MinThrowSpeedCmPerSec;
	}

	return FMath::Lerp(MinThrowSpeedCmPerSec, MaxThrowSpeedCmPerSec, GetCurrentChargeAlpha());
}

float UGrenadeThrowerComponent::GetRemainingFuseSeconds() const
{
	if (!bThrowInputHeld)
	{
		return FuseSeconds;
	}

	return FMath::Max(0.0f, FuseSeconds - GetHeldDurationSeconds());
}

bool UGrenadeThrowerComponent::IsStateGreen() const
{
	return ThrowState == EGrenadeThrowState::Ready;
}

bool UGrenadeThrowerComponent::BuildLaunchParams(FGrenadeLaunchParams& OutLaunchParams) const
{
	return BuildCurrentLaunchParams(OutLaunchParams);
}

bool UGrenadeThrowerComponent::BuildCurrentLaunchParams(FGrenadeLaunchParams& OutLaunchParams) const
{
	const float ThrowSpeedForSnapshot = GetCurrentThrowSpeedCmPerSec();
	const float RemainingFuseForSnapshot = GetRemainingFuseSeconds();

	FVector SpawnLocation = FVector::ZeroVector;
	FVector InitialVelocity = FVector::ZeroVector;
	if (!ComputeLaunchTransform(SpawnLocation, InitialVelocity, ThrowSpeedForSnapshot))
	{
		return false;
	}

	OutLaunchParams.SpawnLocation = SpawnLocation;
	OutLaunchParams.InitialVelocity = InitialVelocity;
	OutLaunchParams.FuseSeconds = RemainingFuseForSnapshot;
	OutLaunchParams.bCanThrowNow = IsThrowAvailableNow() && RemainingFuseForSnapshot > KINDA_SMALL_NUMBER;
	return true;
}

void UGrenadeThrowerComponent::TryThrowGrenade(const FGrenadeLaunchParams* LaunchParamsOverride)
{
	if (ThrowState != EGrenadeThrowState::Ready || !GetWorld())
	{
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	if (LaunchParamsOverride)
	{
		LaunchParams = *LaunchParamsOverride;
	}
	else if (!BuildCurrentLaunchParams(LaunchParams))
	{
		EnterCooldown();
		return;
	}

	if (LaunchParams.FuseSeconds <= KINDA_SMALL_NUMBER)
	{
		DetonateInHand();
		return;
	}

	if (CVarGGGrenadeThrowLockDebug.GetValueOnGameThread() != 0)
	{
		FGrenadeLaunchParams CurrentNow;
		const bool bHasCurrentNow = BuildCurrentLaunchParams(CurrentNow);
		const float SpawnDelta = bHasCurrentNow
			? FVector::Distance(CurrentNow.SpawnLocation, LaunchParams.SpawnLocation)
			: -1.0f;
		const float VelocityDelta = bHasCurrentNow
			? FVector::Distance(CurrentNow.InitialVelocity, LaunchParams.InitialVelocity)
			: -1.0f;

		UE_LOG(
			LogTemp,
			Log,
			TEXT("ThrowSpawnParams T=%.4f Spawn=(%.2f,%.2f,%.2f) Vel=(%.2f,%.2f,%.2f) Fuse=%.3f DeltaNow[Spawn=%.3f Vel=%.3f]"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			LaunchParams.SpawnLocation.X,
			LaunchParams.SpawnLocation.Y,
			LaunchParams.SpawnLocation.Z,
			LaunchParams.InitialVelocity.X,
			LaunchParams.InitialVelocity.Y,
			LaunchParams.InitialVelocity.Z,
			LaunchParams.FuseSeconds,
			SpawnDelta,
			VelocityDelta);
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* SpawnClass = GrenadeClass ? GrenadeClass.Get() : AGrenadeActor::StaticClass();
	if (AGrenadeActor* Grenade = GetWorld()->SpawnActor<AGrenadeActor>(SpawnClass, LaunchParams.SpawnLocation, FRotator::ZeroRotator, SpawnParams))
	{
		Grenade->InitializeGrenade(
			LaunchParams.SpawnLocation,
			LaunchParams.InitialVelocity,
			LaunchParams.FuseSeconds,
			SimulationConfig,
			GetOwner());
	}

	EnterCooldown();
}

void UGrenadeThrowerComponent::EnterCooldown()
{
	if (!GetWorld())
	{
		return;
	}

	SetThrowState(EGrenadeThrowState::Cooldown);

	GetWorld()->GetTimerManager().ClearTimer(CooldownTimerHandle);
	GetWorld()->GetTimerManager().SetTimer(CooldownTimerHandle, this, &UGrenadeThrowerComponent::ExitCooldown, ReloadCooldownSeconds, false);
}

void UGrenadeThrowerComponent::ExitCooldown()
{
	SetThrowState(EGrenadeThrowState::Ready);
}

void UGrenadeThrowerComponent::SetThrowState(EGrenadeThrowState NewState)
{
	if (ThrowState == NewState)
	{
		return;
	}

	ThrowState = NewState;
	OnGrenadeStateChanged.Broadcast(ThrowState);

	if (CVarGGGrenadeStateDebug.GetValueOnGameThread() != 0)
	{
		UE_LOG(LogTemp, Log, TEXT("Grenade throw state changed: %d"), static_cast<int32>(ThrowState));
	}

	if (ThrowState == EGrenadeThrowState::Ready && bBufferedThrowPress && !bThrowInputHeld)
	{
		BeginThrowHold();
	}
}

void UGrenadeThrowerComponent::DetonateInHand()
{
	if (bDetonatedInHandThisHold)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		bThrowInputHeld = false;
		bControlArcRaiseInputHeld = false;
		bHasHeldLaunchSnapshot = false;
		ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
		SetComponentTickEnabled(false);
		return;
	}

	FVector ExplosionOrigin = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	if (bHasHeldLaunchSnapshot)
	{
		ExplosionOrigin = HeldLaunchSnapshot.SpawnLocation;
	}
	else
	{
		FGrenadeLaunchParams Snapshot;
		if (BuildCurrentLaunchParams(Snapshot))
		{
			ExplosionOrigin = Snapshot.SpawnLocation;
		}
	}

	float ExplosionRadius = 300.0f;
	UClass* SpawnClass = GrenadeClass ? GrenadeClass.Get() : AGrenadeActor::StaticClass();
	if (SpawnClass)
	{
		if (const AGrenadeActor* GrenadeDefaults = Cast<AGrenadeActor>(SpawnClass->GetDefaultObject()))
		{
			ExplosionRadius = FMath::Max(0.0f, GrenadeDefaults->ExplosionRadiusCm);
		}
	}

	AGrenadeActor::ApplyInstantKillBlast(World, ExplosionOrigin, ExplosionRadius, nullptr, GetOwner());

	bDetonatedInHandThisHold = true;
	bThrowInputHeld = false;
	bBufferedThrowPress = false;
	bControlArcRaiseInputHeld = false;
	bHasHeldLaunchSnapshot = false;
	HoldStartWorldTimeSeconds = 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	LastHeldDurationSeconds = 0.0f;
	SetComponentTickEnabled(false);
	EnterCooldown();
}

float UGrenadeThrowerComponent::GetHeldDurationSeconds() const
{
	if (!bThrowInputHeld)
	{
		return FMath::Max(0.0f, LastHeldDurationSeconds);
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return FMath::Max(0.0f, LastHeldDurationSeconds);
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - HoldStartWorldTimeSeconds);
}

float UGrenadeThrowerComponent::GetControlArcRaiseHeldDurationSeconds() const
{
	if (!bControlArcRaiseInputHeld || !IsControlArcRaiseContextActive())
	{
		return 0.0f;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	return FMath::Max(0.0f, World->GetTimeSeconds() - ControlArcRaiseHoldStartWorldTimeSeconds);
}

float UGrenadeThrowerComponent::GetControlArcRaiseAlpha() const
{
	if (!bEnableControlArcRaise)
	{
		return 0.0f;
	}

	const float FuseDuration = FMath::Max(0.1f, FuseSeconds);
	return FMath::Clamp(GetControlArcRaiseHeldDurationSeconds() / FuseDuration, 0.0f, 1.0f);
}

bool UGrenadeThrowerComponent::IsControlArcRaiseContextActive() const
{
	return bAimModeActive && bThrowInputHeld;
}

bool UGrenadeThrowerComponent::ComputeLaunchTransform(FVector& OutSpawnLocation, FVector& OutInitialVelocity, float ThrowSpeedCmPerSec) const
{
	ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	if (!OwnerCharacter)
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	bool bHasViewTransform = false;
	if (const Ahe_grenade_gameCharacter* GGCharacter = Cast<Ahe_grenade_gameCharacter>(OwnerCharacter))
	{
		if (const UCameraComponent* FirstPersonCamera = GGCharacter->GetFirstPersonCameraComponent())
		{
			ViewLocation = FirstPersonCamera->GetComponentLocation();
			ViewRotation = FirstPersonCamera->GetComponentRotation();
			bHasViewTransform = true;
		}
	}

	if (!bHasViewTransform)
	{
		OwnerCharacter->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const bool bIsCrouched = OwnerCharacter->GetCharacterMovement() && OwnerCharacter->GetCharacterMovement()->IsCrouching();
	const bool bApplyCrouchAdjustment = bEnableCrouchThrowAdjustment && bIsCrouched;

	// Spawn basis should stay stable while charging arc raise; only throw direction changes.
	FRotator SpawnReferenceRotation = ViewRotation;
	if (bApplyCrouchAdjustment)
	{
		SpawnReferenceRotation.Pitch += CrouchThrowPitchOffsetDegrees;
	}

	const FRotationMatrix SpawnBasis(SpawnReferenceRotation);
	const FVector SpawnForward = SpawnBasis.GetScaledAxis(EAxis::X);
	const FVector SpawnRight = SpawnBasis.GetScaledAxis(EAxis::Y);
	const FVector SpawnUp = SpawnBasis.GetScaledAxis(EAxis::Z);

	OutSpawnLocation = ViewLocation
		+ (SpawnForward * ThrowSpawnOffset.X)
		+ (SpawnRight * ThrowSpawnOffset.Y)
		+ (SpawnUp * ThrowSpawnOffset.Z);

	if (bApplyCrouchAdjustment)
	{
		OutSpawnLocation.Z -= FMath::Max(0.0f, CrouchThrowSpawnDropCm);
	}

	FRotator ThrowAimRotation = SpawnReferenceRotation;
	if (bEnableControlArcRaise)
	{
		const float ArcRaiseAlpha = GetControlArcRaiseAlpha();
		if (ArcRaiseAlpha > 0.0f)
		{
			ThrowAimRotation.Pitch += (ControlArcRaiseMaxPitchOffsetDegrees * ArcRaiseAlpha);
		}
	}

	const FVector ThrowAimForward = FRotationMatrix(ThrowAimRotation).GetScaledAxis(EAxis::X);

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + (ThrowAimForward * TrajectoryTraceDistanceCm);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeThrowAim), false);
	QueryParams.AddIgnoredActor(OwnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const FVector AimTarget = bHit ? Hit.ImpactPoint : TraceEnd;

	FVector ThrowDirection = (AimTarget - OutSpawnLocation).GetSafeNormal();
	if (ThrowDirection.IsNearlyZero())
	{
		ThrowDirection = ThrowAimForward;
	}

	OutInitialVelocity = (ThrowDirection * FMath::Max(0.0f, ThrowSpeedCmPerSec))
		+ (OwnerCharacter->GetVelocity() * ThrowInheritVelocityFactor);
	return true;
}
