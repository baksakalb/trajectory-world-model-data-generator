#include "DataGenerator/V2ActionSemantics.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FV2ActionSemanticsTest,
	"HEGrenadeGame.DataGenerator.V2.ActionSemantics",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FV2ActionSemanticsTest::RunTest(const FString& Parameters)
{
	using namespace V2ActionSemantics;

	const FDecision QPriority = Evaluate(
		CurriculumAction::Q | CurriculumAction::W | CurriculumAction::ArrowRight,
		0,
		false,
		0);
	TestTrue(TEXT("Q rising edge is detected"), QPriority.bQRising);
	TestTrue(
		TEXT("Planar input is explicitly suppressed while Q is held"),
		QPriority.bPlanarMovementSuppressed);

	const FDecision FirstFrameQE = Evaluate(
		CurriculumAction::Q | CurriculumAction::E,
		0,
		false,
		0);
	TestTrue(TEXT("First-frame E is an edge"), FirstFrameQE.bERequestEdge);
	TestFalse(TEXT("First-frame Q+E is rejected"), FirstFrameQE.bThrowEligible);
	TestTrue(
		TEXT("First-frame Q+E rejection reason"),
		FirstFrameQE.RejectionReason
			== EThrowRejectionReason::QNotPreviouslyVisible);

	const FDecision ValidThrow = Evaluate(
		CurriculumAction::Q | CurriculumAction::E,
		CurriculumAction::Q,
		true,
		0);
	TestTrue(TEXT("Preceding visible Q permits an E edge"), ValidThrow.bThrowEligible);

	const FDecision WithoutQ = Evaluate(
		CurriculumAction::E,
		0,
		false,
		0);
	TestTrue(
		TEXT("E without Q rejection reason"),
		WithoutQ.RejectionReason == EThrowRejectionReason::QNotHeld);

	const FDecision DuringCooldown = Evaluate(
		CurriculumAction::Q | CurriculumAction::E,
		CurriculumAction::Q,
		true,
		1);
	TestTrue(
		TEXT("Cooldown rejection reason"),
		DuringCooldown.RejectionReason == EThrowRejectionReason::Cooldown);

	const FDecision HeldE = Evaluate(
		CurriculumAction::Q | CurriculumAction::E,
		CurriculumAction::Q | CurriculumAction::E,
		true,
		0);
	TestFalse(TEXT("Held E is not a new request edge"), HeldE.bERequestEdge);
	TestTrue(
		TEXT("Held E rejection reason"),
		HeldE.RejectionReason == EThrowRejectionReason::NotRisingEdge);

	const FDecision QRelease = Evaluate(0, CurriculumAction::Q, true, 0);
	TestTrue(TEXT("Q falling edge is detected"), QRelease.bQFalling);
	TestFalse(
		TEXT("Movement is not suppressed after Q release"),
		Evaluate(CurriculumAction::W, CurriculumAction::Q, true, 0)
			.bPlanarMovementSuppressed);
	TestTrue(
		TEXT("Q-not-held takes precedence over cooldown for an E edge"),
		Evaluate(CurriculumAction::E, 0, false, 40).RejectionReason
			== EThrowRejectionReason::QNotHeld);
	TestTrue(
		TEXT("Q held without E has no rejection"),
		Evaluate(CurriculumAction::Q, 0, false, 0).RejectionReason
			== EThrowRejectionReason::None);
	TestEqual(
		TEXT("Two seconds at 20 Hz is exactly 40 transitions"),
		GetCooldownDurationSteps(20),
		40);
	TestEqual(
		TEXT("Cooldown scales at the minimum observation rate"),
		GetCooldownDurationSteps(1),
		2);
	TestEqual(
		TEXT("Cooldown scales at the maximum observation rate"),
		GetCooldownDurationSteps(120),
		240);
	TestEqual(
		TEXT("Trajectory-hold preview lasts half a second at 20 Hz"),
		GetTrajectoryHoldThrowFrame(20),
		10);
	TestEqual(
		TEXT("Trajectory-hold mission begins by holding Q"),
		SelectTrajectoryHoldAction(0, 20),
		CurriculumAction::Q);
	TestEqual(
		TEXT("Trajectory-hold mission throws once after the preview"),
		SelectTrajectoryHoldAction(10, 20),
		static_cast<uint16>(CurriculumAction::Q | CurriculumAction::E));
	TestEqual(
		TEXT("Trajectory-hold mission keeps Q held after the throw"),
		SelectTrajectoryHoldAction(79, 20),
		CurriculumAction::Q);
	return true;
}

#endif
