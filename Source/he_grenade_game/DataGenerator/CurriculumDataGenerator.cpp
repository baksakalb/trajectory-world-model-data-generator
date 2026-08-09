#include "DataGenerator/CurriculumDataGenerator.h"

#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneCaptureComponent2D.h"
#include "Components/StaticMeshComponent.h"
#include "DataGenerator/CurriculumAction.h"
#include "DataGenerator/V2ActionSemantics.h"
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

#if CURRICULUM_WITH_LIBWEBP
#include <webp/encode.h>
#endif

namespace
{
	constexpr int32 TarBlockSize = 512;
	constexpr int32 TarEndBlockCount = 2;
	constexpr float CurriculumThrowSpeedCmPerSecond = 1400.0f;
	constexpr int32 MaxTrajectorySimulationSteps = 720;
	constexpr int32 CoverageAzimuthBinCount = 12;
	constexpr int32 ObjectScenarioCount = 120;
	constexpr int32 ContactBaseScenarioCount = 135;
	constexpr int32 ContactScenarioCount = 675;
	constexpr int32 RampScenarioCount = 30;
	constexpr int32 HoopScenarioCount = 30;
	constexpr float PlayableSamplingLimitCm = 1400.0f;
	constexpr float RampPitchDegrees = -18.0f;
	constexpr float RampLengthCm = 500.0f;
	constexpr float RampThicknessCm = 36.0f;
	constexpr float CharacterStandingHalfHeightCm = 96.0f;

	constexpr double AutomatedMissionFrameShares[] =
	{
		55.0 / 90.0, // Semi-Markov. The remaining 10% of the final mixture is human.
		20.0 / 90.0, // Object-view navigation.
		5.0 / 90.0,  // Deliberate contact and recovery.
		5.0 / 90.0,  // Ramp traversal.
		5.0 / 90.0   // Hoop passage.
	};
	constexpr double ObjectViewModeFrameShares[] = {0.40, 0.35, 0.20, 0.05};
	constexpr double ObjectGazeIntentFrameShares[] = {0.40, 0.25, 0.20, 0.15};
	constexpr double LocomotionFacingFrameShares[] = {0.35, 0.15, 0.15, 0.15, 0.20};
	constexpr double GuidedCameraStyleFrameShares[] = {0.25, 0.25, 0.25, 0.25};
	constexpr double GuidedPathFrameShares[] = {0.50, 0.25, 0.25};
	constexpr double PitchBandFrameShares[] = {0.75, 0.20, 0.04, 0.01};

	struct FCoverageTargetDefinition
	{
		const TCHAR* Slug;
		FName ActorTag;
		FVector LookTarget;
		float OrbitRadiusCm;
		float ContactRadiusCm;
	};

	struct FContactTargetDefinition
	{
		const TCHAR* Slug;
		FName ActorTag;
		FVector LookTarget;
		FVector WallInwardNormal;
		float ContactRadiusCm;
		bool bWall;
	};

	const FCoverageTargetDefinition& GetCoverageTargetDefinition(const int32 Index)
	{
		static const FCoverageTargetDefinition Targets[] =
		{
			{
				TEXT("rectangle"),
				TEXT("CurriculumObject_Rectangle"),
				FVector(-700.0f, 700.0f, 125.0f),
				560.0f,
				185.0f
			},
			{
				TEXT("pyramid"),
				TEXT("CurriculumObject_Pyramid"),
				FVector(700.0f, 700.0f, 115.0f),
				560.0f,
				190.0f
			},
			{
				TEXT("sphere"),
				TEXT("CurriculumObject_Sphere"),
				FVector(-700.0f, -700.0f, 120.0f),
				560.0f,
				125.0f
			},
			{
				TEXT("hoop"),
				TEXT("CurriculumObject_Hoop"),
				FVector(700.0f, -700.0f, 145.0f),
				560.0f,
				155.0f
			},
			{
				TEXT("ramp"),
				TEXT("CurriculumObject_Ramp"),
				FVector(0.0f, 0.0f, 95.0f),
				650.0f,
				285.0f
			}
		};
		return Targets[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(Targets) - 1)];
	}

	const FContactTargetDefinition& GetContactTargetDefinition(const int32 Index)
	{
		static const FContactTargetDefinition Targets[] =
		{
			{TEXT("rectangle"), TEXT("CurriculumObject_Rectangle"),
				FVector(-700.0f, 700.0f, 125.0f), FVector::ZeroVector, 185.0f, false},
			{TEXT("pyramid"), TEXT("CurriculumObject_Pyramid"),
				FVector(700.0f, 700.0f, 115.0f), FVector::ZeroVector, 190.0f, false},
			{TEXT("sphere"), TEXT("CurriculumObject_Sphere"),
				FVector(-700.0f, -700.0f, 120.0f), FVector::ZeroVector, 125.0f, false},
			{TEXT("hoop"), TEXT("CurriculumObject_Hoop"),
				FVector(700.0f, -700.0f, 145.0f), FVector::ZeroVector, 155.0f, false},
			{TEXT("ramp"), TEXT("CurriculumObject_Ramp"),
				FVector(0.0f, 0.0f, 95.0f), FVector::ZeroVector, 285.0f, false},
			{TEXT("north_wall"), TEXT("CurriculumWall_North"),
				FVector(1600.0f, 0.0f, 120.0f), FVector(-1.0f, 0.0f, 0.0f), 0.0f, true},
			{TEXT("south_wall"), TEXT("CurriculumWall_South"),
				FVector(-1600.0f, 0.0f, 120.0f), FVector(1.0f, 0.0f, 0.0f), 0.0f, true},
			{TEXT("east_wall"), TEXT("CurriculumWall_East"),
				FVector(0.0f, 1600.0f, 120.0f), FVector(0.0f, -1.0f, 0.0f), 0.0f, true},
			{TEXT("west_wall"), TEXT("CurriculumWall_West"),
				FVector(0.0f, -1600.0f, 120.0f), FVector(0.0f, 1.0f, 0.0f), 0.0f, true}
		};
		return Targets[FMath::Clamp(Index, 0, UE_ARRAY_COUNT(Targets) - 1)];
	}

	uint64 MixParameterBits(uint64 Value)
	{
		Value += 0x9E3779B97F4A7C15ull;
		Value = (Value ^ (Value >> 30)) * 0xBF58476D1CE4E5B9ull;
		Value = (Value ^ (Value >> 27)) * 0x94D049BB133111EBull;
		return Value ^ (Value >> 31);
	}

	uint64 HashParameterName(const TCHAR* Text)
	{
		uint64 Hash = 1469598103934665603ull;
		for (const TCHAR* Cursor = Text; Cursor && *Cursor; ++Cursor)
		{
			Hash ^= static_cast<uint64>(*Cursor);
			Hash *= 1099511628211ull;
		}
		return Hash;
	}

	int32 GreatestCommonDivisor(int32 A, int32 B)
	{
		while (B != 0)
		{
			const int32 Remainder = A % B;
			A = B;
			B = Remainder;
		}
		return FMath::Abs(A);
	}

	bool IsInsideSamplingArena(const FVector& Point)
	{
		return FMath::Abs(Point.X) <= PlayableSamplingLimitCm
			&& FMath::Abs(Point.Y) <= PlayableSamplingLimitCm;
	}

	int32 EncodeObjectScenario(
		const int32 TargetIndex,
		const int32 ModeIndex,
		const int32 GazeIndex,
		const bool bClockwise)
	{
		if (ModeIndex == 0)
		{
			return (TargetIndex * 4) + GazeIndex;
		}
		if (ModeIndex == 1)
		{
			return 20 + (TargetIndex * 4) + GazeIndex;
		}
		const int32 OrbitBase = ModeIndex == 2 ? 40 : 80;
		return OrbitBase
			+ (TargetIndex * 8)
			+ (GazeIndex * 2)
			+ (bClockwise ? 0 : 1);
	}

	void DecodeObjectScenario(
		const int32 ScenarioIndex,
		int32& OutTargetIndex,
		int32& OutModeIndex,
		int32& OutGazeIndex,
		bool& bOutClockwise)
	{
		const int32 SafeIndex =
			FMath::Clamp(ScenarioIndex, 0, ObjectScenarioCount - 1);
		if (SafeIndex < 20)
		{
			OutTargetIndex = SafeIndex / 4;
			OutModeIndex = 0;
			OutGazeIndex = SafeIndex % 4;
			bOutClockwise = false;
			return;
		}
		if (SafeIndex < 40)
		{
			const int32 LocalIndex = SafeIndex - 20;
			OutTargetIndex = LocalIndex / 4;
			OutModeIndex = 1;
			OutGazeIndex = LocalIndex % 4;
			bOutClockwise = false;
			return;
		}
		const bool bFullOrbit = SafeIndex >= 80;
		const int32 LocalIndex = SafeIndex - (bFullOrbit ? 80 : 40);
		OutTargetIndex = LocalIndex / 8;
		OutModeIndex = bFullOrbit ? 3 : 2;
		OutGazeIndex = (LocalIndex % 8) / 2;
		bOutClockwise = (LocalIndex % 2) == 0;
	}

	double GetObjectScenarioShare(const int32 ScenarioIndex)
	{
		if (ScenarioIndex < 20)
		{
			return ObjectViewModeFrameShares[0] / 20.0;
		}
		if (ScenarioIndex < 40)
		{
			return ObjectViewModeFrameShares[1] / 20.0;
		}
		if (ScenarioIndex < 80)
		{
			return ObjectViewModeFrameShares[2] / 40.0;
		}
		return ObjectViewModeFrameShares[3] / 40.0;
	}

	int32 SelectFrameDeficitBucket(
		const int64* FrameCounts,
		const double* TargetShares,
		const int32 BucketCount,
		const int64 ProjectedAdditionalFrames,
		const uint64 TieKey = 0)
	{
		int64 CurrentTotal = 0;
		for (int32 Index = 0; Index < BucketCount; ++Index)
		{
			CurrentTotal += FrameCounts[Index];
		}

		const double ProjectedTotal =
			static_cast<double>(CurrentTotal + FMath::Max<int64>(1, ProjectedAdditionalFrames));
		int32 BestIndex = 0;
		double BestDeficit = -DBL_MAX;
		for (int32 Index = 0; Index < BucketCount; ++Index)
		{
			const double Deficit =
				(ProjectedTotal * TargetShares[Index])
				- static_cast<double>(FrameCounts[Index]);
			if (Deficit > BestDeficit + UE_DOUBLE_SMALL_NUMBER
				|| (FMath::IsNearlyEqual(Deficit, BestDeficit)
					&& MixParameterBits(TieKey ^ static_cast<uint64>(Index))
						> MixParameterBits(TieKey ^ static_cast<uint64>(BestIndex))))
			{
				BestDeficit = Deficit;
				BestIndex = Index;
			}
		}
		return BestIndex;
	}

	float DistancePointToSegment2D(
		const FVector& Point,
		const FVector& SegmentStart,
		const FVector& SegmentEnd)
	{
		const FVector2D Point2D(Point.X, Point.Y);
		const FVector2D Start2D(SegmentStart.X, SegmentStart.Y);
		const FVector2D End2D(SegmentEnd.X, SegmentEnd.Y);
		const FVector2D Segment = End2D - Start2D;
		const float SegmentSizeSquared = Segment.SizeSquared();
		if (SegmentSizeSquared <= UE_SMALL_NUMBER)
		{
			return FVector2D::Distance(Point2D, Start2D);
		}
		const float Along = FMath::Clamp(
			FVector2D::DotProduct(Point2D - Start2D, Segment)
				/ SegmentSizeSquared,
			0.0f,
			1.0f);
		return FVector2D::Distance(Point2D, Start2D + (Segment * Along));
	}

	bool SegmentClearsLearningObjects(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const int32 ViewedTargetIndex)
	{
		for (int32 TargetIndex = 0; TargetIndex < 5; ++TargetIndex)
		{
			const FCoverageTargetDefinition& Target =
				GetCoverageTargetDefinition(TargetIndex);
			const float RequiredClearance =
				Target.ContactRadiusCm
				+ (TargetIndex == ViewedTargetIndex ? 90.0f : 100.0f);
			if (DistancePointToSegment2D(
					Target.LookTarget,
					SegmentStart,
					SegmentEnd)
				< RequiredClearance)
			{
				return false;
			}
		}
		return true;
	}

	bool SegmentClearsOtherLearningObjects(
		const FVector& SegmentStart,
		const FVector& SegmentEnd,
		const int32 ExcludedTargetIndex)
	{
		for (int32 TargetIndex = 0; TargetIndex < 5; ++TargetIndex)
		{
			if (TargetIndex == ExcludedTargetIndex)
			{
				continue;
			}
			const FCoverageTargetDefinition& Target =
				GetCoverageTargetDefinition(TargetIndex);
			if (DistancePointToSegment2D(
					Target.LookTarget,
					SegmentStart,
					SegmentEnd)
				< Target.ContactRadiusCm + 100.0f)
			{
				return false;
			}
		}
		return true;
	}

	float RampTopSurfaceZ(const float WorldX)
	{
		const float PitchRadians = FMath::DegreesToRadians(RampPitchDegrees);
		const float SupportHeight =
			(FMath::Abs(FMath::Sin(PitchRadians)) * RampLengthCm * 0.5f)
			+ (FMath::Abs(FMath::Cos(PitchRadians)) * RampThicknessCm * 0.5f);
		return SupportHeight
			+ (FMath::Sin(PitchRadians) * WorldX)
			+ (FMath::Abs(FMath::Cos(PitchRadians)) * RampThicknessCm * 0.5f);
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
			FPlatformMisc::RequestExitWithStatus(false, 1);
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
			FPlatformMisc::RequestExitWithStatus(false, 1);
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
		TEXT("Dataset generator configured: stage %s, %d episode(s), %d transitions each, %d Hz, %dx%d, worker %d, storage %s, WebP effort %d."),
		*GetStageSlug(),
		EpisodeCount,
		TransitionsPerEpisode,
		ObservationRate,
		CaptureWidth,
		CaptureHeight,
		WorkerId,
		StorageFormat == EStorageFormat::WebPParquet
			? TEXT("webp_parquet")
			: TEXT("png_jsonl"),
		WebPLosslessEffort);
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

	const bool bPostSuccessRolloutComplete =
		bCoverageMissionSucceeded
		&& CoveragePostSuccessSteps
			>= FMath::Max(1, CoverageRequiredPostSuccessSteps);
	const bool bMissionTimedOut =
		!bCoverageMissionSucceeded
		&& FrameIndex >= TransitionsPerEpisode;
	if (bPostSuccessRolloutComplete
		|| bCoverageMissionFailed
		|| bMissionTimedOut)
	{
		EndEpisode();
		++EpisodeOrdinal;
		if (EpisodeOrdinal >= EpisodeCount)
		{
			FinishRun(true);
		}
		else
		{
			EpisodeIndex = RequestedEpisodeIndices.IsEmpty()
				? EpisodeOrdinal
				: RequestedEpisodeIndices[EpisodeOrdinal];
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
		Character->SetCurriculumV2ActionSemanticsEnabled(false);
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
				CurriculumStage = ECurriculumStage::LegacyTrajectory;
			}
			else if (StageValue == TEXT("throw") || StageValue == TEXT("throw_v3"))
			{
				CurriculumStage = ECurriculumStage::LegacyThrow;
			}
			else if (StageValue == TEXT("v2")
				|| StageValue == TEXT("trajectory_throw")
				|| StageValue == TEXT("trajectory_throw_v2"))
			{
				CurriculumStage = ECurriculumStage::TrajectoryThrowV2;
			}
			else
			{
				LastError = FString::Printf(TEXT("Unknown curriculum stage: %s"), *StageValue);
				return false;
			}
		}
		FString StorageFormatValue;
		if (Config->TryGetStringField(TEXT("storage_format"), StorageFormatValue))
		{
			StorageFormatValue.ToLowerInline();
			if (StorageFormatValue == TEXT("png_jsonl"))
			{
				StorageFormat = EStorageFormat::PngJsonl;
			}
			else if (StorageFormatValue == TEXT("webp_parquet"))
			{
				StorageFormat = EStorageFormat::WebPParquet;
			}
			else
			{
				LastError = FString::Printf(
					TEXT("Unknown storage format: %s"),
					*StorageFormatValue);
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
		if (Config->TryGetNumberField(TEXT("webp_lossless_effort"), NumberValue))
		{
			WebPLosslessEffort = FMath::RoundToInt(NumberValue);
		}
		Config->TryGetStringField(TEXT("output"), OutputDirectory);
		Config->TryGetStringField(TEXT("mission_override"), MissionOverride);
		Config->TryGetStringField(TEXT("object_view_mode_override"), ObjectViewModeOverride);
		Config->TryGetStringField(TEXT("coverage_target_override"), CoverageTargetOverride);
		Config->TryGetStringField(TEXT("mission_direction_override"), MissionDirectionOverride);
		Config->TryGetStringField(TEXT("recipe_manifest"), RecipeManifestPath);
		Config->TryGetBoolField(TEXT("exit_on_complete"), bExitOnComplete);
		Config->TryGetBoolField(TEXT("coverage_guided"), bCoverageGuided);
		Config->TryGetBoolField(TEXT("v2_runtime_smoke"), bV2RuntimeSmoke);
		Config->TryGetBoolField(
			TEXT("v2_trajectory_hold_mission"),
			bV2TrajectoryHoldMission);

		if (!OutputDirectory.IsEmpty() && FPaths::IsRelative(OutputDirectory))
		{
			OutputDirectory = FPaths::Combine(
				FPaths::GetPath(ConfigPath),
				OutputDirectory);
		}
		if (!RecipeManifestPath.IsEmpty() && FPaths::IsRelative(RecipeManifestPath))
		{
			RecipeManifestPath = FPaths::Combine(
				FPaths::GetPath(ConfigPath),
				RecipeManifestPath);
		}
	}

	FParse::Value(CommandLine, TEXT("Episodes="), EpisodeCount);
	FParse::Value(CommandLine, TEXT("EpisodeSeconds="), EpisodeSeconds);
	FParse::Value(CommandLine, TEXT("SeedStart="), SeedStart);
	FParse::Value(CommandLine, TEXT("WorkerId="), WorkerId);
	FParse::Value(CommandLine, TEXT("ObservationRate="), ObservationRate);
	FParse::Value(CommandLine, TEXT("Width="), CaptureWidth);
	FParse::Value(CommandLine, TEXT("Height="), CaptureHeight);
	FParse::Value(CommandLine, TEXT("WebPEffort="), WebPLosslessEffort);
	FParse::Value(CommandLine, TEXT("Mission="), MissionOverride);
	FParse::Value(CommandLine, TEXT("ObjectViewMode="), ObjectViewModeOverride);
	FParse::Value(CommandLine, TEXT("CoverageTarget="), CoverageTargetOverride);
	FParse::Value(CommandLine, TEXT("MissionDirection="), MissionDirectionOverride);
	FParse::Value(CommandLine, TEXT("RecipeManifest="), RecipeManifestPath);
	FString CommandLineEpisodeIndices;
	if (FParse::Value(CommandLine, TEXT("EpisodeIndices="), CommandLineEpisodeIndices))
	{
		TArray<FString> EpisodeIndexTokens;
		CommandLineEpisodeIndices.ParseIntoArray(
			EpisodeIndexTokens,
			TEXT("+"),
			true);
		for (const FString& Token : EpisodeIndexTokens)
		{
			if (!Token.IsNumeric())
			{
				LastError = FString::Printf(
					TEXT("Invalid episode index in -EpisodeIndices: %s"),
					*Token);
				return false;
			}
			const int32 RequestedIndex = FCString::Atoi(*Token);
			if (RequestedIndex < 0 || RequestedEpisodeIndices.Contains(RequestedIndex))
			{
				LastError = FString::Printf(
					TEXT("Episode indices must be unique non-negative integers: %s"),
					*Token);
				return false;
			}
			RequestedEpisodeIndices.Add(RequestedIndex);
		}
		if (RequestedEpisodeIndices.IsEmpty())
		{
			LastError = TEXT("-EpisodeIndices did not contain any episode indices.");
			return false;
		}
		EpisodeCount = RequestedEpisodeIndices.Num();
		EpisodeIndex = RequestedEpisodeIndices[0];
	}
	MissionOverride.ToLowerInline();
	ObjectViewModeOverride.ToLowerInline();
	CoverageTargetOverride.ToLowerInline();
	MissionDirectionOverride.ToLowerInline();
	if (!RecipeManifestPath.IsEmpty())
	{
		RecipeManifestPath = FPaths::ConvertRelativePathToFull(RecipeManifestPath);
		if (!LoadRecipeManifest(RecipeManifestPath))
		{
			return false;
		}
	}
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
			CurriculumStage = ECurriculumStage::LegacyTrajectory;
		}
		else if (CommandLineStage == TEXT("throw") || CommandLineStage == TEXT("throw_v3"))
		{
			CurriculumStage = ECurriculumStage::LegacyThrow;
		}
		else if (CommandLineStage == TEXT("v2")
			|| CommandLineStage == TEXT("trajectory_throw")
			|| CommandLineStage == TEXT("trajectory_throw_v2"))
		{
			CurriculumStage = ECurriculumStage::TrajectoryThrowV2;
		}
		else
		{
			LastError = FString::Printf(TEXT("Unknown curriculum stage: %s"), *CommandLineStage);
			return false;
		}
	}
	FString CommandLineStorageFormat;
	if (FParse::Value(CommandLine, TEXT("StorageFormat="), CommandLineStorageFormat))
	{
		CommandLineStorageFormat.ToLowerInline();
		if (CommandLineStorageFormat == TEXT("png_jsonl"))
		{
			StorageFormat = EStorageFormat::PngJsonl;
		}
		else if (CommandLineStorageFormat == TEXT("webp_parquet"))
		{
			StorageFormat = EStorageFormat::WebPParquet;
		}
		else
		{
			LastError = FString::Printf(
				TEXT("Unknown storage format: %s"),
				*CommandLineStorageFormat);
			return false;
		}
	}
#if !CURRICULUM_WITH_LIBWEBP
	if (StorageFormat == EStorageFormat::WebPParquet)
	{
		LastError = TEXT("This platform was built without libwebp support.");
		return false;
	}
#endif
	if (!FParse::Value(CommandLine, TEXT("BuildRevision="), BuildRevision))
	{
		BuildRevision = FApp::GetBuildVersion();
	}

	EpisodeCount = FMath::Clamp(EpisodeCount, 1, 100000);
	EpisodeSeconds = FMath::Clamp(EpisodeSeconds, 1, 3600);
	ObservationRate = FMath::Clamp(ObservationRate, 1, 120);
	CaptureWidth = FMath::Clamp(CaptureWidth, 64, 4096);
	CaptureHeight = FMath::Clamp(CaptureHeight, 64, 4096);
	WebPLosslessEffort = FMath::Clamp(WebPLosslessEffort, 0, 9);
	TransitionsPerEpisode = EpisodeSeconds * ObservationRate;
	if (FParse::Param(CommandLine, TEXT("NoExitOnComplete")))
	{
		bExitOnComplete = false;
	}
	bTrajectoryShowcase =
		FParse::Param(CommandLine, TEXT("TrajectoryShowcase"));
	bMissionReviewSuite =
		FParse::Param(CommandLine, TEXT("MissionReviewSuite"));
	bV2RuntimeSmoke = bV2RuntimeSmoke
		|| FParse::Param(CommandLine, TEXT("V2RuntimeSmoke"));
	bV2TrajectoryHoldMission = bV2TrajectoryHoldMission
		|| FParse::Param(CommandLine, TEXT("V2TrajectoryHoldMission"));
	if (bV2RuntimeSmoke && bV2TrajectoryHoldMission)
	{
		LastError =
			TEXT("-V2RuntimeSmoke and -V2TrajectoryHoldMission are mutually exclusive.");
		return false;
	}
	if (bTrajectoryShowcase && bMissionReviewSuite)
	{
		LastError =
			TEXT("-TrajectoryShowcase and -MissionReviewSuite are mutually exclusive.");
		return false;
	}
	if (bMissionReviewSuite)
	{
		// One free-exploration episode, ten linear object missions, twenty orbit
		// missions covering both directions, all nine contact targets, and five
		// explicit facing profiles in both ramp and both hoop directions.
		EpisodeCount = 60;
		bCoverageGuided = true;
	}
	if (FParse::Param(CommandLine, TEXT("CoverageGuided")))
	{
		bCoverageGuided = true;
	}
	if (FParse::Param(CommandLine, TEXT("NoCoverageGuided")))
	{
		bCoverageGuided = false;
	}
	if (bMissionReviewSuite)
	{
		bCoverageGuided = true;
	}
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& bPrescribedRecipes)
	{
		LastError =
			TEXT("A Movement V1 recipe manifest cannot be overridden into combined V2; the V2 catalog parser is not implemented yet.");
		return false;
	}
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& !bV2RuntimeSmoke
		&& !bV2TrajectoryHoldMission)
	{
		LastError =
			TEXT("Combined V2 currently requires -V2RuntimeSmoke or -V2TrajectoryHoldMission until the coherent random/mission policy is implemented; refusing to use the legacy independent Q/E sampler.");
		return false;
	}
	if (bV2TrajectoryHoldMission
		&& CurriculumStage != ECurriculumStage::TrajectoryThrowV2)
	{
		LastError =
			TEXT("-V2TrajectoryHoldMission requires -Stage=trajectory_throw_v2.");
		return false;
	}
	if (bV2TrajectoryHoldMission && EpisodeSeconds < 4)
	{
		LastError =
			TEXT("-V2TrajectoryHoldMission requires at least four episode seconds so Q remains visible through the complete cooldown.");
		return false;
	}
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
	{
		bCoverageGuided = false;
	}
	if (bTrajectoryShowcase
		&& CurriculumStage != ECurriculumStage::LegacyTrajectory)
	{
		LastError =
			TEXT("-TrajectoryShowcase is restricted to the explicit legacy trajectory comparison stage.");
		return false;
	}
	if (bMissionReviewSuite
		&& CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
	{
		LastError =
			TEXT("The Movement V1 mission review suite is incompatible with combined V2.");
		return false;
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

bool ACurriculumDataGenerator::LoadRecipeManifest(const FString& ManifestPath)
{
	FString ManifestText;
	if (!FFileHelper::LoadFileToString(ManifestText, *ManifestPath))
	{
		LastError = FString::Printf(
			TEXT("Could not read recipe manifest: %s"),
			*ManifestPath);
		return false;
	}

	TSharedPtr<FJsonObject> Manifest;
	const TSharedRef<TJsonReader<>> Reader =
		TJsonReaderFactory<>::Create(ManifestText);
	if (!FJsonSerializer::Deserialize(Reader, Manifest) || !Manifest.IsValid())
	{
		LastError = FString::Printf(
			TEXT("Recipe manifest is not valid JSON: %s"),
			*ManifestPath);
		return false;
	}

	Manifest->TryGetStringField(TEXT("plan_id"), PlanId);
	Manifest->TryGetStringField(TEXT("plan_version"), PlanVersion);
	Manifest->TryGetStringField(TEXT("assignment_id"), AssignmentId);
	Manifest->TryGetStringField(TEXT("attempt_id"), AttemptId);
	Manifest->TryGetStringField(TEXT("executor_id"), ExecutorId);
	Manifest->TryGetStringField(TEXT("split"), DatasetSplit);
	double NumberValue = 0.0;
	if (Manifest->TryGetNumberField(TEXT("logical_worker_id"), NumberValue))
	{
		WorkerId = FMath::RoundToInt(NumberValue);
	}
	const TSharedPtr<FJsonObject>* GeneratorObject = nullptr;
	if (Manifest->TryGetObjectField(TEXT("generator"), GeneratorObject)
		&& GeneratorObject
		&& GeneratorObject->IsValid())
	{
		FString StageValue;
		if ((*GeneratorObject)->TryGetStringField(TEXT("stage"), StageValue))
		{
			StageValue.ToLowerInline();
			if (StageValue == TEXT("movement") || StageValue == TEXT("movement_v1"))
			{
				CurriculumStage = ECurriculumStage::Movement;
			}
			else if (StageValue == TEXT("v2")
				|| StageValue == TEXT("trajectory_throw")
				|| StageValue == TEXT("trajectory_throw_v2"))
			{
				LastError =
					TEXT("Combined V2 prescribed recipes require the V2 catalog fields and are not enabled by the Movement V1 manifest parser.");
				return false;
			}
			else
			{
				LastError =
					TEXT("Legacy trajectory/throw stages are comparison-only and cannot run prescribed production recipes.");
				return false;
			}
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("episode_seconds"), NumberValue))
		{
			EpisodeSeconds = FMath::RoundToInt(NumberValue);
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("observation_rate_hz"), NumberValue))
		{
			ObservationRate = FMath::RoundToInt(NumberValue);
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("rgb_width"), NumberValue))
		{
			CaptureWidth = FMath::RoundToInt(NumberValue);
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("rgb_height"), NumberValue))
		{
			CaptureHeight = FMath::RoundToInt(NumberValue);
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("webp_lossless_effort"), NumberValue))
		{
			WebPLosslessEffort = FMath::RoundToInt(NumberValue);
		}
		if ((*GeneratorObject)->TryGetNumberField(TEXT("seed_start"), NumberValue))
		{
			SeedStart = FMath::RoundToInt(NumberValue);
		}
		FString StorageValue;
		if ((*GeneratorObject)->TryGetStringField(TEXT("storage_format"), StorageValue))
		{
			StorageValue.ToLowerInline();
			if (StorageValue == TEXT("webp_parquet"))
			{
				StorageFormat = EStorageFormat::WebPParquet;
			}
			else if (StorageValue == TEXT("png_jsonl"))
			{
				StorageFormat = EStorageFormat::PngJsonl;
			}
			else
			{
				LastError = FString::Printf(
					TEXT("Unknown prescribed storage format: %s"),
					*StorageValue);
				return false;
			}
		}
	}

	const TArray<TSharedPtr<FJsonValue>>* RecipeValues = nullptr;
	if (!Manifest->TryGetArrayField(TEXT("recipes"), RecipeValues)
		|| !RecipeValues
		|| RecipeValues->IsEmpty())
	{
		LastError = TEXT("Recipe manifest must contain a non-empty recipes array.");
		return false;
	}

	PrescribedRecipes.Reset();
	RequestedEpisodeIndices.Reset();
	TSet<FString> RecipeIds;
	TSet<int32> EpisodeIndices;
	for (const TSharedPtr<FJsonValue>& RecipeValue : *RecipeValues)
	{
		const TSharedPtr<FJsonObject> RecipeObject =
			RecipeValue.IsValid() ? RecipeValue->AsObject() : nullptr;
		if (!RecipeObject.IsValid())
		{
			LastError = TEXT("Every recipe manifest entry must be an object.");
			return false;
		}
		FPrescribedRecipe Recipe;
		if (!RecipeObject->TryGetStringField(TEXT("recipe_id"), Recipe.RecipeId)
			|| Recipe.RecipeId.IsEmpty()
			|| RecipeIds.Contains(Recipe.RecipeId))
		{
			LastError = TEXT("Recipe IDs must be present and unique within an assignment.");
			return false;
		}
		if (!RecipeObject->TryGetStringField(TEXT("mission"), Recipe.Mission))
		{
			LastError = FString::Printf(
				TEXT("Recipe %s does not specify a mission."),
				*Recipe.RecipeId);
			return false;
		}
		Recipe.Mission.ToLowerInline();
		if (!RecipeObject->TryGetNumberField(TEXT("episode_index"), NumberValue))
		{
			LastError = FString::Printf(
				TEXT("Recipe %s does not specify episode_index."),
				*Recipe.RecipeId);
			return false;
		}
		Recipe.EpisodeIndex = FMath::RoundToInt(NumberValue);
		if (Recipe.EpisodeIndex < 0 || EpisodeIndices.Contains(Recipe.EpisodeIndex))
		{
			LastError = TEXT("Recipe episode indices must be unique non-negative integers.");
			return false;
		}
		if (RecipeObject->TryGetNumberField(TEXT("scenario_index"), NumberValue))
		{
			Recipe.ScenarioIndex = FMath::RoundToInt(NumberValue);
		}
		if (RecipeObject->TryGetNumberField(TEXT("continuous_sample_ordinal"), NumberValue))
		{
			Recipe.ContinuousSampleOrdinal = FMath::RoundToInt(NumberValue);
		}
		if (RecipeObject->TryGetNumberField(TEXT("refinement_level"), NumberValue))
		{
			Recipe.RefinementLevel = FMath::RoundToInt(NumberValue);
		}
		if (RecipeObject->TryGetNumberField(TEXT("repetition_index"), NumberValue))
		{
			Recipe.RepetitionIndex = FMath::RoundToInt(NumberValue);
		}
		RecipeIds.Add(Recipe.RecipeId);
		EpisodeIndices.Add(Recipe.EpisodeIndex);
		RequestedEpisodeIndices.Add(Recipe.EpisodeIndex);
		PrescribedRecipes.Add(MoveTemp(Recipe));
	}

	EpisodeCount = PrescribedRecipes.Num();
	EpisodeIndex = PrescribedRecipes[0].EpisodeIndex;
	bCoverageGuided = true;
	bPrescribedRecipes = true;
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
	Character->SetCurriculumV2ActionSemanticsEnabled(
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2);

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
	if (bPrescribedRecipes)
	{
		if (!PrescribedRecipes.IsValidIndex(EpisodeOrdinal))
		{
			LastError = TEXT("Recipe manifest and episode ordinal are inconsistent.");
			return false;
		}
		const FPrescribedRecipe& Recipe = PrescribedRecipes[EpisodeOrdinal];
		EpisodeIndex = Recipe.EpisodeIndex;
		CurrentRecipeId = Recipe.RecipeId;
		MissionOverride = Recipe.Mission;
		CurrentPrescribedScenarioIndex = Recipe.ScenarioIndex;
		CurrentContinuousSampleOrdinal = Recipe.ContinuousSampleOrdinal;
		CurrentRefinementLevel = Recipe.RefinementLevel;
		CurrentRepetitionIndex = Recipe.RepetitionIndex;
	}

	const int32 EpisodeSeed = SeedStart + EpisodeIndex;
	const int32 MixedStreamSeed = static_cast<int32>(
		GetParameterBits(TEXT("mutable_episode_stream")) & 0x7fffffffull);
	EpisodeRandom.Initialize(MixedStreamSeed);
	FrameIndex = 0;
	ResetStageState();
	SelectCoverageMission();
	if (!bCoverageMissionConfigurationValid)
	{
		LastError = FString::Printf(
			TEXT("Could not construct valid mission geometry for episode %d."),
			EpisodeIndex);
		return false;
	}
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
		Pitch = bCanonicalSpawn ? 0.0f : SelectPitchTargetDegrees();
	}
	float MinimumPitch = -89.9f;
	float MaximumPitch = 89.9f;
	Character->GetCurriculumCameraPitchLimits(MinimumPitch, MaximumPitch);
	Pitch = FMath::Clamp(FRotator::NormalizeAxis(Pitch), MinimumPitch, MaximumPitch);
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
	HeldCameraPitchTargetDegrees = Pitch;
	LastAppliedActionMask = 0;
	ActionScriptMasks.Reset();
	ActionScriptHoldSteps.Reset();
	ActionScriptIndex = 0;
	ActionScriptStepsRemaining = 0;
	NextThrowRequestFrame = EpisodeRandom.RandRange(8, 24);
	bCoveragePreviousPositionValid = false;
	CoveragePreviousPosition = SpawnLocation;
	CoverageLastHoopSide = SpawnLocation.X - 700.0f;
	bRampMounted =
		CoverageMission == ECoverageMission::RampTraverse
		&& RampDirection == ERampDirection::Downhill;
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
		EpisodeOrdinal + 1,
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
	const bool bDiagnosticV2 =
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& (bV2RuntimeSmoke || bV2TrajectoryHoldMission);
	const bool bAcceptedForBalancing =
		!bDiagnosticV2
		&& (!bMissionRequired || bCoverageMissionSucceeded);
	const int32 MissionObservationFrames =
		bCoverageMissionSucceeded
			&& CoverageMissionSuccessFrameIndex != INDEX_NONE
			? CoverageMissionSuccessFrameIndex + 1
			: FrameIndex + 1;
	const int32 PostSuccessObservationFrames =
		bCoverageMissionSucceeded
			&& CoverageMissionSuccessFrameIndex != INDEX_NONE
			? FrameIndex - CoverageMissionSuccessFrameIndex
			: 0;
	const int32 MissionBucket = static_cast<int32>(CoverageMission);
	if (bAcceptedForBalancing
		&& MissionBucket >= 0
		&& MissionBucket < static_cast<int32>(ECoverageMission::Count))
	{
		OverallMissionObservationFrames[MissionBucket] += MissionObservationFrames;
	}
	if (bAcceptedForBalancing
		&& CoverageMission == ECoverageMission::ObjectView)
	{
		OverallObjectModeObservationFrames[static_cast<int32>(ObjectViewMode)]
			+= MissionObservationFrames;
		OverallObjectGazePatternObservationFrames[
			static_cast<int32>(ObjectGazePattern)] += MissionObservationFrames;
		if (CoverageTargetIndex != INDEX_NONE)
		{
			OverallObjectTargetObservationFrames[CoverageTargetIndex]
				+= MissionObservationFrames;
		}
		if (CurrentObjectScenarioIndex >= 0
			&& CurrentObjectScenarioIndex < ObjectScenarioCount)
		{
			OverallObjectScenarioObservationFrames[CurrentObjectScenarioIndex]
				+= MissionObservationFrames;
		}
		if (ObjectViewMode == EObjectViewMode::PartialOrbit
			|| ObjectViewMode == EObjectViewMode::FullOrbit)
		{
			OverallObjectOrbitDirectionObservationFrames[
				bCoverageOrbitClockwise ? 0 : 1] += MissionObservationFrames;
		}
		for (int32 IntentIndex = 0;
			IntentIndex < UE_ARRAY_COUNT(CurrentEpisodeObjectGazeIntentFrames);
			++IntentIndex)
		{
			OverallObjectGazeIntentObservationFrames[IntentIndex]
				+= CurrentEpisodeObjectGazeIntentFrames[IntentIndex];
		}
	}
	else if (bAcceptedForBalancing
		&& CoverageMission == ECoverageMission::ContactRecovery
		&& ContactTargetIndex != INDEX_NONE)
	{
		OverallContactTargetObservationFrames[ContactTargetIndex]
			+= MissionObservationFrames;
		OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(ContactRecoveryStyle)] += MissionObservationFrames;
		OverallContactApproachProfileObservationFrames[
			static_cast<int32>(ContactApproachProfile)] += MissionObservationFrames;
		OverallContactFacingObservationFrames[
			static_cast<int32>(LocomotionFacingProfile)]
			+= MissionObservationFrames;
		const int32 ContactBaseScenarioIndex =
			CurrentContactScenarioIndex
				/ static_cast<int32>(ELocomotionFacingProfile::Count);
		if (ContactBaseScenarioIndex >= 0
			&& ContactBaseScenarioIndex < ContactBaseScenarioCount)
		{
			OverallContactBaseScenarioObservationFrames[
				ContactBaseScenarioIndex] += MissionObservationFrames;
		}
		if (CurrentContactScenarioIndex >= 0
			&& CurrentContactScenarioIndex < ContactScenarioCount)
		{
			OverallContactScenarioObservationFrames[CurrentContactScenarioIndex]
				+= MissionObservationFrames;
		}
		if (LocomotionFacingProfile
			== ELocomotionFacingProfile::FreeAttention)
		{
			OverallGuidedCameraStyleObservationFrames[
				static_cast<int32>(GuidedCameraStyle)]
				+= MissionObservationFrames;
		}
	}
	else if (bAcceptedForBalancing
		&& CoverageMission == ECoverageMission::RampTraverse)
	{
		OverallRampDirectionObservationFrames[
			RampDirection == ERampDirection::Uphill ? 0 : 1]
			+= MissionObservationFrames;
		OverallRampPathObservationFrames[
			static_cast<int32>(RampPathProfile)] += MissionObservationFrames;
		OverallRampFacingObservationFrames[
			static_cast<int32>(LocomotionFacingProfile)]
			+= MissionObservationFrames;
		if (CurrentRampScenarioIndex >= 0
			&& CurrentRampScenarioIndex < RampScenarioCount)
		{
			OverallRampScenarioObservationFrames[CurrentRampScenarioIndex]
				+= MissionObservationFrames;
		}
		if (LocomotionFacingProfile
			== ELocomotionFacingProfile::FreeAttention)
		{
			OverallGuidedCameraStyleObservationFrames[
				static_cast<int32>(GuidedCameraStyle)]
				+= MissionObservationFrames;
		}
	}
	else if (bAcceptedForBalancing
		&& CoverageMission == ECoverageMission::HoopPass)
	{
		OverallHoopDirectionObservationFrames[bHoopPositiveToNegative ? 0 : 1]
			+= MissionObservationFrames;
		OverallHoopPathObservationFrames[
			static_cast<int32>(HoopPathProfile)] += MissionObservationFrames;
		OverallHoopFacingObservationFrames[
			static_cast<int32>(LocomotionFacingProfile)]
			+= MissionObservationFrames;
		if (CurrentHoopScenarioIndex >= 0
			&& CurrentHoopScenarioIndex < HoopScenarioCount)
		{
			OverallHoopScenarioObservationFrames[CurrentHoopScenarioIndex]
				+= MissionObservationFrames;
		}
		if (LocomotionFacingProfile
			== ELocomotionFacingProfile::FreeAttention)
		{
			OverallGuidedCameraStyleObservationFrames[
				static_cast<int32>(GuidedCameraStyle)]
				+= MissionObservationFrames;
		}
	}

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
	const float EpisodeFacingMatchRatio =
		CurrentEpisodeFacingMovingFrames > 0
			? static_cast<float>(CurrentEpisodeFacingMatchedFrames)
				/ static_cast<float>(CurrentEpisodeFacingMovingFrames)
			: 0.0f;
	FString MissionParameters = TEXT("{}");
	if (CoverageMission == ECoverageMission::ObjectView)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"scenario_index\":%d,\"mode\":\"%s\",\"start\":%s,\"goal\":%s,")
			TEXT("\"orbit_radius_cm\":%s,\"clockwise\":%s,\"orbit_direction\":%s,")
			TEXT("\"waypoint_count\":%d,")
			TEXT("\"gaze_pattern\":\"%s\",\"gaze_plan\":%s,")
			TEXT("\"required_azimuth_bins_mask\":%u,")
			TEXT("\"required_azimuth_bin_count\":%d,\"required_visible_hold_steps\":%d,")
			TEXT("\"required_post_objective_steps\":%d,")
			TEXT("\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			CurrentObjectScenarioIndex,
			*GetObjectViewModeSlug(),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageMissionGoal),
			*JsonNumber(CoverageOrbitRadiusCm),
			JsonBool(bCoverageOrbitClockwise),
			ObjectViewMode == EObjectViewMode::PartialOrbit
					|| ObjectViewMode == EObjectViewMode::FullOrbit
				? (bCoverageOrbitClockwise
					? TEXT("\"clockwise\"")
					: TEXT("\"counter_clockwise\""))
				: TEXT("null"),
			CoverageWaypoints.Num(),
			*GetObjectGazePatternSlug(),
			*BuildObjectGazePlanJson(),
			CoverageRequiredAzimuthBinsMask,
			CoverageRequiredAzimuthBinCount,
			CoverageRequiredVisibleHoldSteps,
			CoverageRequiredPostObjectiveSteps,
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	else if (CoverageMission == ECoverageMission::ContactRecovery)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"scenario_index\":%d,\"base_scenario_index\":%d,")
			TEXT("\"recovery_style\":\"%s\",\"approach_profile\":\"%s\",")
			TEXT("\"approach_sector\":%d,\"locomotion_facing_profile\":\"%s\",")
			TEXT("\"free_attention_camera_style\":\"%s\",")
			TEXT("\"start\":%s,\"contact_point\":%s,\"press_goal\":%s,")
			TEXT("\"clearance_goal\":%s,\"recovery_goal\":%s,")
			TEXT("\"required_contact_hold_steps\":%d,\"required_recovery_steps\":%d,")
			TEXT("\"facing_moving_frames\":%d,\"facing_matched_frames\":%d,")
			TEXT("\"facing_match_ratio\":%s,")
			TEXT("\"required_post_objective_steps\":%d,")
			TEXT("\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			CurrentContactScenarioIndex,
			CurrentContactScenarioIndex
				/ static_cast<int32>(ELocomotionFacingProfile::Count),
			*GetContactRecoveryStyleSlug(),
			*GetContactApproachProfileSlug(),
			CurrentContactApproachSector,
			*GetLocomotionFacingProfileSlug(),
			*GetGuidedCameraStyleSlug(),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageContactPoint),
			*JsonVector(CoverageMissionGoal),
			CoverageWaypoints.IsEmpty()
				? TEXT("null")
				: *JsonVector(CoverageWaypoints[0]),
			*JsonVector(CoverageRecoveryGoal),
			CoverageRequiredContactHoldSteps,
			CoverageRequiredRecoverySteps,
			CurrentEpisodeFacingMovingFrames,
			CurrentEpisodeFacingMatchedFrames,
			*JsonNumber(EpisodeFacingMatchRatio),
			CoverageRequiredPostObjectiveSteps,
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"scenario_index\":%d,\"direction\":\"%s\",")
			TEXT("\"path_profile\":\"%s\",\"locomotion_facing_profile\":\"%s\",")
			TEXT("\"free_attention_camera_style\":\"%s\",")
			TEXT("\"start\":%s,\"goal\":%s,\"required_post_objective_steps\":%d,")
			TEXT("\"facing_moving_frames\":%d,\"facing_matched_frames\":%d,")
			TEXT("\"facing_match_ratio\":%s,")
			TEXT("\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s}"),
			CurrentRampScenarioIndex,
			*GetRampDirectionSlug(),
			*GetRampPathProfileSlug(),
			*GetLocomotionFacingProfileSlug(),
			*GetGuidedCameraStyleSlug(),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageMissionGoal),
			CoverageRequiredPostObjectiveSteps,
			CurrentEpisodeFacingMovingFrames,
			CurrentEpisodeFacingMatchedFrames,
			*JsonNumber(EpisodeFacingMatchRatio),
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees));
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		MissionParameters = FString::Printf(
			TEXT("{\"scenario_index\":%d,\"direction\":\"%s\",")
			TEXT("\"path_profile\":\"%s\",\"locomotion_facing_profile\":\"%s\",")
			TEXT("\"free_attention_camera_style\":\"%s\",")
			TEXT("\"start\":%s,\"goal\":%s,")
			TEXT("\"crossing_recorded\":%s,\"crossing_y\":%s,\"crossing_z\":%s,")
			TEXT("\"facing_moving_frames\":%d,\"facing_matched_frames\":%d,")
			TEXT("\"facing_match_ratio\":%s,")
			TEXT("\"required_passages\":1,\"initial_yaw_offset_degrees\":%s,")
			TEXT("\"initial_pitch_offset_degrees\":%s,")
			TEXT("\"required_post_objective_steps\":%d}"),
			CurrentHoopScenarioIndex,
			bHoopPositiveToNegative
				? TEXT("positive_x_to_negative_x")
				: TEXT("negative_x_to_positive_x"),
			*GetHoopPathProfileSlug(),
			*GetLocomotionFacingProfileSlug(),
			*GetGuidedCameraStyleSlug(),
			*JsonVector(CoverageMissionStart),
			*JsonVector(CoverageMissionGoal),
			JsonBool(bCoverageHoopCrossingRecorded),
			*JsonNumber(CoverageLastHoopCrossingY),
			*JsonNumber(CoverageLastHoopCrossingZ),
			CurrentEpisodeFacingMovingFrames,
			CurrentEpisodeFacingMatchedFrames,
			*JsonNumber(EpisodeFacingMatchRatio),
			*JsonNumber(CoverageInitialYawOffsetDegrees),
			*JsonNumber(CoverageInitialPitchOffsetDegrees),
			CoverageRequiredPostObjectiveSteps);
	}
	const FVector CompletionGoal =
		CoverageMission == ECoverageMission::ContactRecovery
			? CoverageRecoveryGoal
			: CoverageMissionGoal;
	const float FinalDistanceToGoalCm =
		bMissionRequired
			? FVector::Dist2D(PreviousState.Position, CompletionGoal)
			: 0.0f;
	EpisodesJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"episode_index\":%d,\"worker_id\":%d,")
		TEXT("\"seed\":%d,\"prescribed\":%s,\"plan_id\":%s,")
		TEXT("\"plan_version\":%s,\"assignment_id\":%s,\"attempt_id\":%s,")
		TEXT("\"executor_id\":%s,\"split\":%s,\"recipe_id\":%s,")
		TEXT("\"v2_contract_version\":%s,\"v2_source\":%s,\"v2_cell_id\":%s,")
		TEXT("\"continuous_sample_ordinal\":%s,\"refinement_level\":%s,")
		TEXT("\"repetition_index\":%s,\"prescribed_scenario_index\":%s,")
		TEXT("\"requested_transitions\":%d,\"actual_transitions\":%d,")
		TEXT("\"observation_count\":%d,\"collection_mission\":\"%s\",")
		TEXT("\"mission_review_slug\":%s,")
		TEXT("\"mission_observation_frames\":%d,")
		TEXT("\"post_success_observation_frames\":%d,")
		TEXT("\"mission_success_frame_index\":%s,\"coverage_target\":%s,")
		TEXT("\"object_view_mode\":%s,\"object_gaze_pattern\":%s,")
		TEXT("\"object_scenario_index\":%s,\"orbit_direction\":%s,")
		TEXT("\"contact_scenario_index\":%s,\"contact_recovery_style\":%s,")
		TEXT("\"contact_approach_profile\":%s,")
		TEXT("\"guided_camera_style\":%s,\"locomotion_facing_profile\":%s,")
		TEXT("\"ramp_scenario_index\":%s,\"ramp_direction\":%s,")
		TEXT("\"ramp_path_profile\":%s,\"hoop_scenario_index\":%s,")
		TEXT("\"hoop_path_profile\":%s,")
		TEXT("\"visited_azimuth_bins_mask\":%u,\"visited_azimuth_bin_count\":%d,")
		TEXT("\"visible_azimuth_bins_mask\":%u,\"required_azimuth_bins_mask\":%u,")
		TEXT("\"required_azimuth_bin_count\":%d,")
		TEXT("\"visible_azimuth_bin_count\":%d,\"visible_hold_steps\":%d,")
		TEXT("\"ramp_traversals\":%d,")
		TEXT("\"hoop_passes\":%d,\"contact_hold_steps\":%d,")
		TEXT("\"verified_contact_steps\":%d,")
		TEXT("\"recovery_steps\":%d,\"primary_objective_achieved\":%s,")
		TEXT("\"post_objective_steps\":%d,\"required_post_objective_steps\":%d,")
		TEXT("\"post_success_steps\":%d,\"required_post_success_steps\":%d,")
		TEXT("\"post_success_style\":%s,")
		TEXT("\"facing_moving_frames\":%d,\"facing_matched_frames\":%d,")
		TEXT("\"facing_match_ratio\":%s,")
		TEXT("\"hoop_crossing_recorded\":%s,")
		TEXT("\"hoop_crossing_y\":%s,\"hoop_crossing_z\":%s,")
		TEXT("\"final_distance_to_goal_cm\":%s,")
		TEXT("\"distance_to_goal_at_success_cm\":%s,")
		TEXT("\"natural_play_contact_escape_count\":%d,")
		TEXT("\"maximum_consecutive_contact_steps\":%d,")
		TEXT("\"accepted_for_balancing\":%s,")
		TEXT("\"mission_required\":%s,\"mission_success\":%s,")
		TEXT("\"mission_parameters\":%s,\"termination_reason\":\"%s\"}\n"),
		*MakeEpisodeId(),
		EpisodeIndex,
		WorkerId,
		SeedStart + EpisodeIndex,
		JsonBool(bPrescribedRecipes),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *PlanId.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *PlanVersion.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *AssignmentId.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *AttemptId.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *ExecutorId.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *DatasetSplit.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		bPrescribedRecipes
			? *FString::Printf(TEXT("\"%s\""), *CurrentRecipeId.ReplaceCharWithEscapedChar())
			: TEXT("null"),
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
			? TEXT("\"v2-data-generation-spec-2\"")
			: TEXT("null"),
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
			? TEXT("\"random_play\"")
			: TEXT("null"),
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
			? (bV2TrajectoryHoldMission
				? TEXT("\"R08_throw_hold_cooldown_diagnostic\"")
				: TEXT("\"runtime_smoke_unqualified\""))
			: TEXT("null"),
		bPrescribedRecipes ? *FString::FromInt(CurrentContinuousSampleOrdinal) : TEXT("null"),
		bPrescribedRecipes ? *FString::FromInt(CurrentRefinementLevel) : TEXT("null"),
		bPrescribedRecipes ? *FString::FromInt(CurrentRepetitionIndex) : TEXT("null"),
		bPrescribedRecipes ? *FString::FromInt(CurrentPrescribedScenarioIndex) : TEXT("null"),
		TransitionsPerEpisode,
		FrameIndex,
		FrameIndex + 1,
		*GetCoverageMissionSlug(),
		bV2TrajectoryHoldMission
			? *FString::Printf(
				TEXT("\"v2-r08-trajectory-hold-e%06d\""),
				EpisodeIndex)
			: (bMissionReviewSuite
				? *FString::Printf(TEXT("\"%s\""), *GetMissionReviewSlug())
				: TEXT("null")),
		MissionObservationFrames,
		PostSuccessObservationFrames,
		CoverageMissionSuccessFrameIndex != INDEX_NONE
			? *FString::FromInt(CoverageMissionSuccessFrameIndex)
			: TEXT("null"),
		(CoverageTargetIndex != INDEX_NONE || ContactTargetIndex != INDEX_NONE)
			? *FString::Printf(TEXT("\"%s\""), *GetCoverageTargetSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetObjectViewModeSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetObjectGazePatternSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::FromInt(CurrentObjectScenarioIndex)
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
				&& (ObjectViewMode == EObjectViewMode::PartialOrbit
					|| ObjectViewMode == EObjectViewMode::FullOrbit)
			? (bCoverageOrbitClockwise
				? TEXT("\"clockwise\"")
				: TEXT("\"counter_clockwise\""))
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::FromInt(CurrentContactScenarioIndex)
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::Printf(TEXT("\"%s\""), *GetContactRecoveryStyleSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::Printf(TEXT("\"%s\""), *GetContactApproachProfileSlug())
			: TEXT("null"),
		CoverageMission != ECoverageMission::SemiMarkov
				&& CoverageMission != ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetGuidedCameraStyleSlug())
			: TEXT("null"),
		bCoverageFacingProfileRequired
			? *FString::Printf(
				TEXT("\"%s\""),
				*GetLocomotionFacingProfileSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::RampTraverse
			? *FString::FromInt(CurrentRampScenarioIndex)
			: TEXT("null"),
		CoverageMission == ECoverageMission::RampTraverse
			? *FString::Printf(TEXT("\"%s\""), *GetRampDirectionSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::RampTraverse
			? *FString::Printf(TEXT("\"%s\""), *GetRampPathProfileSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::HoopPass
			? *FString::FromInt(CurrentHoopScenarioIndex)
			: TEXT("null"),
		CoverageMission == ECoverageMission::HoopPass
			? *FString::Printf(TEXT("\"%s\""), *GetHoopPathProfileSlug())
			: TEXT("null"),
		CurrentEpisodeVisitedBinsMask,
		FMath::CountBits(static_cast<uint32>(CurrentEpisodeVisitedBinsMask)),
		CurrentEpisodeViewBinsMask,
		CoverageRequiredAzimuthBinsMask,
		CoverageRequiredAzimuthBinCount,
		FMath::CountBits(static_cast<uint32>(CurrentEpisodeViewBinsMask)),
		CoverageVisibleHoldSteps,
		CurrentEpisodeRampTraversals,
		CurrentEpisodeHoopPasses,
		CoverageContactHoldSteps,
		CoverageVerifiedContactSteps,
		CoverageRecoverySteps,
		JsonBool(bCoveragePrimaryObjectiveAchieved),
		CoveragePostObjectiveSteps,
		CoverageRequiredPostObjectiveSteps,
		CoveragePostSuccessSteps,
		CoverageRequiredPostSuccessSteps,
		bMissionRequired
			? *FString::Printf(TEXT("\"%s\""), *GetPostSuccessStyleSlug())
			: TEXT("null"),
		CurrentEpisodeFacingMovingFrames,
		CurrentEpisodeFacingMatchedFrames,
		*JsonNumber(EpisodeFacingMatchRatio),
		JsonBool(bCoverageHoopCrossingRecorded),
		*JsonNumber(CoverageLastHoopCrossingY),
		*JsonNumber(CoverageLastHoopCrossingZ),
		*JsonNumber(FinalDistanceToGoalCm),
		bCoverageMissionSucceeded
			? *JsonNumber(CoverageDistanceToGoalAtSuccessCm)
			: TEXT("null"),
		NaturalPlayEscapeCount,
		NaturalPlayMaximumContactSteps,
		JsonBool(bAcceptedForBalancing),
		JsonBool(bMissionRequired),
		JsonBool(bCoverageMissionSucceeded),
		*MissionParameters,
		TerminationReason);
	bEpisodeActive = false;
	UE_LOG(
		LogTemp,
		Display,
		TEXT("Dataset episode %d/%d completed."),
		EpisodeOrdinal + 1,
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
		if (StorageFormat == EStorageFormat::WebPParquet)
		{
			bWriteSuccess =
				FFileHelper::SaveStringToFile(
					FramesJsonLines,
					*FPaths::Combine(OutputDirectory, TEXT(".frames.jsonl.staging")),
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				&& FFileHelper::SaveStringToFile(
					TransitionsJsonLines,
					*FPaths::Combine(OutputDirectory, TEXT(".transitions.jsonl.staging")),
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				&& FFileHelper::SaveStringToFile(
					EpisodesJsonLines,
					*FPaths::Combine(OutputDirectory, TEXT(".episodes.jsonl.staging")),
					FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM)
				&& TarWriter->Finalize()
				&& bWriteSuccess;
		}
		else
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
				EpisodeOrdinal + (bEpisodeActive ? 1 : 0),
				GlobalTransitionCount,
				GlobalTransitionCount + EpisodeOrdinal + (bEpisodeActive ? 1 : 0));

			bWriteSuccess =
				TarWriter->AddTextFile(TEXT("metadata/frames.jsonl"), FramesJsonLines)
				&& TarWriter->AddTextFile(
					TEXT("metadata/transitions.jsonl"),
					TransitionsJsonLines)
				&& TarWriter->AddTextFile(TEXT("metadata/episodes.jsonl"), EpisodesJsonLines)
				&& TarWriter->AddTextFile(TEXT("manifest.json"), Manifest)
				&& TarWriter->Finalize()
				&& bWriteSuccess;
		}
		bTarFinalized = true;
		delete TarWriter;
		TarWriter = nullptr;
	}

	const bool bNeedsParquetFinalization =
		bWriteSuccess && StorageFormat == EStorageFormat::WebPParquet;
	FString EffectiveError = ErrorMessage;
	if (bNeedsParquetFinalization)
	{
		EffectiveError = TEXT("Parquet finalization required.");
	}
	else if (!bWriteSuccess && EffectiveError.IsEmpty())
	{
		EffectiveError = TEXT("Failed while finalizing dataset output.");
	}
	const FString DatasetJson = BuildDatasetJson(
		bWriteSuccess && !bNeedsParquetFinalization,
		EffectiveError);
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
		if (StorageFormat == EStorageFormat::WebPParquet)
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("Dataset capture completed; Parquet finalization is required. Output: %s"),
				*OutputDirectory);
		}
		else
		{
			UE_LOG(
				LogTemp,
				Display,
				TEXT("Dataset generation completed. Output: %s"),
				*OutputDirectory);
		}
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
		FPlatformMisc::RequestExitWithStatus(false, bWriteSuccess ? 0 : 1);
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
	if (CurriculumStage == ECurriculumStage::LegacyThrow
		|| CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
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

	const FString ImageKey = MakeImageKey(ObservationIndex);
	bool bImageWritten = false;
	if (StorageFormat == EStorageFormat::WebPParquet)
	{
#if CURRICULUM_WITH_LIBWEBP
		WebPConfig Config;
		WebPPicture Picture;
		WebPMemoryWriter Writer;
		const bool bConfigReady =
			WebPConfigInit(&Config) != 0
			&& WebPConfigLosslessPreset(&Config, WebPLosslessEffort) != 0;
		const bool bPictureReady = WebPPictureInit(&Picture) != 0;
		if (bConfigReady && bPictureReady)
		{
			Picture.use_argb = 1;
			Picture.width = CaptureWidth;
			Picture.height = CaptureHeight;
			Picture.writer = WebPMemoryWrite;
			Picture.custom_ptr = &Writer;
			WebPMemoryWriterInit(&Writer);
			const bool bEncoded =
				WebPPictureImportBGRA(
					&Picture,
					reinterpret_cast<const uint8*>(Pixels.GetData()),
					CaptureWidth * static_cast<int32>(sizeof(FColor)))
				&& WebPEncode(&Config, &Picture);
			if (bEncoded && Writer.mem && Writer.size > 0)
			{
				bImageWritten = TarWriter->AddFile(
					ImageKey,
					Writer.mem,
					static_cast<int64>(Writer.size));
			}
			WebPMemoryWriterClear(&Writer);
		}
		if (bPictureReady)
		{
			WebPPictureFree(&Picture);
		}
#endif
	}
	else
	{
		TArray64<uint8> CompressedPng;
		FImageUtils::PNGCompressImageArray(
			CaptureWidth,
			CaptureHeight,
			Pixels,
			CompressedPng);
		bImageWritten = !CompressedPng.IsEmpty()
			&& TarWriter->AddFile(
				ImageKey,
				CompressedPng.GetData(),
				CompressedPng.Num());
	}
	if (!bImageWritten)
	{
		LastError = FString::Printf(TEXT("Could not add image to shard: %s"), *ImageKey);
		return false;
	}

	OutState.Position = Character->GetActorLocation();
	OutState.Velocity = Character->GetVelocity();
	OutState.CameraRotation =
		Character->GetControlRotation().GetNormalized();
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
	const bool bAimLockActive =
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& bQVisible;
	int32 FlyingGrenadeCount = 0;
	int32 RestingGrenadeCount = 0;
	int32 VisibleGrenadeCount = 0;
	const FTransform CameraTransform = PlayerCamera->GetComponentTransform();
	for (const FGeneratedGrenade& Grenade : Grenades)
	{
		if (Grenade.State.bMotionStopped)
		{
			++RestingGrenadeCount;
		}
		else
		{
			++FlyingGrenadeCount;
		}
		FVector2D ProjectedGrenade;
		if (ProjectToCapture(
				Grenade.State.Position,
				CameraTransform,
				PlayerCamera->FieldOfView,
				CaptureWidth,
				CaptureHeight,
				ProjectedGrenade))
		{
			FHitResult VisibilityHit;
			FCollisionQueryParams VisibilityQuery(
				SCENE_QUERY_STAT(CurriculumGrenadeVisibility),
				false,
				Character);
			const FVector ViewLocation = CameraTransform.GetLocation();
			const bool bOccluded = GetWorld()->LineTraceSingleByChannel(
				VisibilityHit,
				ViewLocation,
				Grenade.State.Position,
				ECC_Visibility,
				VisibilityQuery)
				&& VisibilityHit.Distance
					< FVector::Distance(ViewLocation, Grenade.State.Position)
						- FMath::Max(1.0f, GrenadeSimConfig.RadiusCm);
			if (!bOccluded)
			{
				++VisibleGrenadeCount;
			}
		}
	}
	UpdatePitchMetrics(OutState.CameraRotation.Pitch);
	UpdateCoverageMetrics(OutState, ObservationIndex);
	const FString GrenadesJson = BuildGrenadesJson();
	FramesJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"frame_index\":%d,\"simulation_step\":%d,")
		TEXT("\"rgb_key\":\"%s\",\"position\":{\"x\":%s,\"y\":%s,\"z\":%s},")
		TEXT("\"velocity\":{\"x\":%s,\"y\":%s,\"z\":%s},")
		TEXT("\"camera\":{\"yaw\":%s,\"pitch\":%s,\"roll\":0.0},")
		TEXT("\"grounded\":%s,\"contact\":%s,\"contact_object\":%s,")
		TEXT("\"crosshair_state\":\"%s\",")
		TEXT("\"cooldown_remaining_steps\":%d,\"q_visibility\":%s,")
		TEXT("\"aim_lock_active\":%s,\"trajectory_visible\":%s,")
		TEXT("\"flying_grenade_count\":%d,\"resting_grenade_count\":%d,")
		TEXT("\"visible_grenade_count\":%d,\"total_grenade_count\":%d,")
		TEXT("\"v2_episode_phase\":\"%s\",\"grenades\":%s,")
		TEXT("\"collection_mission\":\"%s\",\"mission_phase\":\"%s\",")
		TEXT("\"mission_success_frame_index\":%s,\"coverage_target\":%s,")
		TEXT("\"mission_review_slug\":%s,")
		TEXT("\"object_view_mode\":%s,\"object_gaze_pattern\":%s,")
		TEXT("\"object_gaze_intent\":%s,\"object_gaze_target\":%s,")
		TEXT("\"orbit_direction\":%s,\"contact_phase\":%s,")
		TEXT("\"contact_recovery_style\":%s,\"contact_approach_profile\":%s,")
		TEXT("\"guided_camera_style\":%s,\"locomotion_facing_profile\":%s,")
		TEXT("\"ramp_direction\":%s,\"ramp_path_profile\":%s,")
		TEXT("\"hoop_path_profile\":%s,")
		TEXT("\"coverage_target_visible\":%s,\"coverage_position_azimuth_bin\":%s,")
		TEXT("\"coverage_position_distance_band\":%s,\"coverage_waypoint_index\":%d,")
		TEXT("\"visited_azimuth_bins_mask\":%u,")
		TEXT("\"required_azimuth_bins_mask\":%u,")
		TEXT("\"required_azimuth_bin_count\":%d,\"visible_hold_steps\":%d,")
		TEXT("\"pitch_band\":%d,")
		TEXT("\"ramp_traversals\":%d,\"hoop_passes\":%d,")
		TEXT("\"contact_hold_steps\":%d,\"verified_contact_steps\":%d,")
		TEXT("\"recovery_steps\":%d,\"primary_objective_achieved\":%s,")
		TEXT("\"post_objective_steps\":%d,\"required_post_objective_steps\":%d,")
		TEXT("\"post_success_steps\":%d,\"required_post_success_steps\":%d,")
		TEXT("\"post_success_style\":%s,")
		TEXT("\"facing_moving_frames\":%d,\"facing_matched_frames\":%d,")
		TEXT("\"facing_match_ratio\":%s,")
		TEXT("\"movement_camera_yaw_delta_degrees\":%s,")
		TEXT("\"hoop_crossing_recorded\":%s,")
		TEXT("\"hoop_crossing_y\":%s,\"hoop_crossing_z\":%s,")
		TEXT("\"mission_success\":%s,\"mission_failed\":%s,")
		TEXT("\"no_progress_steps\":%d,")
		TEXT("\"natural_play_contact_steps\":%d,")
		TEXT("\"natural_play_contact_limit_steps\":%d,")
		TEXT("\"natural_play_escape_active\":%s}\n"),
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
		JsonBool(bAimLockActive),
		JsonBool(bQVisible),
		FlyingGrenadeCount,
		RestingGrenadeCount,
		VisibleGrenadeCount,
		Grenades.Num(),
		*GetV2EpisodePhaseSlug(),
		*GrenadesJson,
		*GetCoverageMissionSlug(),
		*GetMissionPhaseSlug(),
		CoverageMissionSuccessFrameIndex != INDEX_NONE
			? *FString::FromInt(CoverageMissionSuccessFrameIndex)
			: TEXT("null"),
		(CoverageTargetIndex != INDEX_NONE || ContactTargetIndex != INDEX_NONE)
			? *FString::Printf(TEXT("\"%s\""), *GetCoverageTargetSlug())
			: TEXT("null"),
		bV2TrajectoryHoldMission
			? *FString::Printf(
				TEXT("\"v2-r08-trajectory-hold-e%06d\""),
				EpisodeIndex)
			: (bMissionReviewSuite
				? *FString::Printf(TEXT("\"%s\""), *GetMissionReviewSlug())
				: TEXT("null")),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetObjectViewModeSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetObjectGazePatternSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *FString::Printf(
				TEXT("\"%s\""),
				*GetObjectGazeIntentSlug(CurrentObjectGazeIntent))
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
			? *JsonVector(CurrentObjectGazeTarget)
			: TEXT("null"),
		CoverageMission == ECoverageMission::ObjectView
				&& (ObjectViewMode == EObjectViewMode::PartialOrbit
					|| ObjectViewMode == EObjectViewMode::FullOrbit)
			? (bCoverageOrbitClockwise
				? TEXT("\"clockwise\"")
				: TEXT("\"counter_clockwise\""))
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::Printf(TEXT("\"%s\""), *GetContactPhaseSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::Printf(TEXT("\"%s\""), *GetContactRecoveryStyleSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::ContactRecovery
			? *FString::Printf(TEXT("\"%s\""), *GetContactApproachProfileSlug())
			: TEXT("null"),
		CoverageMission != ECoverageMission::SemiMarkov
				&& CoverageMission != ECoverageMission::ObjectView
			? *FString::Printf(TEXT("\"%s\""), *GetGuidedCameraStyleSlug())
			: TEXT("null"),
		bCoverageFacingProfileRequired
			? *FString::Printf(
				TEXT("\"%s\""),
				*GetLocomotionFacingProfileSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::RampTraverse
			? *FString::Printf(TEXT("\"%s\""), *GetRampDirectionSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::RampTraverse
			? *FString::Printf(TEXT("\"%s\""), *GetRampPathProfileSlug())
			: TEXT("null"),
		CoverageMission == ECoverageMission::HoopPass
			? *FString::Printf(TEXT("\"%s\""), *GetHoopPathProfileSlug())
			: TEXT("null"),
		JsonBool(bCurrentCoverageTargetVisible),
		CurrentCoveragePositionBin != INDEX_NONE
			? *FString::FromInt(CurrentCoveragePositionBin)
			: TEXT("null"),
		CurrentCoveragePositionDistanceBand != INDEX_NONE
			? *FString::FromInt(CurrentCoveragePositionDistanceBand)
			: TEXT("null"),
		CoverageWaypointIndex,
		CurrentEpisodeVisitedBinsMask,
		CoverageRequiredAzimuthBinsMask,
		CoverageRequiredAzimuthBinCount,
		CoverageVisibleHoldSteps,
		GetPitchBandIndex(OutState.CameraRotation.Pitch),
		CurrentEpisodeRampTraversals,
		CurrentEpisodeHoopPasses,
		CoverageContactHoldSteps,
		CoverageVerifiedContactSteps,
		CoverageRecoverySteps,
		JsonBool(bCoveragePrimaryObjectiveAchieved),
		CoveragePostObjectiveSteps,
		CoverageRequiredPostObjectiveSteps,
		CoveragePostSuccessSteps,
		CoverageRequiredPostSuccessSteps,
		CoverageMission != ECoverageMission::SemiMarkov
			? *FString::Printf(TEXT("\"%s\""), *GetPostSuccessStyleSlug())
			: TEXT("null"),
		CurrentEpisodeFacingMovingFrames,
		CurrentEpisodeFacingMatchedFrames,
		*JsonNumber(
			CurrentEpisodeFacingMovingFrames > 0
				? static_cast<float>(CurrentEpisodeFacingMatchedFrames)
					/ static_cast<float>(CurrentEpisodeFacingMovingFrames)
				: 0.0f),
		*JsonNumber(CurrentMovementCameraYawDeltaDegrees),
		JsonBool(bCoverageHoopCrossingRecorded),
		*JsonNumber(CoverageLastHoopCrossingY),
		*JsonNumber(CoverageLastHoopCrossingZ),
		JsonBool(bCoverageMissionSucceeded),
		JsonBool(bCoverageMissionFailed),
		CoverageNoProgressSteps,
		NaturalPlayContactSteps,
		NaturalPlayContactLimitSteps,
		JsonBool(bNaturalPlayEscapeActionActive));
	bQVisibleInLatestObservation =
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& bQVisible;
	return true;
}

void ACurriculumDataGenerator::AppendTransition(
	const int32 SourceFrameIndex,
	const uint16 ActionMask,
	const FRecordedState& SourceState,
	const FRecordedState& TargetState)
{
	const bool bAimLockActive =
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& (ActionMask & CurriculumAction::Q) != 0;
	const float ForwardAxis = bAimLockActive
		? 0.0f
		: CurriculumAction::ForwardAxis(ActionMask);
	const float RightAxis = bAimLockActive
		? 0.0f
		: CurriculumAction::RightAxis(ActionMask);
	const float PitchAxis = CurriculumAction::PitchAxis(ActionMask);
	const float YawAxis = CurriculumAction::YawAxis(ActionMask);

	TransitionsJsonLines += FString::Printf(
		TEXT("{\"episode_id\":\"%s\",\"source_frame_index\":%d,\"action_mask\":%u,")
		TEXT("\"w\":%s,\"a\":%s,\"s\":%s,\"d\":%s,")
		TEXT("\"arrow_up\":%s,\"arrow_down\":%s,\"arrow_left\":%s,\"arrow_right\":%s,")
		TEXT("\"q\":%s,\"e\":%s,\"forward_axis\":%s,\"right_axis\":%s,")
		TEXT("\"pitch_axis\":%s,\"yaw_axis\":%s,\"e_request_edge\":%s,")
		TEXT("\"e_accepted\":%s,\"planar_movement_suppressed\":%s,")
		TEXT("\"q_rising_edge\":%s,\"q_falling_edge\":%s,")
		TEXT("\"e_rejection_reason\":\"%s\",\"accepted_throw_grenade_id\":%s,")
		TEXT("\"cooldown_before_steps\":%d,\"cooldown_after_steps\":%d,")
		TEXT("\"cooldown_remaining_steps\":%d,")
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
		JsonBool(bCurrentPlanarMovementSuppressed),
		JsonBool(bCurrentQRising),
		JsonBool(bCurrentQFalling),
		V2ActionSemantics::GetRejectionReasonSlug(CurrentERejectionReason),
		CurrentAcceptedGrenadeId != INDEX_NONE
			? *FString::FromInt(CurrentAcceptedGrenadeId)
			: TEXT("null"),
		CurrentCooldownBeforeSteps,
		CurrentCooldownAfterSteps,
		CurrentCooldownAfterSteps);
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
	if (bPrescribedRecipes
		&& CoverageMission == ECoverageMission::SemiMarkov
		&& FrameIndex == 0
		&& CurrentPrescribedScenarioIndex >= 0)
	{
		switch ((CurrentPrescribedScenarioIndex / 4) % 8)
		{
		case 0: return 0;
		case 1: return CurriculumAction::W;
		case 2: return SampleParameterBool(TEXT("prescribed_strafe"))
			? CurriculumAction::A : CurriculumAction::D;
		case 3: return SampleParameterBool(TEXT("prescribed_yaw"))
			? CurriculumAction::ArrowLeft : CurriculumAction::ArrowRight;
		case 4: return SampleParameterBool(TEXT("prescribed_pitch"))
			? CurriculumAction::ArrowUp : CurriculumAction::ArrowDown;
		case 5: return CurriculumAction::W
			| (SampleParameterBool(TEXT("prescribed_move_camera"))
				? CurriculumAction::ArrowLeft : CurriculumAction::ArrowRight);
		case 6: return CurriculumAction::W | CurriculumAction::S;
		default: return SelectMovementBits(true);
		}
	}
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
	if (bPrescribedRecipes
		&& CoverageMission == ECoverageMission::SemiMarkov
		&& FrameIndex == 0
		&& CurrentPrescribedScenarioIndex >= 0)
	{
		switch (CurrentPrescribedScenarioIndex % 4)
		{
		case 0: return 2;
		case 1: return 8;
		case 2: return 24;
		default: return 60;
		}
	}
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
	HeldCameraPitchTargetDegrees = SelectPitchTargetDegrees();
	const float CurrentPitch =
		Character && Character->GetController()
			? FRotator::NormalizeAxis(
				Character->GetController()->GetControlRotation().Pitch)
			: 0.0f;
	const float PitchDeadZone =
		37.5f / static_cast<float>(FMath::Max(1, ObservationRate));
	uint16 PitchBit = 0;
	if (HeldCameraPitchTargetDegrees > CurrentPitch + PitchDeadZone)
	{
		PitchBit = CurriculumAction::ArrowUp;
	}
	else if (HeldCameraPitchTargetDegrees < CurrentPitch - PitchDeadZone)
	{
		PitchBit = CurriculumAction::ArrowDown;
	}

	switch (EpisodeRandom.RandRange(0, 5))
	{
	case 0: return CurriculumAction::ArrowLeft;
	case 1: return CurriculumAction::ArrowRight;
	case 2:
	case 3:
		return PitchBit;
	case 4:
		return CurriculumAction::ArrowLeft | PitchBit;
	default:
		return CurriculumAction::ArrowRight | PitchBit;
	}
}

float ACurriculumDataGenerator::SelectPitchTargetDegrees()
{
	float MinimumPitch = -89.9f;
	float MaximumPitch = 89.9f;
	if (Character)
	{
		Character->GetCurriculumCameraPitchLimits(MinimumPitch, MaximumPitch);
	}
	const auto SampleClampedRange =
		[this, MinimumPitch, MaximumPitch](float Minimum, float Maximum)
		{
			Minimum = FMath::Clamp(Minimum, MinimumPitch, MaximumPitch);
			Maximum = FMath::Clamp(Maximum, MinimumPitch, MaximumPitch);
			if (Minimum > Maximum)
			{
				Swap(Minimum, Maximum);
			}
			return FMath::IsNearlyEqual(Minimum, Maximum)
				? Minimum
				: EpisodeRandom.FRandRange(Minimum, Maximum);
		};
	const int32 PitchBand = SelectFrameDeficitBucket(
		OverallPitchBandObservationFrames,
		PitchBandFrameShares,
		UE_ARRAY_COUNT(PitchBandFrameShares),
		FMath::Max(1, ObservationRate),
		GetParameterBits(TEXT("pitch_band_tie"), FrameIndex));
	const bool bDownward = EpisodeRandom.FRand() < 0.58f;
	switch (PitchBand)
	{
	case 1:
		return bDownward
			? SampleClampedRange(-40.0f, -15.0f)
			: SampleClampedRange(15.0f, 40.0f);
	case 2:
		return bDownward
			? SampleClampedRange(MinimumPitch + 10.0f, -40.0f)
			: SampleClampedRange(40.0f, MaximumPitch - 10.0f);
	case 3:
		return bDownward
			? SampleClampedRange(MinimumPitch, MinimumPitch + 10.0f)
			: SampleClampedRange(MaximumPitch - 10.0f, MaximumPitch);
	default:
		// Average two uniform samples to concentrate ordinary views near level,
		// with the intended small downward gameplay bias.
		return FMath::Clamp(
			((EpisodeRandom.FRandRange(-15.0f, 15.0f)
				+ EpisodeRandom.FRandRange(-15.0f, 15.0f)) * 0.5f) - 2.0f,
			FMath::Max(-15.0f, MinimumPitch),
			FMath::Min(15.0f, MaximumPitch));
	}
}

int32 ACurriculumDataGenerator::GetPitchBandIndex(const float PitchDegrees) const
{
	float MinimumPitch = -89.9f;
	float MaximumPitch = 89.9f;
	if (Character)
	{
		Character->GetCurriculumCameraPitchLimits(MinimumPitch, MaximumPitch);
	}
	const float NormalizedPitch = FRotator::NormalizeAxis(PitchDegrees);
	const float AbsolutePitch = FMath::Abs(NormalizedPitch);
	if (AbsolutePitch <= 15.0f)
	{
		return 0;
	}
	if (AbsolutePitch <= 40.0f)
	{
		return 1;
	}
	if (NormalizedPitch <= MinimumPitch + 10.0f
		|| NormalizedPitch >= MaximumPitch - 10.0f)
	{
		return 3;
	}
	return 2;
}

void ACurriculumDataGenerator::UpdatePitchMetrics(const float PitchDegrees)
{
	++OverallPitchBandObservationFrames[GetPitchBandIndex(PitchDegrees)];
}

uint16 ACurriculumDataGenerator::BalancePitchAction(uint16 ActionMask)
{
	if (!Character || !Character->GetController())
	{
		return ActionMask;
	}

	const float CurrentPitch = FRotator::NormalizeAxis(
		Character->GetController()->GetControlRotation().Pitch);
	const int32 CurrentBand = GetPitchBandIndex(CurrentPitch);
	int64 TotalPitchFrames = 0;
	for (const int64 BandFrames : OverallPitchBandObservationFrames)
	{
		TotalPitchFrames += BandFrames;
	}

	// A rare pitch target can otherwise remain on screen for an entire long
	// movement hold after the requested angle has been reached. Once a
	// non-eye-height band has supplied its current frame budget, immediately
	// steer back toward the ordinary gameplay band. This feedback is based on
	// recorded observation frames, not on how often an action was sampled.
	if (CurrentBand > 0)
	{
		const int64 AllowedCurrentBandFrames = FMath::Max<int64>(
			1,
			FMath::CeilToInt64(
				static_cast<double>(FMath::Max<int64>(1, TotalPitchFrames + 1))
				* PitchBandFrameShares[CurrentBand]));
		if (OverallPitchBandObservationFrames[CurrentBand]
			>= AllowedCurrentBandFrames)
		{
			HeldCameraPitchTargetDegrees = -2.0f;
			ActionMask &= ~(CurriculumAction::ArrowUp | CurriculumAction::ArrowDown);
		}
	}

	const bool bArrowUp =
		(ActionMask & CurriculumAction::ArrowUp) != 0;
	const bool bArrowDown =
		(ActionMask & CurriculumAction::ArrowDown) != 0;
	if (bArrowUp && bArrowDown)
	{
		// Preserve deliberately contradictory input examples. Their pitch axis
		// is zero and they cannot cause target overshoot.
		return ActionMask;
	}

	const float PitchDeadZone =
		37.5f / static_cast<float>(FMath::Max(1, ObservationRate));
	if (HeldCameraPitchTargetDegrees > CurrentPitch + PitchDeadZone)
	{
		ActionMask &= ~CurriculumAction::ArrowDown;
		ActionMask |= CurriculumAction::ArrowUp;
	}
	else if (HeldCameraPitchTargetDegrees < CurrentPitch - PitchDeadZone)
	{
		ActionMask &= ~CurriculumAction::ArrowUp;
		ActionMask |= CurriculumAction::ArrowDown;
	}
	else
	{
		ActionMask &= ~(CurriculumAction::ArrowUp | CurriculumAction::ArrowDown);
	}
	return ActionMask;
}

uint64 ACurriculumDataGenerator::GetParameterBits(
	const TCHAR* ParameterName,
	const int32 SampleIndex) const
{
	uint64 Key = static_cast<uint64>(static_cast<uint32>(SeedStart));
	if (!bPrescribedRecipes)
	{
		Key ^= static_cast<uint64>(static_cast<uint32>(WorkerId)) << 32;
	}
	Key = MixParameterBits(
		Key ^ static_cast<uint64>(static_cast<uint32>(EpisodeIndex)));
	Key ^= HashParameterName(ParameterName);
	Key ^= MixParameterBits(
		static_cast<uint64>(static_cast<uint32>(SampleIndex))
			+ 0xD1B54A32D192ED03ull);
	return MixParameterBits(Key);
}

float ACurriculumDataGenerator::SampleParameterUnit(
	const TCHAR* ParameterName,
	const int32 SampleIndex) const
{
	// Use the high 24 bits so the conversion is stable and exactly representable
	// by a float. The interval is [0, 1), never the inclusive upper endpoint.
	const uint32 Mantissa =
		static_cast<uint32>(GetParameterBits(ParameterName, SampleIndex) >> 40);
	return static_cast<float>(Mantissa)
		/ static_cast<float>(1u << 24);
}

float ACurriculumDataGenerator::SampleParameterRange(
	const TCHAR* ParameterName,
	const float Minimum,
	const float Maximum,
	const int32 SampleIndex) const
{
	return FMath::Lerp(
		Minimum,
		Maximum,
		SampleParameterUnit(ParameterName, SampleIndex));
}

int32 ACurriculumDataGenerator::SampleParameterIndex(
	const TCHAR* ParameterName,
	const int32 Count,
	const int32 SampleIndex) const
{
	if (Count <= 1)
	{
		return 0;
	}
	return static_cast<int32>(
		GetParameterBits(ParameterName, SampleIndex)
			% static_cast<uint64>(Count));
}

bool ACurriculumDataGenerator::SampleParameterBool(
	const TCHAR* ParameterName,
	const int32 SampleIndex) const
{
	return (GetParameterBits(ParameterName, SampleIndex) & 1ull) != 0;
}

float ACurriculumDataGenerator::SampleStratifiedRange(
	const TCHAR* ParameterName,
	const int32 BinCount,
	const float Minimum,
	const float Maximum,
	const int32 SampleIndex) const
{
	const int32 SafeBinCount = FMath::Max(1, BinCount);
	if (bPrescribedRecipes)
	{
		// A digitally shifted base-2 radical-inverse sequence is nested: every
		// larger prefix refines the same earlier prefix. Parameter-specific digital
		// shifts decorrelate dimensions without changing that prefix property.
		uint32 Bits = static_cast<uint32>(
			FMath::Max(0, CurrentContinuousSampleOrdinal) + 1);
		uint32 Reversed = 0;
		for (int32 BitIndex = 0; BitIndex < 24; ++BitIndex)
		{
			Reversed = (Reversed << 1) | (Bits & 1u);
			Bits >>= 1;
		}
		const uint32 Shift = static_cast<uint32>(
			MixParameterBits(
				HashParameterName(ParameterName)
				^ static_cast<uint64>(static_cast<uint32>(SampleIndex)))
			& 0x00ffffffull);
		const float ProgressiveUnit = static_cast<float>((Reversed ^ Shift) & 0x00ffffffu)
			/ static_cast<float>(1u << 24);
		const int32 BinIndex = FMath::Min(
			SafeBinCount - 1,
			FMath::FloorToInt(ProgressiveUnit * static_cast<float>(SafeBinCount)));
		const float BinWidth =
			(Maximum - Minimum) / static_cast<float>(SafeBinCount);
		return Minimum
			+ ((static_cast<float>(BinIndex)
				+ SampleParameterUnit(ParameterName, SampleIndex)) * BinWidth);
	}
	const uint64 NameHash = HashParameterName(ParameterName);
	int32 Stride =
		1 + static_cast<int32>((NameHash >> 17) % static_cast<uint64>(SafeBinCount));
	while (GreatestCommonDivisor(Stride, SafeBinCount) != 1)
	{
		Stride = (Stride % SafeBinCount) + 1;
	}
	const int32 Offset =
		static_cast<int32>(NameHash % static_cast<uint64>(SafeBinCount));
	const int32 CycleIndex =
		FMath::Abs(SeedStart + EpisodeIndex + (SampleIndex * 17));
	const int32 BinIndex =
		((CycleIndex * Stride) + Offset) % SafeBinCount;
	const float BinWidth =
		(Maximum - Minimum) / static_cast<float>(SafeBinCount);
	return Minimum
		+ ((static_cast<float>(BinIndex)
			+ SampleParameterUnit(ParameterName, SampleIndex + 104729))
			* BinWidth);
}

void ACurriculumDataGenerator::BuildTransitionScript()
{
	ActionScriptMasks.Reset();
	ActionScriptHoldSteps.Reset();
	ActionScriptIndex = 0;
	ActionScriptStepsRemaining = 0;

	const auto AddSegment =
		[this](const uint16 Mask, const int32 MinimumSteps, const int32 MaximumSteps)
		{
			ActionScriptMasks.Add(Mask);
			ActionScriptHoldSteps.Add(EpisodeRandom.RandRange(MinimumSteps, MaximumSteps));
		};

	switch (EpisodeRandom.RandRange(0, 7))
	{
	case 0:
		AddSegment(0, 1, 3);
		AddSegment(CurriculumAction::W, 3, 10);
		AddSegment(0, 1, 3);
		break;
	case 1:
		AddSegment(CurriculumAction::W, 3, 8);
		AddSegment(CurriculumAction::S, 2, 6);
		break;
	case 2:
		AddSegment(CurriculumAction::A, 3, 8);
		AddSegment(CurriculumAction::D, 2, 6);
		break;
	case 3:
	{
		const uint16 Strafe =
			SampleParameterBool(
				TEXT("script_ramp_strafe_direction"),
				ActionScriptIndex)
				? CurriculumAction::A
				: CurriculumAction::D;
		AddSegment(CurriculumAction::W, 2, 6);
		AddSegment(CurriculumAction::W | Strafe, 2, 6);
		AddSegment(Strafe, 2, 6);
		break;
	}
	case 4:
	{
		const bool bLeftFirst =
			SampleParameterBool(
				TEXT("script_camera_sweep_direction"),
				ActionScriptIndex);
		AddSegment(
			bLeftFirst
				? CurriculumAction::ArrowLeft
				: CurriculumAction::ArrowRight,
			2,
			6);
		AddSegment(0, 1, 3);
		AddSegment(
			bLeftFirst
				? CurriculumAction::ArrowRight
				: CurriculumAction::ArrowLeft,
			2,
			6);
		break;
	}
	case 5:
	{
		const uint16 Yaw =
			SampleParameterBool(
				TEXT("script_forward_camera_direction"),
				ActionScriptIndex)
				? CurriculumAction::ArrowLeft
				: CurriculumAction::ArrowRight;
		AddSegment(CurriculumAction::W, 2, 6);
		AddSegment(CurriculumAction::W | Yaw, 3, 10);
		AddSegment(Yaw, 2, 6);
		break;
	}
	case 6:
	{
		const bool bMirror =
			SampleParameterBool(
				TEXT("script_diagonal_pair_direction"),
				ActionScriptIndex);
		AddSegment(
			CurriculumAction::S
				| (bMirror ? CurriculumAction::A : CurriculumAction::D),
			2,
			6);
		AddSegment(
			CurriculumAction::W
				| (bMirror ? CurriculumAction::D : CurriculumAction::A),
			2,
			6);
		break;
	}
	default:
		AddSegment(CurriculumAction::ArrowUp, 1, 4);
		AddSegment(CurriculumAction::ArrowDown, 1, 4);
		AddSegment(0, 1, 3);
		break;
	}
}

FVector ACurriculumDataGenerator::GetNaturalPlayEscapeDirection(
	const FRecordedState& State) const
{
	if (State.bContact && !State.ContactObject.IsEmpty())
	{
		for (int32 TargetIndex = 0; TargetIndex < 9; ++TargetIndex)
		{
			const FContactTargetDefinition& Target =
				GetContactTargetDefinition(TargetIndex);
			if (State.ContactObject != Target.ActorTag.ToString())
			{
				continue;
			}

			if (Target.bWall)
			{
				return Target.WallInwardNormal.GetSafeNormal2D();
			}

			const FVector AwayFromObject =
				(State.Position - Target.LookTarget).GetSafeNormal2D();
			if (!AwayFromObject.IsNearlyZero())
			{
				return AwayFromObject;
			}
		}
	}

	const FVector OppositeVelocity = (-State.Velocity).GetSafeNormal2D();
	if (!OppositeVelocity.IsNearlyZero())
	{
		return OppositeVelocity;
	}

	if (Character && Character->GetController())
	{
		const FRotator CameraYaw(
			0.0f,
			Character->GetController()->GetControlRotation().Yaw,
			0.0f);
		return -FRotationMatrix(CameraYaw).GetUnitAxis(EAxis::X);
	}
	return FVector(-1.0f, 0.0f, 0.0f);
}

uint16 ACurriculumDataGenerator::SelectNaturalPlayEscapeAction(
	const FVector& EscapeDirection) const
{
	const FVector SafeDirection =
		EscapeDirection.IsNearlyZero()
			? FVector(-1.0f, 0.0f, 0.0f)
			: EscapeDirection.GetSafeNormal2D();
	uint16 ActionMask = WorldDirectionToMovementBits(SafeDirection);
	if (Character)
	{
		ActionMask |= CameraBitsToward(
			Character->GetActorLocation()
				+ (SafeDirection * 1000.0f)
				+ FVector(0.0f, 0.0f, 70.0f));
	}
	if (CurriculumStage != ECurriculumStage::Movement
		&& SampleParameterBool(
			TEXT("natural_play_escape_q"),
			NaturalPlayEscapeCount))
	{
		ActionMask |= CurriculumAction::Q;
	}
	return ActionMask;
}

void ACurriculumDataGenerator::StartPostSuccessRollout(
	const FRecordedState& State,
	const int32 SuccessObservationIndex)
{
	CoverageMissionSuccessFrameIndex = SuccessObservationIndex;
	CoveragePostSuccessSteps = 0;
	PostSuccessBaseActionMask =
		CurrentActionMask
		& CurriculumAction::CanonicalMask
		& ~CurriculumAction::E;
	const FVector CompletionGoal =
		CoverageMission == ECoverageMission::ContactRecovery
			? CoverageRecoveryGoal
			: CoverageMissionGoal;
	CoverageDistanceToGoalAtSuccessCm =
		FVector::Dist2D(State.Position, CompletionGoal);
}

uint16 ACurriculumDataGenerator::SelectPostSuccessAction() const
{
	if (PreviousState.bContact)
	{
		return SelectNaturalPlayEscapeAction(
			GetNaturalPlayEscapeDirection(PreviousState));
	}

	const uint16 MovementBits =
		CurriculumAction::W
		| CurriculumAction::A
		| CurriculumAction::S
		| CurriculumAction::D;
	const uint16 CameraBits =
		CurriculumAction::ArrowUp
		| CurriculumAction::ArrowDown
		| CurriculumAction::ArrowLeft
		| CurriculumAction::ArrowRight;
	const uint16 YawBit =
		bPostSuccessMirror
			? CurriculumAction::ArrowLeft
			: CurriculumAction::ArrowRight;
	const uint16 OppositeYawBit =
		bPostSuccessMirror
			? CurriculumAction::ArrowRight
			: CurriculumAction::ArrowLeft;
	const uint16 StrafeBit =
		bPostSuccessMirror ? CurriculumAction::A : CurriculumAction::D;
	const uint16 OppositeStrafeBit =
		bPostSuccessMirror ? CurriculumAction::D : CurriculumAction::A;
	const int32 SafeDuration =
		FMath::Max(1, CoverageRequiredPostSuccessSteps);
	const int32 LeadSteps =
		FMath::Clamp(SafeDuration / 4, 3, 6);
	const int32 Step =
		FMath::Clamp(CoveragePostSuccessSteps, 0, SafeDuration);
	const int32 Midpoint = SafeDuration / 2;
	const int32 FinalThird = (SafeDuration * 2) / 3;
	uint16 ActionMask = PostSuccessBaseActionMask;

	// The first few post-success steps exactly preserve the action which
	// completed the mission. Later changes are held for coherent segments rather
	// than sampled independently on every frame.
	if (Step < LeadSteps)
	{
		return ActionMask;
	}

	switch (PostSuccessStyle)
	{
	case EPostSuccessStyle::GentleTurn:
		ActionMask &= ~OppositeYawBit;
		ActionMask |= YawBit;
		break;
	case EPostSuccessStyle::GlanceReacquire:
		ActionMask &= ~(CurriculumAction::ArrowLeft | CurriculumAction::ArrowRight);
		ActionMask |= Step < Midpoint ? YawBit : OppositeYawBit;
		break;
	case EPostSuccessStyle::StrafeBlend:
		ActionMask &= ~OppositeStrafeBit;
		ActionMask |= StrafeBit;
		break;
	case EPostSuccessStyle::EaseAndObserve:
		if (Step >= Midpoint)
		{
			ActionMask &= ~MovementBits;
			if ((ActionMask & CameraBits) == 0)
			{
				ActionMask |= YawBit;
			}
		}
		break;
	case EPostSuccessStyle::DriftAndSettle:
		if (Step < FinalThird)
		{
			ActionMask &= ~OppositeStrafeBit;
			ActionMask |= StrafeBit;
		}
		else
		{
			ActionMask &= ~(MovementBits | CameraBits);
		}
		break;
	default:
		// A completely idle completion still receives one restrained look so
		// "continue" does not create a long duplicate-frame tail.
		if ((ActionMask & (MovementBits | CameraBits)) == 0
			&& Step < FinalThird)
		{
			ActionMask |= YawBit;
		}
		break;
	}
	return ActionMask;
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

uint16 ACurriculumDataGenerator::SelectV2RuntimeSmokeAction() const
{
	// This is a bounded diagnostic sequence, not the production random policy.
	// It deliberately exercises every frozen Q/E gate in a stable order.
	switch (FrameIndex)
	{
	case 0:
		return CurriculumAction::W;
	case 1:
		return CurriculumAction::Q | CurriculumAction::W;
	case 2:
		return CurriculumAction::Q | CurriculumAction::ArrowRight;
	case 3:
		return CurriculumAction::Q | CurriculumAction::E;
	case 4:
		return CurriculumAction::Q | CurriculumAction::E;
	case 5:
		return 0;
	case 6:
		return CurriculumAction::E;
	case 7:
		return 0;
	case 8:
		return CurriculumAction::Q | CurriculumAction::E;
	case 9:
		return CurriculumAction::Q;
	case 10:
		return 0;
	case 11:
	case 12:
		return CurriculumAction::Q | CurriculumAction::ArrowUp;
	case 13:
		return CurriculumAction::Q | CurriculumAction::E;
	case 14:
		return 0;
	default:
		return 0;
	}
}

uint16 ACurriculumDataGenerator::SelectV2TrajectoryHoldMissionAction() const
{
	// Diagnostic implementation of frozen random family R08. The initial
	// observation is Q-off; every generated action then holds Q. E rises once
	// after a half-second stable preview and Q remains held for the rest of the
	// episode so the preview can be compared with the live grenade simulation.
	return V2ActionSemantics::SelectTrajectoryHoldAction(
		FrameIndex,
		ObservationRate);
}

void ACurriculumDataGenerator::SelectCoverageMission()
{
	CoverageMission = ECoverageMission::SemiMarkov;
	ObjectViewMode = EObjectViewMode::ApproachObserve;
	ObjectGazePattern = EObjectGazePattern::TargetCenter;
	CurrentObjectGazeIntent = EObjectGazeIntent::TargetCenter;
	ContactPhase = EContactPhase::Approach;
	ContactRecoveryStyle = EContactRecoveryStyle::Backward;
	ContactApproachProfile = EContactApproachProfile::Direct;
	GuidedCameraStyle = EGuidedCameraStyle::ObjectiveCenter;
	LocomotionFacingProfile = ELocomotionFacingProfile::Forward;
	RampPathProfile = ERampPathProfile::Center;
	HoopPathProfile = EHoopPathProfile::Center;
	RampDirection = ERampDirection::Uphill;
	PostSuccessStyle = EPostSuccessStyle::Continue;
	CoverageTargetIndex = INDEX_NONE;
	ContactTargetIndex = INDEX_NONE;
	CoverageWaypointIndex = 0;
	CurrentObjectGazePhaseIndex = 0;
	CurrentObjectScenarioIndex = INDEX_NONE;
	CurrentContactScenarioIndex = INDEX_NONE;
	CurrentContactApproachSector = INDEX_NONE;
	CurrentRampScenarioIndex = INDEX_NONE;
	CurrentHoopScenarioIndex = INDEX_NONE;
	CurrentCoveragePositionBin = INDEX_NONE;
	CurrentCoveragePositionDistanceBand = INDEX_NONE;
	CurrentEpisodeViewBinsMask = 0;
	CurrentEpisodeVisitedBinsMask = 0;
	CoverageRequiredAzimuthBinsMask = 0;
	CurrentEpisodeRampTraversals = 0;
	CurrentEpisodeHoopPasses = 0;
	CoverageRequiredHoopPasses = 1;
	CoverageNoProgressSteps = 0;
	CoverageVisibleHoldSteps = 0;
	CoverageRequiredVisibleHoldSteps = 0;
	CoverageRequiredAzimuthBinCount = 0;
	CoverageContactHoldSteps = 0;
	CoverageVerifiedContactSteps = 0;
	CoverageRequiredContactHoldSteps = 0;
	CoverageRecoverySteps = 0;
	CoverageRequiredRecoverySteps = 0;
	CoveragePostObjectiveSteps = 0;
	CoverageRequiredPostObjectiveSteps = 0;
	CoverageMissionSuccessFrameIndex = INDEX_NONE;
	CoveragePostSuccessSteps = 0;
	CoverageRequiredPostSuccessSteps = 0;
	CurrentEpisodeFacingMovingFrames = 0;
	CurrentEpisodeFacingMatchedFrames = 0;
	NaturalPlayContactSteps = 0;
	NaturalPlayContactLimitSteps = 0;
	NaturalPlayContactEventIndex = 0;
	NaturalPlayEscapeStepsRemaining = 0;
	NaturalPlayEscapeCount = 0;
	NaturalPlayMaximumContactSteps = 0;
	PostSuccessBaseActionMask = 0;
	CoverageWaypoints.Reset();
	ObjectGazePlanIntents.Reset();
	ObjectGazePlanDurations.Reset();
	ObjectGazePlanOffsets.Reset();
	CoverageMissionStart = FVector::ZeroVector;
	CoverageMissionGoal = FVector::ZeroVector;
	CoverageLookTarget = FVector::ZeroVector;
	CurrentObjectGazeTarget = FVector::ZeroVector;
	CoverageContactPoint = FVector::ZeroVector;
	CoverageRecoveryGoal = FVector::ZeroVector;
	NaturalPlayEscapeDirection = FVector::ZeroVector;
	CoverageOrbitRadiusCm = 0.0f;
	CoverageCameraOffset = FVector::ZeroVector;
	CoverageInitialLookTarget = FVector::ZeroVector;
	CoverageInitialYawOffsetDegrees = 0.0f;
	CoverageInitialPitchOffsetDegrees = 0.0f;
	CoverageLastHoopCrossingY = 0.0f;
	CoverageLastHoopCrossingZ = 0.0f;
	CurrentMovementCameraYawDeltaDegrees = 0.0f;
	CoverageDistanceToGoalAtSuccessCm = 0.0f;
	bPostSuccessMirror = false;
	bNaturalPlayEscapeActionActive = false;
	bCurrentCoverageTargetVisible = false;
	bCoverageOrbitClockwise = false;
	bCoveragePrimaryObjectiveAchieved = false;
	bCoverageFacingProfileRequired = false;
	bCoverageFacingMeasurementComplete = false;
	bCoverageInitialLookTargetValid = false;
	bCoverageHoopCrossingRecorded = false;
	bCoverageMissionSucceeded = false;
	bCoverageMissionFailed = false;
	bCoverageMissionConfigurationValid = true;
	bRampMounted = false;
	bHoopPositiveToNegative = false;
	for (int64& IntentFrames : CurrentEpisodeObjectGazeIntentFrames)
	{
		IntentFrames = 0;
	}

	if (!bCoverageGuided || bTrajectoryShowcase)
	{
		return;
	}

	// Preserve one canonical semi-Markov reference episode in every ten. All
	// remaining automated episodes are selected by actual observation-frame
	// deficits, not raw episode counts. Guided early termination therefore cannot
	// silently skew the intended 55/20/5/5/5 final frame mixture (the remaining
	// 10% is supplied by separately captured human sessions).
	bool bMissionOverridden = false;
	if (bPrescribedRecipes)
	{
		if (MissionOverride == TEXT("semi_markov") || MissionOverride == TEXT("semimarkov"))
		{
			CoverageMission = ECoverageMission::SemiMarkov;
		}
		else if (MissionOverride == TEXT("object_view"))
		{
			CoverageMission = ECoverageMission::ObjectView;
		}
		else if (MissionOverride == TEXT("contact_recovery"))
		{
			CoverageMission = ECoverageMission::ContactRecovery;
		}
		else if (MissionOverride == TEXT("ramp_traverse"))
		{
			CoverageMission = ECoverageMission::RampTraverse;
		}
		else if (MissionOverride == TEXT("hoop_pass"))
		{
			CoverageMission = ECoverageMission::HoopPass;
		}
		else
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		bMissionOverridden = true;
	}
	else if (bMissionReviewSuite)
	{
		if (EpisodeIndex == 0)
		{
			CoverageMission = ECoverageMission::SemiMarkov;
		}
		else if (EpisodeIndex <= 30)
		{
			CoverageMission = ECoverageMission::ObjectView;
		}
		else if (EpisodeIndex <= 39)
		{
			CoverageMission = ECoverageMission::ContactRecovery;
		}
		else if (EpisodeIndex <= 49)
		{
			CoverageMission = ECoverageMission::RampTraverse;
		}
		else
		{
			CoverageMission = ECoverageMission::HoopPass;
		}
		bMissionOverridden = true;
	}
	else if (!MissionOverride.IsEmpty())
	{
		if (MissionOverride == TEXT("semi_markov") || MissionOverride == TEXT("semimarkov"))
		{
			CoverageMission = ECoverageMission::SemiMarkov;
			bMissionOverridden = true;
		}
		else if (MissionOverride == TEXT("object_view"))
		{
			CoverageMission = ECoverageMission::ObjectView;
			bMissionOverridden = true;
		}
		else if (MissionOverride == TEXT("contact_recovery"))
		{
			CoverageMission = ECoverageMission::ContactRecovery;
			bMissionOverridden = true;
		}
		else if (MissionOverride == TEXT("ramp_traverse"))
		{
			CoverageMission = ECoverageMission::RampTraverse;
			bMissionOverridden = true;
		}
		else if (MissionOverride == TEXT("hoop_pass"))
		{
			CoverageMission = ECoverageMission::HoopPass;
			bMissionOverridden = true;
		}
	}

	if (!bMissionOverridden && (EpisodeIndex % 10) != 0)
	{
		CoverageMission = static_cast<ECoverageMission>(
			SelectFrameDeficitBucket(
				OverallMissionObservationFrames,
				AutomatedMissionFrameShares,
				static_cast<int32>(ECoverageMission::Count),
				TransitionsPerEpisode + 1,
				GetParameterBits(TEXT("mission_tie_break"))));
	}

	CoverageInitialYawOffsetDegrees = SampleStratifiedRange(
		TEXT("initial_yaw_offset"),
		8,
		-12.0f,
		12.0f);
	CoverageInitialPitchOffsetDegrees = SampleStratifiedRange(
		TEXT("initial_pitch_offset"),
		6,
		-6.0f,
		6.0f);
	CoverageRequiredPostObjectiveSteps = FMath::RoundToInt(
		SampleStratifiedRange(
			TEXT("post_objective_seconds"),
			6,
			0.20f,
			0.65f)
		* static_cast<float>(ObservationRate));
	if (CoverageMission != ECoverageMission::SemiMarkov)
	{
		CoverageRequiredPostSuccessSteps = FMath::Max(
			1,
			FMath::RoundToInt(
				SampleStratifiedRange(
					TEXT("post_success_seconds"),
					16,
					0.75f,
					1.50f)
				* static_cast<float>(ObservationRate)));
		const int32 StyleCount =
			static_cast<int32>(EPostSuccessStyle::Count);
		const int32 StyleOffset =
			FMath::Abs(SeedStart + WorkerId) % StyleCount;
		PostSuccessStyle = static_cast<EPostSuccessStyle>(
			(EpisodeIndex + StyleOffset) % StyleCount);
		bPostSuccessMirror =
			((EpisodeIndex
				+ (FMath::Abs(SeedStart + WorkerId) % 2))
				% 2) != 0;
	}

	switch (CoverageMission)
	{
	case ECoverageMission::ObjectView:
		ConfigureObjectViewMission();
		break;
	case ECoverageMission::ContactRecovery:
		ConfigureContactRecoveryMission();
		break;
	case ECoverageMission::RampTraverse:
		ConfigureRampMission();
		break;
	case ECoverageMission::HoopPass:
		ConfigureHoopMission();
		break;
	default:
		break;
	}
}

void ACurriculumDataGenerator::ConfigureObjectViewMission()
{
	if (bPrescribedRecipes)
	{
		if (CurrentPrescribedScenarioIndex < 0
			|| CurrentPrescribedScenarioIndex >= ObjectScenarioCount)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CurrentObjectScenarioIndex = CurrentPrescribedScenarioIndex;
		int32 ModeIndex = 0;
		int32 GazeIndex = 0;
		DecodeObjectScenario(
			CurrentObjectScenarioIndex,
			CoverageTargetIndex,
			ModeIndex,
			GazeIndex,
			bCoverageOrbitClockwise);
		ObjectViewMode = static_cast<EObjectViewMode>(ModeIndex);
		ObjectGazePattern = static_cast<EObjectGazePattern>(GazeIndex);
	}
	else if (bMissionReviewSuite)
	{
		if (EpisodeIndex <= 10)
		{
			const int32 VariantIndex = EpisodeIndex - 1;
			CoverageTargetIndex = VariantIndex / 2;
			ObjectViewMode = static_cast<EObjectViewMode>(VariantIndex % 2);
			ObjectGazePattern = static_cast<EObjectGazePattern>(VariantIndex % 4);
			bCoverageOrbitClockwise = false;
		}
		else
		{
			const int32 VariantIndex = EpisodeIndex - 11;
			const int32 LocalVariant = VariantIndex % 4;
			CoverageTargetIndex = VariantIndex / 4;
			ObjectViewMode =
				LocalVariant < 2
					? EObjectViewMode::PartialOrbit
					: EObjectViewMode::FullOrbit;
			bCoverageOrbitClockwise = (LocalVariant % 2) == 0;
			ObjectGazePattern = static_cast<EObjectGazePattern>(
				(CoverageTargetIndex + LocalVariant)
					% static_cast<int32>(EObjectGazePattern::Count));
		}
		CurrentObjectScenarioIndex = EncodeObjectScenario(
			CoverageTargetIndex,
			static_cast<int32>(ObjectViewMode),
			static_cast<int32>(ObjectGazePattern),
			bCoverageOrbitClockwise);
	}
	else
	{
		int32 RequiredTargetIndex = INDEX_NONE;
		for (int32 TargetIndex = 0; TargetIndex < 5; ++TargetIndex)
		{
			if (!CoverageTargetOverride.IsEmpty()
				&& CoverageTargetOverride
					== GetCoverageTargetDefinition(TargetIndex).Slug)
			{
				RequiredTargetIndex = TargetIndex;
				break;
			}
		}
		int32 RequiredModeIndex = INDEX_NONE;
		if (ObjectViewModeOverride == TEXT("approach_observe"))
		{
			RequiredModeIndex = static_cast<int32>(EObjectViewMode::ApproachObserve);
		}
		else if (ObjectViewModeOverride == TEXT("pass_by"))
		{
			RequiredModeIndex = static_cast<int32>(EObjectViewMode::PassBy);
		}
		else if (ObjectViewModeOverride == TEXT("partial_orbit"))
		{
			RequiredModeIndex = static_cast<int32>(EObjectViewMode::PartialOrbit);
		}
		else if (ObjectViewModeOverride == TEXT("full_orbit"))
		{
			RequiredModeIndex = static_cast<int32>(EObjectViewMode::FullOrbit);
		}
		if (RequiredModeIndex == INDEX_NONE)
		{
			RequiredModeIndex = SelectFrameDeficitBucket(
				OverallObjectModeObservationFrames,
				ObjectViewModeFrameShares,
				static_cast<int32>(EObjectViewMode::Count),
				TransitionsPerEpisode + 1,
				GetParameterBits(TEXT("object_mode_tie")));
		}
		const int32 RequiredGazeIndex = SelectFrameDeficitBucket(
			OverallObjectGazeIntentObservationFrames,
			ObjectGazeIntentFrameShares,
			UE_ARRAY_COUNT(ObjectGazeIntentFrameShares),
			TransitionsPerEpisode + 1,
			GetParameterBits(TEXT("object_realized_gaze_intent_tie")));

		double AllowedShareTotal = 0.0;
		int64 AllowedFrameTotal = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < ObjectScenarioCount;
			++ScenarioIndex)
		{
			int32 TargetIndex = 0;
			int32 ModeIndex = 0;
			int32 GazeIndex = 0;
			bool bClockwise = false;
			DecodeObjectScenario(
				ScenarioIndex,
				TargetIndex,
				ModeIndex,
				GazeIndex,
				bClockwise);
			const bool bAllowed =
				(RequiredTargetIndex == INDEX_NONE
					|| TargetIndex == RequiredTargetIndex)
				&& (RequiredModeIndex == INDEX_NONE
					|| ModeIndex == RequiredModeIndex)
				&& GazeIndex == RequiredGazeIndex
				&& (MissionDirectionOverride != TEXT("clockwise")
					|| ModeIndex < 2
					|| bClockwise)
				&& (MissionDirectionOverride != TEXT("counter_clockwise")
					|| ModeIndex < 2
					|| !bClockwise);
			if (bAllowed)
			{
				AllowedShareTotal += GetObjectScenarioShare(ScenarioIndex);
				AllowedFrameTotal +=
					OverallObjectScenarioObservationFrames[ScenarioIndex];
			}
		}

		double BestDeficit = -DBL_MAX;
		uint64 BestTie = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < ObjectScenarioCount;
			++ScenarioIndex)
		{
			int32 TargetIndex = 0;
			int32 ModeIndex = 0;
			int32 GazeIndex = 0;
			bool bClockwise = false;
			DecodeObjectScenario(
				ScenarioIndex,
				TargetIndex,
				ModeIndex,
				GazeIndex,
				bClockwise);
			const bool bAllowed =
				(RequiredTargetIndex == INDEX_NONE
					|| TargetIndex == RequiredTargetIndex)
				&& (RequiredModeIndex == INDEX_NONE
					|| ModeIndex == RequiredModeIndex)
				&& GazeIndex == RequiredGazeIndex
				&& (MissionDirectionOverride != TEXT("clockwise")
					|| ModeIndex < 2
					|| bClockwise)
				&& (MissionDirectionOverride != TEXT("counter_clockwise")
					|| ModeIndex < 2
					|| !bClockwise);
			if (!bAllowed)
			{
				continue;
			}
			const double NormalizedShare =
				GetObjectScenarioShare(ScenarioIndex)
				/ FMath::Max(AllowedShareTotal, UE_DOUBLE_SMALL_NUMBER);
			const double ProjectedTotal = static_cast<double>(
				AllowedFrameTotal + FMath::Max(1, TransitionsPerEpisode + 1));
			const double Deficit =
				(ProjectedTotal * NormalizedShare)
				- static_cast<double>(
					OverallObjectScenarioObservationFrames[ScenarioIndex]);
			const uint64 Tie = MixParameterBits(
				GetParameterBits(TEXT("object_scenario_tie"))
					^ static_cast<uint64>(ScenarioIndex));
			if (Deficit > BestDeficit + 1e-9
				|| (FMath::IsNearlyEqual(Deficit, BestDeficit) && Tie > BestTie))
			{
				BestDeficit = Deficit;
				BestTie = Tie;
				CurrentObjectScenarioIndex = ScenarioIndex;
			}
		}
		if (CurrentObjectScenarioIndex == INDEX_NONE)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		int32 ModeIndex = 0;
		int32 GazeIndex = 0;
		DecodeObjectScenario(
			CurrentObjectScenarioIndex,
			CoverageTargetIndex,
			ModeIndex,
			GazeIndex,
			bCoverageOrbitClockwise);
		ObjectViewMode = static_cast<EObjectViewMode>(ModeIndex);
		ObjectGazePattern = static_cast<EObjectGazePattern>(GazeIndex);
	}

	const FCoverageTargetDefinition& Target =
		GetCoverageTargetDefinition(CoverageTargetIndex);
	CoverageLookTarget = Target.LookTarget;
	CoverageOrbitStartAngleDegrees = SampleStratifiedRange(
		TEXT("object_primary_angle"),
		CoverageAzimuthBinCount,
		-180.0f,
		180.0f);
	float SafeMaximumOrbitRadius =
		PlayableSamplingLimitCm
			- FMath::Max(
				FMath::Abs(Target.LookTarget.X),
				FMath::Abs(Target.LookTarget.Y));
	for (int32 OtherTargetIndex = 0; OtherTargetIndex < 5; ++OtherTargetIndex)
	{
		if (OtherTargetIndex == CoverageTargetIndex)
		{
			continue;
		}
		const FCoverageTargetDefinition& OtherTarget =
			GetCoverageTargetDefinition(OtherTargetIndex);
		SafeMaximumOrbitRadius = FMath::Min(
			SafeMaximumOrbitRadius,
			FVector::Dist2D(Target.LookTarget, OtherTarget.LookTarget)
				- OtherTarget.ContactRadiusCm
				- 165.0f);
	}
	const float SafeMinimumOrbitRadius =
		FMath::Max(380.0f, Target.OrbitRadiusCm - 120.0f);
	if (SafeMaximumOrbitRadius <= SafeMinimumOrbitRadius + 10.0f)
	{
		bCoverageMissionConfigurationValid = false;
		return;
	}
	const float RadiusJitterMargin =
		SafeMaximumOrbitRadius - SafeMinimumOrbitRadius > 90.0f ? 35.0f : 0.0f;
	CoverageOrbitRadiusCm = SampleStratifiedRange(
		TEXT("object_orbit_radius"),
		8,
		SafeMinimumOrbitRadius + RadiusJitterMargin,
		SafeMaximumOrbitRadius - RadiusJitterMargin);

	if (ObjectViewMode == EObjectViewMode::ApproachObserve)
	{
		bool bFoundPath = false;
		for (int32 Attempt = 0; Attempt < 96; ++Attempt)
		{
			const float GoalAngleDegrees = SampleStratifiedRange(
				TEXT("approach_angle"),
				24,
				-180.0f,
				180.0f,
				Attempt);
			const float GoalAngleRadians =
				FMath::DegreesToRadians(GoalAngleDegrees);
			const FVector RadialDirection(
				FMath::Cos(GoalAngleRadians),
				FMath::Sin(GoalAngleRadians),
				0.0f);
			const FVector CandidateGoal =
				Target.LookTarget + (RadialDirection * CoverageOrbitRadiusCm);
			const float StartAngleRadians = FMath::DegreesToRadians(
				GoalAngleDegrees
					+ SampleParameterRange(
						TEXT("approach_start_angle_jitter"),
						-20.0f,
						20.0f,
						Attempt));
			const FVector StartDirection(
				FMath::Cos(StartAngleRadians),
				FMath::Sin(StartAngleRadians),
				0.0f);
			const FVector CandidateStart =
				Target.LookTarget
				+ (StartDirection
					* (CoverageOrbitRadiusCm
						+ SampleStratifiedRange(
							TEXT("approach_start_distance"),
							8,
							430.0f,
							760.0f,
							Attempt)));
			if (IsInsideSamplingArena(CandidateStart)
				&& IsInsideSamplingArena(CandidateGoal)
				&& SegmentClearsLearningObjects(
					CandidateStart,
					CandidateGoal,
					CoverageTargetIndex))
			{
				CoverageMissionGoal = CandidateGoal;
				CoverageMissionStart = CandidateStart;
				CoverageOrbitStartAngleDegrees = GoalAngleDegrees;
				bFoundPath = true;
				break;
			}
		}
		if (!bFoundPath)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CoverageMissionGoal.Z = 100.0f;
		CoverageMissionStart.Z = 100.0f;
		CoverageWaypoints.Add(CoverageMissionGoal);
		// The character capsule may settle a few centimeters to either side of an
		// azimuth-bin boundary. Approach/observe success is therefore based on
		// verified visibility at the sampled goal, while the achieved bin is still
		// recorded for dataset balancing.
		CoverageRequiredAzimuthBinsMask = 0;
		CoverageRequiredVisibleHoldSteps =
			FMath::RoundToInt(
				SampleStratifiedRange(
					TEXT("approach_visible_hold_seconds"),
					6,
					0.45f,
					1.25f)
				* static_cast<float>(ObservationRate));
	}
	else if (ObjectViewMode == EObjectViewMode::PassBy)
	{
		// Sample a long visible chord which clears both the viewed object and
		// every neighboring learning object. A target-relative offset alone is
		// insufficient for the central ramp because its chord can intersect a
		// corner object.
		bool bFoundPath = false;
		for (int32 Attempt = 0; Attempt < 128; ++Attempt)
		{
			const float TravelAngleDegrees = SampleStratifiedRange(
				TEXT("pass_travel_angle"),
				24,
				-180.0f,
				180.0f,
				Attempt);
			const float TravelAngleRadians =
				FMath::DegreesToRadians(TravelAngleDegrees);
			const FVector TravelDirection(
				FMath::Cos(TravelAngleRadians),
				FMath::Sin(TravelAngleRadians),
				0.0f);
			const FVector LateralDirection(
				-TravelDirection.Y,
				TravelDirection.X,
				0.0f);
			const float Clearance =
				SampleStratifiedRange(
					TEXT("pass_clearance"),
					8,
					Target.ContactRadiusCm + 120.0f,
					Target.ContactRadiusCm + 310.0f,
					Attempt);
			const FVector LateralOffset =
				LateralDirection
					* (SampleParameterBool(TEXT("pass_lateral_side"), Attempt)
						? Clearance
						: -Clearance);
			const float StartDistance =
				SampleStratifiedRange(
					TEXT("pass_start_distance"),
					8,
					620.0f,
					1000.0f,
					Attempt);
			const float EndDistance =
				SampleStratifiedRange(
					TEXT("pass_end_distance"),
					8,
					620.0f,
					1000.0f,
					Attempt);
			const FVector CandidateStart =
				Target.LookTarget
				- (TravelDirection * StartDistance)
				+ LateralOffset;
			const FVector CandidateGoal =
				Target.LookTarget
				+ (TravelDirection * EndDistance)
				+ LateralOffset;

			if (IsInsideSamplingArena(CandidateStart)
				&& IsInsideSamplingArena(CandidateGoal)
				&& SegmentClearsLearningObjects(
					CandidateStart,
					CandidateGoal,
					CoverageTargetIndex))
			{
				CoverageMissionStart = CandidateStart;
				CoverageMissionGoal = CandidateGoal;
				CoverageOrbitStartAngleDegrees = TravelAngleDegrees;
				bFoundPath = true;
				break;
			}
		}
		if (!bFoundPath)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CoverageMissionStart.Z = 100.0f;
		CoverageMissionGoal.Z = 100.0f;
		CoverageWaypoints.Add(CoverageMissionGoal);
		CoverageRequiredVisibleHoldSteps =
			FMath::RoundToInt(
				SampleStratifiedRange(
					TEXT("pass_visible_seconds"),
					6,
					0.35f,
					0.90f)
				* static_cast<float>(ObservationRate));
	}
	else
	{
		const int32 RequiredWaypointCount =
			ObjectViewMode == EObjectViewMode::FullOrbit
				? CoverageAzimuthBinCount
				: 4 + SampleParameterIndex(TEXT("partial_orbit_bins"), 4);
		CoverageRequiredAzimuthBinCount = RequiredWaypointCount;
		const float DirectionSign = bCoverageOrbitClockwise ? -1.0f : 1.0f;
		// Place every orbit waypoint near the center of a 30-degree bin. A
		// shared phase keeps adjacent waypoints exactly one bin apart, while the
		// small offset retains seed-to-seed variation without boundary ambiguity.
		const int32 StartAzimuthBin =
			SampleParameterIndex(
				TEXT("orbit_start_bin"),
				CoverageAzimuthBinCount);
		CoverageOrbitStartAngleDegrees =
			(static_cast<float>(StartAzimuthBin) + 0.5f) * 30.0f
			+ SampleParameterRange(
				TEXT("orbit_start_phase_jitter"),
				-3.0f,
				3.0f);
		const int32 PathWaypointCount =
			ObjectViewMode == EObjectViewMode::FullOrbit
				? RequiredWaypointCount + 1
				: RequiredWaypointCount;
		for (int32 WaypointIndex = 0;
			WaypointIndex < PathWaypointCount;
			++WaypointIndex)
		{
			const float AngleDegrees =
				CoverageOrbitStartAngleDegrees
				+ (DirectionSign * 30.0f * static_cast<float>(WaypointIndex));
			const float Radius =
				CoverageOrbitRadiusCm
					+ SampleParameterRange(
						TEXT("orbit_waypoint_radius_jitter"),
						-RadiusJitterMargin,
						RadiusJitterMargin,
						WaypointIndex);
			const float AngleRadians = FMath::DegreesToRadians(AngleDegrees);
			const FVector Waypoint(
				Target.LookTarget.X + (FMath::Cos(AngleRadians) * Radius),
				Target.LookTarget.Y + (FMath::Sin(AngleRadians) * Radius),
				100.0f);
			if (!IsInsideSamplingArena(Waypoint))
			{
				bCoverageMissionConfigurationValid = false;
				return;
			}
			CoverageWaypoints.Add(Waypoint);
			const float NormalizedAngle =
				FMath::Fmod(AngleRadians + (2.0f * PI), 2.0f * PI);
			const int32 Bin = FMath::Clamp(
				FMath::FloorToInt(
					NormalizedAngle
						* static_cast<float>(CoverageAzimuthBinCount)
						/ (2.0f * PI)),
				0,
				CoverageAzimuthBinCount - 1);
			CoverageRequiredAzimuthBinsMask |= static_cast<uint16>(1u << Bin);
		}
		if (ObjectViewMode == EObjectViewMode::FullOrbit)
		{
			CoverageRequiredAzimuthBinsMask =
				static_cast<uint16>((1u << CoverageAzimuthBinCount) - 1u);
		}
		CoverageMissionGoal = CoverageWaypoints.Last();
		const FVector FirstDirection =
			(CoverageWaypoints[0] - Target.LookTarget).GetSafeNormal2D();
		const FVector FirstTangent(
			-FirstDirection.Y,
			FirstDirection.X,
			0.0f);
		const FVector CandidateStart =
			CoverageWaypoints[0]
				+ (FirstTangent
					* (bCoverageOrbitClockwise ? -1.0f : 1.0f)
					* SampleParameterRange(
						TEXT("orbit_leadin_distance"),
						70.0f,
						115.0f));
		CoverageMissionStart =
			IsInsideSamplingArena(CandidateStart)
				? CandidateStart
				: CoverageWaypoints[0];
		CoverageMissionStart.Z = 100.0f;
	}

	ConfigureObjectGazePlan();
}

void ACurriculumDataGenerator::ConfigureObjectGazePlan()
{
	ObjectGazePlanIntents.Reset();
	ObjectGazePlanDurations.Reset();
	ObjectGazePlanOffsets.Reset();
	CurrentObjectGazePhaseIndex = 0;
	CurrentObjectGazeIntent = EObjectGazeIntent::TargetCenter;

	const int32 PlannedSteps =
		FMath::Max(1, TransitionsPerEpisode + ObservationRate);
	int32 TotalPlannedSteps = 0;
	int32 SampleCursor = 0;
	auto RandomSteps =
		[this, &SampleCursor](
			const TCHAR* ParameterName,
			const float MinimumSeconds,
			const float MaximumSeconds)
	{
		return FMath::Max(
			1,
			FMath::RoundToInt(
				SampleParameterRange(
					ParameterName,
					MinimumSeconds,
					MaximumSeconds,
					SampleCursor++)
					* static_cast<float>(ObservationRate)));
	};
	auto SamplePolarOffset =
		[this, &SampleCursor](
			const TCHAR* ParameterName,
			const float MinimumRadius,
			const float MaximumRadius,
			const float MinimumZ,
			const float MaximumZ)
	{
		const int32 OffsetIndex = SampleCursor++;
		const float Angle = SampleParameterRange(
			ParameterName,
			-PI,
			PI,
			OffsetIndex * 3);
		const float Radius =
			SampleParameterRange(
				ParameterName,
				MinimumRadius,
				MaximumRadius,
				(OffsetIndex * 3) + 1);
		return FVector(
			FMath::Cos(Angle) * Radius,
			FMath::Sin(Angle) * Radius,
			SampleParameterRange(
				ParameterName,
				MinimumZ,
				MaximumZ,
				(OffsetIndex * 3) + 2));
	};
	auto AddPhase =
		[this, &TotalPlannedSteps](
			const EObjectGazeIntent Intent,
			const int32 Duration,
			const FVector& Offset)
	{
		ObjectGazePlanIntents.Add(Intent);
		ObjectGazePlanDurations.Add(FMath::Max(1, Duration));
		ObjectGazePlanOffsets.Add(Offset);
		TotalPlannedSteps += FMath::Max(1, Duration);
	};

	const FCoverageTargetDefinition& Target =
		GetCoverageTargetDefinition(CoverageTargetIndex);
	const float TargetOffsetRadius =
		FMath::Clamp(Target.ContactRadiusCm * 0.55f, 65.0f, 155.0f);

	if (ObjectGazePattern == EObjectGazePattern::TargetCenter)
	{
		AddPhase(
			EObjectGazeIntent::TargetCenter,
			PlannedSteps,
			FVector::ZeroVector);
	}
	else if (ObjectGazePattern == EObjectGazePattern::TargetOffset)
	{
		AddPhase(
			EObjectGazeIntent::TargetOffset,
			PlannedSteps,
			SamplePolarOffset(
				TEXT("gaze_target_offset"),
				TargetOffsetRadius * 0.35f,
				TargetOffsetRadius,
				-70.0f,
				100.0f));
	}
	else if (ObjectGazePattern == EObjectGazePattern::TravelDirection)
	{
		while (TotalPlannedSteps < PlannedSteps)
		{
			AddPhase(
				EObjectGazeIntent::TravelDirection,
				RandomSteps(TEXT("gaze_travel_duration"), 0.7f, 1.5f),
				FVector(
					0.0f,
					0.0f,
					SampleParameterRange(
						TEXT("gaze_travel_height"),
						-75.0f,
						85.0f,
						SampleCursor++)));
			AddPhase(
				SampleParameterBool(
					TEXT("gaze_travel_reacquire_kind"),
					SampleCursor++)
					? EObjectGazeIntent::TargetCenter
					: EObjectGazeIntent::TargetOffset,
				RandomSteps(TEXT("gaze_travel_reacquire_duration"), 0.45f, 0.85f),
				SamplePolarOffset(
					TEXT("gaze_travel_reacquire_offset"),
					TargetOffsetRadius * 0.25f,
					TargetOffsetRadius * 0.75f,
					-55.0f,
					80.0f));
		}
	}
	else
	{
		static constexpr EObjectGazeIntent RoamIntents[] =
		{
			EObjectGazeIntent::TargetCenter,
			EObjectGazeIntent::SurveyPoint,
			EObjectGazeIntent::TravelDirection,
			EObjectGazeIntent::TargetOffset,
			EObjectGazeIntent::SurveyPoint
		};
		const int32 StartIntentIndex =
			SampleParameterIndex(
				TEXT("gaze_roam_start_phase"),
				UE_ARRAY_COUNT(RoamIntents));
		int32 RoamPhaseIndex = 0;
		while (TotalPlannedSteps < PlannedSteps)
		{
			const EObjectGazeIntent Intent =
				RoamIntents[
					(StartIntentIndex + RoamPhaseIndex)
						% UE_ARRAY_COUNT(RoamIntents)];
			++RoamPhaseIndex;
			if (Intent == EObjectGazeIntent::TargetCenter)
			{
				AddPhase(
					Intent,
					RandomSteps(TEXT("gaze_roam_center_duration"), 0.45f, 0.90f),
					FVector::ZeroVector);
			}
			else if (Intent == EObjectGazeIntent::TargetOffset)
			{
				AddPhase(
					Intent,
					RandomSteps(TEXT("gaze_roam_offset_duration"), 0.45f, 0.90f),
					SamplePolarOffset(
						TEXT("gaze_roam_target_offset"),
						TargetOffsetRadius * 0.25f,
						TargetOffsetRadius,
						-70.0f,
						100.0f));
			}
			else if (Intent == EObjectGazeIntent::TravelDirection)
			{
				AddPhase(
					Intent,
					RandomSteps(TEXT("gaze_roam_travel_duration"), 0.55f, 1.05f),
					FVector(
						0.0f,
						0.0f,
						SampleParameterRange(
							TEXT("gaze_roam_travel_height"),
							-90.0f,
							100.0f,
							SampleCursor++)));
			}
			else
			{
				AddPhase(
					Intent,
					RandomSteps(TEXT("gaze_roam_survey_duration"), 0.60f, 1.15f),
					SamplePolarOffset(
						TEXT("gaze_roam_survey_offset"),
						500.0f,
						950.0f,
						-150.0f,
						240.0f));
			}
		}
	}

	UpdateObjectGazeTarget(CoverageMissionStart);
}

void ACurriculumDataGenerator::UpdateObjectGazeTarget(
	const FVector& ObserverLocation)
{
	if (CoverageMission != ECoverageMission::ObjectView
		|| CoverageTargetIndex == INDEX_NONE
		|| ObjectGazePlanIntents.IsEmpty())
	{
		return;
	}

	int32 RemainingFrame = FMath::Max(0, FrameIndex);
	CurrentObjectGazePhaseIndex = ObjectGazePlanIntents.Num() - 1;
	for (int32 PhaseIndex = 0;
		PhaseIndex < ObjectGazePlanIntents.Num();
		++PhaseIndex)
	{
		const int32 Duration =
			ObjectGazePlanDurations.IsValidIndex(PhaseIndex)
				? ObjectGazePlanDurations[PhaseIndex]
				: 1;
		if (RemainingFrame < Duration)
		{
			CurrentObjectGazePhaseIndex = PhaseIndex;
			break;
		}
		RemainingFrame -= Duration;
	}

	CurrentObjectGazeIntent =
		ObjectGazePlanIntents[CurrentObjectGazePhaseIndex];
	const FCoverageTargetDefinition& Target =
		GetCoverageTargetDefinition(CoverageTargetIndex);
	const FVector PhaseOffset =
		ObjectGazePlanOffsets.IsValidIndex(CurrentObjectGazePhaseIndex)
			? ObjectGazePlanOffsets[CurrentObjectGazePhaseIndex]
			: FVector::ZeroVector;

	// Approach/observe retains its semantic endpoint. Pass-by and orbit missions
	// keep their scheduled independent gaze all the way through completion.
	const bool bMustObserveAtGoal =
		ObjectViewMode == EObjectViewMode::ApproachObserve
		&& FVector::Dist2D(ObserverLocation, CoverageMissionGoal) < 150.0f;
	if (bMustObserveAtGoal)
	{
		CurrentObjectGazeIntent = EObjectGazeIntent::TargetCenter;
	}

	switch (CurrentObjectGazeIntent)
	{
	case EObjectGazeIntent::TargetOffset:
		CurrentObjectGazeTarget = Target.LookTarget + PhaseOffset;
		break;
	case EObjectGazeIntent::TravelDirection:
	{
		const int32 SafeWaypointIndex =
			CoverageWaypoints.IsEmpty()
				? INDEX_NONE
				: FMath::Clamp(
					CoverageWaypointIndex,
					0,
					CoverageWaypoints.Num() - 1);
		const FVector TravelGoal =
			SafeWaypointIndex == INDEX_NONE
				? CoverageMissionGoal
				: CoverageWaypoints[SafeWaypointIndex];
		const FVector TravelDirection =
			(TravelGoal - ObserverLocation).GetSafeNormal2D();
		if (TravelDirection.IsNearlyZero())
		{
			CurrentObjectGazeIntent = EObjectGazeIntent::TargetCenter;
			CurrentObjectGazeTarget = Target.LookTarget;
		}
		else
		{
			CurrentObjectGazeTarget =
				ObserverLocation
				+ (TravelDirection * 1200.0f)
				+ FVector(0.0f, 0.0f, 64.0f + PhaseOffset.Z);
		}
		break;
	}
	case EObjectGazeIntent::SurveyPoint:
	{
		CurrentObjectGazeTarget = Target.LookTarget + PhaseOffset;
		// Reflect an out-of-bounds survey target back into the arena rather than
		// clamping many independent samples onto the same boundary coordinate.
		auto ReflectIntoArena = [](float Coordinate)
		{
			while (Coordinate > PlayableSamplingLimitCm
				|| Coordinate < -PlayableSamplingLimitCm)
			{
				if (Coordinate > PlayableSamplingLimitCm)
				{
					Coordinate =
						(2.0f * PlayableSamplingLimitCm) - Coordinate;
				}
				else
				{
					Coordinate =
						(-2.0f * PlayableSamplingLimitCm) - Coordinate;
				}
			}
			return Coordinate;
		};
		CurrentObjectGazeTarget.X =
			ReflectIntoArena(CurrentObjectGazeTarget.X);
		CurrentObjectGazeTarget.Y =
			ReflectIntoArena(CurrentObjectGazeTarget.Y);
		if (FVector::Dist2D(ObserverLocation, CurrentObjectGazeTarget) < 250.0f)
		{
			CurrentObjectGazeTarget +=
				PhaseOffset.GetSafeNormal2D() * 500.0f;
			CurrentObjectGazeTarget.X =
				ReflectIntoArena(CurrentObjectGazeTarget.X);
			CurrentObjectGazeTarget.Y =
				ReflectIntoArena(CurrentObjectGazeTarget.Y);
		}
		break;
	}
	default:
		CurrentObjectGazeTarget = Target.LookTarget;
		break;
	}

	CoverageLookTarget = CurrentObjectGazeTarget;
}

void ACurriculumDataGenerator::ConfigureContactRecoveryMission()
{
	constexpr int32 RecoveryStyleCount =
		static_cast<int32>(EContactRecoveryStyle::Count);
	constexpr int32 ApproachProfileCount =
		static_cast<int32>(EContactApproachProfile::Count);
	constexpr int32 FacingProfileCount =
		static_cast<int32>(ELocomotionFacingProfile::Count);
	int32 BaseScenarioIndex = INDEX_NONE;
	if (bPrescribedRecipes)
	{
		if (CurrentPrescribedScenarioIndex < 0
			|| CurrentPrescribedScenarioIndex >= ContactScenarioCount)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CurrentContactScenarioIndex = CurrentPrescribedScenarioIndex;
		BaseScenarioIndex = CurrentContactScenarioIndex / FacingProfileCount;
		ContactTargetIndex =
			BaseScenarioIndex / (RecoveryStyleCount * ApproachProfileCount);
		const int32 LocalBaseScenario =
			BaseScenarioIndex % (RecoveryStyleCount * ApproachProfileCount);
		ContactRecoveryStyle = static_cast<EContactRecoveryStyle>(
			LocalBaseScenario / ApproachProfileCount);
		ContactApproachProfile = static_cast<EContactApproachProfile>(
			LocalBaseScenario % ApproachProfileCount);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			CurrentContactScenarioIndex % FacingProfileCount);
	}
	else if (bMissionReviewSuite)
	{
		ContactTargetIndex = EpisodeIndex - 31;
		ContactRecoveryStyle = static_cast<EContactRecoveryStyle>(
			ContactTargetIndex % RecoveryStyleCount);
		ContactApproachProfile = static_cast<EContactApproachProfile>(
			ContactTargetIndex % ApproachProfileCount);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			ContactTargetIndex % FacingProfileCount);
		BaseScenarioIndex =
			((ContactTargetIndex * RecoveryStyleCount)
				+ static_cast<int32>(ContactRecoveryStyle))
				* ApproachProfileCount
			+ static_cast<int32>(ContactApproachProfile);
		CurrentContactScenarioIndex =
			(BaseScenarioIndex * FacingProfileCount)
			+ static_cast<int32>(LocomotionFacingProfile);
	}
	else
	{
		int32 RequiredTargetIndex = INDEX_NONE;
		for (int32 TargetIndex = 0; TargetIndex < 9; ++TargetIndex)
		{
			if (!CoverageTargetOverride.IsEmpty()
				&& CoverageTargetOverride
					== GetContactTargetDefinition(TargetIndex).Slug)
			{
				RequiredTargetIndex = TargetIndex;
				break;
			}
		}
		int64 AllowedFrameTotal = 0;
		int32 AllowedScenarioCount = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < ContactBaseScenarioCount;
			++ScenarioIndex)
		{
			const int32 TargetIndex =
				ScenarioIndex / (RecoveryStyleCount * ApproachProfileCount);
			if (RequiredTargetIndex == INDEX_NONE
				|| TargetIndex == RequiredTargetIndex)
			{
				AllowedFrameTotal +=
					OverallContactBaseScenarioObservationFrames[ScenarioIndex];
				++AllowedScenarioCount;
			}
		}
		double BestDeficit = -DBL_MAX;
		uint64 BestTie = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < ContactBaseScenarioCount;
			++ScenarioIndex)
		{
			const int32 TargetIndex =
				ScenarioIndex / (RecoveryStyleCount * ApproachProfileCount);
			if (RequiredTargetIndex != INDEX_NONE
				&& TargetIndex != RequiredTargetIndex)
			{
				continue;
			}
			const double ProjectedTotal = static_cast<double>(
				AllowedFrameTotal + FMath::Max(1, TransitionsPerEpisode + 1));
			const double Deficit =
				(ProjectedTotal
					/ static_cast<double>(FMath::Max(1, AllowedScenarioCount)))
				- static_cast<double>(
					OverallContactBaseScenarioObservationFrames[ScenarioIndex]);
			const uint64 Tie = MixParameterBits(
				GetParameterBits(TEXT("contact_scenario_tie"))
					^ static_cast<uint64>(ScenarioIndex));
			if (Deficit > BestDeficit + 1e-9
				|| (FMath::IsNearlyEqual(Deficit, BestDeficit) && Tie > BestTie))
			{
				BestDeficit = Deficit;
				BestTie = Tie;
				BaseScenarioIndex = ScenarioIndex;
			}
		}
		if (BaseScenarioIndex == INDEX_NONE)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		ContactTargetIndex =
			BaseScenarioIndex / (RecoveryStyleCount * ApproachProfileCount);
		const int32 LocalBaseScenario =
			BaseScenarioIndex % (RecoveryStyleCount * ApproachProfileCount);
		ContactRecoveryStyle = static_cast<EContactRecoveryStyle>(
			LocalBaseScenario / ApproachProfileCount);
		ContactApproachProfile = static_cast<EContactApproachProfile>(
			LocalBaseScenario % ApproachProfileCount);

		bool bHasUnseenFacing = false;
		for (int32 FacingIndex = 0;
			FacingIndex < FacingProfileCount;
			++FacingIndex)
		{
			bHasUnseenFacing =
				bHasUnseenFacing
				|| OverallContactScenarioObservationFrames[
					(BaseScenarioIndex * FacingProfileCount) + FacingIndex] == 0;
		}
		int64 GlobalFacingFrameTotal = 0;
		for (const int64 FacingFrames : OverallContactFacingObservationFrames)
		{
			GlobalFacingFrameTotal += FacingFrames;
		}
		double BestFacingDeficit = -DBL_MAX;
		uint64 BestFacingTie = 0;
		int32 SelectedFacingIndex = INDEX_NONE;
		for (int32 FacingIndex = 0;
			FacingIndex < FacingProfileCount;
			++FacingIndex)
		{
			const int32 FullScenarioIndex =
				(BaseScenarioIndex * FacingProfileCount) + FacingIndex;
			if (bHasUnseenFacing
				&& OverallContactScenarioObservationFrames[FullScenarioIndex] != 0)
			{
				continue;
			}
			const double GlobalDeficit =
				(static_cast<double>(
					GlobalFacingFrameTotal
						+ FMath::Max(1, TransitionsPerEpisode + 1))
					* LocomotionFacingFrameShares[FacingIndex])
				- static_cast<double>(
					OverallContactFacingObservationFrames[FacingIndex]);
			const double LocalPenalty =
				static_cast<double>(
					OverallContactScenarioObservationFrames[FullScenarioIndex]);
			const double Deficit = GlobalDeficit - LocalPenalty;
			const uint64 Tie = MixParameterBits(
				GetParameterBits(TEXT("contact_facing_tie"))
					^ static_cast<uint64>(FacingIndex));
			if (Deficit > BestFacingDeficit + 1e-9
				|| (FMath::IsNearlyEqual(Deficit, BestFacingDeficit)
					&& Tie > BestFacingTie))
			{
				BestFacingDeficit = Deficit;
				BestFacingTie = Tie;
				SelectedFacingIndex = FacingIndex;
			}
		}
		if (SelectedFacingIndex == INDEX_NONE)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		LocomotionFacingProfile =
			static_cast<ELocomotionFacingProfile>(SelectedFacingIndex);
		CurrentContactScenarioIndex =
			(BaseScenarioIndex * FacingProfileCount) + SelectedFacingIndex;
	}
	bCoverageFacingProfileRequired = true;
	const FContactTargetDefinition& Target =
		GetContactTargetDefinition(ContactTargetIndex);
	if (LocomotionFacingProfile == ELocomotionFacingProfile::FreeAttention)
	{
		GuidedCameraStyle = static_cast<EGuidedCameraStyle>(
			SelectFrameDeficitBucket(
				OverallGuidedCameraStyleObservationFrames,
				GuidedCameraStyleFrameShares,
				static_cast<int32>(EGuidedCameraStyle::Count),
				TransitionsPerEpisode + 1,
				GetParameterBits(TEXT("contact_free_camera_style_tie"))));
	}
	CoverageCameraOffset = FVector(
		SampleStratifiedRange(
			TEXT("contact_camera_lateral_offset"),
			6,
			-180.0f,
			180.0f),
		SampleStratifiedRange(
			TEXT("contact_camera_depth_offset"),
			6,
			-180.0f,
			180.0f),
		SampleStratifiedRange(
			TEXT("contact_camera_height_offset"),
			6,
			-55.0f,
			95.0f));

	const auto BuildRecoveryGoal =
		[this](
			const FVector& ContactPoint,
			const FVector& OutwardDirection,
			const int32 Attempt)
	{
		const FVector Tangent(
			-OutwardDirection.Y,
			OutwardDirection.X,
			0.0f);
		const float RecoveryDistance = SampleStratifiedRange(
			TEXT("contact_recovery_distance"),
			8,
			400.0f,
			640.0f,
			Attempt);
		switch (ContactRecoveryStyle)
		{
		case EContactRecoveryStyle::StrafeLeft:
			return ContactPoint
				+ (Tangent * RecoveryDistance)
				+ (OutwardDirection * 220.0f);
		case EContactRecoveryStyle::StrafeRight:
			return ContactPoint
				- (Tangent * RecoveryDistance)
				+ (OutwardDirection * 220.0f);
		case EContactRecoveryStyle::DiagonalLeft:
			return ContactPoint
				+ ((OutwardDirection + (Tangent * 0.8f)).GetSafeNormal2D()
					* RecoveryDistance);
		case EContactRecoveryStyle::DiagonalRight:
			return ContactPoint
				+ ((OutwardDirection - (Tangent * 0.8f)).GetSafeNormal2D()
					* RecoveryDistance);
		default:
			return ContactPoint + (OutwardDirection * RecoveryDistance);
		}
	};

	bool bFoundGeometry = false;
	for (int32 Attempt = 0; Attempt < 128; ++Attempt)
	{
		FVector OutwardDirection = FVector::ForwardVector;
		FVector CandidateContactPoint = FVector::ZeroVector;
		FVector CandidateStart = FVector::ZeroVector;
		if (Target.bWall)
		{
			const FVector Tangent(
				-Target.WallInwardNormal.Y,
				Target.WallInwardNormal.X,
				0.0f);
			const float AlongWall = SampleStratifiedRange(
				TEXT("wall_contact_along"),
				8,
				-980.0f,
				980.0f,
				Attempt);
			OutwardDirection = Target.WallInwardNormal;
			CandidateContactPoint =
				Target.LookTarget
					+ (Tangent * AlongWall)
					+ (Target.WallInwardNormal * 80.0f);
			CandidateStart =
				CandidateContactPoint
					+ (Target.WallInwardNormal
						* SampleStratifiedRange(
							TEXT("wall_contact_start_distance"),
							8,
							500.0f,
							840.0f,
							Attempt));
			const float GlanceOffset =
				SampleStratifiedRange(
					TEXT("wall_contact_glance_offset"),
					6,
					220.0f,
					420.0f,
					Attempt);
			if (ContactApproachProfile == EContactApproachProfile::GlanceLeft)
			{
				CandidateStart -= Tangent * GlanceOffset;
			}
			else if (ContactApproachProfile
				== EContactApproachProfile::GlanceRight)
			{
				CandidateStart += Tangent * GlanceOffset;
			}
			CurrentContactApproachSector =
				FMath::Clamp(
					FMath::FloorToInt((AlongWall + 980.0f) / 245.0f),
					0,
					7);
		}
		else
		{
			const float ApproachAngleDegrees = SampleStratifiedRange(
				TEXT("object_contact_approach_angle"),
				8,
				-180.0f,
				180.0f,
				Attempt);
			const float ApproachAngle =
				FMath::DegreesToRadians(ApproachAngleDegrees);
			OutwardDirection = FVector(
				FMath::Cos(ApproachAngle),
				FMath::Sin(ApproachAngle),
				0.0f);
			CandidateContactPoint =
				Target.LookTarget
					+ (OutwardDirection * Target.ContactRadiusCm);
			CandidateStart =
				Target.LookTarget
					+ (OutwardDirection
						* (Target.ContactRadiusCm
							+ SampleStratifiedRange(
								TEXT("object_contact_start_distance"),
								8,
								480.0f,
								820.0f,
								Attempt)));
			const FVector Tangent(
				-OutwardDirection.Y,
				OutwardDirection.X,
				0.0f);
			const float GlanceOffset =
				SampleStratifiedRange(
					TEXT("object_contact_glance_offset"),
					6,
					220.0f,
					400.0f,
					Attempt);
			if (ContactApproachProfile == EContactApproachProfile::GlanceLeft)
			{
				CandidateStart -= Tangent * GlanceOffset;
			}
			else if (ContactApproachProfile
				== EContactApproachProfile::GlanceRight)
			{
				CandidateStart += Tangent * GlanceOffset;
			}
			CurrentContactApproachSector =
				FMath::Clamp(
					FMath::FloorToInt((ApproachAngleDegrees + 180.0f) / 45.0f),
					0,
					7);
		}
		const FVector CandidateRecoveryGoal =
			BuildRecoveryGoal(
				CandidateContactPoint,
				OutwardDirection,
				Attempt);
		const bool bPathClear =
			Target.bWall
				? SegmentClearsOtherLearningObjects(
					CandidateStart,
					CandidateContactPoint,
					INDEX_NONE)
				: SegmentClearsOtherLearningObjects(
					CandidateStart,
					Target.LookTarget,
					ContactTargetIndex);
		if (IsInsideSamplingArena(CandidateStart)
			&& IsInsideSamplingArena(CandidateRecoveryGoal)
			&& bPathClear)
		{
			CoverageContactPoint = CandidateContactPoint;
			CoverageMissionStart = CandidateStart;
			CoverageRecoveryGoal = CandidateRecoveryGoal;
			CoverageWaypoints.Add(
				CandidateContactPoint + (OutwardDirection * 280.0f));
			CoverageMissionGoal =
				Target.bWall ? CandidateContactPoint : Target.LookTarget;
			bFoundGeometry = true;
			break;
		}
	}
	if (!bFoundGeometry)
	{
		bCoverageMissionConfigurationValid = false;
		return;
	}

	CoverageMissionStart.Z = 100.0f;
	CoverageContactPoint.Z = 100.0f;
	CoverageRecoveryGoal.Z = 100.0f;
	CoverageMissionGoal.Z = 100.0f;
	if (!CoverageWaypoints.IsEmpty())
	{
		CoverageWaypoints[0].Z = 100.0f;
	}
	CoverageLookTarget =
		Target.bWall ? CoverageContactPoint : Target.LookTarget;
	CoverageRequiredContactHoldSteps = FMath::RoundToInt(
		SampleStratifiedRange(
			TEXT("contact_hold_seconds"),
			6,
			0.20f,
			0.70f)
		* static_cast<float>(ObservationRate));
	CoverageRequiredRecoverySteps = FMath::RoundToInt(
		SampleStratifiedRange(
			TEXT("contact_recovery_hold_seconds"),
			6,
			0.25f,
			0.80f)
		* static_cast<float>(ObservationRate));
	ConfigureInitialFacingTarget(CoverageMissionGoal);
}

void ACurriculumDataGenerator::ConfigureRampMission()
{
	int32 DirectionIndex = 0;
	if (bPrescribedRecipes)
	{
		if (CurrentPrescribedScenarioIndex < 0
			|| CurrentPrescribedScenarioIndex >= RampScenarioCount)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CurrentRampScenarioIndex = CurrentPrescribedScenarioIndex;
		DirectionIndex = CurrentRampScenarioIndex / 15;
		RampPathProfile = static_cast<ERampPathProfile>(
			(CurrentRampScenarioIndex / 5) % 3);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			CurrentRampScenarioIndex % 5);
	}
	else if (bMissionReviewSuite)
	{
		const int32 ReviewVariantIndex = EpisodeIndex - 40;
		DirectionIndex = ReviewVariantIndex / 5;
		RampPathProfile = static_cast<ERampPathProfile>(
			ReviewVariantIndex
				% static_cast<int32>(ERampPathProfile::Count));
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			ReviewVariantIndex
				% static_cast<int32>(ELocomotionFacingProfile::Count));
	}
	else
	{
		int32 RequiredDirectionIndex = INDEX_NONE;
		if (MissionDirectionOverride == TEXT("uphill"))
		{
			RequiredDirectionIndex = 0;
		}
		else if (MissionDirectionOverride == TEXT("downhill"))
		{
			RequiredDirectionIndex = 1;
		}
		bool bHasUnseenAllowedScenario = false;
		int64 AllowedFrameTotal = 0;
		double AllowedShareTotal = 0.0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < RampScenarioCount;
			++ScenarioIndex)
		{
			const int32 ScenarioDirection = ScenarioIndex / 15;
			if (RequiredDirectionIndex != INDEX_NONE
				&& ScenarioDirection != RequiredDirectionIndex)
			{
				continue;
			}
			const int32 ScenarioPath = (ScenarioIndex / 5) % 3;
			const int32 ScenarioFacing = ScenarioIndex % 5;
			bHasUnseenAllowedScenario =
				bHasUnseenAllowedScenario
				|| OverallRampScenarioObservationFrames[ScenarioIndex] == 0;
			AllowedFrameTotal +=
				OverallRampScenarioObservationFrames[ScenarioIndex];
			AllowedShareTotal +=
				0.5
				* GuidedPathFrameShares[ScenarioPath]
				* LocomotionFacingFrameShares[ScenarioFacing];
		}
		double BestDeficit = -DBL_MAX;
		uint64 BestTie = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < RampScenarioCount;
			++ScenarioIndex)
		{
			const int32 ScenarioDirection = ScenarioIndex / 15;
			if ((RequiredDirectionIndex != INDEX_NONE
					&& ScenarioDirection != RequiredDirectionIndex)
				|| (bHasUnseenAllowedScenario
					&& OverallRampScenarioObservationFrames[ScenarioIndex] != 0))
			{
				continue;
			}
			const int32 ScenarioPath = (ScenarioIndex / 5) % 3;
			const int32 ScenarioFacing = ScenarioIndex % 5;
			const double ScenarioShare =
				0.5
				* GuidedPathFrameShares[ScenarioPath]
				* LocomotionFacingFrameShares[ScenarioFacing];
			const double ProjectedTotal = static_cast<double>(
				AllowedFrameTotal + FMath::Max(1, TransitionsPerEpisode + 1));
			const double Deficit =
				(ProjectedTotal
					* ScenarioShare
					/ FMath::Max(AllowedShareTotal, UE_DOUBLE_SMALL_NUMBER))
				- static_cast<double>(
					OverallRampScenarioObservationFrames[ScenarioIndex]);
			const uint64 Tie = MixParameterBits(
				GetParameterBits(TEXT("ramp_scenario_tie"))
					^ static_cast<uint64>(ScenarioIndex));
			if (Deficit > BestDeficit + 1e-9
				|| (FMath::IsNearlyEqual(Deficit, BestDeficit) && Tie > BestTie))
			{
				BestDeficit = Deficit;
				BestTie = Tie;
				CurrentRampScenarioIndex = ScenarioIndex;
			}
		}
		if (CurrentRampScenarioIndex == INDEX_NONE)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		DirectionIndex = CurrentRampScenarioIndex / 15;
		RampPathProfile = static_cast<ERampPathProfile>(
			(CurrentRampScenarioIndex / 5) % 3);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			CurrentRampScenarioIndex % 5);
	}
	RampDirection =
		DirectionIndex == 0
			? ERampDirection::Uphill
			: ERampDirection::Downhill;
	if (bMissionReviewSuite)
	{
		CurrentRampScenarioIndex =
			((DirectionIndex * static_cast<int32>(ERampPathProfile::Count))
				+ static_cast<int32>(RampPathProfile))
				* static_cast<int32>(ELocomotionFacingProfile::Count)
			+ static_cast<int32>(LocomotionFacingProfile);
	}
	bCoverageFacingProfileRequired = true;

	if (LocomotionFacingProfile == ELocomotionFacingProfile::FreeAttention)
	{
		GuidedCameraStyle = static_cast<EGuidedCameraStyle>(
			SelectFrameDeficitBucket(
				OverallGuidedCameraStyleObservationFrames,
				GuidedCameraStyleFrameShares,
				static_cast<int32>(EGuidedCameraStyle::Count),
				TransitionsPerEpisode + 1,
				GetParameterBits(TEXT("ramp_free_camera_style_tie"))));
	}
	CoverageCameraOffset = FVector(
		0.0f,
		SampleStratifiedRange(
			TEXT("ramp_camera_lateral_offset"),
			6,
			-160.0f,
			160.0f),
		SampleStratifiedRange(
			TEXT("ramp_camera_height_offset"),
			6,
			-45.0f,
			90.0f));
	float EntryY = 0.0f;
	float ExitY = 0.0f;
	if (RampPathProfile == ERampPathProfile::DiagonalLeftToRight)
	{
		EntryY = SampleStratifiedRange(
			TEXT("ramp_diagonal_left_entry_y"),
			6,
			-78.0f,
			-50.0f);
		ExitY = SampleStratifiedRange(
			TEXT("ramp_diagonal_left_exit_y"),
			6,
			50.0f,
			78.0f);
	}
	else if (RampPathProfile == ERampPathProfile::DiagonalRightToLeft)
	{
		EntryY = SampleStratifiedRange(
			TEXT("ramp_diagonal_right_entry_y"),
			6,
			50.0f,
			78.0f);
		ExitY = SampleStratifiedRange(
			TEXT("ramp_diagonal_right_exit_y"),
			6,
			-78.0f,
			-50.0f);
	}
	else
	{
		EntryY = SampleStratifiedRange(
			TEXT("ramp_center_entry_y"),
			6,
			-24.0f,
			24.0f);
		ExitY = SampleStratifiedRange(
			TEXT("ramp_center_exit_y"),
			6,
			-24.0f,
			24.0f);
	}
	if (RampDirection == ERampDirection::Uphill)
	{
		CoverageMissionStart = FVector(
			SampleStratifiedRange(
				TEXT("ramp_uphill_start_x"),
				8,
				620.0f,
				930.0f),
			EntryY
				+ SampleParameterRange(
					TEXT("ramp_uphill_start_y_jitter"),
					-32.0f,
					32.0f),
			100.0f);
		CoverageWaypoints.Add(FVector(230.0f, EntryY, 100.0f));
		CoverageWaypoints.Add(FVector(-275.0f, ExitY, 190.0f));
		CoverageMissionGoal = FVector(
			SampleStratifiedRange(
				TEXT("ramp_uphill_goal_x"),
				8,
				-850.0f,
				-580.0f),
			ExitY
				+ SampleParameterRange(
					TEXT("ramp_uphill_goal_y_jitter"),
					-40.0f,
					40.0f),
			100.0f);
		CoverageWaypoints.Add(CoverageMissionGoal);
	}
	else
	{
		const float StartX = SampleStratifiedRange(
			TEXT("ramp_downhill_start_x"),
			8,
			-190.0f,
			-105.0f);
		CoverageMissionStart = FVector(
			StartX,
			EntryY,
			RampTopSurfaceZ(StartX) + CharacterStandingHalfHeightCm + 4.0f);
		CoverageWaypoints.Add(FVector(215.0f, ExitY, 112.0f));
		CoverageMissionGoal = FVector(
			SampleStratifiedRange(
				TEXT("ramp_downhill_goal_x"),
				8,
				580.0f,
				900.0f),
			ExitY
				+ SampleParameterRange(
					TEXT("ramp_downhill_goal_y_jitter"),
					-40.0f,
					40.0f),
			100.0f);
		CoverageWaypoints.Add(CoverageMissionGoal);
	}
	CoverageLookTarget = CoverageMissionGoal;
	ConfigureInitialFacingTarget(CoverageWaypoints[0]);
}

void ACurriculumDataGenerator::ConfigureHoopMission()
{
	int32 DirectionIndex = 0;
	if (bPrescribedRecipes)
	{
		if (CurrentPrescribedScenarioIndex < 0
			|| CurrentPrescribedScenarioIndex >= HoopScenarioCount)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		CurrentHoopScenarioIndex = CurrentPrescribedScenarioIndex;
		DirectionIndex = CurrentHoopScenarioIndex / 15;
		HoopPathProfile = static_cast<EHoopPathProfile>(
			(CurrentHoopScenarioIndex / 5) % 3);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			CurrentHoopScenarioIndex % 5);
	}
	else if (bMissionReviewSuite)
	{
		const int32 ReviewVariantIndex = EpisodeIndex - 50;
		DirectionIndex = ReviewVariantIndex / 5;
		HoopPathProfile = static_cast<EHoopPathProfile>(
			ReviewVariantIndex
				% static_cast<int32>(EHoopPathProfile::Count));
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			ReviewVariantIndex
				% static_cast<int32>(ELocomotionFacingProfile::Count));
	}
	else
	{
		int32 RequiredDirectionIndex = INDEX_NONE;
		if (MissionDirectionOverride == TEXT("positive_x_to_negative_x"))
		{
			RequiredDirectionIndex = 0;
		}
		else if (MissionDirectionOverride == TEXT("negative_x_to_positive_x"))
		{
			RequiredDirectionIndex = 1;
		}
		bool bHasUnseenAllowedScenario = false;
		int64 AllowedFrameTotal = 0;
		double AllowedShareTotal = 0.0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < HoopScenarioCount;
			++ScenarioIndex)
		{
			const int32 ScenarioDirection = ScenarioIndex / 15;
			if (RequiredDirectionIndex != INDEX_NONE
				&& ScenarioDirection != RequiredDirectionIndex)
			{
				continue;
			}
			const int32 ScenarioPath = (ScenarioIndex / 5) % 3;
			const int32 ScenarioFacing = ScenarioIndex % 5;
			bHasUnseenAllowedScenario =
				bHasUnseenAllowedScenario
				|| OverallHoopScenarioObservationFrames[ScenarioIndex] == 0;
			AllowedFrameTotal +=
				OverallHoopScenarioObservationFrames[ScenarioIndex];
			AllowedShareTotal +=
				0.5
				* GuidedPathFrameShares[ScenarioPath]
				* LocomotionFacingFrameShares[ScenarioFacing];
		}
		double BestDeficit = -DBL_MAX;
		uint64 BestTie = 0;
		for (int32 ScenarioIndex = 0;
			ScenarioIndex < HoopScenarioCount;
			++ScenarioIndex)
		{
			const int32 ScenarioDirection = ScenarioIndex / 15;
			if ((RequiredDirectionIndex != INDEX_NONE
					&& ScenarioDirection != RequiredDirectionIndex)
				|| (bHasUnseenAllowedScenario
					&& OverallHoopScenarioObservationFrames[ScenarioIndex] != 0))
			{
				continue;
			}
			const int32 ScenarioPath = (ScenarioIndex / 5) % 3;
			const int32 ScenarioFacing = ScenarioIndex % 5;
			const double ScenarioShare =
				0.5
				* GuidedPathFrameShares[ScenarioPath]
				* LocomotionFacingFrameShares[ScenarioFacing];
			const double ProjectedTotal = static_cast<double>(
				AllowedFrameTotal + FMath::Max(1, TransitionsPerEpisode + 1));
			const double Deficit =
				(ProjectedTotal
					* ScenarioShare
					/ FMath::Max(AllowedShareTotal, UE_DOUBLE_SMALL_NUMBER))
				- static_cast<double>(
					OverallHoopScenarioObservationFrames[ScenarioIndex]);
			const uint64 Tie = MixParameterBits(
				GetParameterBits(TEXT("hoop_scenario_tie"))
					^ static_cast<uint64>(ScenarioIndex));
			if (Deficit > BestDeficit + 1e-9
				|| (FMath::IsNearlyEqual(Deficit, BestDeficit) && Tie > BestTie))
			{
				BestDeficit = Deficit;
				BestTie = Tie;
				CurrentHoopScenarioIndex = ScenarioIndex;
			}
		}
		if (CurrentHoopScenarioIndex == INDEX_NONE)
		{
			bCoverageMissionConfigurationValid = false;
			return;
		}
		DirectionIndex = CurrentHoopScenarioIndex / 15;
		HoopPathProfile = static_cast<EHoopPathProfile>(
			(CurrentHoopScenarioIndex / 5) % 3);
		LocomotionFacingProfile = static_cast<ELocomotionFacingProfile>(
			CurrentHoopScenarioIndex % 5);
	}
	bHoopPositiveToNegative = DirectionIndex == 0;
	const float StartSide = bHoopPositiveToNegative ? 1.0f : -1.0f;
	if (bMissionReviewSuite)
	{
		CurrentHoopScenarioIndex =
			((DirectionIndex * static_cast<int32>(EHoopPathProfile::Count))
				+ static_cast<int32>(HoopPathProfile))
				* static_cast<int32>(ELocomotionFacingProfile::Count)
			+ static_cast<int32>(LocomotionFacingProfile);
	}
	bCoverageFacingProfileRequired = true;
	if (LocomotionFacingProfile == ELocomotionFacingProfile::FreeAttention)
	{
		GuidedCameraStyle = static_cast<EGuidedCameraStyle>(
			SelectFrameDeficitBucket(
				OverallGuidedCameraStyleObservationFrames,
				GuidedCameraStyleFrameShares,
				static_cast<int32>(EGuidedCameraStyle::Count),
				TransitionsPerEpisode + 1,
				GetParameterBits(TEXT("hoop_free_camera_style_tie"))));
	}
	CoverageCameraOffset = FVector(
		0.0f,
		SampleStratifiedRange(
			TEXT("hoop_camera_lateral_offset"),
			6,
			-150.0f,
			150.0f),
		SampleStratifiedRange(
			TEXT("hoop_camera_height_offset"),
			6,
			-45.0f,
			85.0f));
	const float StartDistance = SampleStratifiedRange(
		TEXT("hoop_start_distance"),
		8,
		520.0f,
		650.0f);
	const float GoalDistance = SampleStratifiedRange(
		TEXT("hoop_goal_distance"),
		8,
		520.0f,
		650.0f);
	float StartLateralOffset = 0.0f;
	float GoalLateralOffset = 0.0f;
	if (HoopPathProfile == EHoopPathProfile::ObliqueLeftToRight)
	{
		StartLateralOffset = SampleStratifiedRange(
			TEXT("hoop_oblique_left_start_y"),
			6,
			-110.0f,
			-75.0f);
		GoalLateralOffset = SampleStratifiedRange(
			TEXT("hoop_oblique_left_goal_y"),
			6,
			75.0f,
			110.0f);
	}
	else if (HoopPathProfile == EHoopPathProfile::ObliqueRightToLeft)
	{
		StartLateralOffset = SampleStratifiedRange(
			TEXT("hoop_oblique_right_start_y"),
			6,
			75.0f,
			110.0f);
		GoalLateralOffset = SampleStratifiedRange(
			TEXT("hoop_oblique_right_goal_y"),
			6,
			-110.0f,
			-75.0f);
	}
	else
	{
		StartLateralOffset = SampleStratifiedRange(
			TEXT("hoop_center_start_y"),
			6,
			-18.0f,
			18.0f);
		GoalLateralOffset = SampleStratifiedRange(
			TEXT("hoop_center_goal_y"),
			6,
			-18.0f,
			18.0f);
	}
	CoverageMissionStart = FVector(
		700.0f + (StartSide * StartDistance),
		-700.0f + StartLateralOffset,
		100.0f);
	CoverageMissionGoal = FVector(
		700.0f - (StartSide * GoalDistance),
		-700.0f + GoalLateralOffset,
		100.0f);
	CoverageLookTarget = CoverageMissionGoal;
	CoverageRequiredHoopPasses = 1;
	ConfigureInitialFacingTarget(CoverageMissionGoal);
}

bool ACurriculumDataGenerator::GetCoverageMissionSpawn(
	FVector& OutLocation,
	float& OutYaw,
	float& OutPitch) const
{
	FVector LookTarget = FVector::ZeroVector;
	if (CoverageMission == ECoverageMission::ObjectView
		&& CoverageTargetIndex != INDEX_NONE)
	{
		OutLocation = CoverageMissionStart;
		LookTarget = CoverageLookTarget;
	}
	else if (CoverageMission == ECoverageMission::ContactRecovery
		&& ContactTargetIndex != INDEX_NONE)
	{
		OutLocation = CoverageMissionStart;
		LookTarget = bCoverageInitialLookTargetValid
			? CoverageInitialLookTarget
			: CoverageLookTarget;
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		OutLocation = CoverageMissionStart;
		LookTarget = bCoverageInitialLookTargetValid
			? CoverageInitialLookTarget
			: CoverageLookTarget;
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		// The hoop's opening lies in the YZ plane, so this path crosses it on X.
		OutLocation = CoverageMissionStart;
		LookTarget = bCoverageInitialLookTargetValid
			? CoverageInitialLookTarget
			: CoverageMissionGoal;
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
		-40.0f,
		30.0f);
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

FVector ACurriculumDataGenerator::GetLocomotionFacingDirection(
	const FVector& TravelDirection) const
{
	const FVector Forward = TravelDirection.GetSafeNormal2D();
	if (Forward.IsNearlyZero())
	{
		return FVector::ZeroVector;
	}
	switch (LocomotionFacingProfile)
	{
	case ELocomotionFacingProfile::Backward:
		return -Forward;
	case ELocomotionFacingProfile::StrafeLeft:
		return FVector(-Forward.Y, Forward.X, 0.0f);
	case ELocomotionFacingProfile::StrafeRight:
		return FVector(Forward.Y, -Forward.X, 0.0f);
	default:
		return Forward;
	}
}

void ACurriculumDataGenerator::ConfigureInitialFacingTarget(
	const FVector& FirstTravelGoal)
{
	const FVector TravelDirection =
		(FirstTravelGoal - CoverageMissionStart).GetSafeNormal2D();
	if (LocomotionFacingProfile == ELocomotionFacingProfile::FreeAttention)
	{
		CoverageInitialLookTarget = CoverageLookTarget;
	}
	else
	{
		const FVector FacingDirection =
			GetLocomotionFacingDirection(TravelDirection);
		if (FacingDirection.IsNearlyZero())
		{
			CoverageInitialLookTarget = CoverageLookTarget;
		}
		else
		{
			CoverageInitialLookTarget =
				CoverageMissionStart
				+ (FacingDirection * 1200.0f)
				+ FVector(0.0f, 0.0f, 64.0f);
		}
	}
	bCoverageInitialLookTargetValid = true;
}

FVector ACurriculumDataGenerator::SelectGuidedCameraTarget(
	const FVector& ObjectiveTarget,
	const FVector& TravelGoal) const
{
	if (!Character)
	{
		return ObjectiveTarget;
	}
	if (bCoverageFacingProfileRequired
		&& LocomotionFacingProfile != ELocomotionFacingProfile::FreeAttention)
	{
		const FVector TravelDirection =
			(TravelGoal - Character->GetActorLocation()).GetSafeNormal2D();
		const FVector FacingDirection =
			GetLocomotionFacingDirection(TravelDirection);
		if (!FacingDirection.IsNearlyZero())
		{
			return Character->GetActorLocation()
				+ (FacingDirection * 1200.0f)
				+ FVector(0.0f, 0.0f, 64.0f);
		}
	}
	const int32 SafeObservationRate = FMath::Max(1, ObservationRate);
	const int32 TwoSecondCycle = FMath::Max(2, SafeObservationRate * 2);
	const int32 CycleFrame = FrameIndex % TwoSecondCycle;
	switch (GuidedCameraStyle)
	{
	case EGuidedCameraStyle::ObjectiveOffset:
		return ObjectiveTarget + CoverageCameraOffset;
	case EGuidedCameraStyle::TravelReacquire:
		if (CycleFrame < (TwoSecondCycle * 2) / 3)
		{
			const FVector TravelDirection =
				(TravelGoal - Character->GetActorLocation()).GetSafeNormal2D();
			if (!TravelDirection.IsNearlyZero())
			{
				return Character->GetActorLocation()
					+ (TravelDirection * 1200.0f)
					+ FVector(0.0f, 0.0f, 70.0f + CoverageCameraOffset.Z);
			}
		}
		return ObjectiveTarget;
	case EGuidedCameraStyle::ScanReacquire:
		if (CycleFrame < TwoSecondCycle / 2)
		{
			const float Side = CycleFrame < TwoSecondCycle / 4 ? 1.0f : -1.0f;
			return ObjectiveTarget
				+ FVector(
					CoverageCameraOffset.X * Side,
					CoverageCameraOffset.Y * Side,
					CoverageCameraOffset.Z);
		}
		return ObjectiveTarget;
	default:
		return ObjectiveTarget;
	}
}

uint16 ACurriculumDataGenerator::SelectCoverageGuidedAction()
{
	if (!Character)
	{
		return 0;
	}

	uint16 ActionMask = 0;
	if (CoverageMission == ECoverageMission::ObjectView
		&& CoverageTargetIndex != INDEX_NONE)
	{
		UpdateObjectGazeTarget(Character->GetActorLocation());
		ActionMask |= CameraBitsToward(CurrentObjectGazeTarget);

		if (!CoverageWaypoints.IsEmpty())
		{
			const int32 SafeWaypointIndex =
				FMath::Clamp(CoverageWaypointIndex, 0, CoverageWaypoints.Num() - 1);
			FVector Waypoint = CoverageWaypoints[SafeWaypointIndex];
			Waypoint.Z = Character->GetActorLocation().Z;
			const float DistanceToWaypoint =
				FVector::Dist2D(Character->GetActorLocation(), Waypoint);
			const float WaypointAcceptanceRadius =
				ObjectViewMode == EObjectViewMode::PartialOrbit
					|| ObjectViewMode == EObjectViewMode::FullOrbit
					? 75.0f
					: 115.0f;
			const bool bAtWaypoint =
				DistanceToWaypoint < WaypointAcceptanceRadius;
			if (bAtWaypoint
				&& CoverageWaypointIndex + 1 < CoverageWaypoints.Num())
			{
				++CoverageWaypointIndex;
				Waypoint = CoverageWaypoints[CoverageWaypointIndex];
				Waypoint.Z = Character->GetActorLocation().Z;
			}

			const bool bObserveAtGoal =
				ObjectViewMode == EObjectViewMode::ApproachObserve
				&& bAtWaypoint;
			// Translation follows the world-space mission path independently of
			// camera gaze. WorldDirectionToMovementBits converts that path to
			// camera-relative WASD, so looking away produces natural strafing or
			// backpedaling instead of pausing the mission.
			if (!bObserveAtGoal)
			{
				ActionMask |= WorldDirectionToMovementBits(
					Waypoint - Character->GetActorLocation());
			}
		}
	}
	else if (CoverageMission == ECoverageMission::ContactRecovery
		&& ContactTargetIndex != INDEX_NONE)
	{
		if (ContactPhase == EContactPhase::Recover)
		{
			FVector ActiveRecoveryGoal = CoverageRecoveryGoal;
			if (!CoverageWaypoints.IsEmpty()
				&& CoverageWaypointIndex < CoverageWaypoints.Num())
			{
				const FVector ClearanceGoal =
					CoverageWaypoints[CoverageWaypointIndex];
				if (FVector::Dist2D(
						Character->GetActorLocation(),
						ClearanceGoal) < 110.0f)
				{
					++CoverageWaypointIndex;
				}
				else
				{
					ActiveRecoveryGoal = ClearanceGoal;
				}
			}
			ActionMask |= CameraBitsToward(
				SelectGuidedCameraTarget(
					CoverageLookTarget,
					ActiveRecoveryGoal));
			ActionMask |= WorldDirectionToMovementBits(
				ActiveRecoveryGoal - Character->GetActorLocation());
		}
		else
		{
			ActionMask |= CameraBitsToward(
				SelectGuidedCameraTarget(
					CoverageLookTarget,
					CoverageMissionGoal));
			ActionMask |= WorldDirectionToMovementBits(
				CoverageMissionGoal - Character->GetActorLocation());
		}
	}
	else if (CoverageMission == ECoverageMission::RampTraverse)
	{
		FVector Waypoint = CoverageMissionGoal;
		if (!CoverageWaypoints.IsEmpty())
		{
			const int32 SafeWaypointIndex =
				FMath::Clamp(CoverageWaypointIndex, 0, CoverageWaypoints.Num() - 1);
			Waypoint = CoverageWaypoints[SafeWaypointIndex];
		}
		if (!CoverageWaypoints.IsEmpty()
			&& FVector::Dist2D(Character->GetActorLocation(), Waypoint) < 115.0f
			&& CoverageWaypointIndex + 1 < CoverageWaypoints.Num())
		{
			++CoverageWaypointIndex;
			Waypoint = CoverageWaypoints[CoverageWaypointIndex];
		}
		ActionMask |= CameraBitsToward(
			SelectGuidedCameraTarget(CoverageMissionGoal, Waypoint));
		ActionMask |= WorldDirectionToMovementBits(
			Waypoint - Character->GetActorLocation());
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		ActionMask |= CameraBitsToward(
			SelectGuidedCameraTarget(
				FVector(700.0f, -700.0f, 135.0f),
				CoverageMissionGoal));
		ActionMask |= WorldDirectionToMovementBits(
			CoverageMissionGoal - Character->GetActorLocation());
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

void ACurriculumDataGenerator::UpdateCoverageMetrics(
	const FRecordedState& State,
	const int32 ObservationIndex)
{
	bCurrentCoverageTargetVisible = false;
	CurrentCoveragePositionBin = INDEX_NONE;
	CurrentCoveragePositionDistanceBand = INDEX_NONE;
	CurrentMovementCameraYawDeltaDegrees = 0.0f;

	if (CoverageMission == ECoverageMission::SemiMarkov)
	{
		if (State.bContact)
		{
			if (NaturalPlayContactSteps == 0)
			{
				NaturalPlayContactLimitSteps =
					4 + SampleParameterIndex(
						TEXT("natural_play_contact_limit_steps"),
						7,
						NaturalPlayContactEventIndex);
				++NaturalPlayContactEventIndex;
			}
			++NaturalPlayContactSteps;
			NaturalPlayMaximumContactSteps = FMath::Max(
				NaturalPlayMaximumContactSteps,
				NaturalPlayContactSteps);
		}
		else
		{
			NaturalPlayContactSteps = 0;
			NaturalPlayContactLimitSteps = 0;
		}
	}

	// Success is latched. Post-success observations remain valid transition data,
	// but they must not change mission credit, facing ratios, visibility masks,
	// or completion counters.
	if (bCoverageMissionSucceeded)
	{
		++CoveragePostSuccessSteps;
		++OverallPostSuccessObservationFrames;
		if (CoverageMission == ECoverageMission::ObjectView
			&& CoverageTargetIndex != INDEX_NONE)
		{
			bCurrentCoverageTargetVisible =
				IsCoverageTargetVisible(CoverageTargetIndex);
		}
		CoveragePreviousPosition = State.Position;
		bCoveragePreviousPositionValid = true;
		return;
	}

	const bool bMissionSucceededBeforeUpdate = bCoverageMissionSucceeded;

	if (bCoverageFacingProfileRequired
		&& CoverageMission != ECoverageMission::SemiMarkov)
	{
		const float Speed2D = State.Velocity.Size2D();
		const bool bContactTravelInProgress =
			CoverageMission == ECoverageMission::ContactRecovery
			&& ContactPhase == EContactPhase::Approach
			&& !bCoverageFacingMeasurementComplete
			&& FVector::Dist2D(
				State.Position,
				CoverageContactPoint) > 130.0f;
		const bool bInBehaviorRegion =
			(CoverageMission == ECoverageMission::RampTraverse
				&& FMath::Abs(State.Position.X) < 350.0f)
			|| (CoverageMission == ECoverageMission::HoopPass
				&& FMath::Abs(State.Position.X - 700.0f) < 400.0f)
			|| bContactTravelInProgress;
		if (Speed2D > 80.0f && bInBehaviorRegion)
		{
			const float MovementYaw = State.Velocity.Rotation().Yaw;
			CurrentMovementCameraYawDeltaDegrees =
				FMath::FindDeltaAngleDegrees(
					State.CameraRotation.Yaw,
					MovementYaw);
			++CurrentEpisodeFacingMovingFrames;
			const float AbsoluteDelta =
				FMath::Abs(CurrentMovementCameraYawDeltaDegrees);
			bool bFacingMatched = false;
			switch (LocomotionFacingProfile)
			{
			case ELocomotionFacingProfile::Backward:
				bFacingMatched = AbsoluteDelta >= 135.0f;
				break;
			case ELocomotionFacingProfile::StrafeLeft:
				bFacingMatched =
					CurrentMovementCameraYawDeltaDegrees > -135.0f
					&& CurrentMovementCameraYawDeltaDegrees <= -45.0f;
				break;
			case ELocomotionFacingProfile::StrafeRight:
				bFacingMatched =
					CurrentMovementCameraYawDeltaDegrees >= 45.0f
					&& CurrentMovementCameraYawDeltaDegrees < 135.0f;
				break;
			case ELocomotionFacingProfile::FreeAttention:
				bFacingMatched = true;
				break;
			default:
				bFacingMatched = AbsoluteDelta < 45.0f;
				break;
			}
			if (bFacingMatched)
			{
				++CurrentEpisodeFacingMatchedFrames;
			}
		}
	}

	if (CoverageMission == ECoverageMission::ObjectView
		&& CoverageTargetIndex != INDEX_NONE)
	{
		const int32 GazeIntentIndex =
			static_cast<int32>(CurrentObjectGazeIntent);
		if (GazeIntentIndex >= 0
			&& GazeIntentIndex < UE_ARRAY_COUNT(CurrentEpisodeObjectGazeIntentFrames))
		{
			++CurrentEpisodeObjectGazeIntentFrames[GazeIntentIndex];
		}
		const FCoverageTargetDefinition& Target =
			GetCoverageTargetDefinition(CoverageTargetIndex);
		const FVector RelativePosition = State.Position - Target.LookTarget;
		const float Distance = RelativePosition.Size2D();
		const float Angle = FMath::Atan2(RelativePosition.Y, RelativePosition.X);
		const float NormalizedAngle =
			FMath::Fmod(Angle + (2.0f * PI), 2.0f * PI);
		CurrentCoveragePositionBin = FMath::Clamp(
			FMath::FloorToInt(
				NormalizedAngle
				* static_cast<float>(CoverageAzimuthBinCount)
				/ (2.0f * PI)),
			0,
			CoverageAzimuthBinCount - 1);
		CurrentCoveragePositionDistanceBand =
			Distance < 400.0f ? 0 : (Distance < 750.0f ? 1 : 2);
		const uint16 BinBit =
			static_cast<uint16>(1u << CurrentCoveragePositionBin);
		CurrentEpisodeVisitedBinsMask |= BinBit;
		OverallObjectVisitedBins[CoverageTargetIndex] |= BinBit;
		bCurrentCoverageTargetVisible =
			IsCoverageTargetVisible(CoverageTargetIndex);
		if (bCurrentCoverageTargetVisible)
		{
			CurrentEpisodeViewBinsMask |= BinBit;
			OverallObjectViewBins[CoverageTargetIndex] |= BinBit;
		}
		if (ObjectViewMode == EObjectViewMode::ApproachObserve)
		{
			const bool bAtObservationGoal =
				FVector::Dist2D(State.Position, CoverageMissionGoal) < 135.0f;
			CoverageVisibleHoldSteps =
				bAtObservationGoal && bCurrentCoverageTargetVisible
					? CoverageVisibleHoldSteps + 1
					: 0;
			bCoveragePrimaryObjectiveAchieved =
				bCoveragePrimaryObjectiveAchieved
				|| CoverageVisibleHoldSteps >= CoverageRequiredVisibleHoldSteps;
		}
		else if (ObjectViewMode == EObjectViewMode::PassBy)
		{
			if (bCurrentCoverageTargetVisible)
			{
				++CoverageVisibleHoldSteps;
			}
			bCoveragePrimaryObjectiveAchieved =
				bCoveragePrimaryObjectiveAchieved
				|| (FVector::Dist2D(State.Position, CoverageMissionGoal) < 140.0f
					&& CoverageVisibleHoldSteps
						>= CoverageRequiredVisibleHoldSteps);
		}
		else
		{
			const uint16 VisitedRequiredBins =
				CurrentEpisodeVisitedBinsMask
				& CoverageRequiredAzimuthBinsMask;
			bCoveragePrimaryObjectiveAchieved =
				bCoveragePrimaryObjectiveAchieved
				|| (CoverageRequiredAzimuthBinCount > 0
				&& FMath::CountBits(
					static_cast<uint32>(VisitedRequiredBins))
					>= CoverageRequiredAzimuthBinCount);
		}
	}
	else if (CoverageMission == ECoverageMission::ContactRecovery
		&& ContactTargetIndex != INDEX_NONE)
	{
		const bool bMatchingContact =
			State.bContact && IsCurrentContactTarget(State.ContactObject);
		if (ContactPhase == EContactPhase::Approach && bMatchingContact)
		{
			bCoverageFacingMeasurementComplete = true;
			ContactPhase = EContactPhase::Hold;
			CoverageContactHoldSteps = 1;
			CoverageVerifiedContactSteps = 1;
		}
		else if (ContactPhase == EContactPhase::Hold)
		{
			++CoverageContactHoldSteps;
			if (bMatchingContact)
			{
				++CoverageVerifiedContactSteps;
			}
			const bool bLostTarget =
				!bMatchingContact
				&& FVector::Dist2D(State.Position, CoverageContactPoint) > 220.0f;
			if (bLostTarget)
			{
				ContactPhase = EContactPhase::Approach;
				CoverageContactHoldSteps = 0;
				CoverageVerifiedContactSteps = 0;
			}
			else if (CoverageContactHoldSteps >= CoverageRequiredContactHoldSteps
				&& CoverageVerifiedContactSteps
					>= FMath::Min(2, CoverageRequiredContactHoldSteps))
			{
				ContactPhase = EContactPhase::Recover;
				CoverageWaypointIndex = 0;
				CoverageRecoverySteps = 0;
			}
		}
		else if (ContactPhase == EContactPhase::Recover)
		{
			const bool bSeparated =
				!bMatchingContact
				&& FVector::Dist2D(State.Position, CoverageContactPoint) > 180.0f;
			CoverageRecoverySteps =
				bSeparated ? CoverageRecoverySteps + 1 : 0;
			bCoveragePrimaryObjectiveAchieved =
				bCoveragePrimaryObjectiveAchieved
				|| CoverageRecoverySteps >= CoverageRequiredRecoverySteps;
		}
	}

	if (CoverageMission == ECoverageMission::RampTraverse)
	{
		// Floor actor Z is about 96 cm. A substantially higher capsule center
		// proves that the character mounted the inclined collision surface.
		bRampMounted = bRampMounted || State.Position.Z > 145.0f;
		const bool bReachedFarSide =
			RampDirection == ERampDirection::Uphill
				? State.Position.X < -340.0f
				: State.Position.X > 340.0f;
		if (!bCoveragePrimaryObjectiveAchieved
			&& bRampMounted
			&& bReachedFarSide
			&& FMath::Abs(State.Position.Y) < 165.0f)
		{
			++CurrentEpisodeRampTraversals;
			++OverallRampTraversals;
			bRampMounted = false;
			bCoveragePrimaryObjectiveAchieved = true;
		}
	}
	else if (CoverageMission == ECoverageMission::HoopPass)
	{
		const float CurrentSide = State.Position.X - 700.0f;
		if (!bCoveragePrimaryObjectiveAchieved
			&& bCoveragePreviousPositionValid)
		{
			const float DeltaX = State.Position.X - CoveragePreviousPosition.X;
			if (FMath::Abs(DeltaX) > UE_KINDA_SMALL_NUMBER)
			{
				const float CrossingAlpha =
					(700.0f - CoveragePreviousPosition.X) / DeltaX;
				if (CrossingAlpha >= 0.0f && CrossingAlpha <= 1.0f)
				{
					const FVector CrossingPosition =
						FMath::Lerp(
							CoveragePreviousPosition,
							State.Position,
							CrossingAlpha);
					CoverageLastHoopCrossingY = CrossingPosition.Y;
					CoverageLastHoopCrossingZ = CrossingPosition.Z;
					bCoverageHoopCrossingRecorded = true;
					if (FMath::Abs(CrossingPosition.Y + 700.0f) < 90.0f
						&& CrossingPosition.Z >= 80.0f
						&& CrossingPosition.Z <= 145.0f)
					{
						++CurrentEpisodeHoopPasses;
						++OverallHoopPasses;
						bCoveragePrimaryObjectiveAchieved =
							CurrentEpisodeHoopPasses
								>= CoverageRequiredHoopPasses;
					}
				}
			}
		}
		CoverageLastHoopSide = CurrentSide;
	}

	if (CoverageMission != ECoverageMission::SemiMarkov
		&& bCoveragePrimaryObjectiveAchieved)
	{
		float CompletionDistance = 150.0f;
		FVector CompletionGoal = CoverageMissionGoal;
		if (CoverageMission == ECoverageMission::ContactRecovery)
		{
			CompletionGoal = CoverageRecoveryGoal;
			CompletionDistance = 180.0f;
		}
		else if (CoverageMission == ECoverageMission::HoopPass)
		{
			CompletionDistance = 165.0f;
		}
		const bool bAtCompletionGoal =
			FVector::Dist2D(State.Position, CompletionGoal) < CompletionDistance;
		CoveragePostObjectiveSteps =
			bAtCompletionGoal ? CoveragePostObjectiveSteps + 1 : 0;
		const float FacingMatchRatio =
			CurrentEpisodeFacingMovingFrames > 0
				? static_cast<float>(CurrentEpisodeFacingMatchedFrames)
					/ static_cast<float>(CurrentEpisodeFacingMovingFrames)
				: 0.0f;
		const bool bFacingSatisfied =
			!bCoverageFacingProfileRequired
			|| LocomotionFacingProfile
				== ELocomotionFacingProfile::FreeAttention
			|| (CurrentEpisodeFacingMovingFrames >= 5
				&& FacingMatchRatio >= 0.45f);
		bCoverageMissionSucceeded =
			CoveragePostObjectiveSteps
				>= FMath::Max(1, CoverageRequiredPostObjectiveSteps)
			&& bFacingSatisfied;
	}

	if (!bMissionSucceededBeforeUpdate && bCoverageMissionSucceeded)
	{
		StartPostSuccessRollout(State, ObservationIndex);
	}

	if (CoverageMission != ECoverageMission::SemiMarkov
		&& !bCoverageMissionSucceeded
		&& bCoveragePreviousPositionValid)
	{
		const bool bIntentionalContactHold =
			CoverageMission == ECoverageMission::ContactRecovery
			&& ContactPhase == EContactPhase::Hold;
		const bool bMovementCommanded =
			(CurrentActionMask
				& (CurriculumAction::W
					| CurriculumAction::A
					| CurriculumAction::S
					| CurriculumAction::D)) != 0;
		const float Displacement =
			FVector::Dist2D(CoveragePreviousPosition, State.Position);
		CoverageNoProgressSteps =
			!bIntentionalContactHold
				&& bMovementCommanded
				&& Displacement < 1.0f
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
	case ECoverageMission::ObjectView:
		return TEXT("object_view");
	case ECoverageMission::ContactRecovery:
		return TEXT("contact_recovery");
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
	if (CoverageMission == ECoverageMission::ContactRecovery
		&& ContactTargetIndex != INDEX_NONE)
	{
		return GetContactTargetDefinition(ContactTargetIndex).Slug;
	}
	return CoverageTargetIndex != INDEX_NONE
		? GetCoverageTargetDefinition(CoverageTargetIndex).Slug
		: TEXT("");
}

FString ACurriculumDataGenerator::GetObjectViewModeSlug() const
{
	switch (ObjectViewMode)
	{
	case EObjectViewMode::PassBy:
		return TEXT("pass_by");
	case EObjectViewMode::PartialOrbit:
		return TEXT("partial_orbit");
	case EObjectViewMode::FullOrbit:
		return TEXT("full_orbit");
	default:
		return TEXT("approach_observe");
	}
}

FString ACurriculumDataGenerator::GetObjectGazePatternSlug() const
{
	switch (ObjectGazePattern)
	{
	case EObjectGazePattern::TargetOffset:
		return TEXT("target_offset");
	case EObjectGazePattern::TravelDirection:
		return TEXT("travel_direction");
	case EObjectGazePattern::RoamReacquire:
		return TEXT("roam_reacquire");
	default:
		return TEXT("target_center");
	}
}

FString ACurriculumDataGenerator::GetObjectGazeIntentSlug(
	const EObjectGazeIntent Intent) const
{
	switch (Intent)
	{
	case EObjectGazeIntent::TargetOffset:
		return TEXT("target_offset");
	case EObjectGazeIntent::TravelDirection:
		return TEXT("travel_direction");
	case EObjectGazeIntent::SurveyPoint:
		return TEXT("survey_point");
	default:
		return TEXT("target_center");
	}
}

FString ACurriculumDataGenerator::BuildObjectGazePlanJson() const
{
	FString Json = TEXT("[");
	for (int32 PhaseIndex = 0;
		PhaseIndex < ObjectGazePlanIntents.Num();
		++PhaseIndex)
	{
		if (PhaseIndex > 0)
		{
			Json += TEXT(",");
		}
		const int32 Duration =
			ObjectGazePlanDurations.IsValidIndex(PhaseIndex)
				? ObjectGazePlanDurations[PhaseIndex]
				: 1;
		const FVector Offset =
			ObjectGazePlanOffsets.IsValidIndex(PhaseIndex)
				? ObjectGazePlanOffsets[PhaseIndex]
				: FVector::ZeroVector;
		Json += FString::Printf(
			TEXT("{\"intent\":\"%s\",\"duration_steps\":%d,\"offset\":%s}"),
			*GetObjectGazeIntentSlug(ObjectGazePlanIntents[PhaseIndex]),
			Duration,
			*JsonVector(Offset));
	}
	Json += TEXT("]");
	return Json;
}

FString ACurriculumDataGenerator::GetContactPhaseSlug() const
{
	switch (ContactPhase)
	{
	case EContactPhase::Hold:
		return TEXT("hold");
	case EContactPhase::Recover:
		return TEXT("recover");
	default:
		return TEXT("approach");
	}
}

FString ACurriculumDataGenerator::GetContactRecoveryStyleSlug() const
{
	switch (ContactRecoveryStyle)
	{
	case EContactRecoveryStyle::StrafeLeft:
		return TEXT("strafe_left");
	case EContactRecoveryStyle::StrafeRight:
		return TEXT("strafe_right");
	case EContactRecoveryStyle::DiagonalLeft:
		return TEXT("diagonal_left");
	case EContactRecoveryStyle::DiagonalRight:
		return TEXT("diagonal_right");
	default:
		return TEXT("backward");
	}
}

FString ACurriculumDataGenerator::GetContactApproachProfileSlug() const
{
	switch (ContactApproachProfile)
	{
	case EContactApproachProfile::GlanceLeft:
		return TEXT("glance_left");
	case EContactApproachProfile::GlanceRight:
		return TEXT("glance_right");
	default:
		return TEXT("direct");
	}
}

FString ACurriculumDataGenerator::GetGuidedCameraStyleSlug() const
{
	switch (GuidedCameraStyle)
	{
	case EGuidedCameraStyle::ObjectiveOffset:
		return TEXT("objective_offset");
	case EGuidedCameraStyle::TravelReacquire:
		return TEXT("travel_reacquire");
	case EGuidedCameraStyle::ScanReacquire:
		return TEXT("scan_reacquire");
	default:
		return TEXT("objective_center");
	}
}

FString ACurriculumDataGenerator::GetLocomotionFacingProfileSlug() const
{
	switch (LocomotionFacingProfile)
	{
	case ELocomotionFacingProfile::Backward:
		return TEXT("backward");
	case ELocomotionFacingProfile::StrafeLeft:
		return TEXT("strafe_left");
	case ELocomotionFacingProfile::StrafeRight:
		return TEXT("strafe_right");
	case ELocomotionFacingProfile::FreeAttention:
		return TEXT("free_attention");
	default:
		return TEXT("forward");
	}
}

FString ACurriculumDataGenerator::GetRampDirectionSlug() const
{
	return RampDirection == ERampDirection::Uphill
		? TEXT("uphill")
		: TEXT("downhill");
}

FString ACurriculumDataGenerator::GetRampPathProfileSlug() const
{
	switch (RampPathProfile)
	{
	case ERampPathProfile::DiagonalLeftToRight:
		return TEXT("diagonal_left_to_right");
	case ERampPathProfile::DiagonalRightToLeft:
		return TEXT("diagonal_right_to_left");
	default:
		return TEXT("center");
	}
}

FString ACurriculumDataGenerator::GetHoopPathProfileSlug() const
{
	switch (HoopPathProfile)
	{
	case EHoopPathProfile::ObliqueLeftToRight:
		return TEXT("oblique_left_to_right");
	case EHoopPathProfile::ObliqueRightToLeft:
		return TEXT("oblique_right_to_left");
	default:
		return TEXT("center");
	}
}

FString ACurriculumDataGenerator::GetMissionPhaseSlug() const
{
	if (CoverageMission == ECoverageMission::SemiMarkov)
	{
		return TEXT("semi_markov");
	}
	if (bCoverageMissionSucceeded)
	{
		return CoveragePostSuccessSteps > 0
			? TEXT("post_success")
			: TEXT("success");
	}
	return bCoveragePrimaryObjectiveAchieved
		? TEXT("completion_hold")
		: TEXT("objective");
}

FString ACurriculumDataGenerator::GetPostSuccessStyleSlug() const
{
	switch (PostSuccessStyle)
	{
	case EPostSuccessStyle::GentleTurn:
		return TEXT("gentle_turn");
	case EPostSuccessStyle::GlanceReacquire:
		return TEXT("glance_reacquire");
	case EPostSuccessStyle::StrafeBlend:
		return TEXT("strafe_blend");
	case EPostSuccessStyle::EaseAndObserve:
		return TEXT("ease_and_observe");
	case EPostSuccessStyle::DriftAndSettle:
		return TEXT("drift_and_settle");
	default:
		return TEXT("continue");
	}
}

FString ACurriculumDataGenerator::GetMissionReviewSlug() const
{
	if (!bMissionReviewSuite)
	{
		return FString();
	}
	switch (CoverageMission)
	{
	case ECoverageMission::ObjectView:
		if (ObjectViewMode == EObjectViewMode::PartialOrbit
			|| ObjectViewMode == EObjectViewMode::FullOrbit)
		{
			return FString::Printf(
				TEXT("object_view_%s_%s_%s_%s"),
				*GetCoverageTargetSlug(),
				*GetObjectViewModeSlug(),
				bCoverageOrbitClockwise
					? TEXT("clockwise")
					: TEXT("counter_clockwise"),
				*GetObjectGazePatternSlug());
		}
		return FString::Printf(
			TEXT("object_view_%s_%s_%s"),
			*GetCoverageTargetSlug(),
			*GetObjectViewModeSlug(),
			*GetObjectGazePatternSlug());
	case ECoverageMission::ContactRecovery:
		return FString::Printf(
			TEXT("contact_recovery_%s_%s_%s_%s"),
			*GetCoverageTargetSlug(),
			*GetContactApproachProfileSlug(),
			*GetContactRecoveryStyleSlug(),
			*GetLocomotionFacingProfileSlug());
	case ECoverageMission::RampTraverse:
		return FString::Printf(
			TEXT("ramp_traverse_%s_%s_%s"),
			*GetRampDirectionSlug(),
			*GetRampPathProfileSlug(),
			*GetLocomotionFacingProfileSlug());
	case ECoverageMission::HoopPass:
		return FString::Printf(
			TEXT("hoop_pass_%s_%s_%s"),
			bHoopPositiveToNegative
				? TEXT("positive_x_to_negative_x")
				: TEXT("negative_x_to_positive_x"),
			*GetHoopPathProfileSlug(),
			*GetLocomotionFacingProfileSlug());
	default:
		return TEXT("semi_markov_free_exploration");
	}
}

bool ACurriculumDataGenerator::IsCurrentContactTarget(
	const FString& ContactObject) const
{
	return ContactTargetIndex != INDEX_NONE
		&& ContactObject
			== GetContactTargetDefinition(ContactTargetIndex).ActorTag.ToString();
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
	else if (CurriculumStage == ECurriculumStage::LegacyTrajectory)
	{
		CurrentActionMask &= ~CurriculumAction::E;
	}

	bCurrentQRising = false;
	bCurrentQFalling = false;
	bCurrentERequestEdge = false;
	bCurrentEAccepted = false;
	bCurrentPlanarMovementSuppressed = false;
	CurrentERejectionReason =
		V2ActionSemantics::EThrowRejectionReason::None;
	CurrentAcceptedGrenadeId = INDEX_NONE;
	CurrentCooldownBeforeSteps = CooldownRemainingSteps;
	CurrentCooldownAfterSteps = CooldownRemainingSteps;
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
	{
		const V2ActionSemantics::FDecision Decision =
			V2ActionSemantics::Evaluate(
				CurrentActionMask,
				LastAppliedActionMask,
				bQVisibleInLatestObservation,
				CooldownRemainingSteps);
		bCurrentQRising = Decision.bQRising;
		bCurrentQFalling = Decision.bQFalling;
		bCurrentERequestEdge = Decision.bERequestEdge;
		bCurrentPlanarMovementSuppressed =
			Decision.bPlanarMovementSuppressed;
		CurrentERejectionReason = Decision.RejectionReason;
		if (Decision.bThrowEligible)
		{
			bCurrentEAccepted = (CurrentAcceptedGrenadeId = AcceptThrow()) != INDEX_NONE;
		}
		CurrentCooldownAfterSteps = CooldownRemainingSteps;
	}
	else if (CurriculumStage == ECurriculumStage::LegacyThrow)
	{
		bCurrentERequestEdge =
			(CurrentActionMask & CurriculumAction::E) != 0
			&& (LastAppliedActionMask & CurriculumAction::E) == 0;
		bCurrentEAccepted = bCurrentERequestEdge
			&& (CurrentAcceptedGrenadeId = AcceptThrow()) != INDEX_NONE;
		CurrentCooldownAfterSteps = CooldownRemainingSteps;
	}
	LastAppliedActionMask = CurrentActionMask;

	if (Character)
	{
		Character->SetCurriculumActionOverride(true, CurrentActionMask);
	}
}

void ACurriculumDataGenerator::PrepareNextAction()
{
	bNaturalPlayEscapeActionActive = false;
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& bV2TrajectoryHoldMission)
	{
		ApplyAction(SelectV2TrajectoryHoldMissionAction());
		return;
	}
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		&& bV2RuntimeSmoke)
	{
		ApplyAction(SelectV2RuntimeSmokeAction());
		return;
	}

	if (bTrajectoryShowcase && CurriculumStage == ECurriculumStage::LegacyTrajectory)
	{
		ApplyAction(SelectTrajectoryShowcaseAction());
		return;
	}

	if (bCoverageMissionSucceeded)
	{
		bNaturalPlayEscapeActionActive = PreviousState.bContact;
		ApplyAction(SelectPostSuccessAction());
		return;
	}

	if (bCoverageGuided && CoverageMission != ECoverageMission::SemiMarkov)
	{
		uint16 NextActionMask = SelectCoverageGuidedAction();
		if ((CurriculumStage == ECurriculumStage::LegacyThrow
				|| CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
			&& FrameIndex >= NextThrowRequestFrame)
		{
			NextActionMask |= CurriculumAction::E;
			NextThrowRequestFrame = FrameIndex + EpisodeRandom.RandRange(30, 70);
		}
		ApplyAction(NextActionMask);
		return;
	}

	if (CoverageMission == ECoverageMission::SemiMarkov
		&& (NaturalPlayEscapeStepsRemaining > 0
			|| (PreviousState.bContact
				&& NaturalPlayContactLimitSteps > 0
				&& NaturalPlayContactSteps >= NaturalPlayContactLimitSteps)))
	{
		if (NaturalPlayEscapeStepsRemaining <= 0)
		{
			NaturalPlayEscapeDirection =
				GetNaturalPlayEscapeDirection(PreviousState);
			NaturalPlayEscapeStepsRemaining =
				8 + SampleParameterIndex(
					TEXT("natural_play_escape_duration_steps"),
					7,
					NaturalPlayEscapeCount);
			++NaturalPlayEscapeCount;
			HoldStepsRemaining = 0;
			ActionScriptMasks.Reset();
			ActionScriptHoldSteps.Reset();
			ActionScriptIndex = 0;
			ActionScriptStepsRemaining = 0;
		}
		bNaturalPlayEscapeActionActive = true;
		ApplyAction(
			SelectNaturalPlayEscapeAction(NaturalPlayEscapeDirection));
		--NaturalPlayEscapeStepsRemaining;
		return;
	}

	if (ActionScriptIndex >= ActionScriptMasks.Num()
		&& HoldStepsRemaining <= 0
		&& !(bPrescribedRecipes && FrameIndex == 0)
		&& EpisodeRandom.FRand() < 0.30f)
	{
		BuildTransitionScript();
	}

	if (ActionScriptIndex < ActionScriptMasks.Num())
	{
		if (ActionScriptStepsRemaining <= 0)
		{
			HeldActionMask = ActionScriptMasks[ActionScriptIndex];
			const float CurrentPitch =
				Character && Character->GetController()
					? FRotator::NormalizeAxis(
						Character->GetController()->GetControlRotation().Pitch)
					: 0.0f;
			const bool bScriptPitchUp =
				(HeldActionMask & CurriculumAction::ArrowUp) != 0
				&& (HeldActionMask & CurriculumAction::ArrowDown) == 0;
			const bool bScriptPitchDown =
				(HeldActionMask & CurriculumAction::ArrowDown) != 0
				&& (HeldActionMask & CurriculumAction::ArrowUp) == 0;
			if (bScriptPitchUp)
			{
				HeldCameraPitchTargetDegrees = FMath::Clamp(
					CurrentPitch + EpisodeRandom.FRandRange(8.0f, 18.0f),
					-40.0f,
					40.0f);
			}
			else if (bScriptPitchDown)
			{
				HeldCameraPitchTargetDegrees = FMath::Clamp(
					CurrentPitch - EpisodeRandom.FRandRange(8.0f, 18.0f),
					-40.0f,
					40.0f);
			}
			if (CurriculumStage != ECurriculumStage::Movement
				&& EpisodeRandom.FRand() < 0.45f)
			{
				HeldActionMask |= CurriculumAction::Q;
			}
			ActionScriptStepsRemaining =
				ActionScriptHoldSteps.IsValidIndex(ActionScriptIndex)
					? ActionScriptHoldSteps[ActionScriptIndex]
					: 1;
			++ActionScriptIndex;
		}

		uint16 NextActionMask =
			BalancePitchAction(HeldActionMask & ~CurriculumAction::E);
		if ((CurriculumStage == ECurriculumStage::LegacyThrow
				|| CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
			&& FrameIndex >= NextThrowRequestFrame)
		{
			NextActionMask |= CurriculumAction::E;
			NextThrowRequestFrame = FrameIndex + EpisodeRandom.RandRange(30, 70);
		}
		ApplyAction(NextActionMask);
		--ActionScriptStepsRemaining;
		return;
	}

	ActionScriptMasks.Reset();
	ActionScriptHoldSteps.Reset();
	ActionScriptIndex = 0;
	ActionScriptStepsRemaining = 0;
	if (HoldStepsRemaining <= 0)
	{
		HeldActionMask = SelectAction();
		HoldStepsRemaining = SelectHoldSteps();
	}

	uint16 NextActionMask =
		BalancePitchAction(HeldActionMask & ~CurriculumAction::E);
	if ((CurriculumStage == ECurriculumStage::LegacyThrow
			|| CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
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
	CurrentAcceptedGrenadeId = INDEX_NONE;
	CurrentCooldownBeforeSteps = 0;
	CurrentCooldownAfterSteps = 0;
	bCurrentERequestEdge = false;
	bCurrentEAccepted = false;
	bCurrentQRising = false;
	bCurrentQFalling = false;
	bCurrentPlanarMovementSuppressed = false;
	bQVisibleInLatestObservation = false;
	CurrentERejectionReason =
		V2ActionSemantics::EThrowRejectionReason::None;
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

int32 ACurriculumDataGenerator::AcceptThrow()
{
	if ((CurriculumStage != ECurriculumStage::LegacyThrow
			&& CurriculumStage != ECurriculumStage::TrajectoryThrowV2)
		|| CooldownRemainingSteps > 0
		|| !GetWorld())
	{
		return INDEX_NONE;
	}

	FVector SpawnLocation;
	FVector InitialVelocity;
	if (!BuildLaunchState(SpawnLocation, InitialVelocity))
	{
		return INDEX_NONE;
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
		return INDEX_NONE;
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
	CooldownRemainingSteps =
		V2ActionSemantics::GetCooldownDurationSteps(ObservationRate);
	return Grenade.Id;
}

void ACurriculumDataGenerator::AdvanceGrenades()
{
	if ((CurriculumStage != ECurriculumStage::LegacyThrow
			&& CurriculumStage != ECurriculumStage::TrajectoryThrowV2)
		|| Grenades.IsEmpty())
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

ACurriculumDataGenerator::EV2EpisodePhase
ACurriculumDataGenerator::GetV2EpisodePhase() const
{
	if (CurriculumStage != ECurriculumStage::TrajectoryThrowV2)
	{
		return EV2EpisodePhase::NotApplicable;
	}
	if (!Grenades.IsEmpty() && CooldownRemainingSteps > 0)
	{
		return EV2EpisodePhase::Cooldown;
	}
	if (!Grenades.IsEmpty())
	{
		return EV2EpisodePhase::PostThrow;
	}
	if ((CurrentActionMask & CurriculumAction::Q) != 0)
	{
		return EV2EpisodePhase::Aim;
	}
	return EV2EpisodePhase::Traverse;
}

FString ACurriculumDataGenerator::GetV2EpisodePhaseSlug() const
{
	switch (GetV2EpisodePhase())
	{
	case EV2EpisodePhase::Traverse:
		return TEXT("traverse");
	case EV2EpisodePhase::Aim:
		return TEXT("aim");
	case EV2EpisodePhase::Cooldown:
		return TEXT("cooldown");
	case EV2EpisodePhase::PostThrow:
		return TEXT("post_throw");
	default:
		return TEXT("not_applicable");
	}
}

FString ACurriculumDataGenerator::GetStageSlug() const
{
	switch (CurriculumStage)
	{
	case ECurriculumStage::LegacyTrajectory:
		return TEXT("trajectory_v2");
	case ECurriculumStage::LegacyThrow:
		return TEXT("throw_v3");
	case ECurriculumStage::TrajectoryThrowV2:
		return TEXT("trajectory_throw_v2");
	default:
		return TEXT("movement_v1");
	}
}

FString ACurriculumDataGenerator::GetStageSchemaVersion() const
{
	if (CurriculumStage == ECurriculumStage::TrajectoryThrowV2)
	{
		return FString::Printf(
			TEXT("%s-%s-11"),
			*GetStageSlug(),
			StorageFormat == EStorageFormat::WebPParquet
				? TEXT("production")
				: TEXT("preflight"));
	}
	if (StorageFormat == EStorageFormat::WebPParquet)
	{
		return FString::Printf(TEXT("%s-production-1"), *GetStageSlug());
	}
	return FString::Printf(TEXT("%s-preflight-10"), *GetStageSlug());
}

FString ACurriculumDataGenerator::MakeEpisodeId() const
{
	if (bPrescribedRecipes)
	{
		return FString::Printf(TEXT("p-e%09d"), EpisodeIndex);
	}
	return FString::Printf(TEXT("w%03d-e%06d"), WorkerId, EpisodeIndex);
}

FString ACurriculumDataGenerator::MakeImageKey(const int32 ObservationIndex) const
{
	if (StorageFormat == EStorageFormat::WebPParquet)
	{
		return FString::Printf(
			TEXT("episodes/%s/frame-%06d.webp"),
			*MakeEpisodeId(),
			ObservationIndex);
	}
	return FString::Printf(
		TEXT("episodes/%s/frame-%06d.png"),
		*MakeEpisodeId(),
		ObservationIndex);
}

FString ACurriculumDataGenerator::BuildDatasetJson(
	const bool bComplete,
	const FString& ErrorMessage) const
{
	const int32 CompletedEpisodes =
		EpisodeOrdinal + (bEpisodeActive ? 1 : 0);
	float CameraPitchMinimum = -89.9f;
	float CameraPitchMaximum = 89.9f;
	float CameraPitchRate = 75.0f;
	if (Character)
	{
		Character->GetCurriculumCameraPitchLimits(
			CameraPitchMinimum,
			CameraPitchMaximum);
		CameraPitchRate =
			Character->GetCurriculumCameraPitchRateDegreesPerSecond();
	}
	const TCHAR* CollectionPolicy =
		CurriculumStage == ECurriculumStage::TrajectoryThrowV2
		? (bV2TrajectoryHoldMission
			? TEXT("diagnostic_v2_trajectory_hold_mission")
			: TEXT("diagnostic_v2_runtime_smoke"))
		: (bPrescribedRecipes
		? TEXT("training_central_prescribed_recipes_v1")
		: (bTrajectoryShowcase
		? TEXT("inspection_only_trajectory_showcase")
		: (bMissionReviewSuite
			? TEXT("inspection_only_mission_review_suite")
			: (bCoverageGuided
				? TEXT("training_frame_balanced_final_agent_v5")
				: TEXT("training_semimarkov")))));
	const TCHAR* ParameterSampler = bPrescribedRecipes
		? TEXT("prescribed_nested_radical_inverse_stratified_v1")
		: TEXT("enumerated_cells_global_replay_stratified_stateless_v2");
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
		TEXT("  \"mission_review_suite\": %s,\n")
		TEXT("  \"mission_override\": \"%s\",\n")
		TEXT("  \"object_view_mode_override\": \"%s\",\n")
		TEXT("  \"coverage_target_override\": \"%s\",\n")
		TEXT("  \"mission_direction_override\": \"%s\",\n")
		TEXT("  \"prescribed_recipes\": %s,\n")
		TEXT("  \"plan_id\": \"%s\",\n")
		TEXT("  \"plan_version\": \"%s\",\n")
		TEXT("  \"assignment_id\": \"%s\",\n")
		TEXT("  \"attempt_id\": \"%s\",\n")
		TEXT("  \"executor_id\": \"%s\",\n")
		TEXT("  \"split\": \"%s\",\n")
		TEXT("  \"parameter_sampler\": \"%s\",\n")
		TEXT("  \"balancing_counter_policy\": \"accepted_successful_realized_behavior_frames_only\",\n")
		TEXT("  \"coverage_azimuth_bin_count\": %d,\n")
		TEXT("  \"coverage_summary\": {\n")
		TEXT("    \"object_view_visible_bin_masks\": {\"rectangle\": %u, \"pyramid\": %u, ")
		TEXT("\"sphere\": %u, \"hoop\": %u, \"ramp\": %u},\n")
		TEXT("    \"object_view_visited_bin_masks\": {\"rectangle\": %u, \"pyramid\": %u, ")
		TEXT("\"sphere\": %u, \"hoop\": %u, \"ramp\": %u},\n")
		TEXT("    \"ramp_traversals\": %d,\n")
		TEXT("    \"hoop_passes\": %d,\n")
		TEXT("    \"mission_successes\": %d,\n")
		TEXT("    \"mission_failures\": %d\n")
		TEXT("  },\n")
		TEXT("  \"agent_frame_summary\": {\n")
		TEXT("    \"mission_observations\": {\"semi_markov\": %lld, ")
		TEXT("\"object_view\": %lld, \"contact_recovery\": %lld, ")
		TEXT("\"ramp_traverse\": %lld, \"hoop_pass\": %lld},\n")
		TEXT("    \"post_success_observations\": %lld,\n")
		TEXT("    \"object_view_modes\": {\"approach_observe\": %lld, ")
		TEXT("\"pass_by\": %lld, \"partial_orbit\": %lld, \"full_orbit\": %lld},\n")
		TEXT("    \"object_gaze_patterns\": {\"target_center\": %lld, ")
		TEXT("\"target_offset\": %lld, \"travel_direction\": %lld, ")
		TEXT("\"roam_reacquire\": %lld},\n")
		TEXT("    \"object_gaze_intents\": {\"target_center\": %lld, ")
		TEXT("\"target_offset\": %lld, \"travel_direction\": %lld, ")
		TEXT("\"survey_point\": %lld},\n")
		TEXT("    \"object_orbit_directions\": {\"clockwise\": %lld, ")
		TEXT("\"counter_clockwise\": %lld},\n")
		TEXT("    \"object_view_targets\": {\"rectangle\": %lld, \"pyramid\": %lld, ")
		TEXT("\"sphere\": %lld, \"hoop\": %lld, \"ramp\": %lld},\n")
		TEXT("    \"contact_targets\": {\"rectangle\": %lld, \"pyramid\": %lld, ")
		TEXT("\"sphere\": %lld, \"hoop\": %lld, \"ramp\": %lld, ")
		TEXT("\"north_wall\": %lld, \"south_wall\": %lld, ")
		TEXT("\"east_wall\": %lld, \"west_wall\": %lld},\n")
		TEXT("    \"contact_recovery_styles\": {\"backward\": %lld, ")
		TEXT("\"strafe_left\": %lld, \"strafe_right\": %lld, ")
		TEXT("\"diagonal_left\": %lld, \"diagonal_right\": %lld},\n")
		TEXT("    \"contact_approach_profiles\": {\"direct\": %lld, ")
		TEXT("\"glance_left\": %lld, \"glance_right\": %lld},\n")
		TEXT("    \"contact_facing_profiles\": {\"forward\": %lld, ")
		TEXT("\"backward\": %lld, \"strafe_left\": %lld, ")
		TEXT("\"strafe_right\": %lld, \"free_attention\": %lld},\n")
		TEXT("    \"ramp_directions\": {\"uphill\": %lld, \"downhill\": %lld},\n")
		TEXT("    \"ramp_path_profiles\": {\"center\": %lld, ")
		TEXT("\"diagonal_left_to_right\": %lld, ")
		TEXT("\"diagonal_right_to_left\": %lld},\n")
		TEXT("    \"ramp_facing_profiles\": {\"forward\": %lld, ")
		TEXT("\"backward\": %lld, \"strafe_left\": %lld, ")
		TEXT("\"strafe_right\": %lld, \"free_attention\": %lld},\n")
		TEXT("    \"hoop_directions\": {\"positive_to_negative\": %lld, ")
		TEXT("\"negative_to_positive\": %lld},\n")
		TEXT("    \"hoop_path_profiles\": {\"center\": %lld, ")
		TEXT("\"oblique_left_to_right\": %lld, ")
		TEXT("\"oblique_right_to_left\": %lld},\n")
		TEXT("    \"hoop_facing_profiles\": {\"forward\": %lld, ")
		TEXT("\"backward\": %lld, \"strafe_left\": %lld, ")
		TEXT("\"strafe_right\": %lld, \"free_attention\": %lld},\n")
		TEXT("    \"free_attention_camera_styles\": {\"objective_center\": %lld, ")
		TEXT("\"objective_offset\": %lld, \"travel_reacquire\": %lld, ")
		TEXT("\"scan_reacquire\": %lld},\n")
		TEXT("    \"pitch_bands\": {\"eye_height\": %lld, \"moderate\": %lld, ")
		TEXT("\"extreme\": %lld, \"near_limit\": %lld}\n")
		TEXT("  },\n")
		TEXT("  \"map_configuration_hash\": \"fixed-arena-r4-realized-facing\",\n")
		TEXT("  \"worker_id\": %d,\n")
		TEXT("  \"seed_start\": %d,\n")
		TEXT("  \"requested_episode_count\": %d,\n")
		TEXT("  \"completed_episode_count\": %d,\n")
		TEXT("  \"episode_seconds\": %d,\n")
		TEXT("  \"observation_rate_hz\": %d,\n")
		TEXT("  \"camera_pitch_min_degrees\": %s,\n")
		TEXT("  \"camera_pitch_max_degrees\": %s,\n")
		TEXT("  \"camera_pitch_rate_degrees_per_second\": %s,\n")
		TEXT("  \"transitions_per_episode\": %d,\n")
		TEXT("  \"transition_count\": %d,\n")
		TEXT("  \"observation_count\": %d,\n")
		TEXT("  \"rgb_width\": %d,\n")
		TEXT("  \"rgb_height\": %d,\n")
		TEXT("  \"rgb_format\": \"%s\",\n")
		TEXT("  \"metadata_format\": \"%s\",\n")
		TEXT("  \"webp_lossless_effort\": %d,\n")
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
		JsonBool(bMissionReviewSuite),
		*MissionOverride.ReplaceCharWithEscapedChar(),
		*ObjectViewModeOverride.ReplaceCharWithEscapedChar(),
		*CoverageTargetOverride.ReplaceCharWithEscapedChar(),
		*MissionDirectionOverride.ReplaceCharWithEscapedChar(),
		JsonBool(bPrescribedRecipes),
		*PlanId.ReplaceCharWithEscapedChar(),
		*PlanVersion.ReplaceCharWithEscapedChar(),
		*AssignmentId.ReplaceCharWithEscapedChar(),
		*AttemptId.ReplaceCharWithEscapedChar(),
		*ExecutorId.ReplaceCharWithEscapedChar(),
		*DatasetSplit.ReplaceCharWithEscapedChar(),
		ParameterSampler,
		CoverageAzimuthBinCount,
		OverallObjectViewBins[0],
		OverallObjectViewBins[1],
		OverallObjectViewBins[2],
		OverallObjectViewBins[3],
		OverallObjectViewBins[4],
		OverallObjectVisitedBins[0],
		OverallObjectVisitedBins[1],
		OverallObjectVisitedBins[2],
		OverallObjectVisitedBins[3],
		OverallObjectVisitedBins[4],
		OverallRampTraversals,
		OverallHoopPasses,
		OverallMissionSuccesses,
		OverallMissionFailures,
		static_cast<long long>(OverallMissionObservationFrames[
			static_cast<int32>(ECoverageMission::SemiMarkov)]),
		static_cast<long long>(OverallMissionObservationFrames[
			static_cast<int32>(ECoverageMission::ObjectView)]),
		static_cast<long long>(OverallMissionObservationFrames[
			static_cast<int32>(ECoverageMission::ContactRecovery)]),
		static_cast<long long>(OverallMissionObservationFrames[
			static_cast<int32>(ECoverageMission::RampTraverse)]),
		static_cast<long long>(OverallMissionObservationFrames[
			static_cast<int32>(ECoverageMission::HoopPass)]),
		static_cast<long long>(OverallPostSuccessObservationFrames),
		static_cast<long long>(OverallObjectModeObservationFrames[
			static_cast<int32>(EObjectViewMode::ApproachObserve)]),
		static_cast<long long>(OverallObjectModeObservationFrames[
			static_cast<int32>(EObjectViewMode::PassBy)]),
		static_cast<long long>(OverallObjectModeObservationFrames[
			static_cast<int32>(EObjectViewMode::PartialOrbit)]),
		static_cast<long long>(OverallObjectModeObservationFrames[
			static_cast<int32>(EObjectViewMode::FullOrbit)]),
		static_cast<long long>(OverallObjectGazePatternObservationFrames[
			static_cast<int32>(EObjectGazePattern::TargetCenter)]),
		static_cast<long long>(OverallObjectGazePatternObservationFrames[
			static_cast<int32>(EObjectGazePattern::TargetOffset)]),
		static_cast<long long>(OverallObjectGazePatternObservationFrames[
			static_cast<int32>(EObjectGazePattern::TravelDirection)]),
		static_cast<long long>(OverallObjectGazePatternObservationFrames[
			static_cast<int32>(EObjectGazePattern::RoamReacquire)]),
		static_cast<long long>(OverallObjectGazeIntentObservationFrames[
			static_cast<int32>(EObjectGazeIntent::TargetCenter)]),
		static_cast<long long>(OverallObjectGazeIntentObservationFrames[
			static_cast<int32>(EObjectGazeIntent::TargetOffset)]),
		static_cast<long long>(OverallObjectGazeIntentObservationFrames[
			static_cast<int32>(EObjectGazeIntent::TravelDirection)]),
		static_cast<long long>(OverallObjectGazeIntentObservationFrames[
			static_cast<int32>(EObjectGazeIntent::SurveyPoint)]),
		static_cast<long long>(OverallObjectOrbitDirectionObservationFrames[0]),
		static_cast<long long>(OverallObjectOrbitDirectionObservationFrames[1]),
		static_cast<long long>(OverallObjectTargetObservationFrames[0]),
		static_cast<long long>(OverallObjectTargetObservationFrames[1]),
		static_cast<long long>(OverallObjectTargetObservationFrames[2]),
		static_cast<long long>(OverallObjectTargetObservationFrames[3]),
		static_cast<long long>(OverallObjectTargetObservationFrames[4]),
		static_cast<long long>(OverallContactTargetObservationFrames[0]),
		static_cast<long long>(OverallContactTargetObservationFrames[1]),
		static_cast<long long>(OverallContactTargetObservationFrames[2]),
		static_cast<long long>(OverallContactTargetObservationFrames[3]),
		static_cast<long long>(OverallContactTargetObservationFrames[4]),
		static_cast<long long>(OverallContactTargetObservationFrames[5]),
		static_cast<long long>(OverallContactTargetObservationFrames[6]),
		static_cast<long long>(OverallContactTargetObservationFrames[7]),
		static_cast<long long>(OverallContactTargetObservationFrames[8]),
		static_cast<long long>(OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(EContactRecoveryStyle::Backward)]),
		static_cast<long long>(OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(EContactRecoveryStyle::StrafeLeft)]),
		static_cast<long long>(OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(EContactRecoveryStyle::StrafeRight)]),
		static_cast<long long>(OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(EContactRecoveryStyle::DiagonalLeft)]),
		static_cast<long long>(OverallContactRecoveryStyleObservationFrames[
			static_cast<int32>(EContactRecoveryStyle::DiagonalRight)]),
		static_cast<long long>(OverallContactApproachProfileObservationFrames[
			static_cast<int32>(EContactApproachProfile::Direct)]),
		static_cast<long long>(OverallContactApproachProfileObservationFrames[
			static_cast<int32>(EContactApproachProfile::GlanceLeft)]),
		static_cast<long long>(OverallContactApproachProfileObservationFrames[
			static_cast<int32>(EContactApproachProfile::GlanceRight)]),
		static_cast<long long>(OverallContactFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Forward)]),
		static_cast<long long>(OverallContactFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Backward)]),
		static_cast<long long>(OverallContactFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeLeft)]),
		static_cast<long long>(OverallContactFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeRight)]),
		static_cast<long long>(OverallContactFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::FreeAttention)]),
		static_cast<long long>(OverallRampDirectionObservationFrames[0]),
		static_cast<long long>(OverallRampDirectionObservationFrames[1]),
		static_cast<long long>(OverallRampPathObservationFrames[
			static_cast<int32>(ERampPathProfile::Center)]),
		static_cast<long long>(OverallRampPathObservationFrames[
			static_cast<int32>(ERampPathProfile::DiagonalLeftToRight)]),
		static_cast<long long>(OverallRampPathObservationFrames[
			static_cast<int32>(ERampPathProfile::DiagonalRightToLeft)]),
		static_cast<long long>(OverallRampFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Forward)]),
		static_cast<long long>(OverallRampFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Backward)]),
		static_cast<long long>(OverallRampFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeLeft)]),
		static_cast<long long>(OverallRampFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeRight)]),
		static_cast<long long>(OverallRampFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::FreeAttention)]),
		static_cast<long long>(OverallHoopDirectionObservationFrames[0]),
		static_cast<long long>(OverallHoopDirectionObservationFrames[1]),
		static_cast<long long>(OverallHoopPathObservationFrames[
			static_cast<int32>(EHoopPathProfile::Center)]),
		static_cast<long long>(OverallHoopPathObservationFrames[
			static_cast<int32>(EHoopPathProfile::ObliqueLeftToRight)]),
		static_cast<long long>(OverallHoopPathObservationFrames[
			static_cast<int32>(EHoopPathProfile::ObliqueRightToLeft)]),
		static_cast<long long>(OverallHoopFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Forward)]),
		static_cast<long long>(OverallHoopFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::Backward)]),
		static_cast<long long>(OverallHoopFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeLeft)]),
		static_cast<long long>(OverallHoopFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::StrafeRight)]),
		static_cast<long long>(OverallHoopFacingObservationFrames[
			static_cast<int32>(ELocomotionFacingProfile::FreeAttention)]),
		static_cast<long long>(OverallGuidedCameraStyleObservationFrames[
			static_cast<int32>(EGuidedCameraStyle::ObjectiveCenter)]),
		static_cast<long long>(OverallGuidedCameraStyleObservationFrames[
			static_cast<int32>(EGuidedCameraStyle::ObjectiveOffset)]),
		static_cast<long long>(OverallGuidedCameraStyleObservationFrames[
			static_cast<int32>(EGuidedCameraStyle::TravelReacquire)]),
		static_cast<long long>(OverallGuidedCameraStyleObservationFrames[
			static_cast<int32>(EGuidedCameraStyle::ScanReacquire)]),
		static_cast<long long>(OverallPitchBandObservationFrames[0]),
		static_cast<long long>(OverallPitchBandObservationFrames[1]),
		static_cast<long long>(OverallPitchBandObservationFrames[2]),
		static_cast<long long>(OverallPitchBandObservationFrames[3]),
		WorkerId,
		SeedStart,
		EpisodeCount,
		CompletedEpisodes,
		EpisodeSeconds,
		ObservationRate,
		*JsonNumber(CameraPitchMinimum),
		*JsonNumber(CameraPitchMaximum),
		*JsonNumber(CameraPitchRate),
		TransitionsPerEpisode,
		GlobalTransitionCount,
		GlobalTransitionCount + CompletedEpisodes,
		CaptureWidth,
		CaptureHeight,
		StorageFormat == EStorageFormat::WebPParquet
			? TEXT("lossless_webp")
			: TEXT("lossless_png"),
		StorageFormat == EStorageFormat::WebPParquet
			? TEXT("parquet_pending")
			: TEXT("jsonl"),
		WebPLosslessEffort,
		*FPaths::GetCleanFilename(ShardPath));
}
