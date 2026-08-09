#pragma once

#include "CoreMinimal.h"

namespace CurriculumAction
{
	enum EBit : uint16
	{
		W = 1u << 0,
		A = 1u << 1,
		S = 1u << 2,
		D = 1u << 3,
		ArrowUp = 1u << 4,
		ArrowDown = 1u << 5,
		ArrowLeft = 1u << 6,
		ArrowRight = 1u << 7,
		Q = 1u << 8,
		E = 1u << 9
	};

	constexpr uint16 CanonicalMask =
		W | A | S | D | ArrowUp | ArrowDown | ArrowLeft | ArrowRight | Q | E;
	constexpr uint16 MovementMask = W | A | S | D;
	constexpr uint16 CameraMask = ArrowUp | ArrowDown | ArrowLeft | ArrowRight;

	inline float ForwardAxis(const uint16 Mask)
	{
		return ((Mask & W) ? 1.0f : 0.0f) - ((Mask & S) ? 1.0f : 0.0f);
	}

	inline float RightAxis(const uint16 Mask)
	{
		return ((Mask & D) ? 1.0f : 0.0f) - ((Mask & A) ? 1.0f : 0.0f);
	}

	inline float YawAxis(const uint16 Mask)
	{
		return ((Mask & ArrowRight) ? 1.0f : 0.0f)
			- ((Mask & ArrowLeft) ? 1.0f : 0.0f);
	}

	inline float PitchAxis(const uint16 Mask)
	{
		return ((Mask & ArrowUp) ? 1.0f : 0.0f)
			- ((Mask & ArrowDown) ? 1.0f : 0.0f);
	}
}
