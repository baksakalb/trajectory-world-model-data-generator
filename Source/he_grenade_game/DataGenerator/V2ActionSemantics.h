#pragma once

#include "CoreMinimal.h"
#include "DataGenerator/CurriculumAction.h"

namespace V2ActionSemantics
{
	inline int32 GetCooldownDurationSteps(const int32 ObservationRate)
	{
		return FMath::Max(
			1,
			FMath::RoundToInt(2.0f * static_cast<float>(FMath::Max(1, ObservationRate))));
	}

	inline int32 GetTrajectoryHoldThrowFrame(const int32 ObservationRate)
	{
		// Keep a stable preview visible for half a second before the throw. The
		// initial observation is Q-off, so this always satisfies the frozen
		// preceding-visible-observation gate even at very low observation rates.
		return FMath::Max(
			2,
			FMath::RoundToInt(
				0.5f * static_cast<float>(FMath::Max(1, ObservationRate))));
	}

	inline uint16 SelectTrajectoryHoldAction(
		const int32 FrameIndex,
		const int32 ObservationRate)
	{
		const uint16 ThrowBit =
			FrameIndex == GetTrajectoryHoldThrowFrame(ObservationRate)
				? CurriculumAction::E
				: 0;
		return CurriculumAction::Q | ThrowBit;
	}

	enum class EThrowRejectionReason : uint8
	{
		None,
		QNotHeld,
		QNotPreviouslyVisible,
		Cooldown,
		NotRisingEdge
	};

	struct FDecision
	{
		bool bQRising = false;
		bool bQFalling = false;
		bool bERequestEdge = false;
		bool bPlanarMovementSuppressed = false;
		bool bThrowEligible = false;
		EThrowRejectionReason RejectionReason = EThrowRejectionReason::None;
	};

	inline FDecision Evaluate(
		const uint16 RequestedActionMask,
		const uint16 PreviousActionMask,
		const bool bQVisibleInSourceObservation,
		const int32 CooldownBeforeSteps)
	{
		FDecision Decision;
		const bool bQHeld =
			(RequestedActionMask & CurriculumAction::Q) != 0;
		const bool bQPreviouslyHeld =
			(PreviousActionMask & CurriculumAction::Q) != 0;
		const bool bEHeld =
			(RequestedActionMask & CurriculumAction::E) != 0;
		const bool bEPreviouslyHeld =
			(PreviousActionMask & CurriculumAction::E) != 0;

		Decision.bQRising = bQHeld && !bQPreviouslyHeld;
		Decision.bQFalling = !bQHeld && bQPreviouslyHeld;
		Decision.bERequestEdge = bEHeld && !bEPreviouslyHeld;
		Decision.bPlanarMovementSuppressed =
			bQHeld
			&& (RequestedActionMask & CurriculumAction::MovementMask) != 0;

		if (!bEHeld)
		{
			return Decision;
		}
		if (!Decision.bERequestEdge)
		{
			Decision.RejectionReason = EThrowRejectionReason::NotRisingEdge;
			return Decision;
		}
		if (!bQHeld)
		{
			Decision.RejectionReason = EThrowRejectionReason::QNotHeld;
			return Decision;
		}
		if (!bQVisibleInSourceObservation)
		{
			Decision.RejectionReason =
				EThrowRejectionReason::QNotPreviouslyVisible;
			return Decision;
		}
		if (CooldownBeforeSteps > 0)
		{
			Decision.RejectionReason = EThrowRejectionReason::Cooldown;
			return Decision;
		}

		Decision.bThrowEligible = true;
		return Decision;
	}

	inline const TCHAR* GetRejectionReasonSlug(
		const EThrowRejectionReason RejectionReason)
	{
		switch (RejectionReason)
		{
		case EThrowRejectionReason::QNotHeld:
			return TEXT("q_not_held");
		case EThrowRejectionReason::QNotPreviouslyVisible:
			return TEXT("q_not_previously_visible");
		case EThrowRejectionReason::Cooldown:
			return TEXT("cooldown");
		case EThrowRejectionReason::NotRisingEdge:
			return TEXT("not_rising_edge");
		default:
			return TEXT("none");
		}
	}
}
