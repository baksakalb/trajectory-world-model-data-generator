#include "Grenade/GrenadeThrowerComponent.h"

#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Grenade/GrenadeActor.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarGGGrenadeStateDebug(
		TEXT("gg.Grenade.DebugState"),
		0,
		TEXT("Logs grenade throw state transitions. 0=off, 1=on"),
		ECVF_Default);
}

UGrenadeThrowerComponent::UGrenadeThrowerComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UGrenadeThrowerComponent::BeginPlay()
{
	Super::BeginPlay();
	SetThrowState(EGrenadeThrowState::Ready);
}

void UGrenadeThrowerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
	}
}

void UGrenadeThrowerComponent::OnThrowPressed()
{
	bThrowInputHeld = true;
}

void UGrenadeThrowerComponent::OnThrowReleased()
{
	if (!bThrowInputHeld)
	{
		return;
	}

	bThrowInputHeld = false;
	TryThrowGrenade();
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

void UGrenadeThrowerComponent::TryThrowGrenade()
{
	if (ThrowState != EGrenadeThrowState::Ready || !GetWorld())
	{
		return;
	}

	FVector SpawnLocation = FVector::ZeroVector;
	FVector InitialVelocity = FVector::ZeroVector;
	if (!ComputeLaunchTransform(SpawnLocation, InitialVelocity))
	{
		EnterCooldown();
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* SpawnClass = GrenadeClass ? GrenadeClass.Get() : AGrenadeActor::StaticClass();
	if (AGrenadeActor* Grenade = GetWorld()->SpawnActor<AGrenadeActor>(SpawnClass, SpawnLocation, FRotator::ZeroRotator, SpawnParams))
	{
		Grenade->InitializeGrenade(SpawnLocation, InitialVelocity, FuseSeconds, SimulationConfig, GetOwner());
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
	OwnerCharacter->GetActorEyesViewPoint(ViewLocation, ViewRotation);

	const FVector ViewForward = ViewRotation.Vector();
	const FVector ViewRight = FRotationMatrix(ViewRotation).GetScaledAxis(EAxis::Y);
	const FVector ViewUp = FRotationMatrix(ViewRotation).GetScaledAxis(EAxis::Z);

	OutSpawnLocation = ViewLocation
		+ (ViewForward * ThrowSpawnOffset.X)
		+ (ViewRight * ThrowSpawnOffset.Y)
		+ (ViewUp * ThrowSpawnOffset.Z);

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
