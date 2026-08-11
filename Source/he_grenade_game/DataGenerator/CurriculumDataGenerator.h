#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Grenade/GrenadeSim.h"
#include "DataGenerator/V2ActionSemantics.h"
#include "CurriculumDataGenerator.generated.h"

class AStaticMeshActor;
class Ahe_grenade_gameCharacter;
class UCameraComponent;
class USceneCaptureComponent2D;
class FJsonValue;
class UTextureRenderTarget2D;

/**
 * Single-worker curriculum dataset generator.
 *
 * The actor is spawned only when -GenerateDataset is present. It drives a
 * deterministic frame-balanced semi-Markov and guided mission policy at a fixed
 * simulation rate, captures observations from the player camera, and writes one
 * directly streamed tar shard. No inspection video is rendered during
 * collection.
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
		LegacyTrajectory = 2,
		LegacyThrow = 3,
		TrajectoryThrowV2 = 4
	};

	enum class EV2EpisodePhase : uint8
	{
		NotApplicable,
		Traverse,
		Aim,
		Cooldown,
		PostThrow
	};

	enum class EStorageFormat : uint8
	{
		PngJsonl,
		WebPParquet
	};

	enum class ECoverageMission : uint8
	{
		SemiMarkov,
		ObjectView,
		ContactRecovery,
		RampTraverse,
		HoopPass,
		Count
	};

	enum class EObjectViewMode : uint8
	{
		ApproachObserve,
		PassBy,
		PartialOrbit,
		FullOrbit,
		Count
	};

	enum class EObjectGazePattern : uint8
	{
		TargetCenter,
		TargetOffset,
		TravelDirection,
		RoamReacquire,
		Count
	};

	enum class EObjectGazeIntent : uint8
	{
		TargetCenter,
		TargetOffset,
		TravelDirection,
		SurveyPoint
	};

	enum class EContactPhase : uint8
	{
		Approach,
		Hold,
		Recover
	};

	enum class EContactRecoveryStyle : uint8
	{
		Backward,
		StrafeLeft,
		StrafeRight,
		DiagonalLeft,
		DiagonalRight,
		Count
	};

	enum class EContactApproachProfile : uint8
	{
		Direct,
		GlanceLeft,
		GlanceRight,
		Count
	};

	enum class EGuidedCameraStyle : uint8
	{
		ObjectiveCenter,
		ObjectiveOffset,
		TravelReacquire,
		ScanReacquire,
		Count
	};

	enum class ELocomotionFacingProfile : uint8
	{
		Forward,
		Backward,
		StrafeLeft,
		StrafeRight,
		FreeAttention,
		Count
	};

	enum class ERampPathProfile : uint8
	{
		Center,
		DiagonalLeftToRight,
		DiagonalRightToLeft,
		Count
	};

	enum class EHoopPathProfile : uint8
	{
		Center,
		ObliqueLeftToRight,
		ObliqueRightToLeft,
		Count
	};

	enum class ERampDirection : uint8
	{
		Uphill,
		Downhill
	};

	enum class EPostSuccessStyle : uint8
	{
		Continue,
		GentleTurn,
		GlanceReacquire,
		StrafeBlend,
		EaseAndObserve,
		DriftAndSettle,
		Count
	};

	struct FGeneratedGrenade
	{
		int32 Id = 0;
		FGrenadeSimState State;
		TWeakObjectPtr<AStaticMeshActor> VisualActor;
		int32 PreviewStartFrame = 0;
		int32 ThrowSourceFrame = INDEX_NONE;
		int32 FirstContactFrame = INDEX_NONE;
		int32 RestFrame = INDEX_NONE;
		int32 PostRestTailSteps = 0;
		int32 ArenaExitFrame = INDEX_NONE;
		int32 PredictedFirstContactStep = INDEX_NONE;
		int32 PredictedRestStep = INDEX_NONE;
		int32 VisibleObservationCount = 0;
		int32 PredictedBounceCount = 0;
		FRotator ThrowCamera = FRotator::ZeroRotator;
		FVector LaunchPosition = FVector::ZeroVector;
		FVector LaunchVelocity = FVector::ZeroVector;
		FVector FirstContactPosition = FVector::ZeroVector;
		FVector FirstContactNormal = FVector::ZeroVector;
		FVector FirstContactVelocity = FVector::ZeroVector;
		FVector PredictedRestPosition = FVector::ZeroVector;
		FGrenadeSimConfig SimConfig;
		FString RealizedTarget;
		FString ArenaExitDirection;
		FString PredictedExitDirection;
		TArray<FString> ContactOrder;
		TArray<FString> PredictedContactOrder;
	};

	struct FPrescribedRecipe
	{
		FString RecipeId;
		FString Mission;
		int32 EpisodeIndex = 0;
		int32 ScenarioIndex = INDEX_NONE;
		int32 ContinuousSampleOrdinal = 0;
		int32 RefinementLevel = 0;
		int32 RepetitionIndex = 0;
		int32 PlannedCreditedFrames = 0;
		FString Source;
		FString CellId;
		FString ReplayIdentity;
		FString Split;
		FString MissionType;
		FString Family;
		FString EventKind;
		FString TargetActor;
		FString TargetRegion;
		FString CanonicalPhysicsId;
		FString Boundary;
		FString Direction;
		FVector MissionTargetPoint = FVector::ZeroVector;
		FVector MissionPlayerSpawn = FVector::ZeroVector;
		float MissionRegionRadiusCm = 0.0f;
		float MaximumContactDistanceCm = 0.0f;
		float HoopSafePassageRadiusCm = 0.0f;
		float RampCrossingHalfWidthCm = 0.0f;
		float RampOppositeLandingOffsetCm = 0.0f;
		int32 EstablishSteps = 0;
		int32 CameraAdjustSteps = 0;
		int32 PreviewDwellSteps = 0;
		int32 MissionObservationFrames = 0;
		TArray<FString> ExpectedContactOrder;
	};

	bool ParseConfiguration();
	bool LoadRecipeManifest(const FString& ManifestPath);
	bool OpenOutput();
	bool ResolvePlayer();
	bool BeginEpisode();
	void RunV2PlanCertification();
	void EndEpisode();
	void FinishRun(bool bSuccess, const FString& ErrorMessage = FString());
	FTransform GetObservationCameraTransform() const;
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
	float SelectPitchTargetDegrees();
	int32 GetPitchBandIndex(float PitchDegrees) const;
	void UpdatePitchMetrics(float PitchDegrees);
	uint16 BalancePitchAction(uint16 ActionMask);
	uint64 GetParameterBits(const TCHAR* ParameterName, int32 SampleIndex = 0) const;
	float SampleParameterUnit(const TCHAR* ParameterName, int32 SampleIndex = 0) const;
	float SampleParameterRange(
		const TCHAR* ParameterName,
		float Minimum,
		float Maximum,
		int32 SampleIndex = 0) const;
	int32 SampleParameterIndex(
		const TCHAR* ParameterName,
		int32 Count,
		int32 SampleIndex = 0) const;
	bool SampleParameterBool(const TCHAR* ParameterName, int32 SampleIndex = 0) const;
	float SampleStratifiedRange(
		const TCHAR* ParameterName,
		int32 BinCount,
		float Minimum,
		float Maximum,
		int32 SampleIndex = 0) const;
	void BuildTransitionScript();
	FVector GetNaturalPlayEscapeDirection(const FRecordedState& State) const;
	uint16 SelectNaturalPlayEscapeAction(const FVector& EscapeDirection) const;
	void StartPostSuccessRollout(
		const FRecordedState& State,
		int32 SuccessObservationIndex);
	uint16 SelectPostSuccessAction() const;
	uint16 SelectTrajectoryShowcaseAction() const;
	uint16 SelectPersistentSemiMarkovAction(bool bAllowThrows);
	uint16 SelectPersistentSemiMarkovBaseAction(bool bAllowGrenadeControls);
	uint16 SelectPersistentSemiMarkovMovementBits();
	uint16 SelectPersistentSemiMarkovCameraBits();
	uint16 ApplySemiMarkovPostThrowAttention(uint16 ActionMask);
	int32 SelectPersistentSemiMarkovHoldSteps();
	uint16 SelectV2MissionAction() const;
	bool ConfigureV2MissionSpawn(FVector& OutLocation, float& OutYaw, float& OutPitch);
	bool CertifyV2MissionConstruction();
	bool IsV2MissionRegionVisible() const;
	bool IsV2PreviewUsefulSegmentVisible() const;
	bool IsV2OpeningArenaContextVisible() const;
	void UpdateV2MissionEvidence(
		const FGeneratedGrenade& Grenade,
		const FGrenadeSimStepResult& StepResult,
		const FVector& PreviousPosition,
		const FVector& PreviousVelocity);
	FString BuildV2ThrowsJson() const;
	uint16 SelectCoverageGuidedAction();
	uint16 WorldDirectionToMovementBits(const FVector& DesiredWorldDirection) const;
	uint16 CameraBitsToward(const FVector& WorldTarget, float* OutYawError = nullptr) const;
	uint16 SelectStableCameraBitsToward(const FVector& WorldTarget);
	void ResetStableCameraSteering();
	FVector GetLocomotionFacingDirection(const FVector& TravelDirection) const;
	FVector SelectGuidedCameraTarget(
		const FVector& ObjectiveTarget,
		const FVector& TravelGoal) const;
	void ConfigureInitialFacingTarget(const FVector& FirstTravelGoal);
	void SelectCoverageMission();
	void ConfigureObjectViewMission();
	void ConfigureObjectGazePlan();
	void UpdateObjectGazeTarget(const FVector& ObserverLocation);
	void ConfigureContactRecoveryMission();
	void ConfigureRampMission();
	void ConfigureHoopMission();
	bool GetCoverageMissionSpawn(FVector& OutLocation, float& OutYaw, float& OutPitch) const;
	void UpdateCoverageMetrics(
		const FRecordedState& State,
		int32 ObservationIndex);
	bool IsCoverageTargetVisible(int32 TargetIndex) const;
	bool IsCurrentContactTarget(const FString& ContactObject) const;
	FString GetCoverageMissionSlug() const;
	FString GetCoverageTargetSlug() const;
	FString GetObjectViewModeSlug() const;
	FString GetObjectGazePatternSlug() const;
	FString GetObjectGazeIntentSlug(EObjectGazeIntent Intent) const;
	FString BuildObjectGazePlanJson() const;
	FString GetContactPhaseSlug() const;
	FString GetContactRecoveryStyleSlug() const;
	FString GetContactApproachProfileSlug() const;
	FString GetGuidedCameraStyleSlug() const;
	FString GetLocomotionFacingProfileSlug() const;
	FString GetRampDirectionSlug() const;
	FString GetRampPathProfileSlug() const;
	FString GetHoopPathProfileSlug() const;
	FString GetMissionPhaseSlug() const;
	FString GetPostSuccessStyleSlug() const;
	FString GetMissionReviewSlug() const;
	bool FindEpisodeSpawn(FVector& OutLocation);
	void PrepareNextAction();
	void ApplyAction(uint16 ActionMask);
	void ResetStageState();
	void AdvanceGrenades();
	bool BuildLaunchState(FVector& OutSpawnLocation, FVector& OutVelocity) const;
	int32 AcceptThrow();
	void DrawTrajectoryOverlay(TArray<FColor>& Pixels) const;
	FString BuildGrenadesJson() const;
	EV2EpisodePhase GetV2EpisodePhase() const;
	FString GetV2EpisodePhaseSlug() const;
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
	FRandomStream GrenadeActionRandom;
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
	FString MissionOverride;
	FString ObjectViewModeOverride;
	FString CoverageTargetOverride;
	FString MissionDirectionOverride;
	FString RecipeManifestPath;
	FString PlanId;
	FString PlanVersion;
	FString AssignmentId;
	FString AttemptId;
	FString ExecutorId;
	FString DatasetSplit;
	FString CurrentRecipeId;
	FString CurrentV2Source;
	FString CurrentV2ReplayIdentity;
	FString CurrentV2MissionType;
	FString CurrentV2Family;
	FString CurrentV2EventKind;
	FString CurrentV2TargetActor;
	FString CurrentV2TargetRegion;
	FString CurrentV2CanonicalPhysicsId;
	FString CurrentV2Boundary;
	FString CurrentV2Direction;
	TArray<int32> RequestedEpisodeIndices;
	TArray<FPrescribedRecipe> PrescribedRecipes;

	int32 EpisodeCount = 2;
	int32 EpisodeSeconds = 10;
	int32 SeedStart = 1000;
	int32 WorkerId = 0;
	int32 ObservationRate = 20;
	int32 CaptureWidth = 256;
	int32 CaptureHeight = 256;
	int32 WebPLosslessEffort = 0;
	int32 CooldownRemainingSteps = 0;
	int32 NextGrenadeId = 0;
	int32 CurrentAcceptedGrenadeId = INDEX_NONE;
	int32 CurrentCooldownBeforeSteps = 0;
	int32 CurrentCooldownAfterSteps = 0;
	int32 NextThrowRequestFrame = 0;
	int32 TransitionsPerEpisode = 200;
	int32 EpisodeIndex = 0;
	int32 EpisodeOrdinal = 0;
	int32 FrameIndex = 0;
	int32 GlobalTransitionCount = 0;
	int32 HoldStepsRemaining = 0;
	int32 StartupFramesRemaining = 3;
	int32 CoverageTargetIndex = INDEX_NONE;
	int32 CoverageWaypointIndex = 0;
	int32 CurrentObjectGazePhaseIndex = 0;
	int32 CurrentObjectScenarioIndex = INDEX_NONE;
	int32 CurrentContactScenarioIndex = INDEX_NONE;
	int32 CurrentContactApproachSector = INDEX_NONE;
	int32 CurrentRampScenarioIndex = INDEX_NONE;
	int32 CurrentHoopScenarioIndex = INDEX_NONE;
	int32 CurrentCoveragePositionBin = INDEX_NONE;
	int32 CurrentCoveragePositionDistanceBand = INDEX_NONE;
	int32 CurrentPrescribedScenarioIndex = INDEX_NONE;
	int32 CurrentContinuousSampleOrdinal = 0;
	int32 CurrentRefinementLevel = 0;
	int32 CurrentRepetitionIndex = 0;
	int32 CurrentPlannedCreditedFrames = 0;
	int32 CurrentV2AcceptedThrowCount = 0;
	int32 CurrentV2EstablishSteps = 0;
	int32 CurrentV2CameraAdjustSteps = 0;
	int32 CurrentV2PreviewDwellSteps = 0;
	int32 CurrentV2MissionObservationFrames = 0;
	int32 CurrentV2MissionEventFrame = INDEX_NONE;
	int32 CurrentV2PreviewStartFrame = 0;
	int32 CurrentV2ERequestCount = 0;
	int32 CurrentV2QRisingCount = 0;
	int32 CurrentV2QFallingCount = 0;
	int32 CurrentV2RejectedQNotHeldCount = 0;
	int32 CurrentV2RejectedPreviewCount = 0;
	int32 CurrentV2RejectedCooldownCount = 0;
	int32 CurrentV2MovementActionCount = 0;
	int32 CurrentV2PostThrowAttentionMode = 0;
	int32 CurrentV2PostThrowAttentionStepsRemaining = 0;
	int32 CurrentV2PostThrowLookBackDelaySteps = 0;
	int32 CurrentV2InactiveFixedActionSteps = 0;
	int32 CurrentV2CameraSteeringStepsRemaining = 0;
	int32 CurrentV2CameraNeutralStepsRemaining = 0;
	int32 CurrentEpisodeRampTraversals = 0;
	int32 CurrentEpisodeHoopPasses = 0;
	int32 OverallRampTraversals = 0;
	int32 OverallHoopPasses = 0;
	int32 OverallMissionSuccesses = 0;
	int32 OverallMissionFailures = 0;
	int32 CoverageRequiredHoopPasses = 1;
	int32 CoverageNoProgressSteps = 0;
	int32 CoverageVisibleHoldSteps = 0;
	int32 CoverageRequiredVisibleHoldSteps = 0;
	int32 CoverageRequiredAzimuthBinCount = 0;
	int32 CoverageContactHoldSteps = 0;
	int32 CoverageVerifiedContactSteps = 0;
	int32 CoverageRequiredContactHoldSteps = 0;
	int32 CoverageRecoverySteps = 0;
	int32 CoverageRequiredRecoverySteps = 0;
	int32 CoveragePostObjectiveSteps = 0;
	int32 CoverageRequiredPostObjectiveSteps = 0;
	int32 CoverageMissionSuccessFrameIndex = INDEX_NONE;
	int32 CoveragePostSuccessSteps = 0;
	int32 CoverageRequiredPostSuccessSteps = 0;
	int32 CurrentEpisodeFacingMovingFrames = 0;
	int32 CurrentEpisodeFacingMatchedFrames = 0;
	int32 NaturalPlayContactSteps = 0;
	int32 NaturalPlayContactLimitSteps = 0;
	int32 NaturalPlayContactEventIndex = 0;
	int32 NaturalPlayEscapeStepsRemaining = 0;
	int32 NaturalPlayEscapeCount = 0;
	int32 NaturalPlayMaximumContactSteps = 0;
	int32 ActionScriptIndex = 0;
	int32 ActionScriptStepsRemaining = 0;
	int32 ContactTargetIndex = INDEX_NONE;
	uint16 CurrentActionMask = 0;
	uint16 HeldActionMask = 0;
	uint16 LastAppliedActionMask = 0;
	uint16 CurrentV2CameraSteeringMask = 0;
	uint16 CurrentV2InactiveFixedActionMask = 0;
	bool bCurrentV2PendingStressEEdge = false;
	uint16 PostSuccessBaseActionMask = 0;
	uint16 CurrentEpisodeViewBinsMask = 0;
	uint16 CurrentEpisodeVisitedBinsMask = 0;
	uint16 CoverageRequiredAzimuthBinsMask = 0;
	uint16 OverallObjectViewBins[5] = {};
	uint16 OverallObjectVisitedBins[5] = {};
	int64 OverallMissionObservationFrames[static_cast<int32>(ECoverageMission::Count)] = {};
	int64 OverallPostSuccessObservationFrames = 0;
	int64 OverallObjectModeObservationFrames[static_cast<int32>(EObjectViewMode::Count)] = {};
	int64 OverallObjectGazePatternObservationFrames[
		static_cast<int32>(EObjectGazePattern::Count)] = {};
	int64 OverallObjectTargetObservationFrames[5] = {};
	int64 OverallObjectScenarioObservationFrames[120] = {};
	int64 OverallObjectOrbitDirectionObservationFrames[2] = {};
	int64 OverallObjectGazeIntentObservationFrames[4] = {};
	int64 CurrentEpisodeObjectGazeIntentFrames[4] = {};
	int64 OverallContactTargetObservationFrames[9] = {};
	int64 OverallContactBaseScenarioObservationFrames[135] = {};
	int64 OverallContactScenarioObservationFrames[675] = {};
	int64 OverallContactRecoveryStyleObservationFrames[5] = {};
	int64 OverallContactApproachProfileObservationFrames[3] = {};
	int64 OverallContactFacingObservationFrames[5] = {};
	int64 OverallRampDirectionObservationFrames[2] = {};
	int64 OverallRampPathObservationFrames[3] = {};
	int64 OverallRampFacingObservationFrames[5] = {};
	int64 OverallRampScenarioObservationFrames[30] = {};
	int64 OverallHoopDirectionObservationFrames[2] = {};
	int64 OverallHoopPathObservationFrames[3] = {};
	int64 OverallHoopFacingObservationFrames[5] = {};
	int64 OverallHoopScenarioObservationFrames[30] = {};
	int64 OverallGuidedCameraStyleObservationFrames[4] = {};
	int64 OverallPitchBandObservationFrames[4] = {};
	int32 PersistentPositionBinFrames[9] = {};
	int32 PersistentViewBinFrames[8] = {};
	TArray<uint16> ActionScriptMasks;
	TArray<int32> ActionScriptHoldSteps;
	TArray<FVector> CoverageWaypoints;
	TArray<EObjectGazeIntent> ObjectGazePlanIntents;
	TArray<int32> ObjectGazePlanDurations;
	TArray<FVector> ObjectGazePlanOffsets;
	FVector CoverageMissionStart = FVector::ZeroVector;
	FVector CurrentV2MissionTargetPoint = FVector::ZeroVector;
	FVector CurrentV2MissionPlayerSpawn = FVector::ZeroVector;
	FVector CurrentV2CertifiedLaunchPosition = FVector::ZeroVector;
	FVector CurrentV2CertifiedLaunchVelocity = FVector::ZeroVector;
	FVector CurrentV2MissionEventPosition = FVector::ZeroVector;
	FVector CurrentV2MissionEventNormal = FVector::ZeroVector;
	FVector CurrentV2MissionEventVelocity = FVector::ZeroVector;
	TArray<FString> CurrentV2ExpectedContactOrder;
	FVector CoverageMissionGoal = FVector::ZeroVector;
	FVector CoverageLookTarget = FVector::ZeroVector;
	FVector CurrentObjectGazeTarget = FVector::ZeroVector;
	FVector CoverageContactPoint = FVector::ZeroVector;
	FVector CoverageRecoveryGoal = FVector::ZeroVector;
	FVector CoveragePreviousPosition = FVector::ZeroVector;
	FVector NaturalPlayEscapeDirection = FVector::ZeroVector;
	FVector CoverageCameraOffset = FVector::ZeroVector;
	FVector CoverageInitialLookTarget = FVector::ZeroVector;
	float CoverageOrbitStartAngleDegrees = 0.0f;
	float CoverageOrbitRadiusCm = 0.0f;
	float CoverageInitialYawOffsetDegrees = 0.0f;
	float CoverageInitialPitchOffsetDegrees = 0.0f;
	float CoverageLastHoopSide = 0.0f;
	float CoverageLastHoopCrossingY = 0.0f;
	float CoverageLastHoopCrossingZ = 0.0f;
	float CurrentMovementCameraYawDeltaDegrees = 0.0f;
	float HeldCameraPitchTargetDegrees = 0.0f;
	float CoverageDistanceToGoalAtSuccessCm = 0.0f;
	float CurrentV2MissionRegionRadiusCm = 0.0f;
	float CurrentV2MaximumContactDistanceCm = 0.0f;
	float CurrentV2HoopSafePassageRadiusCm = 0.0f;
	float CurrentV2RampCrossingHalfWidthCm = 0.0f;
	float CurrentV2RampOppositeLandingOffsetCm = 0.0f;
	float CurrentV2MissionEventClearanceCm = 0.0f;
	float CurrentV2MissionAimYaw = 0.0f;
	float CurrentV2MissionAimPitch = 0.0f;
	ECurriculumStage CurriculumStage = ECurriculumStage::Movement;
	EStorageFormat StorageFormat = EStorageFormat::PngJsonl;
	ECoverageMission CoverageMission = ECoverageMission::SemiMarkov;
	EObjectViewMode ObjectViewMode = EObjectViewMode::ApproachObserve;
	EObjectGazePattern ObjectGazePattern = EObjectGazePattern::TargetCenter;
	EObjectGazeIntent CurrentObjectGazeIntent = EObjectGazeIntent::TargetCenter;
	EContactPhase ContactPhase = EContactPhase::Approach;
	EContactRecoveryStyle ContactRecoveryStyle = EContactRecoveryStyle::Backward;
	EContactApproachProfile ContactApproachProfile = EContactApproachProfile::Direct;
	EGuidedCameraStyle GuidedCameraStyle = EGuidedCameraStyle::ObjectiveCenter;
	ELocomotionFacingProfile LocomotionFacingProfile =
		ELocomotionFacingProfile::Forward;
	ERampPathProfile RampPathProfile = ERampPathProfile::Center;
	EHoopPathProfile HoopPathProfile = EHoopPathProfile::Center;
	ERampDirection RampDirection = ERampDirection::Uphill;
	EPostSuccessStyle PostSuccessStyle = EPostSuccessStyle::Continue;
	bool bPostSuccessMirror = false;
	bool bNaturalPlayEscapeActionActive = false;
	bool bCurrentERequestEdge = false;
	bool bCurrentEAccepted = false;
	bool bCurrentQRising = false;
	bool bCurrentQFalling = false;
	bool bCurrentPlanarMovementSuppressed = false;
	bool bQVisibleInLatestObservation = false;
	bool bV2PreviewRequiresFreshReadyQPress = false;
	bool bCurrentCoverageTargetVisible = false;
	bool bCoveragePreviousPositionValid = false;
	bool bCoverageOrbitClockwise = false;
	bool bCoveragePrimaryObjectiveAchieved = false;
	bool bCoverageFacingProfileRequired = false;
	bool bCoverageFacingMeasurementComplete = false;
	bool bCoverageInitialLookTargetValid = false;
	bool bCoverageHoopCrossingRecorded = false;
	bool bCoverageMissionSucceeded = false;
	bool bCoverageMissionFailed = false;
	bool bCoverageMissionConfigurationValid = true;
	bool bRampMounted = false;
	bool bHoopPositiveToNegative = false;
	bool bConfigured = false;
	bool bRunFinished = false;
	bool bEpisodeActive = false;
	bool bTarFinalized = false;
	bool bExitOnComplete = true;
	// QA-only escape hatch for rendering a rejected immutable recipe. Normal
	// collection commands never set this and still fail closed.
	bool bAllowUncertifiedV2Diagnostic = false;
	bool bV2PlanCertificationOnly = false;
	int32 V2CertificationCertifiedCount = 0;
	int32 V2CertificationFailedCount = 0;
	TArray<TSharedPtr<FJsonValue>> V2CertificationResults;
	bool bTrajectoryShowcase = false;
	bool bMissionReviewSuite = false;
	bool bV2SemiMarkovRecipe = false;
	bool bV2RecipeManifest = false;
	bool bV2MissionRecipe = false;
	bool bV2ConstructionCertified = false;
	bool bV2MissionEventObserved = false;
	bool bV2RampBodyCrossed = false;
	bool bV2HoopContactObserved = false;
	bool bV2MissionRegionVisibleAllFrames = true;
	bool bV2PreviewRegionVisibleAllQFrames = true;
	bool bV2OpeningArenaContextVisible = true;
	bool bV2ControlPreviewHiddenDuringCooldown = false;
	bool bCoverageGuided = true;
	bool bPrescribedRecipes = false;
	V2ActionSemantics::EThrowRejectionReason CurrentERejectionReason =
		V2ActionSemantics::EThrowRejectionReason::None;
};
