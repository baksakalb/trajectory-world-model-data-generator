#include "Grenade/GrenadeThrowerComponent.h"

#include "Engine/World.h"
#include "Camera/CameraComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Grenade/GrenadeActor.h"
#include "GrenadeGameState.h"
#include "GrenadePlayerState.h"
#include "he_grenade_gameCharacter.h"
#include "Misc/CommandLine.h"
#include "Misc/Parse.h"

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

	static TAutoConsoleVariable<int32> CVarGGGrenadeNetworkSelfTest(
		TEXT("gg.Grenade.NetworkSelfTest"),
		0,
		TEXT("Development test: each locally controlled pawn performs one normal authoritative throw. 0=off, 1=on"),
		ECVF_Cheat);
}

UGrenadeThrowerComponent::UGrenadeThrowerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
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

	if (CVarGGGrenadeNetworkSelfTest.GetValueOnGameThread() != 0
		|| FParse::Param(FCommandLine::Get(), TEXT("GGNetworkSelfTest")))
	{
		if (const APawn* OwnerPawn = Cast<APawn>(GetOwner()); OwnerPawn && OwnerPawn->IsLocallyControlled())
		{
			const float DelaySeconds = GetOwner() && GetOwner()->HasAuthority() ? 7.0f : 7.1f;
			GetWorld()->GetTimerManager().SetTimer(
				NetworkSelfTestTimerHandle,
				this,
				&UGrenadeThrowerComponent::RunNetworkSelfTestThrow,
				DelaySeconds,
				false);
		}
	}

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
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(CooldownTimerHandle);
		World->GetTimerManager().ClearTimer(NetworkSelfTestTimerHandle);
	}

	for (const TPair<uint32, TWeakObjectPtr<AGrenadeActor>>& Pair : PredictedGrenades)
	{
		if (AGrenadeActor* PredictedGrenade = Pair.Value.Get())
		{
			PredictedGrenade->CancelPredictedVisual();
		}
	}
	PredictedGrenades.Reset();

	bThrowInputHeld = false;
	bBufferedThrowPress = false;
	bControlArcRaiseInputHeld = false;
	bDetonatedInHandThisHold = false;
	bHasHeldLaunchSnapshot = false;
	HoldStartWorldTimeSeconds = 0.0f;
	ControlArcRaiseHoldStartWorldTimeSeconds = 0.0f;
	LastHeldDurationSeconds = 0.0f;
	SetComponentTickEnabled(false);
	Super::EndPlay(EndPlayReason);
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
	if (!IsGameplayReady())
	{
		return;
	}

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

bool UGrenadeThrowerComponent::IsGameplayReady() const
{
	const UWorld* World = GetWorld();
	const AGrenadeGameState* GameState =
		World ? World->GetGameState<AGrenadeGameState>() : nullptr;
	const APawn* OwnerPawn = Cast<APawn>(GetOwner());
	const AGrenadePlayerState* PlayerState =
		OwnerPawn ? OwnerPawn->GetPlayerState<AGrenadePlayerState>() : nullptr;
	return GameState
		&& GameState->HasCompleteArenaState()
		&& GameState->GetGrenadeMatchPhase() == EGGMatchPhase::InProgress
		&& PlayerState
		&& PlayerState->IsArenaReady();
}

void UGrenadeThrowerComponent::TryThrowGrenade(const FGrenadeLaunchParams* LaunchParamsOverride)
{
	if (ThrowState != EGrenadeThrowState::Ready || !GetWorld() || !IsGameplayReady())
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

	FGrenadeThrowRequest Request;
	if (!BuildThrowRequest(LaunchParams, Request))
	{
		return;
	}

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		ProcessAuthoritativeThrow(Request);
	}
	else
	{
		SpawnPredictedGrenade(Request.ThrowId, LaunchParams);
		ServerThrowGrenade(Request);
	}

	EnterCooldown();
}

bool UGrenadeThrowerComponent::BuildThrowRequest(
	const FGrenadeLaunchParams& LaunchParams,
	FGrenadeThrowRequest& OutRequest)
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	const AGrenadeGameState* GameState = World ? World->GetGameState<AGrenadeGameState>() : nullptr;
	if (!OwnerCharacter || !World || !GameState)
	{
		return false;
	}

	const FVector InheritedVelocity =
		OwnerCharacter->GetVelocity() * FMath::Max(0.0f, ThrowInheritVelocityFactor);
	const FVector AimDirection = (LaunchParams.InitialVelocity - InheritedVelocity).GetSafeNormal();
	if (AimDirection.IsNearlyZero() || AimDirection.ContainsNaN())
	{
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	if (const Ahe_grenade_gameCharacter* GGCharacter = Cast<Ahe_grenade_gameCharacter>(OwnerCharacter))
	{
		if (const UCameraComponent* FirstPersonCamera = GGCharacter->GetFirstPersonCameraComponent())
		{
			ViewLocation = FirstPersonCamera->GetComponentLocation();
			ViewRotation = FirstPersonCamera->GetComponentRotation();
		}
		else
		{
			OwnerCharacter->GetActorEyesViewPoint(ViewLocation, ViewRotation);
		}
	}
	else
	{
		OwnerCharacter->GetActorEyesViewPoint(ViewLocation, ViewRotation);
	}

	const float HeldDuration = FMath::Clamp(GetHeldDurationSeconds(), 0.0f, 65.535f);
	const float ChargeAlpha = FMath::Clamp(
		HeldDuration / FMath::Max(0.01f, ChargeDurationSeconds),
		0.0f,
		1.0f);

	OutRequest.ThrowId = NextLocalThrowId++;
	if (NextLocalThrowId == 0)
	{
		NextLocalThrowId = 1;
	}
	OutRequest.AimDirection = AimDirection;
	OutRequest.ViewRotation = ViewRotation.GetNormalized();
	OutRequest.ChargeQuantized = static_cast<uint16>(FMath::RoundToInt(ChargeAlpha * 65535.0f));
	OutRequest.HeldDurationMilliseconds =
		static_cast<uint16>(FMath::RoundToInt(HeldDuration * 1000.0f));
	OutRequest.FuseMilliseconds = static_cast<uint16>(FMath::Clamp(
		FMath::RoundToInt(LaunchParams.FuseSeconds * 1000.0f),
		1,
		65535));
	OutRequest.ClientReleaseServerWorldTimeSeconds = GameState->GetServerWorldTimeSeconds();
	OutRequest.ArenaLayoutRevision = GameState->GetArenaLayoutRevision();
	return true;
}

bool UGrenadeThrowerComponent::ValidateAndBuildServerLaunch(
	const FGrenadeThrowRequest& Request,
	FGrenadeLaunchParams& OutLaunchParams,
	FString& OutRejectionReason) const
{
	const ACharacter* OwnerCharacter = Cast<ACharacter>(GetOwner());
	const UWorld* World = GetWorld();
	const AGrenadeGameState* GameState = World ? World->GetGameState<AGrenadeGameState>() : nullptr;
	if (!OwnerCharacter
		|| !OwnerCharacter->HasAuthority()
		|| !GameState
		|| !GameState->HasCompleteArenaState()
		|| GameState->GetGrenadeMatchPhase() != EGGMatchPhase::InProgress)
	{
		OutRejectionReason = TEXT("gameplay-not-ready");
		return false;
	}

	if (Request.ThrowId == 0 || Request.ThrowId <= LastAcceptedServerThrowId)
	{
		OutRejectionReason = TEXT("non-monotonic-id");
		return false;
	}
	if (Request.ArenaLayoutRevision != GameState->GetArenaLayoutRevision())
	{
		OutRejectionReason = TEXT("layout-revision");
		return false;
	}

	const float ServerNow = GameState->GetServerWorldTimeSeconds();
	const float TimestampAge = ServerNow - Request.ClientReleaseServerWorldTimeSeconds;
	if (!FMath::IsFinite(Request.ClientReleaseServerWorldTimeSeconds)
		|| TimestampAge < -0.25f
		|| TimestampAge > 1.5f)
	{
		OutRejectionReason = TEXT("timestamp");
		return false;
	}
	if (World->GetTimeSeconds() + KINDA_SMALL_NUMBER < NextServerThrowAllowedWorldTimeSeconds)
	{
		OutRejectionReason = TEXT("cooldown");
		return false;
	}

	const FVector AimDirection = FVector(Request.AimDirection);
	const float AimLength = AimDirection.Size();
	if (AimDirection.ContainsNaN()
		|| !FMath::IsFinite(AimLength)
		|| AimLength < 0.95f
		|| AimLength > 1.05f
		|| Request.ViewRotation.ContainsNaN())
	{
		OutRejectionReason = TEXT("aim-data");
		return false;
	}

	const FVector ViewForward = Request.ViewRotation.Vector().GetSafeNormal();
	const float ViewAimDot = FMath::Clamp(
		FVector::DotProduct(ViewForward, AimDirection.GetSafeNormal()),
		-1.0f,
		1.0f);
	if (FMath::RadiansToDegrees(FMath::Acos(ViewAimDot)) > 85.0f)
	{
		OutRejectionReason = TEXT("aim-cone");
		return false;
	}

	const float HeldDuration =
		static_cast<float>(Request.HeldDurationMilliseconds) / 1000.0f;
	const float RequestedCharge =
		static_cast<float>(Request.ChargeQuantized) / 65535.0f;
	const float ExpectedCharge = FMath::Clamp(
		HeldDuration / FMath::Max(0.01f, ChargeDurationSeconds),
		0.0f,
		1.0f);
	const float RequestedFuse =
		static_cast<float>(Request.FuseMilliseconds) / 1000.0f;
	const float ExpectedFuse = FMath::Max(0.0f, FuseSeconds - HeldDuration);
	if (HeldDuration < 0.0f
		|| HeldDuration > FuseSeconds + 0.25f
		|| FMath::Abs(RequestedCharge - ExpectedCharge) > 0.03f
		|| FMath::Abs(RequestedFuse - ExpectedFuse) > 0.075f
		|| RequestedFuse <= KINDA_SMALL_NUMBER)
	{
		OutRejectionReason = TEXT("charge-or-fuse");
		return false;
	}

	FVector ViewLocation = FVector::ZeroVector;
	FRotator ServerViewRotation = FRotator::ZeroRotator;
	OwnerCharacter->GetActorEyesViewPoint(ViewLocation, ServerViewRotation);
	const float ControlRotationDelta = FMath::Abs(
		FRotator::NormalizeAxis(Request.ViewRotation.Yaw - ServerViewRotation.Yaw));
	if (ControlRotationDelta > 100.0f)
	{
		OutRejectionReason = TEXT("view-rotation");
		return false;
	}

	const bool bIsCrouched =
		OwnerCharacter->GetCharacterMovement()
		&& OwnerCharacter->GetCharacterMovement()->IsCrouching();
	const bool bApplyCrouchAdjustment = bEnableCrouchThrowAdjustment && bIsCrouched;
	FRotator SpawnReferenceRotation = Request.ViewRotation.GetNormalized();
	if (bApplyCrouchAdjustment)
	{
		SpawnReferenceRotation.Pitch += CrouchThrowPitchOffsetDegrees;
	}

	const FRotationMatrix SpawnBasis(SpawnReferenceRotation);
	OutLaunchParams.SpawnLocation = ViewLocation
		+ SpawnBasis.GetScaledAxis(EAxis::X) * ThrowSpawnOffset.X
		+ SpawnBasis.GetScaledAxis(EAxis::Y) * ThrowSpawnOffset.Y
		+ SpawnBasis.GetScaledAxis(EAxis::Z) * ThrowSpawnOffset.Z;
	if (bApplyCrouchAdjustment)
	{
		OutLaunchParams.SpawnLocation.Z -= FMath::Max(0.0f, CrouchThrowSpawnDropCm);
	}

	const float ThrowSpeed = FMath::Lerp(
		MinThrowSpeedCmPerSec,
		MaxThrowSpeedCmPerSec,
		ExpectedCharge);
	OutLaunchParams.InitialVelocity =
		AimDirection.GetSafeNormal() * ThrowSpeed
		+ OwnerCharacter->GetVelocity() * FMath::Max(0.0f, ThrowInheritVelocityFactor);
	OutLaunchParams.FuseSeconds = RequestedFuse;
	OutLaunchParams.bCanThrowNow = true;

	constexpr float MaxAllowedSpawnDistanceCm = 250.0f;
	const float MaxAllowedSpeed =
		MaxThrowSpeedCmPerSec
		+ OwnerCharacter->GetVelocity().Size() * FMath::Max(0.0f, ThrowInheritVelocityFactor)
		+ 100.0f;
	if (FVector::DistSquared(OutLaunchParams.SpawnLocation, OwnerCharacter->GetActorLocation())
			> FMath::Square(MaxAllowedSpawnDistanceCm)
		|| OutLaunchParams.InitialVelocity.SizeSquared() > FMath::Square(MaxAllowedSpeed))
	{
		OutRejectionReason = TEXT("spawn-or-velocity-bounds");
		return false;
	}

	return true;
}

void UGrenadeThrowerComponent::ProcessAuthoritativeThrow(const FGrenadeThrowRequest& Request)
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	FString RejectionReason;
	if (!ValidateAndBuildServerLaunch(Request, LaunchParams, RejectionReason))
	{
		UE_LOG(
			LogTemp,
			Warning,
			TEXT("GRENADE_THROW_REJECT Owner=%s ThrowId=%u Reason=%s"),
			*GetNameSafe(GetOwner()),
			Request.ThrowId,
			*RejectionReason);
		if (Cast<APawn>(GetOwner()) && !CastChecked<APawn>(GetOwner())->IsLocallyControlled())
		{
			ClientRejectGrenadeThrow(Request.ThrowId);
		}
		return;
	}

	LastAcceptedServerThrowId = Request.ThrowId;
	NextServerThrowAllowedWorldTimeSeconds =
		GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, ReloadCooldownSeconds);
	SpawnGrenadeAuthoritative(Request.ThrowId, LaunchParams);
	EnterCooldown();
	UE_LOG(
		LogTemp,
		Log,
		TEXT("GRENADE_THROW_ACCEPT Owner=%s ThrowId=%u LayoutRevision=%d"),
		*GetNameSafe(GetOwner()),
		Request.ThrowId,
		Request.ArenaLayoutRevision);
}

void UGrenadeThrowerComponent::ServerThrowGrenade_Implementation(FGrenadeThrowRequest Request)
{
	ProcessAuthoritativeThrow(Request);
}

void UGrenadeThrowerComponent::ClientRejectGrenadeThrow_Implementation(uint32 ThrowId)
{
	if (TWeakObjectPtr<AGrenadeActor>* PredictedPtr = PredictedGrenades.Find(ThrowId))
	{
		if (AGrenadeActor* PredictedGrenade = PredictedPtr->Get())
		{
			PredictedGrenade->CancelPredictedVisual();
		}
		PredictedGrenades.Remove(ThrowId);
	}
	UE_LOG(LogTemp, Warning, TEXT("GRENADE_PREDICTION_REJECT ThrowId=%u"), ThrowId);
}

void UGrenadeThrowerComponent::RunNetworkSelfTestThrow()
{
	if (!IsGameplayReady())
	{
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(
				NetworkSelfTestTimerHandle,
				this,
				&UGrenadeThrowerComponent::RunNetworkSelfTestThrow,
				0.5f,
				false);
		}
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	if (ThrowState == EGrenadeThrowState::Ready && BuildCurrentLaunchParams(LaunchParams))
	{
		UE_LOG(
			LogTemp,
			Log,
			TEXT("Network self-test throw initiated by %s (authority=%d)."),
			*GetNameSafe(GetOwner()),
			GetOwner() && GetOwner()->HasAuthority() ? 1 : 0);
		TryThrowGrenade(&LaunchParams);
	}
}

void UGrenadeThrowerComponent::SpawnGrenadeAuthoritative(
	uint32 ThrowId,
	const FGrenadeLaunchParams& LaunchParams)
{
	if (!GetWorld()
		|| GetWorld()->GetNetMode() == NM_Client
		|| !GetOwner()
		|| !GetOwner()->HasAuthority())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* SpawnClass = GrenadeClass ? GrenadeClass.Get() : AGrenadeActor::StaticClass();
	if (AGrenadeActor* Grenade = GetWorld()->SpawnActor<AGrenadeActor>(
		SpawnClass,
		LaunchParams.SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParams))
	{
		// Blueprint defaults may predate multiplayer support, so enforce the
		// authoritative actor's replication settings at runtime as well.
		Grenade->SetReplicates(true);
		Grenade->SetReplicateMovement(true);
		Grenade->InitializeAuthoritativeGrenade(
			ThrowId,
			LaunchParams.SpawnLocation,
			LaunchParams.InitialVelocity,
			LaunchParams.FuseSeconds,
			SimulationConfig,
			GetOwner());

		UE_LOG(
			LogTemp,
			Log,
			TEXT("Authoritative grenade ThrowId=%u spawned for %s at (%.1f, %.1f, %.1f)."),
			ThrowId,
			*GetNameSafe(GetOwner()),
			LaunchParams.SpawnLocation.X,
			LaunchParams.SpawnLocation.Y,
			LaunchParams.SpawnLocation.Z);
	}
}

void UGrenadeThrowerComponent::SpawnPredictedGrenade(
	uint32 ThrowId,
	const FGrenadeLaunchParams& LaunchParams)
{
	if (!GetWorld() || !GetOwner() || GetOwner()->HasAuthority())
	{
		return;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = GetOwner();
	SpawnParams.Instigator = Cast<APawn>(GetOwner());
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

	UClass* SpawnClass = GrenadeClass ? GrenadeClass.Get() : AGrenadeActor::StaticClass();
	if (AGrenadeActor* PredictedGrenade = GetWorld()->SpawnActor<AGrenadeActor>(
		SpawnClass,
		LaunchParams.SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParams))
	{
		PredictedGrenade->InitializePredictedVisual(
			ThrowId,
			LaunchParams.SpawnLocation,
			LaunchParams.InitialVelocity,
			LaunchParams.FuseSeconds,
			SimulationConfig,
			GetOwner());
		PredictedGrenades.Add(ThrowId, PredictedGrenade);
		UE_LOG(LogTemp, Display, TEXT("GRENADE_PREDICTION_SPAWN ThrowId=%u"), ThrowId);
	}
}

void UGrenadeThrowerComponent::ReconcilePredictedGrenade(
	uint32 ThrowId,
	AGrenadeActor* AuthoritativeGrenade)
{
	if (!AuthoritativeGrenade)
	{
		return;
	}

	if (TWeakObjectPtr<AGrenadeActor>* PredictedPtr = PredictedGrenades.Find(ThrowId))
	{
		if (AGrenadeActor* PredictedGrenade = PredictedPtr->Get())
		{
			PredictedGrenade->BeginPredictionReconciliation(AuthoritativeGrenade);
		}
		PredictedGrenades.Remove(ThrowId);
	}
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

	if (GetOwner() && GetOwner()->HasAuthority())
	{
		FRotator ViewRotation = FRotator::ZeroRotator;
		GetOwner()->GetActorEyesViewPoint(ExplosionOrigin, ViewRotation);
		DetonateInHandAuthoritative(ExplosionOrigin);
	}
	else
	{
		const uint32 ActionId = NextLocalThrowId++;
		if (NextLocalThrowId == 0)
		{
			NextLocalThrowId = 1;
		}
		const AGrenadeGameState* GameState =
			World->GetGameState<AGrenadeGameState>();
		ServerDetonateInHand(
			ActionId,
			static_cast<uint16>(FMath::Clamp(
				FMath::RoundToInt(GetHeldDurationSeconds() * 1000.0f),
				0,
				65535)),
			GameState ? GameState->GetServerWorldTimeSeconds() : 0.0f);
	}

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

void UGrenadeThrowerComponent::ServerDetonateInHand_Implementation(
	uint32 ActionId,
	uint16 HeldDurationMilliseconds,
	float ClientServerWorldTimeSeconds)
{
	const AGrenadeGameState* GameState =
		GetWorld() ? GetWorld()->GetGameState<AGrenadeGameState>() : nullptr;
	const float HeldDuration = static_cast<float>(HeldDurationMilliseconds) / 1000.0f;
	const float TimestampAge = GameState
		? GameState->GetServerWorldTimeSeconds() - ClientServerWorldTimeSeconds
		: BIG_NUMBER;
	if (ThrowState != EGrenadeThrowState::Ready
		|| !GetOwner()
		|| !GetOwner()->HasAuthority()
		|| !IsGameplayReady()
		|| ActionId == 0
		|| ActionId <= LastAcceptedServerThrowId
		|| HeldDuration < FuseSeconds - 0.075f
		|| TimestampAge < -0.25f
		|| TimestampAge > 1.5f
		|| GetWorld()->GetTimeSeconds() + KINDA_SMALL_NUMBER < NextServerThrowAllowedWorldTimeSeconds)
	{
		return;
	}

	LastAcceptedServerThrowId = ActionId;
	NextServerThrowAllowedWorldTimeSeconds =
		GetWorld()->GetTimeSeconds() + FMath::Max(0.0f, ReloadCooldownSeconds);
	FVector ExplosionOrigin = FVector::ZeroVector;
	FRotator ViewRotation = FRotator::ZeroRotator;
	GetOwner()->GetActorEyesViewPoint(ExplosionOrigin, ViewRotation);
	DetonateInHandAuthoritative(ExplosionOrigin);
	EnterCooldown();
}

void UGrenadeThrowerComponent::DetonateInHandAuthoritative(const FVector& ExplosionOrigin)
{
	if (!GetWorld() || !GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
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

	AGrenadeActor::ApplyInstantKillBlast(GetWorld(), ExplosionOrigin, ExplosionRadius, nullptr, GetOwner());
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
