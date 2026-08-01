#include "DataGenerator/CurriculumDataGenerator.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "DataGenerator/CurriculumAction.h"
#include "Dom/JsonObject.h"
#include "Engine/Engine.h"
#include "Engine/OverlapResult.h"
#include "Engine/StaticMesh.h"
#include "Engine/StaticMeshActor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "HAL/FileManager.h"
#include "ImageUtils.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/App.h"
#include "Misc/CommandLine.h"
#include "Misc/DateTime.h"
#include "Misc/EngineVersion.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Misc/ScopeExit.h"
#include "Serialization/Archive.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "he_grenade_gameCharacter.h"
#include "he_grenade_gameGameMode.h"

namespace
{
	constexpr int32 TarBlockSize = 512;
	constexpr int32 TarEndBlockCount = 2;
	constexpr float CurriculumThrowSpeedCmPerSecond = 1400.0f;
	constexpr int32 MaxTrajectorySimulationSteps = 720;
	constexpr int32 CoverageAzimuthBinCount = 12;

	struct FCoverageTargetDefinition
	{
		const TCHAR* Slug;
		FName ActorTag;
		FVector LookTarget;
		float OrbitRadiusCm;
	};

	const FCoverageTargetDefinition& GetCoverageTargetDefinition(const int32 Index)
	{
		static const FCoverageTargetDefinition Targets[] =
		{
			{
				TEXT("rectangle"),
				TEXT("CurriculumObject_Rectangle"),
				FVector(-700.0f, 700.0f, 125.0f),
				560.0f
			},
			{
				TEXT("pyramid"),
				TEXT("CurriculumObject_Pyramid"),
				FVector(700.0f, 700.0f, 115.0f),
				560.0f
			},
			{
				TEXT("sphere"),
				TEXT("CurriculumObject_Sphere"),
				FVector(-700.0f, -700.0f, 120.0f),
				560.0f
			},
			{
				TEXT("hoop"),
				TEXT("CurriculumObject_Hoop"),
				FVector(700.0f, -700.0f, 145.0f),
				560.0f
			},
			{
				TEXT("ramp"),
				TEXT("CurriculumObject_Ramp"),
				FVector(0.0f, 0.0f, 95.0f),
				650.0f
			}
		};
		return Targets[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(Targets) - 1)];
	}

	FString JsonNumber(const double Value)
	{
		return FString::SanitizeFloat(Value, 6);
	}

	FString JsonVector(const FVector& Value)
	{
		return FString::Printf(
			TEXT("{\"x\":%s,\"y\":%s,\"z\":%s}"),
			*JsonNumber(Value.X),
			*JsonNumber(Value.Y),
			*JsonNumber(Value.Z));
	}

	const TCHAR* JsonBool(const bool bValue)
	{
		return bValue ? TEXT("true") : TEXT("false");
	}

	void BlendPixel(
		TArray<FColor>& Pixels,
		const int32 Width,
		const int32 Height,
		const int32 PixelX,
		const int32 PixelY,
		const FColor Color,
		const float Coverage)
	{
		if (PixelX < 0 || PixelX >= Width || PixelY < 0 || PixelY >= Height)
		{
			return;
		}

		FColor& Destination = Pixels[PixelY * Width + PixelX];
		const float Alpha = FMath::Clamp(Coverage, 0.0f, 1.0f);
		Destination.R = static_cast<uint8>(FMath::RoundToInt(
			FMath::Lerp(static_cast<float>(Destination.R), static_cast<float>(Color.R), Alpha)));
		Destination.G = static_cast<uint8>(FMath::RoundToInt(
			FMath::Lerp(static_cast<float>(Destination.G), static_cast<float>(Color.G), Alpha)));
		Destination.B = static_cast<uint8>(FMath::RoundToInt(
			FMath::Lerp(static_cast<float>(Destination.B), static_cast<float>(Color.B), Alpha)));
		Destination.A = 255;
	}

	void PaintAntialiasedLine(
		TArray<FColor>& Pixels,
		const int32 Width,
		const int32 Height,
		const FVector2D& Start,
		const FVector2D& End,
		const float HalfWidth,
		const FColor Color)
	{
		const FVector2D Segment = End - Start;
		const float SegmentLengthSquared = Segment.SizeSquared();
		const float SafeHalfWidth = FMath::Max(0.25f, HalfWidth);
		const float FeatherWidth = 1.0f;
		const float RasterRadius = SafeHalfWidth + FeatherWidth;
		const int32 MinX = FMath::FloorToInt(FMath::Min(Start.X, End.X) - RasterRadius);
		const int32 MaxX = FMath::CeilToInt(FMath::Max(Start.X, End.X) + RasterRadius);
		const int32 MinY = FMath::FloorToInt(FMath::Min(Start.Y, End.Y) - RasterRadius);
		const int32 MaxY = FMath::CeilToInt(FMath::Max(Start.Y, End.Y) + RasterRadius);

		for (int32 PixelY = MinY; PixelY <= MaxY; ++PixelY)
		{
			for (int32 PixelX = MinX; PixelX <= MaxX; ++PixelX)
			{
				const FVector2D PixelCenter(
					static_cast<float>(PixelX) + 0.5f,
					static_cast<float>(PixelY) + 0.5f);
				const float SegmentAlpha = SegmentLengthSquared > KINDA_SMALL_NUMBER
					? FMath::Clamp(
						FVector2D::DotProduct(PixelCenter - Start, Segment)
							/ SegmentLengthSquared,
						0.0f,
						1.0f)
					: 0.0f;
				const FVector2D ClosestPoint = Start + (Segment * SegmentAlpha);
				const float DistanceToLine = FVector2D::Distance(PixelCenter, ClosestPoint);
				const float Coverage = FMath::Clamp(
					(SafeHalfWidth + FeatherWidth - DistanceToLine) / FeatherWidth,
					0.0f,
					1.0f);
				if (Coverage > 0.0f)
				{
					BlendPixel(
						Pixels,
						Width,
						Height,
						PixelX,
						PixelY,
						Color,
						Coverage);
				}
			}
		}
	}

	bool ProjectToCapture(
		const FVector& WorldPoint,
		const FTransform& CameraTransform,
		const float HorizontalFovDegrees,
		const int32 Width,
		const int32 Height,
		FVector2D& OutPixel)
	{
		const FVector CameraPoint =
			CameraTransform.InverseTransformPositionNoScale(WorldPoint);
		if (CameraPoint.X <= 1.0f)
		{
			return false;
		}

		const float AspectRatio =
			static_cast<float>(Width) / static_cast<float>(FMath::Max(1, Height));
		const float TanHalfHorizontal =
			FMath::Tan(FMath::DegreesToRadians(HorizontalFovDegrees * 0.5f));
		const float TanHalfVertical = TanHalfHorizontal / AspectRatio;
		const float NormalizedX = CameraPoint.Y / (CameraPoint.X * TanHalfHorizontal);
		const float NormalizedY = CameraPoint.Z / (CameraPoint.X * TanHalfVertical);
		if (FMath::Abs(NormalizedX) > 1.2f || FMath::Abs(NormalizedY) > 1.2f)
		{
			return false;
		}

		OutPixel.X = (0.5f + (NormalizedX * 0.5f)) * static_cast<float>(Width);
		OutPixel.Y = (0.5f - (NormalizedY * 0.5f)) * static_cast<float>(Height);
		return true;
	}

	void SetOctalField(ANSICHAR* Field, const int32 FieldSize, const uint64 Value)
	{
		FMemory::Memset(Field, '0', FieldSize);
		Field[FieldSize - 1] = '\0';

		uint64 Remaining = Value;
		int32 WriteIndex = FieldSize - 2;
		while (Remaining > 0 && WriteIndex >= 0)
		{
			Field[WriteIndex--] = static_cast<ANSICHAR>('0' + (Remaining & 7));
			Remaining >>= 3;
		}
	}
}

class FCurriculumTarWriter
{
public:
	bool Open(const FString& Filename)
	{
		Archive.Reset(IFileManager::Get().CreateFileWriter(*Filename));
		return Archive.IsValid();
	}

	bool AddFile(const FString& Name, const void* Data, const int64 DataSize)
	{
		if (!Archive || bFinalized || DataSize < 0)
		{
			return false;
		}

		FTCHARToUTF8 Utf8Name(*Name);
		if (Utf8Name.Length() <= 0 || Utf8Name.Length() >= 100)
		{
			return false;
		}

		ANSICHAR Header[TarBlockSize];
		FMemory::Memzero(Header, sizeof(Header));
		FMemory::Memcpy(Header, Utf8Name.Get(), Utf8Name.Length());
		SetOctalField(Header + 100, 8, 0644);
		SetOctalField(Header + 108, 8, 0);
		SetOctalField(Header + 116, 8, 0);
		SetOctalField(Header + 124, 12, static_cast<uint64>(DataSize));
		SetOctalField(Header + 136, 12, 0);
		FMemory::Memset(Header + 148, ' ', 8);
		Header[156] = '0';
		FMemory::Memcpy(Header + 257, "ustar", 5);
		Header[262] = '\0';
		FMemory::Memcpy(Header + 263, "00", 2);

		uint32 Checksum = 0;
		for (const uint8 Byte : MakeArrayView(
			reinterpret_cast<const uint8*>(Header),
			TarBlockSize))
		{
			Checksum += Byte;
		}
		SetOctalField(Header + 148, 7, Checksum);
		Header[155] = ' ';

		Archive->Serialize(Header, sizeof(Header));
		if (DataSize > 0)
		{
			Archive->Serialize(const_cast<void*>(Data), DataSize);
		}

		const int64 PaddingSize =
			(TarBlockSize - (DataSize % TarBlockSize)) % TarBlockSize;
		if (PaddingSize > 0)
		{
			uint8 Padding[TarBlockSize] = {};
			Archive->Serialize(Padding, PaddingSize);
		}
		return !Archive->IsError();
	}

	bool AddTextFile(const FString& Name, const FString& Text)
	{
		FTCHARToUTF8 Utf8Text(*Text);
		return AddFile(Name, Utf8Text.Get(), Utf8Text.Length());
	}

	bool Finalize()
	{
		if (!Archive)
		{
			return false;
		}
		if (!bFinalized)
		{
			uint8 EndBlocks[TarBlockSize * TarEndBlockCount] = {};
			Archive->Serialize(EndBlocks, sizeof(EndBlocks));
			Archive->Close();
			bFinalized = !Archive->IsError();
		}
		Archive.Reset();
		return bFinalized;
	}

	~FCurriculumTarWriter()
	{
		if (Archive)
		{
			Archive->Close();
		}
	}

private:
	TUniquePtr<FArchive> Archive;
	bool bFinalized = false;
};

ACurriculumDataGenerator::ACurriculumDataGenerator()
{
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	USceneComponent* Root = CreateDefaultSubobject<USceneComponent>(TEXT("Root"));
	SetRootComponent(Root);

	SceneCapture = CreateDefaultSubobject<USceneCaptureComponent2D>(TEXT("DatasetSceneCapture"));
	SceneCapture->SetupAttachment(Root);
	SceneCapture->bCaptureEveryFrame = false;
	SceneCapture->bCaptureOnMovement = false;
	SceneCapture->CaptureSource = ESceneCaptureSource::SCS_FinalColorLDR;
	SceneCapture->PrimitiveRenderMode =
		ESceneCapturePrimitiveRenderMode::PRM_RenderScenePrimitives;
	SceneCapture->ShowFlags.SetMotionBlur(false);
	SceneCapture->ShowFlags.SetTemporalAA(false);
}

void ACurriculumDataGenerator::BeginPlay()
{
	Super::BeginPlay();

	if (!ParseConfiguration())
	{
		bRunFinished = true;
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Dataset generator configuration failed: %s"),
			*LastError);
		if (bExitOnComplete)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
		return;
	}
	if (!OpenOutput())
	{
		// In particular, never replace dataset.json when the output directory
		// already contains a completed run.
		bRunFinished = true;
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Dataset generator output initialization failed: %s"),
			*LastError);
		if (bExitOnComplete)
		{
			FGenericPlatformMisc::RequestExit(false);
		}
		return;
	}

	FApp::SetUseFixedTimeStep(true);
	FApp::SetFixedDeltaTime(1.0 / static_cast<double>(ObservationRate));

	if (IConsoleVariable* VSync = IConsoleManager::Get().FindConsoleVariable(TEXT("r.VSync")))
	{
		VSync->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* MotionBlur =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.MotionBlurQuality")))
	{
		MotionBlur->Set(0, ECVF_SetByCode);
	}
	if (IConsoleVariable* DepthOfField =
		IConsoleManager::Get().FindConsoleVariable(TEXT("r.DepthOfFieldQuality")))
	{
		DepthOfField->Set(0, ECVF_SetByCode);
	}

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Dataset generator configured: stage %s, %d episode(s), %d transitions each, %d Hz, %dx%d, worker %d."),
		*GetStageSlug(),
		EpisodeCount,
		TransitionsPerEpisode,
		ObservationRate,
		CaptureWidth,
		CaptureHeight,
		WorkerId);
}

void ACurriculumDataGenerator::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	if (bRunFinished || !bConfigured)
	{
		return;
	}

	if (!ResolvePlayer())
	{
		if (--StartupFramesRemaining <= -100)
		{
			FinishRun(false, TEXT("Timed out waiting for the local player character."));
		}
		return;
	}

	if (StartupFramesRemaining > 0)
	{
		--StartupFramesRemaining;
		return;
	}

	if (!bEpisodeActive)
	{
		if (!BeginEpisode())
		{
			FinishRun(false, LastError);
		}
		return;
	}

	AdvanceGrenades();

	FRecordedState CurrentState;
	const int32 TargetObservationIndex = FrameIndex + 1;
	if (!CaptureObservation(TargetObservationIndex, CurrentState))
	{
		FinishRun(false, LastError);
		return;
	}

	AppendTransition(FrameIndex, CurrentActionMask, PreviousState, CurrentState);
	PreviousState = CurrentState;
	FrameIndex = TargetObservationIndex;
	++GlobalTransitionCount;
	if (CooldownRemainingSteps > 0)
	{
		--CooldownRemainingSteps;
	}

	if (bCoverageMissionSucceeded
		|| bCoverageMissionFailed
		|| FrameIndex >= TransitionsPerEpisode)
	{
		EndEpisode();
		++EpisodeIndex;
		if (EpisodeIndex >= EpisodeCount)
		{
			FinishRun(true);
		}
		return;
	}

	PrepareNextAction();
}

void ACurriculumDataGenerator::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetStageState();
	if (Character)
	{
		Character->SetCurriculumActionOverride(false, 0);
	}
	if (TarWriter && !bTarFinalized)
	{
		delete TarWriter;
		TarWriter = nullptr;
	}
	FApp::SetUseFixedTimeStep(false);
	Super::EndPlay(EndPlayReason);
}

bool ACurriculumDataGenerator::ParseConfiguration()
{
	const TCHAR* CommandLine = FCommandLine::Get();
	FString ConfigPath;
	if (FParse::Value(CommandLine, TEXT("GeneratorConfig="), ConfigPath))
	{
		ConfigPath = FPaths::ConvertRelativePathToFull(ConfigPath);
		FString ConfigText;
		if (!FFileHelper::LoadFileToString(ConfigText, *ConfigPath))
		{
			LastError = FString::Printf(
				TEXT("Could not read generator configuration: %s"),
				*ConfigPath);
			return false;
		}

		TSharedPtr<FJsonObject> Config;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(ConfigText);
		if (!FJsonSerializer::Deserialize(Reader, Config) || !Config.IsValid())
		{
			LastError = FString::Printf(
				TEXT("Generator configuration is not valid JSON: %s"),
				*ConfigPath);
			return false;
		}

		double NumberValue = 0.0;
		FString StageValue;
		if (Config->TryGetStringField(TEXT("stage"), StageValue))
		{
			StageValue.ToLowerInline();
			if (StageValue == TEXT("movement") || StageValue == TEXT("movement_v1"))
			{
				CurriculumStage = ECurriculumStage::Movement;
			}
			else if (StageValue == TEXT("trajectory") || StageValue == TEXT("trajectory_v2"))
			{
				CurriculumStage = ECurriculumStage::Trajectory;
			}
			else if (StageValue == TEXT("throw") || StageValue == TEXT("throw_v3"))
			{
				CurriculumStage = ECurriculumStage::Throw;
			}
			else
			{
				LastError = FString::Printf(TEXT("Unknown curriculum stage: %s"), *StageValue);
				return false;
			}
		}
		if (Config->TryGetNumberField(TEXT("episodes"), NumberValue))
		{
			EpisodeCount = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("episode_seconds"), NumberValue))
		{
			EpisodeSeconds = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("seed_start"), NumberValue))
		{
			SeedStart = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("worker_id"), NumberValue))
		{
			WorkerId = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("observation_rate_hz"), NumberValue))
		{
			ObservationRate = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("rgb_width"), NumberValue))
		{
			CaptureWidth = FMath::RoundToInt(NumberValue);
		}
		if (Config->TryGetNumberField(TEXT("rgb_height"), NumberValue))
		{
			CaptureHeight = FMath::RoundToInt(NumberValue);
		}
		Config->TryGetStringField(TEXT("output"), OutputDirectory);
		Config->TryGetBoolField(TEXT("exit_on_complete"), bExitOnComplete);
		Config->TryGetBoolField(TEXT("coverage_guided"), bCoverageGuided);

		if (!OutputDirectory.IsEmpty() && FPaths::IsRelative(OutputDirectory))
		{
			OutputDirectory = FPaths::Combine(
				FPaths::GetPath(ConfigPath),
				OutputDirectory);
		}
	}

	FParse::Value(CommandLine, TEXT("Episodes="), EpisodeCount);
	FParse::Value(CommandLine, TEXT("EpisodeSeconds="), EpisodeSeconds);
	FParse::Value(CommandLine, TEXT("SeedStart="), SeedStart);
	FParse::Value(CommandLine, TEXT("WorkerId="), WorkerId);
	FParse::Value(CommandLine, TEXT("ObservationRate="), ObservationRate);
	FParse::Value(CommandLine, TEXT("Width="), CaptureWidth);
	FParse::Value(CommandLine, TEXT("Height="), CaptureHeight);
	FString CommandLineStage;
	if (FParse::Value(CommandLine, TEXT("Stage="), CommandLineStage))
	{
		CommandLineStage.ToLowerInline();
		if (CommandLineStage == TEXT("movement") || CommandLineStage == TEXT("movement_v1"))
		{
			CurriculumStage = ECurriculumStage::Movement;
		}
		else if (CommandLineStage == TEXT("trajectory") || CommandLineStage == TEXT("trajectory_v2"))
		{
			CurriculumStage = ECurriculumStage::Trajectory;
		}
		else if (CommandLineStage == TEXT("throw") || CommandLineStage == TEXT("throw_v3"))
		{
			CurriculumStage = ECurriculumStage::Throw;
		}
		else
		{
			LastError = FString::Printf(TEXT("Unknown curriculum stage: %s"), *CommandLineStage);
			return false;
		}
	}
	if (!FParse::Value(CommandLine, TEXT("BuildRevision="), BuildRevision))
	{
		BuildRevision = FApp::GetBuildVersion();
	}

	EpisodeCount = FMath::Clamp(EpisodeCount, 1, 100000);
	EpisodeSeconds = FMath::Clamp(EpisodeSeconds, 1, 3600);
	ObservationRate = FMath::Clamp(ObservationRate, 1, 120);
	CaptureWidth = FMath::Clamp(CaptureWidth, 64, 4096);
	CaptureHeight = FMath::Clamp(CaptureHeight, 64, 4096);
	TransitionsPerEpisode = EpisodeSeconds * ObservationRate;
	if (FParse::Param(CommandLine, TEXT("NoExitOnComplete")))
	{
		bExitOnComplete = false;
	}
	bTrajectoryShowcase =
		FParse::Param(CommandLine, TEXT("TrajectoryShowcase"));
	if (FParse::Param(CommandLine, TEXT("CoverageGuided")))
	{
		bCoverageGuided = true;
	}
	if (FParse::Param(CommandLine, TEXT("NoCoverageGuided")))
	{
		bCoverageGuided = false;
	}

	const bool bHasCommandLineOutput =
		FParse::Value(CommandLine, TEXT("Output="), OutputDirectory);
	if (!bHasCommandLineOutput && OutputDirectory.IsEmpty())
	{
		OutputDirectory = FPaths::Combine(
			FPaths::ProjectSavedDir(),
			TEXT("GeneratedDatasets"),
			FString::Printf(
				TEXT("%s-%s"),
				*GetStageSlug(),
				*FDateTime::UtcNow().ToString(TEXT("%Y%m%d-%H%M%S"))));
	}
	OutputDirectory = FPaths::ConvertRelativePathToFull(OutputDirectory);
	FPaths::NormalizeDirectoryName(OutputDirectory);
	RunStartedUtc = FDateTime::UtcNow().ToIso8601();
	UnrealEngineVersion = FEngineVersion::Current().ToString();
	bConfigured = true;
	return true;
}

bool ACurriculumDataGenerator::OpenOutput()
{
	if (FPaths::FileExists(FPaths::Combine(OutputDirectory, TEXT("dataset.json"))))
	{
		LastError = FString::Printf(
			TEXT("Refusing to overwrite an existing dataset: %s"),
			*OutputDirectory);
		return false;
	}
	if (!IFileManager::Get().MakeDirectory(*OutputDirectory, true))
	{
		LastError = FString::Printf(
			TEXT("Could not create output directory: %s"),
			*OutputDirectory);
		return false;
	}

	ShardPath = FPaths::Combine(
		OutputDirectory,
		FString::Printf(TEXT("shard-w%03d-000000.tar"), WorkerId));
	TarWriter = new FCurriculumTarWriter();
	if (!TarWriter->Open(ShardPath))
	{
		LastError = FString::Printf(TEXT("Could not create shard: %s"), *ShardPath);
		delete TarWriter;
		TarWriter = nullptr;
		return false;
	}
	return true;
}

bool ACurriculumDataGenerator::ResolvePlayer()
{
	if (Character && PlayerCamera)
	{
		return true;
	}

	APlayerController* PlayerController = UGameplayStatics::GetPlayerController(this, 0);
	Character = PlayerController ? Cast<Ahe_grenade_gameCharacter>(PlayerController->GetPawn()) : nullptr;
	PlayerCamera = Character ? Character->GetFirstPersonCameraComponent() : nullptr;
	if (!Character || !PlayerCamera)
	{
		return false;
	}

	AddTickPrerequisiteActor(Character);

	RenderTarget = NewObject<UTextureRenderTarget2D>(this, TEXT("DatasetRenderTarget"));
	RenderTarget->RenderTargetFormat = ETextureRenderTargetFormat::RTF_RGBA8_SRGB;
	RenderTarget->ClearColor = FLinearColor(0.18f, 0.22f, 0.28f, 1.0f);
	RenderTarget->bAutoGenerateMips = false;
	RenderTarget->InitAutoFormat(CaptureWidth, CaptureHeight);
	RenderTarget->UpdateResourceImmediate(true);

	SceneCapture->TextureTarget = RenderTarget;
	SceneCapture->FOVAngle = PlayerCamera->FieldOfView;
	return true;
}

bool ACurriculumDataGenerator::BeginEpisode()
{
	if (!Character || !Character->GetController())
	{
		LastError = TEXT("Cannot start episode without a possessed player character.");
		return false;
	}

	const int32 EpisodeSeed = SeedStart + EpisodeIndex;
	EpisodeRandom.Initialize(EpisodeSeed);
	ResetStageState();
	SelectCoverageMission();
	FVector SpawnLocation;
	float Yaw = 0.0f;
	float Pitch = 0.0f;
	const bool bHasMissionSpawn =
		bCoverageGuided
		&& CoverageMission != ECoverageMission::SemiMarkov
		&& GetCoverageMissionSpawn(SpawnLocation, Yaw, Pitch);
	if (!bHasMissionSpawn && !FindEpisodeSpawn(SpawnLocation))
	{
		LastError = FString::Printf(
			TEXT("Could not find a valid spawn for episode %d."),
			EpisodeIndex);
		return false;
	}

	if (!bHasMissionSpawn)
	{
		const bool bCanonicalSpawn = (EpisodeIndex % 10) == 0;
		Yaw = bCanonicalSpawn ? 0.0f : EpisodeRandom.FRandRange(-180.0f, 180.0f);
		Pitch = bCanonicalSpawn ? 0.0f : EpisodeRandom.FRandRange(-20.0f, 20.0f);
	}
	Character->TeleportTo(SpawnLocation, FRotator(0.0f, Yaw, 0.0f), false, true);
	if (UCharacterMovementComponent* Movement = Character->GetCharacterMovement())
	{
		Movement->StopMovementImmediately();
	}
	Character->GetController()->SetControlRotation(FRotator(Pitch, Yaw, 0.0f));

	FrameIndex = 0;
	HoldStepsRemaining = 0;
	CurrentActionMask = 0;
	HeldActionMask = 0;
	LastAppliedActionMask = 0;
	NextThrowRequestFrame = EpisodeRandom.RandRange(8, 24);
	bCoveragePreviousPositionValid = false;
	CoveragePreviousPosition = SpawnLocation;
	CoverageLastHoopSide = SpawnLocation.X - 700.0f;
	ApplyAction(0);

	if (!CaptureObservation(0, PreviousState))
	{
		return false;
	}

	PrepareNextAction();
	bEpisodeActive = true;

	UE_LOG(
		LogTemp,
		Display,
		TEXT("Dataset episode %d/%d started (seed %d, mission %s, target %s)."),
		EpisodeIndex + 1,
		EpisodeCount,
		EpisodeSeed,
		*GetCoverageMissionSlug(),
		*GetCoverageTargetSlug());
	return true;
}

void ACurriculumDataGenerator::EndEpisode()
{
	ApplyAction(0);
	const bool bMissionRequired =
		CoverageMission != ECoverageMission::SemiMarkov;
	const TCHAR* TerminationReason = bCoverageMissionSucceeded
		? TEXT("mission_success")
		: (bCoverageMissionFailed
			? TEXT("mission_no_progress")
			: (bMissionRequired ? TEXT("mission_timeout") : TEXT("completed")));
	if (bMissionRequired)
	{
		if (bCoverageMissionSucceeded)
		{
			++OverallMissionSuccesses;
		}
		else
		{
			++OverallMissionFailures;
		}
	}
	FString MissionParameters = TEXT("{}");
	if (CoverageMission == ECoverageMission::ObjectOrbit)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"start\":%s,\"orbit_radius_cm\":%s,\"clockwise\":%s,")
			TEXT("\"azimuth_bins_required\":%d,\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			*JsonVector(CoverageMissionStart),
			*JsonNumber(CoverageOrbitRadiusCm),
			JsonBool(bCoverageOrbitClockwise),
			CoverageAzimuthBinCount,
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"start\":%s,\"goal\":%s,\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageMissionGoal),
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"start\":%s,\"goal_a\":%s,\"goal_b\":%s,")
			TEXT("\"required_passages\":%d,\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageMissionGoal),
			*JsonVector(CoverageAlternateGoal),
			CoverageRequiredHoopPasses,
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	EpisodesJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"episode_index\":%d,\"worker_id\":%d,")
		TEXT("\"seed\":%d,\"requested_transitions\":%d,\"actual_transitions\":%d,")
		TEXT("\"observation_count\":%d,\"collection_mission\":\"%s\",")
		TEXT("\"coverage_target\":%s,\"visible_azimuth_bins_mask\":%u,")
		TEXT("\"visible_azimuth_bin_count\":%d,\"ramp_traversals\":%d,")
		TEXT("\"hoop_passes\":%d,\"mission_required\":%s,\"mission_success\":%s,")
		TEXT("\"mission_parameters\":%s,\"termination_reason\":\"%s\"}\n"),
		*MakeEpisodeId(),
		EpisodeIndex,
		WorkerId,
		SeedStart + EpisodeIndex,
		TransitionsPerEpisode,
		FrameIndex,
		FrameIndex + 1,
		*GetCoverageMissionSlug(),
		CoverageTargetIndex != INDEX_NONE
			? *FString::Printf(TEXT("\"%s\""), *GetCoverageTargetSlug())
			: TEXT("null"),
		CurrentEpisodeViewBinsMask,
		FMath::CountBits(static_cast<uint32>(CurrentEpisodeViewBinsMask)),
		CurrentEpisodeRampTraversals,
		CurrentEpisodeHoopPasses,
		JsonBool(bMissionRequired),
		JsonBool(bCoverageMissionSucceeded),
		*MissionParameters,
		TerminationReason);
	bEpisodeActive = false;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Dataset episode %d/%d completed."),
		EpisodeIndex + 1,
		EpisodeCount);
}

void ACurriculumDataGenerator::FinishRun(
	const bool bSuccess,
	const FString& ErrorMessage)
{
	if (bRunFinished)
	{
		return;
	}
	bRunFinished = true;
	ApplyAction(0);

	bool bWriteSuccess = bSuccess && TarWriter;
	if (TarWriter)
	{
		const FString Manifest = FString::Printf(
			TEXT("{\n  \"schema_version\": \"%s\",\n")
			TEXT("  \"curriculum_version\": \"%s\",\n")
			TEXT("  \"complete\": %s,\n  \"worker_id\": %d,\n")
			TEXT("  \"episode_count\": %d,\n  \"transition_count\": %d,\n")
			TEXT("  \"observation_count\": %d,\n  \"image_format\": \"png\",\n")
			TEXT("  \"metadata_format\": \"jsonl\"\n}\n"),
			*GetStageSchemaVersion(),
			*GetStageSlug(),
			JsonBool(bSuccess),
			WorkerId,
			EpisodeIndex + (bEpisodeActive ? 1 : 0),
			GlobalTransitionCount,
			GlobalTransitionCount + EpisodeIndex + (bEpisodeActive ? 1 : 0));

		bWriteSuccess =
			TarWriter->AddTextFile(TEXT("metadata/frames.jsonl"), FramesJsonLines)
			&& TarWriter->AddTextFile(TEXT("metadata/transitions.jsonl"), TransitionsJsonLines)
			&& TarWriter->AddTextFile(TEXT("metadata/episodes.jsonl"), EpisodesJsonLines)
			&& TarWriter->AddTextFile(TEXT("manifest.json"), Manifest)
			&& TarWriter->Finalize()
			&& bWriteSuccess;
		bTarFinalized = true;
		delete TarWriter;
		TarWriter = nullptr;
	}

	const FString EffectiveError =
		bWriteSuccess ? ErrorMessage
		: (ErrorMessage.IsEmpty() ? TEXT("Failed while finalizing dataset output.") : ErrorMessage);
	const FString DatasetJson = BuildDatasetJson(bWriteSuccess, EffectiveError);
	FFileHelper::SaveStringToFile(
		DatasetJson,
		*FPaths::Combine(OutputDirectory, TEXT("dataset.json")),
		FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);

	if (bTarFinalized && FPaths::FileExists(ShardPath))
	{
		const FMD5Hash Hash = FMD5Hash::HashFile(*ShardPath);
		if (Hash.IsValid())
		{
			const FString ChecksumLine = FString::Printf(
				TEXT("%s  %s\n"),
				*LexToString(Hash),
				*FPaths::GetCleanFilename(ShardPath));
			FFileHelper::SaveStringToFile(
				ChecksumLine,
				*FPaths::Combine(OutputDirectory, TEXT("checksums.md5")),
				FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		}
	}

	if (bWriteSuccess)
	{
		UE_LOG(
			LogTemp,
			Display,
			TEXT("Dataset generation completed. Output: %s"),
			*OutputDirectory);
	}
	else
	{
		UE_LOG(
			LogTemp,
			Error,
			TEXT("Dataset generation failed. Output: %s. Error: %s"),
			*OutputDirectory,
			*EffectiveError);
	}

	FApp::SetUseFixedTimeStep(false);
	if (bExitOnComplete)
	{
		FGenericPlatformMisc::RequestExit(false);
	}
}

bool ACurriculumDataGenerator::CaptureObservation(
	const int32 ObservationIndex,
	FRecordedState& OutState)
{
	if (!SceneCapture || !RenderTarget || !PlayerCamera || !Character || !TarWriter)
	{
		LastError = TEXT("Capture resources are not ready.");
		return false;
	}

	SceneCapture->SetWorldTransform(PlayerCamera->GetComponentTransform());
	SceneCapture->FOVAngle = PlayerCamera->FieldOfView;
	SceneCapture->CaptureScene();

	FTextureRenderTargetResource* Resource = RenderTarget->GameThread_GetRenderTargetResource();
	TArray<FColor> Pixels;
	FReadSurfaceDataFlags ReadFlags(RCM_UNorm);
	ReadFlags.SetLinearToGamma(false);
	if (!Resource || !Resource->ReadPixels(Pixels, ReadFlags)
		|| Pixels.Num() != CaptureWidth * CaptureHeight)
	{
		LastError = FString::Printf(
			TEXT("RGB readback failed for episode %d frame %d."),
			EpisodeIndex,
			ObservationIndex);
		return false;
	}

	for (FColor& Pixel : Pixels)
	{
		Pixel.A = 255;
	}

	DrawTrajectoryOverlay(Pixels);

	const int32 CenterX = CaptureWidth / 2;
	const int32 CenterY = CaptureHeight / 2;
	const int32 HalfSize = 4;
	const int32 Thickness = 1;
	FColor CrosshairColor(242, 242, 242, 255);
	FString CrosshairState = TEXT("Neutral");
	if (CurriculumStage == ECurriculumStage::Throw)
	{
		if (CooldownRemainingSteps > 0)
		{
			CrosshairColor = FColor(242, 48, 48, 255);
			CrosshairState = TEXT("Cooldown");
		}
		else
		{
			CrosshairColor = FColor(48, 242, 72, 255);
			CrosshairState = TEXT("Ready");
		}
	}
	for (int32 Offset = -HalfSize; Offset <= HalfSize; ++Offset)
	{
		for (int32 T = -Thickness / 2; T <= Thickness / 2; ++T)
		{
			const int32 HorizontalY = FMath::Clamp(CenterY + T, 0, CaptureHeight - 1);
			const int32 HorizontalX = FMath::Clamp(CenterX + Offset, 0, CaptureWidth - 1);
			const int32 VerticalY = FMath::Clamp(CenterY + Offset, 0, CaptureHeight - 1);
			const int32 VerticalX = FMath::Clamp(CenterX + T, 0, CaptureWidth - 1);
			Pixels[HorizontalY * CaptureWidth + HorizontalX] = CrosshairColor;
			Pixels[VerticalY * CaptureWidth + VerticalX] = CrosshairColor;
		}
	}

	TArray64<uint8> CompressedPng;
	FImageUtils::PNGCompressImageArray(CaptureWidth, CaptureHeight, Pixels, CompressedPng);
	const FString ImageKey = MakeImageKey(ObservationIndex);
	if (CompressedPng.IsEmpty()
		|| !TarWriter->AddFile(ImageKey, CompressedPng.GetData(), CompressedPng.Num()))
	{
		LastError = FString::Printf(TEXT("Could not add image to shard: %s"), *ImageKey);
		return false;
	}

	OutState.Position = Character->GetActorLocation();
	OutState.Velocity = Character->GetVelocity();
	OutState.CameraRotation = Character->GetController()
		? Character->GetController()->GetControlRotation()
		: PlayerCamera->GetComponentRotation();
	OutState.CameraRotation.Roll = 0.0f;
	OutState.bGrounded =
		Character->GetCharacterMovement()
		&& Character->GetCharacterMovement()->IsMovingOnGround();
	OutState.bContact = false;
	OutState.ContactObject.Reset();

	if (UCapsuleComponent* Capsule = Character->GetCapsuleComponent())
	{
		TArray<FOverlapResult> Overlaps;
		FCollisionObjectQueryParams ObjectQuery;
		ObjectQuery.AddObjectTypesToQuery(ECC_WorldStatic);
		FCollisionQueryParams ContactQuery(
			SCENE_QUERY_STAT(CurriculumContact),
			false,
			Character);
		const FCollisionShape ContactShape = FCollisionShape::MakeCapsule(
			Capsule->GetScaledCapsuleRadius() + 3.0f,
			Capsule->GetScaledCapsuleHalfHeight() + 3.0f);
		GetWorld()->OverlapMultiByObjectType(
			Overlaps,
			Character->GetActorLocation(),
			FQuat::Identity,
			ObjectQuery,
			ContactShape,
			ContactQuery);

		for (const FOverlapResult& Overlap : Overlaps)
		{
			const AActor* ContactActor = Overlap.GetActor();
			if (!ContactActor)
			{
				continue;
			}
			for (const FName Tag : ContactActor->Tags)
			{
				const FString TagText = Tag.ToString();
				if (TagText.StartsWith(TEXT("CurriculumWall_"))
					|| TagText.StartsWith(TEXT("CurriculumObject_")))
				{
					OutState.bContact = true;
					OutState.ContactObject = TagText;
					break;
				}
			}
			if (OutState.bContact)
			{
				break;
			}
		}
	}

	const bool bQVisible =
		CurriculumStage != ECurriculumStage::Movement
		&& (CurrentActionMask & CurriculumAction::Q) != 0;
	UpdateCoverageMetrics(OutState);
	const FString GrenadesJson = BuildGrenadesJson();
	FramesJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"frame_index\":%d,\"simulation_step\":%d,")
		TEXT("\"rgb_key\":\"%s\",\"position\":{\"x\":%s,\"y\":%s,\"z\":%s},")
		TEXT("\"velocity\":{\"x\":%s,\"y\":%s,\"z\":%s},")
		TEXT("\"camera\":{\"yaw\":%s,\"pitch\":%s,\"roll\":0.0},")
		TEXT("\"grounded\":%s,\"contact\":%s,\"contact_object\":%s,")
		TEXT("\"crosshair_state\":\"%s\",")
		TEXT("\"cooldown_remaining_steps\":%d,\"q_visibility\":%s,\"grenades\":%s,")
		TEXT("\"collection_mission\":\"%s\",\"coverage_target\":%s,")
		TEXT("\"coverage_target_visible\":%s,\"coverage_view_azimuth_bin\":%s,")
		TEXT("\"coverage_view_distance_band\":%s,\"coverage_waypoint_index\":%d,")
		TEXT("\"ramp_traversals\":%d,\"hoop_passes\":%d,")
		TEXT("\"mission_success\":%s,\"mission_failed\":%s,")
		TEXT("\"no_progress_steps\":%d}\n"),
		*MakeEpisodeId(),
		ObservationIndex,
		ObservationIndex,
		*ImageKey,
		*JsonNumber(OutState.Position.X),
		*JsonNumber(OutState.Position.Y),
		*JsonNumber(OutState.Position.Z),
		*JsonNumber(OutState.Velocity.X),
		*JsonNumber(OutState.Velocity.Y),
		*JsonNumber(OutState.Velocity.Z),
		*JsonNumber(OutState.CameraRotation.Yaw),
		*JsonNumber(OutState.CameraRotation.Pitch),
		JsonBool(OutState.bGrounded),
		JsonBool(OutState.bContact),
		OutState.bContact
			? *FString::Printf(TEXT("\"%s\""), *OutState.ContactObject)
			: TEXT("null"),
		*CrosshairState,
		CooldownRemainingSteps,
		JsonBool(bQVisible),
		*GrenadesJson,
		*GetCoverageMissionSlug(),
		CoverageTargetIndex != INDEX_NONE
			? *FString::Printf(TEXT("\"%s\""), *GetCoverageTargetSlug())
			: TEXT("null"),
		JsonBool(bCurrentCoverageTargetVisible),
		CurrentCoverageViewBin != INDEX_NONE
			? *FString::FromInt(CurrentCoverageViewBin)
			: TEXT("null"),
		CurrentCoverageDistanceBand != INDEX_NONE
			? *FString::FromInt(CurrentCoverageDistanceBand)
			: TEXT("null"),
		CoverageWaypointIndex,
		CurrentEpisodeRampTraversals,
		CurrentEpisodeHoopPasses,
		JsonBool(bCoverageMissionSucceeded),
		JsonBool(bCoverageMissionFailed),
		CoverageNoProgressSteps);
	return true;
}

void ACurriculumDataGenerator::AppendTransition(
	const int32 SourceFrameIndex,
	const uint16 ActionMask,
	const FRecordedState& SourceState,
	const FRecordedState& TargetState)
{
	const float ForwardAxis = CurriculumAction::ForwardAxis(ActionMask);
	const float RightAxis = CurriculumAction::RightAxis(ActionMask);
	const float PitchAxis = CurriculumAction::PitchAxis(ActionMask);
	const float YawAxis = CurriculumAction::YawAxis(ActionMask);

	TransitionsJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"source_frame_index\":%d,\"action_mask\":%u,")
		TEXT("\"w\":%s,\"a\":%s,\"s\":%s,\"d\":%s,")
		TEXT("\"arrow_up\":%s,\"arrow_down\":%s,\"arrow_left\":%s,\"arrow_right\":%s,")
		TEXT("\"q\":%s,\"e\":%s,\"forward_axis\":%s,\"right_axis\":%s,")
		TEXT("\"pitch_axis\":%s,\"yaw_axis\":%s,\"e_request_edge\":%s,")
		TEXT("\"e_accepted\":%s,\"cooldown_remaining_steps\":%d,")
		TEXT("\"observation_valid\":true}\n"),
		*MakeEpisodeId(),
		SourceFrameIndex,
		ActionMask,
		JsonBool((ActionMask & CurriculumAction::W) != 0),
		JsonBool((ActionMask & CurriculumAction::A) != 0),
		JsonBool((ActionMask & CurriculumAction::S) != 0),
		JsonBool((ActionMask & CurriculumAction::D) != 0),
		JsonBool((ActionMask & CurriculumAction::ArrowUp) != 0),
		JsonBool((ActionMask & CurriculumAction::ArrowDown) != 0),
		JsonBool((ActionMask & CurriculumAction::ArrowLeft) != 0),
		JsonBool((ActionMask & CurriculumAction::ArrowRight) != 0),
		JsonBool((ActionMask & CurriculumAction::Q) != 0),
		JsonBool((ActionMask & CurriculumAction::E) != 0),
		*JsonNumber(ForwardAxis),
		*JsonNumber(RightAxis),
		*JsonNumber(PitchAxis),
		*JsonNumber(YawAxis),
		JsonBool(bCurrentERequestEdge),
		JsonBool(bCurrentEAccepted),
		CooldownRemainingSteps);
}

uint16 ACurriculumDataGenerator::SelectAction()
{
	uint16 ActionMask = SelectBaseAction();
	if (CurriculumStage != ECurriculumStage::Movement
		&& EpisodeRandom.FRand() < 0.45f)
	{
		ActionMask |= CurriculumAction::Q;
	}
	return ActionMask;
}

uint16 ACurriculumDataGenerator::SelectBaseAction()
{
	const float Roll = EpisodeRandom.FRandRange(0.0f, 100.0f);
	if (Roll < 8.0f)
	{
		return 0;
	}
	if (Roll < 38.0f)
	{
		return SelectMovementBits(false);
	}
	if (Roll < 56.0f)
	{
		return SelectCameraBits();
	}
	if (Roll < 90.0f)
	{
		return SelectMovementBits(false) | SelectCameraBits();
	}
	if (Roll < 95.0f)
	{
		const int32 Pair = EpisodeRandom.RandRange(0, 3);
		switch (Pair)
		{
		case 0: return CurriculumAction::W | CurriculumAction::S;
		case 1: return CurriculumAction::A | CurriculumAction::D;
		case 2: return CurriculumAction::ArrowUp | CurriculumAction::ArrowDown;
		default: return CurriculumAction::ArrowLeft | CurriculumAction::ArrowRight;
		}
	}
	return SelectMovementBits(true);
}

int32 ACurriculumDataGenerator::SelectHoldSteps()
{
	const float Roll = EpisodeRandom.FRandRange(0.0f, 100.0f);
	if (Roll < 25.0f)
	{
		return EpisodeRandom.RandRange(1, 3);
	}
	if (Roll < 65.0f)
	{
		return EpisodeRandom.RandRange(4, 12);
	}
	if (Roll < 90.0f)
	{
		return EpisodeRandom.RandRange(13, 40);
	}
	return EpisodeRandom.RandRange(41, 100);
}

uint16 ACurriculumDataGenerator::SelectMovementBits(const bool bTowardWall)
{
	if (bTowardWall && Character && Character->GetController())
	{
		const FVector Location = Character->GetActorLocation();
		const float DistanceX = 1600.0f - FMath::Abs(Location.X);
		const float DistanceY = 1600.0f - FMath::Abs(Location.Y);
		FVector DesiredWorldDirection;
		if (DistanceX < DistanceY)
		{
			DesiredWorldDirection = FVector(Location.X >= 0.0f ? 1.0f : -1.0f, 0.0f, 0.0f);
		}
		else
		{
			DesiredWorldDirection = FVector(0.0f, Location.Y >= 0.0f ? 1.0f : -1.0f, 0.0f);
		}

		const FRotator YawRotation(
			0.0f,
			Character->GetController()->GetControlRotation().Yaw,
			0.0f);
		const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		uint16 Mask = 0;
		const float ForwardDot = FVector::DotProduct(DesiredWorldDirection, Forward);
		const float RightDot = FVector::DotProduct(DesiredWorldDirection, Right);
		if (FMath::Abs(ForwardDot) >= 0.35f)
		{
			Mask |= ForwardDot >= 0.0f ? CurriculumAction::W : CurriculumAction::S;
		}
		if (FMath::Abs(RightDot) >= 0.35f)
		{
			Mask |= RightDot >= 0.0f ? CurriculumAction::D : CurriculumAction::A;
		}
		return Mask == 0 ? CurriculumAction::W : Mask;
	}

	switch (EpisodeRandom.RandRange(0, 7))
	{
	case 0: return CurriculumAction::W;
	case 1: return CurriculumAction::S;
	case 2: return CurriculumAction::A;
	case 3: return CurriculumAction::D;
	case 4: return CurriculumAction::W | CurriculumAction::A;
	case 5: return CurriculumAction::W | CurriculumAction::D;
	case 6: return CurriculumAction::S | CurriculumAction::A;
	default: return CurriculumAction::S | CurriculumAction::D;
	}
}

uint16 ACurriculumDataGenerator::SelectCameraBits()
{
	switch (EpisodeRandom.RandRange(0, 5))
	{
	case 0: return CurriculumAction::ArrowLeft;
	case 1: return CurriculumAction::ArrowRight;
	case 2: return CurriculumAction::ArrowUp;
	case 3: return CurriculumAction::ArrowDown;
	case 4: return CurriculumAction::ArrowLeft
		| (EpisodeRandom.RandBool() ? CurriculumAction::ArrowUp : CurriculumAction::ArrowDown);
	default: return CurriculumAction::ArrowRight
		| (EpisodeRandom.RandBool() ? CurriculumAction::ArrowUp : CurriculumAction::ArrowDown);
	}
}

uint16 ACurriculumDataGenerator::SelectTrajectoryShowcaseAction() const
{
	const int32 SafeObservationRate = FMath::Max(1, ObservationRate);
	const int32 CycleLengthFrames = SafeObservationRate * 4;
	const int32 CycleFrame = FrameIndex % CycleLengthFrames;
	const int32 QuarterSecondFrames = FMath::Max(1, SafeObservationRate / 4);
	uint16 ActionMask = CurriculumAction::Q;

	// Use ordinary held camera inputs so the view advances on every observation.
	// Each four-second cycle returns to its starting yaw and pitch.
	if (CycleFrame < SafeObservationRate)
	{
		ActionMask |= CurriculumAction::ArrowRight;
	}
	else if (CycleFrame < SafeObservationRate * 2)
	{
		ActionMask |= CurriculumAction::ArrowLeft;
	}
	else if (CycleFrame < (SafeObservationRate * 2) + QuarterSecondFrames)
	{
		ActionMask |= CurriculumAction::ArrowUp;
	}
	else if (CycleFrame < SafeObservationRate * 3)
	{
		ActionMask |= CurriculumAction::ArrowRight;
	}
	else if (CycleFrame < (SafeObservationRate * 3) + QuarterSecondFrames)
	{
		ActionMask |= CurriculumAction::ArrowDown;
	}
	else
	{
		ActionMask |= CurriculumAction::ArrowLeft;
	}

	return ActionMask;
}

void ACurriculumDataGenerator::SelectCoverageMission()
{
	CoverageMission = ECoverageMission::SemiMarkov;
	CoverageTargetIndex = INDEX_NONE;
	CoverageWaypointIndex = 0;
	CurrentCoverageViewBin = INDEX_NONE;
	CurrentCoverageDistanceBand = INDEX_NONE;
	CurrentEpisodeViewBinsMask = 0;
	CurrentEpisodeRampTraversals = 0;
	CurrentEpisodeHoopPasses = 0;
	CoverageRequiredHoopPasses = 1;
	CoverageNoProgressSteps = 0;
	CoverageWaypoints.Reset();
	CoverageMissionStart = FVector::ZeroVector;
	CoverageMissionGoal = FVector::ZeroVector;
	CoverageAlternateGoal = FVector::ZeroVector;
	CoverageOrbitRadiusCm = 0.0f;
	CoverageInitialYawOffsetDegrees = 0.0f;
	CoverageInitialPitchOffsetDegrees = 0.0f;
	bCurrentCoverageTargetVisible = false;
	bCoverageOrbitClockwise = false;
	bCoverageMissionSucceeded = false;
	bCoverageMissionFailed = false;
	bRampMounted = false;

	if (!bCoverageGuided || bTrajectoryShowcase)
	{
		return;
	}

	// Ten-episode repeating schedule:
	// 0 canonical reference, 1-5 one full object-view mission each,
	// 6 ramp traversal, 7 hoop passage, and 8-9 general action diversity.
	const int32 ScheduleSlot = EpisodeIndex % 10;
	if (ScheduleSlot >= 1 && ScheduleSlot <= 5)
	{
		CoverageMission = ECoverageMission::ObjectOrbit;
		CoverageTargetIndex = ScheduleSlot - 1;
		const FCoverageTargetDefinition& Target =
			GetCoverageTargetDefinition(CoverageTargetIndex);
		CoverageOrbitStartAngleDegrees = EpisodeRandom.FRandRange(-180.0f, 180.0f);
		CoverageOrbitRadiusCm = FMath::Clamp(
			Target.OrbitRadiusCm + EpisodeRandom.FRandRange(-70.0f, 70.0f),
			470.0f,
			680.0f);
		bCoverageOrbitClockwise = EpisodeRandom.RandBool();
		const float DirectionSign = bCoverageOrbitClockwise ? -1.0f : 1.0f;
		for (int32 BinIndex = 0; BinIndex < CoverageAzimuthBinCount; ++BinIndex)
		{
			const float AngleDegrees =
				CoverageOrbitStartAngleDegrees
				+ (DirectionSign * 360.0f
					* static_cast<float>(BinIndex)
					/ static_cast<float>(CoverageAzimuthBinCount))
				+ EpisodeRandom.FRandRange(-7.0f, 7.0f);
			const float Radius =
				CoverageOrbitRadiusCm + EpisodeRandom.FRandRange(-45.0f, 45.0f);
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			CoverageWaypoints.Add(FVector(
				Target.LookTarget.X + (FMath::Cos(AngleRadians) * Radius),
				Target.LookTarget.Y + (FMath::Sin(AngleRadians) * Radius),
				100.0f));
		}
		const float ApproachAngle = FMath::DegreesToRadians(
			CoverageOrbitStartAngleDegrees + EpisodeRandom.FRandRange(-12.0f, 12.0f));
		const float ApproachRadius =
			CoverageOrbitRadiusCm + EpisodeRandom.FRandRange(80.0f, 140.0f);
		CoverageMissionStart = FVector(
			FMath::Clamp(
				Target.LookTarget.X + (FMath::Cos(ApproachAngle) * ApproachRadius),
				-1450.0f,
				1450.0f),
			FMath::Clamp(
				Target.LookTarget.Y + (FMath::Sin(ApproachAngle) * ApproachRadius),
				-1450.0f,
				1450.0f),
			100.0f);
	}
	else if (ScheduleSlot == 6)
	{
		CoverageMission = ECoverageMission::RampTraverse;
		CoverageMissionStart = FVector(
			EpisodeRandom.FRandRange(540.0f, 730.0f),
			EpisodeRandom.FRandRange(-55.0f, 55.0f),
			100.0f);
		CoverageMissionGoal = FVector(
			EpisodeRandom.FRandRange(-730.0f, -540.0f),
			EpisodeRandom.FRandRange(-55.0f, 55.0f),
			100.0f);
	}
	else if (ScheduleSlot == 7)
	{
		CoverageMission = ECoverageMission::HoopPass;
		const float StartSide = EpisodeRandom.RandBool() ? 1.0f : -1.0f;
		const float StartDistance = EpisodeRandom.FRandRange(300.0f, 440.0f);
		const float GoalDistance = EpisodeRandom.FRandRange(300.0f, 440.0f);
		CoverageMissionStart = FVector(
			700.0f + (StartSide * StartDistance),
			-700.0f + EpisodeRandom.FRandRange(-32.0f, 32.0f),
			100.0f);
		CoverageMissionGoal = FVector(
			700.0f - (StartSide * GoalDistance),
			-700.0f + EpisodeRandom.FRandRange(-32.0f, 32.0f),
			100.0f);
		CoverageAlternateGoal = CoverageMissionStart;
		CoverageRequiredHoopPasses = EpisodeRandom.RandRange(1, 3);
	}

	CoverageInitialYawOffsetDegrees = EpisodeRandom.FRandRange(-10.0f, 10.0f);
	CoverageInitialPitchOffsetDegrees = EpisodeRandom.FRandRange(-5.0f, 5.0f);
}

bool ACurriculumDataGenerator::GetCoverageMissionSpawn(
	FVector& OutLocation,
	float& OutYaw,
	float& OutPitch) const
{
	FVector LookTarget = FVector::ZeroVector;
	if (CoverageMission == ECoverageMission::ObjectOrbit
		&& CoverageTargetIndex != INDEX_NONE)
	{
		const FCoverageTargetDefinition& Target =
			GetCoverageTargetDefinition(CoverageTargetIndex);
		OutLocation = CoverageMissionStart;
		LookTarget = Target.LookTarget;
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		OutLocation = CoverageMissionStart;
		LookTarget = CoverageMissionGoal;
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		// The hoop's opening lies in the YZ plane, so this path crosses it on X.
		OutLocation = CoverageMissionStart;
		LookTarget = CoverageMissionGoal;
	}
	else
	{
		return false;
	}

	const FVector ApproximateEyeLocation = OutLocation + FVector(0.0f, 0.0f, 64.0f);
	const FRotator LookRotation = (LookTarget - ApproximateEyeLocation).Rotation();
	OutYaw = LookRotation.Yaw + CoverageInitialYawOffsetDegrees;
	OutPitch = FMath::Clamp(
		LookRotation.Pitch + CoverageInitialPitchOffsetDegrees,
		-20.0f,
		20.0f);
	return true;
}

uint16 ACurriculumDataGenerator::WorldDirectionToMovementBits(
	const FVector& DesiredWorldDirection) const
{
	if (!Character || !Character->GetController())
	{
		return 0;
	}

	const FVector Direction = DesiredWorldDirection.GetSafeNormal2D();
	if (Direction.IsNearlyZero())
	{
		return 0;
	}

	const FRotator YawRotation(
		0.0f,
		Character->GetController()->GetControlRotation().Yaw,
		0.0f);
	const FVector Forward = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	const FVector Right = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
	const float ForwardDot = FVector::DotProduct(Direction, Forward);
	const float RightDot = FVector::DotProduct(Direction, Right);

	uint16 Mask = 0;
	if (FMath::Abs(ForwardDot) >= 0.30f)
	{
		Mask |= ForwardDot >= 0.0f ? CurriculumAction::W : CurriculumAction::S;
	}
	if (FMath::Abs(RightDot) >= 0.30f)
	{
		Mask |= RightDot >= 0.0f ? CurriculumAction::D : CurriculumAction::A;
	}
	return Mask;
}

uint16 ACurriculumDataGenerator::CameraBitsToward(
	const FVector& WorldTarget,
	float* OutYawError) const
{
	if (!Character || !Character->GetController() || !PlayerCamera)
	{
		if (OutYawError)
		{
			*OutYawError = 0.0f;
		}
		return 0;
	}

	const FRotator CurrentRotation = Character->GetController()->GetControlRotation();
	const FRotator DesiredRotation =
		(WorldTarget - PlayerCamera->GetComponentLocation()).Rotation();
	const float YawError =
		FMath::FindDeltaAngleDegrees(CurrentRotation.Yaw, DesiredRotation.Yaw);
	const float PitchError =
		FMath::FindDeltaAngleDegrees(CurrentRotation.Pitch, DesiredRotation.Pitch);
	if (OutYawError)
	{
		*OutYawError = YawError;
	}

	uint16 Mask = 0;
	const float YawDeadZone =
		45.0f / static_cast<float>(FMath::Max(1, ObservationRate));
	const float PitchDeadZone =
		37.5f / static_cast<float>(FMath::Max(1, ObservationRate));
	if (YawError > YawDeadZone)
	{
		Mask |= CurriculumAction::ArrowRight;
	}
	else if (YawError < -YawDeadZone)
	{
		Mask |= CurriculumAction::ArrowLeft;
	}
	if (PitchError > PitchDeadZone)
	{
		Mask |= CurriculumAction::ArrowUp;
	}
	else if (PitchError < -PitchDeadZone)
	{
		Mask |= CurriculumAction::ArrowDown;
	}
	return Mask;
}

uint16 ACurriculumDataGenerator::SelectCoverageGuidedAction()
{
	if (!Character)
	{
		return 0;
	}

	uint16 ActionMask = 0;
	if (CoverageMission == ECoverageMission::ObjectOrbit
		&& CoverageTargetIndex != INDEX_NONE
		&& CoverageWaypoints.Num() == CoverageAzimuthBinCount)
	{
		const FCoverageTargetDefinition& Target =
			GetCoverageTargetDefinition(CoverageTargetIndex);
		FVector Waypoint = CoverageWaypoints[CoverageWaypointIndex];
		Waypoint.Z = Character->GetActorLocation().Z;
		if (FVector::Dist2D(Character->GetActorLocation(), Waypoint) < 115.0f)
		{
			CoverageWaypointIndex =
				(CoverageWaypointIndex + 1) % CoverageAzimuthBinCount;
			Waypoint = CoverageWaypoints[CoverageWaypointIndex];
			Waypoint.Z = Character->GetActorLocation().Z;
		}

		float YawError = 0.0f;
		ActionMask |= CameraBitsToward(Target.LookTarget, &YawError);
		// Pause translation when the target leaves the central view so ordinary
		// arrow input can catch up. This preserves valid dynamics and clear views.
		if (FMath::Abs(YawError) < 24.0f)
		{
			ActionMask |= WorldDirectionToMovementBits(
				Waypoint - Character->GetActorLocation());
		}
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		ActionMask |= CameraBitsToward(CoverageMissionGoal);
		ActionMask |= WorldDirectionToMovementBits(
			CoverageMissionGoal - Character->GetActorLocation());
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		const FVector Target =
			(CoverageWaypointIndex & 1) == 0
				? CoverageMissionGoal
				: CoverageAlternateGoal;
		if (FVector::Dist2D(Character->GetActorLocation(), Target) < 90.0f)
		{
			++CoverageWaypointIndex;
		}
		ActionMask |= CameraBitsToward(Target);
		ActionMask |= WorldDirectionToMovementBits(
			Target - Character->GetActorLocation());
	}

	if (CurriculumStage != ECurriculumStage::Movement)
	{
		const int32 VisibilityBlockSteps = FMath::Max(1, ObservationRate / 2);
		if (((FrameIndex / VisibilityBlockSteps) & 1) == 0)
		{
			ActionMask |= CurriculumAction::Q;
		}
	}
	return ActionMask;
}

bool ACurriculumDataGenerator::IsCoverageTargetVisible(const int32 TargetIndex) const
{
	if (!PlayerCamera || !GetWorld() || TargetIndex == INDEX_NONE)
	{
		return false;
	}

	const FCoverageTargetDefinition& Target =
		GetCoverageTargetDefinition(TargetIndex);
	const FVector CameraLocation = PlayerCamera->GetComponentLocation();
	const FRotator CameraRotation = PlayerCamera->GetComponentRotation();
	const FRotator DesiredRotation = (Target.LookTarget - CameraLocation).Rotation();
	if (FMath::Abs(FMath::FindDeltaAngleDegrees(CameraRotation.Yaw, DesiredRotation.Yaw)) > 42.0f
		|| FMath::Abs(FMath::FindDeltaAngleDegrees(CameraRotation.Pitch, DesiredRotation.Pitch)) > 38.0f)
	{
		return false;
	}

	FHitResult Hit;
	FCollisionQueryParams QueryParams(
		SCENE_QUERY_STAT(CurriculumCoverageVisibility),
		true,
		Character);
	const bool bHit = GetWorld()->LineTraceSingleByChannel(
		Hit,
		CameraLocation,
		Target.LookTarget,
		ECC_Visibility,
		QueryParams);
	return !bHit
		|| (Hit.GetActor() && Hit.GetActor()->ActorHasTag(Target.ActorTag));
}

void ACurriculumDataGenerator::UpdateCoverageMetrics(const FRecordedState& State)
{
	bCurrentCoverageTargetVisible = false;
	CurrentCoverageViewBin = INDEX_NONE;
	CurrentCoverageDistanceBand = INDEX_NONE;

	if (CoverageMission == ECoverageMission::ObjectOrbit
		&& CoverageTargetIndex != INDEX_NONE)
	{
		const FCoverageTargetDefinition& Target =
			GetCoverageTargetDefinition(CoverageTargetIndex);
		const FVector RelativePosition = State.Position - Target.LookTarget;
		const float Distance = RelativePosition.Size2D();
		const float Angle = FMath::Atan2(RelativePosition.Y, RelativePosition.X);
		const float NormalizedAngle =
			FMath::Fmod(Angle + (2.0f * PI), 2.0f * PI);
		CurrentCoverageViewBin = FMath::Clamp(
			FMath::FloorToInt(
				NormalizedAngle
				* static_cast<float>(CoverageAzimuthBinCount)
				/ (2.0f * PI)),
			0,
			CoverageAzimuthBinCount - 1);
		CurrentCoverageDistanceBand =
			Distance < 400.0f ? 0 : (Distance < 750.0f ? 1 : 2);
		bCurrentCoverageTargetVisible =
			IsCoverageTargetVisible(CoverageTargetIndex);
		if (bCurrentCoverageTargetVisible)
		{
			const uint16 BinBit =
				static_cast<uint16>(1u << CurrentCoverageViewBin);
			CurrentEpisodeViewBinsMask |= BinBit;
			OverallObjectViewBins[CoverageTargetIndex] |= BinBit;
		}
		bCoverageMissionSucceeded =
			CurrentEpisodeViewBinsMask
			== static_cast<uint16>((1u << CoverageAzimuthBinCount) - 1u);
	}

	if (CoverageMission == ECoverageMission::RampTraverse)
	{
		// Floor actor Z is about 96 cm. A substantially higher capsule center
		// proves that the character mounted the inclined collision surface.
		bRampMounted = bRampMounted || State.Position.Z > 145.0f;
		if (bRampMounted
			&& State.Position.X < -340.0f
			&& FMath::Abs(State.Position.Y) < 115.0f)
		{
			++CurrentEpisodeRampTraversals;
			++OverallRampTraversals;
			bRampMounted = false;
			bCoverageMissionSucceeded = true;
		}
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		const float CurrentSide = State.Position.X - 700.0f;
		if (bCoveragePreviousPositionValid
			&& FMath::Sign(CoverageLastHoopSide) != FMath::Sign(CurrentSide)
			&& FMath::Abs(State.Position.Y + 700.0f) < 90.0f
			&& State.Position.Z >= 80.0f
			&& State.Position.Z <= 145.0f)
		{
			++CurrentEpisodeHoopPasses;
			++OverallHoopPasses;
			bCoverageMissionSucceeded =
				CurrentEpisodeHoopPasses >= CoverageRequiredHoopPasses;
		}
		CoverageLastHoopSide = CurrentSide;
	}

	if (CoverageMission != ECoverageMission::SemiMarkov
		&& !bCoverageMissionSucceeded
		&& bCoveragePreviousPositionValid)
	{
		const bool bMovementCommanded =
			(CurrentActionMask
				& (CurriculumAction::W
					| CurriculumAction::A
					| CurriculumAction::S
					| CurriculumAction::D)) != 0;
		const float Displacement =
			FVector::Dist2D(CoveragePreviousPosition, State.Position);
		CoverageNoProgressSteps =
			bMovementCommanded && Displacement < 1.0f
				? CoverageNoProgressSteps + 1
				: 0;
		if (CoverageNoProgressSteps >= FMath::Max(1, ObservationRate))
		{
			bCoverageMissionFailed = true;
		}
	}

	CoveragePreviousPosition = State.Position;
	bCoveragePreviousPositionValid = true;
}

FString ACurriculumDataGenerator::GetCoverageMissionSlug() const
{
	switch (CoverageMission)
	{
	case ECoverageMission::ObjectOrbit:
		return TEXT("object_orbit");
	case ECoverageMission::RampTraverse:
		return TEXT("ramp_traverse");
	case ECoverageMission::HoopPass:
		return TEXT("hoop_pass");
	default:
		return TEXT("semi_markov");
	}
}

FString ACurriculumDataGenerator::GetCoverageTargetSlug() const
{
	return CoverageTargetIndex == INDEX_NONE
		? TEXT("")
		: GetCoverageTargetDefinition(CoverageTargetIndex).Slug;
}

bool ACurriculumDataGenerator::FindEpisodeSpawn(FVector& OutLocation)
{
	if ((EpisodeIndex % 10) == 0)
	{
		OutLocation = FVector(-1250.0f, 0.0f, 100.0f);
		return true;
	}

	UWorld* World = GetWorld();
	UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
	if (!World || !Capsule)
	{
		return false;
	}

	const FCollisionShape Shape = FCollisionShape::MakeCapsule(
		Capsule->GetScaledCapsuleRadius(),
		Capsule->GetScaledCapsuleHalfHeight());
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(CurriculumEpisodeSpawn), false, Character);
	for (int32 Attempt = 0; Attempt < 100; ++Attempt)
	{
		const FVector Candidate(
			EpisodeRandom.FRandRange(-1300.0f, 1300.0f),
			EpisodeRandom.FRandRange(-1300.0f, 1300.0f),
			100.0f);
		const bool bBlocked = World->OverlapBlockingTestByChannel(
			Candidate,
			FQuat::Identity,
			ECC_Pawn,
			Shape,
			QueryParams);
		if (!bBlocked)
		{
			OutLocation = Candidate;
			return true;
		}
	}
	return false;
}

void ACurriculumDataGenerator::ApplyAction(const uint16 ActionMask)
{
	CurrentActionMask = ActionMask & CurriculumAction::CanonicalMask;
	if (CurriculumStage == ECurriculumStage::Movement)
	{
		CurrentActionMask &= ~(CurriculumAction::Q | CurriculumAction::E);
	}
	else if (CurriculumStage == ECurriculumStage::Trajectory)
	{
		CurrentActionMask &= ~CurriculumAction::E;
	}

	bCurrentERequestEdge =
		CurriculumStage == ECurriculumStage::Throw
		&& (CurrentActionMask & CurriculumAction::E) != 0
		&& (LastAppliedActionMask & CurriculumAction::E) == 0;
	bCurrentEAccepted = bCurrentERequestEdge && AcceptThrow();
	LastAppliedActionMask = CurrentActionMask;

	if (Character)
	{
		Character->SetCurriculumActionOverride(true, CurrentActionMask);
	}
}

void ACurriculumDataGenerator::PrepareNextAction()
{
	if (bTrajectoryShowcase && CurriculumStage == ECurriculumStage::Trajectory)
	{
		ApplyAction(SelectTrajectoryShowcaseAction());
		return;
	}

	if (bCoverageGuided && CoverageMission != ECoverageMission::SemiMarkov)
	{
		uint16 NextActionMask = SelectCoverageGuidedAction();
		if (CurriculumStage == ECurriculumStage::Throw
			&& FrameIndex >= NextThrowRequestFrame)
		{
			NextActionMask |= CurriculumAction::E;
			NextThrowRequestFrame = FrameIndex + EpisodeRandom.RandRange(30, 70);
		}
		ApplyAction(NextActionMask);
		return;
	}

	if (HoldStepsRemaining <= 0)
	{
		HeldActionMask = SelectAction();
		HoldStepsRemaining = SelectHoldSteps();
	}

	uint16 NextActionMask = HeldActionMask & ~CurriculumAction::E;
	if (CurriculumStage == ECurriculumStage::Throw
		&& FrameIndex >= NextThrowRequestFrame)
	{
		NextActionMask |= CurriculumAction::E;
		NextThrowRequestFrame = FrameIndex + EpisodeRandom.RandRange(30, 70);
	}

	ApplyAction(NextActionMask);
	--HoldStepsRemaining;
}

void ACurriculumDataGenerator::ResetStageState()
{
	for (FGeneratedGrenade& Grenade : Grenades)
	{
		if (AStaticMeshActor* VisualActor = Grenade.VisualActor.Get())
		{
			VisualActor->Destroy();
		}
	}
	Grenades.Reset();
	CooldownRemainingSteps = 0;
	NextGrenadeId = 0;
	bCurrentERequestEdge = false;
	bCurrentEAccepted = false;
	LastAppliedActionMask = 0;
}

bool ACurriculumDataGenerator::BuildLaunchState(
	FVector& OutSpawnLocation,
	FVector& OutVelocity) const
{
	if (!PlayerCamera)
	{
		return false;
	}

	const FTransform CameraTransform = PlayerCamera->GetComponentTransform();
	const FVector Forward = CameraTransform.GetUnitAxis(EAxis::X);
	const FVector Right = CameraTransform.GetUnitAxis(EAxis::Y);
	const FVector Up = CameraTransform.GetUnitAxis(EAxis::Z);
	OutSpawnLocation =
		CameraTransform.GetLocation()
		+ (Forward * 30.0f)
		+ (Right * 10.0f)
		- (Up * 10.0f);
	OutVelocity = Forward * CurriculumThrowSpeedCmPerSecond;
	return true;
}

bool ACurriculumDataGenerator::AcceptThrow()
{
	if (CurriculumStage != ECurriculumStage::Throw
		|| CooldownRemainingSteps > 0
		|| !GetWorld())
	{
		return false;
	}

	FVector SpawnLocation;
	FVector InitialVelocity;
	if (!BuildLaunchState(SpawnLocation, InitialVelocity))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride =
		ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AStaticMeshActor* VisualActor = GetWorld()->SpawnActor<AStaticMeshActor>(
		SpawnLocation,
		FRotator::ZeroRotator,
		SpawnParams);
	if (!VisualActor)
	{
		return false;
	}

	VisualActor->SetActorEnableCollision(false);
	VisualActor->SetActorScale3D(
		FVector(FMath::Max(1.0f, GrenadeSimConfig.RadiusCm) / 50.0f));
	if (UStaticMeshComponent* MeshComponent = VisualActor->GetStaticMeshComponent())
	{
		MeshComponent->SetMobility(EComponentMobility::Movable);
		MeshComponent->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComponent->SetStaticMesh(
			LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Sphere.Sphere")));
		if (const Ahe_grenade_gameGameMode* GameMode =
			GetWorld()->GetAuthGameMode<Ahe_grenade_gameGameMode>())
		{
			MeshComponent->SetMaterial(0, GameMode->GrenadeMaterial);
		}
	}

	FGeneratedGrenade& Grenade = Grenades.AddDefaulted_GetRef();
	Grenade.Id = NextGrenadeId++;
	Grenade.VisualActor = VisualActor;
	FGrenadeSim::InitializeState(
		Grenade.State,
		SpawnLocation,
		InitialVelocity,
		3600.0f);
	CooldownRemainingSteps = FMath::Max(
		1,
		FMath::RoundToInt(2.0f * static_cast<float>(ObservationRate)));
	return true;
}

void ACurriculumDataGenerator::AdvanceGrenades()
{
	if (CurriculumStage != ECurriculumStage::Throw || Grenades.IsEmpty())
	{
		return;
	}

	const float FixedStep = FMath::Max(0.001f, GrenadeSimConfig.FixedStepSeconds);
	const float ObservationStep = 1.0f / static_cast<float>(FMath::Max(1, ObservationRate));
	const int32 SubstepCount =
		FMath::Max(1, FMath::RoundToInt(ObservationStep / FixedStep));
	const float ExactSubstep = ObservationStep / static_cast<float>(SubstepCount);
	for (FGeneratedGrenade& Grenade : Grenades)
	{
		if (!Grenade.State.bMotionStopped)
		{
			for (int32 Substep = 0; Substep < SubstepCount; ++Substep)
			{
				FGrenadeSim::Step(
					GetWorld(),
					GrenadeSimConfig,
					Grenade.State,
					ExactSubstep,
					Character,
					[](const FHitResult&) { return static_cast<ABreakableTile*>(nullptr); });
				if (Grenade.State.bMotionStopped)
				{
					break;
				}
			}
		}
		if (AStaticMeshActor* VisualActor = Grenade.VisualActor.Get())
		{
			VisualActor->SetActorLocation(Grenade.State.Position);
		}
	}
}

void ACurriculumDataGenerator::DrawTrajectoryOverlay(TArray<FColor>& Pixels) const
{
	if (CurriculumStage == ECurriculumStage::Movement
		|| (CurrentActionMask & CurriculumAction::Q) == 0
		|| Pixels.Num() != CaptureWidth * CaptureHeight
		|| !GetWorld()
		|| !PlayerCamera)
	{
		return;
	}

	FVector SpawnLocation;
	FVector InitialVelocity;
	if (!BuildLaunchState(SpawnLocation, InitialVelocity))
	{
		return;
	}

	FGrenadeSimState PreviewState;
	FGrenadeSim::InitializeState(
		PreviewState,
		SpawnLocation,
		InitialVelocity,
		3600.0f);

	const FTransform CameraTransform = PlayerCamera->GetComponentTransform();
	const float FixedStep = FMath::Max(0.001f, GrenadeSimConfig.FixedStepSeconds);
	FVector2D PreviousPixel = FVector2D::ZeroVector;
	bool bHasPreviousPixel = ProjectToCapture(
		PreviewState.Position,
		CameraTransform,
		PlayerCamera->FieldOfView,
		CaptureWidth,
		CaptureHeight,
		PreviousPixel);
	const FColor TrajectoryColor(40, 255, 65, 255);

	for (int32 StepIndex = 0;
		StepIndex < MaxTrajectorySimulationSteps && !PreviewState.bMotionStopped;
		++StepIndex)
	{
		FGrenadeSim::Step(
			GetWorld(),
			GrenadeSimConfig,
			PreviewState,
			FixedStep,
			Character,
			[](const FHitResult&) { return static_cast<ABreakableTile*>(nullptr); });

		FVector2D Pixel;
		const bool bProjected = ProjectToCapture(
			PreviewState.Position,
			CameraTransform,
			PlayerCamera->FieldOfView,
			CaptureWidth,
			CaptureHeight,
			Pixel);
		if (bProjected && bHasPreviousPixel)
		{
			PaintAntialiasedLine(
				Pixels,
				CaptureWidth,
				CaptureHeight,
				PreviousPixel,
				Pixel,
				1.25f,
				TrajectoryColor);
		}
		PreviousPixel = Pixel;
		bHasPreviousPixel = bProjected;
	}
}

FString ACurriculumDataGenerator::BuildGrenadesJson() const
{
	FString Result = TEXT("[");
	for (int32 Index = 0; Index < Grenades.Num(); ++Index)
	{
		const FGeneratedGrenade& Grenade = Grenades[Index];
		if (Index > 0)
		{
			Result += TEXT(",");
		}
		Result += FString::Printf(
			TEXT("{\"id\":%d,\"position\":{\"x\":%s,\"y\":%s,\"z\":%s},")
			TEXT("\"velocity\":{\"x\":%s,\"y\":%s,\"z\":%s},\"resting\":%s}"),
			Grenade.Id,
			*JsonNumber(Grenade.State.Position.X),
			*JsonNumber(Grenade.State.Position.Y),
			*JsonNumber(Grenade.State.Position.Z),
			*JsonNumber(Grenade.State.Velocity.X),
			*JsonNumber(Grenade.State.Velocity.Y),
			*JsonNumber(Grenade.State.Velocity.Z),
			JsonBool(Grenade.State.bMotionStopped));
	}
	Result += TEXT("]");
	return Result;
}

FString ACurriculumDataGenerator::GetStageSlug() const
{
	switch (CurriculumStage)
	{
	case ECurriculumStage::Trajectory:
		return TEXT("trajectory_v2");
	case ECurriculumStage::Throw:
		return TEXT("throw_v3");
	default:
		return TEXT("movement_v1");
	}
}

FString ACurriculumDataGenerator::GetStageSchemaVersion() const
{
	return FString::Printf(TEXT("%s-preflight-4"), *GetStageSlug());
}

FString ACurriculumDataGenerator::MakeEpisodeId() const
{
	return FString::Printf(TEXT("w%03d-e%06d"), WorkerId, EpisodeIndex);
}

FString ACurriculumDataGenerator::MakeImageKey(const int32 ObservationIndex) const
{
	return FString::Printf(
		TEXT("episodes/%s/frame-%06d.png"),
		*MakeEpisodeId(),
		ObservationIndex);
}

FString ACurriculumDataGenerator::BuildDatasetJson(
	const bool bComplete,
	const FString& ErrorMessage) const
{
	const int32 CompletedEpisodes = EpisodeIndex + (bEpisodeActive ? 1 : 0);
	const TCHAR* CollectionPolicy = bTrajectoryShowcase
		? TEXT("inspection_only_trajectory_showcase")
		: (bCoverageGuided
			? TEXT("training_coverage_guided_missions")
			: TEXT("training_semimarkov"));
	return FString::Printf(
		TEXT("{\n")
		TEXT("  \"schema_version\": \"%s\",\n")
		TEXT("  \"curriculum_version\": \"%s\",\n")
		TEXT("  \"complete\": %s,\n")
		TEXT("  \"error\": \"%s\",\n")
		TEXT("  \"run_started_utc\": \"%s\",\n")
		TEXT("  \"build_revision\": \"%s\",\n")
		TEXT("  \"unreal_engine_version\": \"%s\",\n")
		TEXT("  \"collection_policy\": \"%s\",\n")
		TEXT("  \"coverage_guided\": %s,\n")
		TEXT("  \"coverage_azimuth_bin_count\": %d,\n")
		TEXT("  \"coverage_summary\": {\n")
		TEXT("    \"object_view_bin_masks\": {\"rectangle\": %u, \"pyramid\": %u, ")
		TEXT("\"sphere\": %u, \"hoop\": %u, \"ramp\": %u},\n")
		TEXT("    \"ramp_traversals\": %d,\n")
		TEXT("    \"hoop_passes\": %d,\n")
		TEXT("    \"mission_successes\": %d,\n")
		TEXT("    \"mission_failures\": %d\n")
		TEXT("  },\n")
		TEXT("  \"map_configuration_hash\": \"fixed-arena-r3-balanced-palette\",\n")
		TEXT("  \"worker_id\": %d,\n")
		TEXT("  \"seed_start\": %d,\n")
		TEXT("  \"requested_episode_count\": %d,\n")
		TEXT("  \"completed_episode_count\": %d,\n")
		TEXT("  \"episode_seconds\": %d,\n")
		TEXT("  \"observation_rate_hz\": %d,\n")
		TEXT("  \"transitions_per_episode\": %d,\n")
		TEXT("  \"transition_count\": %d,\n")
		TEXT("  \"observation_count\": %d,\n")
		TEXT("  \"rgb_width\": %d,\n")
		TEXT("  \"rgb_height\": %d,\n")
		TEXT("  \"rgb_format\": \"lossless_png\",\n")
		TEXT("  \"metadata_format\": \"jsonl\",\n")
		TEXT("  \"shards\": [\"%s\"]\n")
		TEXT("}\n"),
		*GetStageSchemaVersion(),
		*GetStageSlug(),
		JsonBool(bComplete),
		*ErrorMessage.ReplaceCharWithEscapedChar(),
		*RunStartedUtc,
		*BuildRevision.ReplaceCharWithEscapedChar(),
		*UnrealEngineVersion.ReplaceCharWithEscapedChar(),
		CollectionPolicy,
		JsonBool(bCoverageGuided && !bTrajectoryShowcase),
		CoverageAzimuthBinCount,
		OverallObjectViewBins[0],
		OverallObjectViewBins[1],
		OverallObjectViewBins[2],
		OverallObjectViewBins[3],
		OverallObjectViewBins[4],
		OverallRampTraversals,
		OverallHoopPasses,
		OverallMissionSuccesses,
		OverallMissionFailures,
		WorkerId,
		SeedStart,
		EpisodeCount,
		CompletedEpisodes,
		EpisodeSeconds,
		ObservationRate,
		TransitionsPerEpisode,
		GlobalTransitionCount,
		GlobalTransitionCount + CompletedEpisodes,
		CaptureWidth,
		CaptureHeight,
		*FPaths::GetCleanFilename(ShardPath));
}
