#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grenade/GrenadeSim.h"
#include "CurriculumDataGenerator.generated.h"

class AStaticMeshActor;
class Ahe_grenade_gameCharacter;
class UCameraComponent;
class USceneCaptureComponent2D;
class UTextureRenderTarget2D;

/**
 * Single-worker curriculum dataset generator.
 *
 * The actor is spawned only when -GenerateDataset is present. It drives a
 * deterministic semi-Markov action policy at a fixed simulation rate, captures
 * observations from the player camera, and writes one directly streamed tar
 * shard. No inspection video is rendered during collection.
 */
UCLASS()
class HE_GRENADE_GAME_API ACurriculumDataGenerator : public AActor
{
	GENERATED_BODY()

public:
	ACurriculumDataGenerator();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	struct FRecordedState
	{
		FVector Position = FVector::ZeroVector;
		FVector Velocity = FVector::ZeroVector;
		FRotator CameraRotation = FRotator::ZeroRotator;
		bool bGrounded = false;
		bool bContact = false;
		FString ContactObject;
	};

	enum class ECurriculumStage : uint8
	{
		Movement = 1,
		Trajectory = 2,
		Throw = 3
	};

	enum class ECoverageMission : uint8
	{
		SemiMarkov,
		ObjectOrbit,
		RampTraverse,
		HoopPass
	};

	struct FGeneratedGrenade
	{
		int32 Id = 0;
		FGrenadeSimState State;
		TWeakObjectPtr<AStaticMeshActor> VisualActor;
	};

	bool ParseConfiguration();
	bool OpenOutput();
	bool ResolvePlayer();
	bool BeginEpisode();
	void EndEpisode();
	void FinishRun(bool bSuccess, const FString& ErrorMessage = FString());
	bool CaptureObservation(int32 ObservationIndex, FRecordedState& OutState);
	void AppendTransition(
		int32 SourceFrameIndex,
		uint16 ActionMask,
		const FRecordedState& SourceState,
		const FRecordedState& TargetState);
	uint16 SelectAction();
	uint16 SelectBaseAction();
	int32 SelectHoldSteps();
	uint16 SelectMovementBits(bool bTowardWall);
	uint16 SelectCameraBits();
	uint16 SelectTrajectoryShowcaseAction() const;
	uint16 SelectCoverageGuidedAction();
	uint16 WorldDirectionToMovementBits(const FVector& DesiredWorldDirection) const;
	uint16 CameraBitsToward(const FVector& WorldTarget, float* OutYawError = nullptr) const;
	void SelectCoverageMission();
	bool GetCoverageMissionSpawn(FVector& OutLocation, float& OutYaw, float& OutPitch) const;
	void UpdateCoverageMetrics(const FRecordedState& State);
	bool IsCoverageTargetVisible(int32 TargetIndex) const;
	FString GetCoverageMissionSlug() const;
	FString GetCoverageTargetSlug() const;
	bool FindEpisodeSpawn(FVector& OutLocation);
	void PrepareNextAction();
	void ApplyAction(uint16 ActionMask);
	void ResetStageState();
	void AdvanceGrenades();
	bool BuildLaunchState(FVector& OutSpawnLocation, FVector& OutVelocity) const;
	bool AcceptThrow();
	void DrawTrajectoryOverlay(TArray<FColor>& Pixels) const;
	FString BuildGrenadesJson() const;
	FString GetStageSlug() const;
	FString GetStageSchemaVersion() const;
	FString MakeEpisodeId() const;
	FString MakeImageKey(int32 ObservationIndex) const;
	FString BuildDatasetJson(bool bComplete, const FString& ErrorMessage) const;

	UPROPERTY()
	TObjectPtr<USceneCaptureComponent2D> SceneCapture;

	UPROPERTY()
	TObjectPtr<UTextureRenderTarget2D> RenderTarget;

	UPROPERTY()
	TObjectPtr<Ahe_grenade_gameCharacter> Character;

	UPROPERTY()
	TObjectPtr<UCameraComponent> PlayerCamera;

	class FCurriculumTarWriter* TarWriter = nullptr;

	FRandomStream EpisodeRandom;
	TArray<FGeneratedGrenade> Grenades;
	FGrenadeSimConfig GrenadeSimConfig;
	FRecordedState PreviousState;
	FString FramesJsonLines;
	FString TransitionsJsonLines;
	FString EpisodesJsonLines;
	FString OutputDirectory;
	FString ShardPath;
	FString RunStartedUtc;
	FString BuildRevision;
	FString UnrealEngineVersion;
	FString LastError;

	int32 EpisodeCount = 2;
	int32 EpisodeSeconds = 10;
	int32 SeedStart = 1000;
	int32 WorkerId = 0;
	int32 ObservationRate = 20;
	int32 CaptureWidth = 256;
	int32 CaptureHeight = 256;
	int32 CooldownRemainingSteps = 0;
	int32 NextGrenadeId = 0;
	int32 NextThrowRequestFrame = 0;
	int32 TransitionsPerEpisode = 200;
	int32 EpisodeIndex = 0;
	int32 FrameIndex = 0;
	int32 GlobalTransitionCount = 0;
	int32 HoldStepsRemaining = 0;
	int32 StartupFramesRemaining = 3;
	int32 CoverageTargetIndex = INDEX_NONE;
	int32 CoverageWaypointIndex = 0;
	int32 CurrentCoverageViewBin = INDEX_NONE;
	int32 CurrentCoverageDistanceBand = INDEX_NONE;
	int32 CurrentEpisodeRampTraversals = 0;
	int32 CurrentEpisodeHoopPasses = 0;
	int32 OverallRampTraversals = 0;
	int32 OverallHoopPasses = 0;
	int32 OverallMissionSuccesses = 0;
	int32 OverallMissionFailures = 0;
	int32 CoverageRequiredHoopPasses = 1;
	int32 CoverageNoProgressSteps = 0;
	uint16 CurrentActionMask = 0;
	uint16 HeldActionMask = 0;
	uint16 LastAppliedActionMask = 0;
	uint16 CurrentEpisodeViewBinsMask = 0;
	uint16 OverallObjectViewBins[5] = {};
	TArray<FVector> CoverageWaypoints;
	FVector CoverageMissionStart = FVector::ZeroVector;
	FVector CoverageMissionGoal = FVector::ZeroVector;
	FVector CoverageAlternateGoal = FVector::ZeroVector;
	FVector CoveragePreviousPosition = FVector::ZeroVector;
	float CoverageOrbitStartAngleDegrees = 0.0f;
	float CoverageOrbitRadiusCm = 0.0f;
	float CoverageInitialYawOffsetDegrees = 0.0f;
	float CoverageInitialPitchOffsetDegrees = 0.0f;
	float CoverageLastHoopSide = 0.0f;
	ECurriculumStage CurriculumStage = ECurriculumStage::Movement;
	ECoverageMission CoverageMission = ECoverageMission::SemiMarkov;
	bool bCurrentERequestEdge = false;
	bool bCurrentEAccepted = false;
	bool bCurrentCoverageTargetVisible = false;
	bool bCoveragePreviousPositionValid = false;
	bool bCoverageOrbitClockwise = false;
	bool bCoverageMissionSucceeded = false;
	bool bCoverageMissionFailed = false;
	bool bRampMounted = false;
	bool bConfigured = false;
	bool bRunFinished = false;
	bool bEpisodeActive = false;
	bool bTarFinalized = false;
	bool bExitOnComplete = true;
	bool bTrajectoryShowcase = false;
	bool bCoverageGuided = true;
};
