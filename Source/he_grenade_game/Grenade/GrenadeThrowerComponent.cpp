#include "Grenade/GrenadeThrowerComponent.h"

#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
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

	SetThrowState(EGrenadeThrowState::Ready);

	UE_LOG(
		LogTemp,
		Log,
		TEXT("Grenade tuning active: BreakableDamping=%.2f RestSpeed=%.1f CrouchPitchOffset=%.1f CrouchSpawnDrop=%.1f"),
		SimulationConfig.BreakableVelocityDamping,
		SimulationConfig.RestSpeedCmPerSec,
		CrouchThrowPitchOffsetDegrees,
		CrouchThrowSpawnDropCm);
}

void UGrenadeThrowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	}

	bHasHeldLaunchSnapshot = false;
	SetComponentTickEnabled(false);
}

void UGrenadeThrowerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bThrowInputHeld)
	{
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
	bThrowInputHeld = true;
	bHasHeldLaunchSnapshot = false;
	SetComponentTickEnabled(true);

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
	if (!bThrowInputHeld)
	{
		return;
	}

	bThrowInputHeld = false;
	SetComponentTickEnabled(false);

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
			TEXT("ThrowReleaseSnapshot T=%.4f Spawn=(%.2f,%.2f,%.2f) Vel=(%.2f,%.2f,%.2f)"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			ReleaseLaunchParams.SpawnLocation.X,
			ReleaseLaunchParams.SpawnLocation.Y,
			ReleaseLaunchParams.SpawnLocation.Z,
			ReleaseLaunchParams.InitialVelocity.X,
			ReleaseLaunchParams.InitialVelocity.Y,
			ReleaseLaunchParams.InitialVelocity.Z);
	}

	bHasHeldLaunchSnapshot = false;
}

void UGrenadeThrowerComponent::SetAimModeActive(bool bActiveAimMode)
{
	bAimModeActive = bActiveAimMode;
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
	FVector SpawnLocation = FVector::ZeroVector;
	FVector InitialVelocity = FVector::ZeroVector;
	if (!ComputeLaunchTransform(SpawnLocation, InitialVelocity))
	{
		return false;
	}

	OutLaunchParams.SpawnLocation = SpawnLocation;
	OutLaunchParams.InitialVelocity = InitialVelocity;
	OutLaunchParams.FuseSeconds = FuseSeconds;
	OutLaunchParams.bCanThrowNow = IsThrowAvailableNow();
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
			TEXT("ThrowSpawnParams T=%.4f Spawn=(%.2f,%.2f,%.2f) Vel=(%.2f,%.2f,%.2f) DeltaNow[Spawn=%.3f Vel=%.3f]"),
			GetWorld() ? GetWorld()->GetTimeSeconds() : -1.0f,
			LaunchParams.SpawnLocation.X,
			LaunchParams.SpawnLocation.Y,
			LaunchParams.SpawnLocation.Z,
			LaunchParams.InitialVelocity.X,
			LaunchParams.InitialVelocity.Y,
			LaunchParams.InitialVelocity.Z,
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
}

bool UGrenadeThrowerComponent::ComputeLaunchTransform(FVector& OutSpawnLocation, FVector& OutInitialVelocity) const
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

	FRotator EffectiveViewRotation = ViewRotation;
	if (bApplyCrouchAdjustment)
	{
		EffectiveViewRotation.Pitch += CrouchThrowPitchOffsetDegrees;
	}

	const FRotationMatrix ViewBasis(EffectiveViewRotation);
	const FVector ViewForward = ViewBasis.GetScaledAxis(EAxis::X);
	const FVector ViewRight = ViewBasis.GetScaledAxis(EAxis::Y);
	const FVector ViewUp = ViewBasis.GetScaledAxis(EAxis::Z);

	OutSpawnLocation = ViewLocation
		+ (ViewForward * ThrowSpawnOffset.X)
		+ (ViewRight * ThrowSpawnOffset.Y)
		+ (ViewUp * ThrowSpawnOffset.Z);

	if (bApplyCrouchAdjustment)
	{
		OutSpawnLocation.Z -= FMath::Max(0.0f, CrouchThrowSpawnDropCm);
	}

	const FVector TraceStart = ViewLocation;
	const FVector TraceEnd = TraceStart + (ViewForward * TrajectoryTraceDistanceCm);

	FHitResult Hit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(GrenadeThrowAim), false);
	QueryParams.AddIgnoredActor(OwnerCharacter);

	const bool bHit = GetWorld()->LineTraceSingleByChannel(Hit, TraceStart, TraceEnd, ECC_Visibility, QueryParams);
	const FVector AimTarget = bHit ? Hit.ImpactPoint : TraceEnd;

	FVector ThrowDirection = (AimTarget - OutSpawnLocation).GetSafeNormal();
	if (ThrowDirection.IsNearlyZero())
	{
		ThrowDirection = ViewForward;
	}

	OutInitialVelocity = (ThrowDirection * ThrowSpeedCmPerSec) + (OwnerCharacter->GetVelocity() * ThrowInheritVelocityFactor);
	return true;
}
