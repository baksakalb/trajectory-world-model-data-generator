#include "Grenade/GrenadeTrajectoryComponent.h"

#include "DrawDebugHelpers.h"
#include "Grenade/GrenadeSim.h"
#include "Grenade/GrenadeThrowerComponent.h"
#include "Grenade/Breakables/BreakableTile.h"

namespace
{
	static TAutoConsoleVariable<int32> CVarGGTrajectoryEnabled(
		TEXT("gg.Grenade.DebugTrajectory"),
		1,
		TEXT("Draw grenade trajectory while aiming. 0=off, 1=on"),
		ECVF_Default);
}

UGrenadeTrajectoryComponent::UGrenadeTrajectoryComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

void UGrenadeTrajectoryComponent::BeginPlay()
{
	Super::BeginPlay();

	if (AActor* OwnerActor = GetOwner())
	{
		ThrowerComponent = OwnerActor->FindComponentByClass<UGrenadeThrowerComponent>();
	}
}

void UGrenadeTrajectoryComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bTrajectoryEnabled || !bAimModeActive || CVarGGTrajectoryEnabled.GetValueOnGameThread() == 0)
	{
		return;
	}

	DrawPredictedPath();
}

void UGrenadeTrajectoryComponent::SetAimModeActive(bool bActive)
{
	bAimModeActive = bActive;
}

ABreakableTile* UGrenadeTrajectoryComponent::ResolveBreakableTile(const FHitResult& Hit) const
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor && Hit.GetComponent())
	{
		HitActor = Hit.GetComponent()->GetOwner();
	}

	ABreakableTile* Tile = Cast<ABreakableTile>(HitActor);
	if (!Tile || Tile->IsBroken())
	{
		return nullptr;
	}

	return Tile;
}

void UGrenadeTrajectoryComponent::DrawPredictedPath()
{
	UGrenadeThrowerComponent* Thrower = ThrowerComponent.Get();
	UWorld* World = GetWorld();
	AActor* OwnerActor = GetOwner();
	if (!Thrower || !World || !OwnerActor)
	{
		return;
	}

	FGrenadeLaunchParams LaunchParams;
	if (!Thrower->BuildLaunchParams(LaunchParams))
	{
		return;
	}

	const FGrenadeSimConfig& SimConfig = Thrower->GetSimulationConfig();
	FGrenadeSimState SimState;
	FGrenadeSim::InitializeState(SimState, LaunchParams.SpawnLocation, LaunchParams.InitialVelocity, LaunchParams.FuseSeconds);

	TArray<FVector> TrajectoryPoints;
	TrajectoryPoints.Reserve(MaxSimulationSteps + 1);
	TrajectoryPoints.Add(SimState.Position);

	const float StepTime = FMath::Max(0.001f, SimConfig.FixedStepSeconds);
	for (int32 StepIndex = 0; StepIndex < MaxSimulationSteps; ++StepIndex)
	{
		const FVector PreviousPosition = SimState.Position;

		FGrenadeSim::Step(
			World,
			SimConfig,
			SimState,
			StepTime,
			OwnerActor,
			[this](const FHitResult& Hit) { return ResolveBreakableTile(Hit); });

		if (!SimState.Position.Equals(PreviousPosition, 0.01f))
		{
			TrajectoryPoints.Add(SimState.Position);
		}

		if (SimState.bExploded)
		{
			break;
		}
	}

	if (TrajectoryPoints.Num() < 2)
	{
		return;
	}

	const FColor DrawColor = (Thrower->IsStateGreen() ? AvailableColor : CooldownColor).ToFColor(true);
	const float SegmentLengthLimitCm = FMath::Max(1.0f, MaxRenderSegmentLengthCm);
	const float DrawDuration = FMath::Max(0.0f, DrawDurationSeconds);
	const uint8 DrawDepthPriority = static_cast<uint8>(DepthPriority);

	for (int32 Index = 0; Index < TrajectoryPoints.Num() - 1; ++Index)
	{
		const FVector SegmentStart = TrajectoryPoints[Index];
		const FVector SegmentEnd = TrajectoryPoints[Index + 1];
		const float SegmentLengthCm = FVector::Distance(SegmentStart, SegmentEnd);
		const int32 SubSegmentCount = FMath::Max(1, FMath::CeilToInt(SegmentLengthCm / SegmentLengthLimitCm));

		FVector DrawStart = SegmentStart;
		for (int32 SubIndex = 1; SubIndex <= SubSegmentCount; ++SubIndex)
		{
			const float Alpha = static_cast<float>(SubIndex) / static_cast<float>(SubSegmentCount);
			const FVector DrawEnd = FMath::Lerp(SegmentStart, SegmentEnd, Alpha);
			DrawDebugLine(World, DrawStart, DrawEnd, DrawColor, false, DrawDuration, DrawDepthPriority, LineThickness);
			DrawStart = DrawEnd;
		}
	}
}
