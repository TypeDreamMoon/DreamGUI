// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamTextTestFont.h"
#include "DreamTextHoldRecordingFont.generated.h"

/**
 * The made-up font, with coverage epochs that count their holders: what a text holds of its font's coverage cells
 * (UDreamUIFontData_BaseObject::GetCoverageEpoch and MoveCoverageHold) is read back from it. Its coverage glyphs come from
 * MockCoverageGlyph, as the made-up font's do.
 */
UCLASS(NotBlueprintable, NotBlueprintType, Transient, HideDropdown)
class UDreamTextHoldRecordingFont : public UDreamTextTestFont
{
	GENERATED_BODY()
public:
	/** What GetCoverageEpoch answers: the epoch the coverage glyphs handed out now belong to. 0: the font hands out none. */
	uint32 MockCoverageEpoch = 1;
	/** Every MoveCoverageHold call there has been. */
	int32 MoveCalls = 0;

	virtual uint32 GetCoverageEpoch() const override { return MockCoverageEpoch; }
	virtual void MoveCoverageHold(uint32 InFromEpoch, uint32 InToEpoch) override
	{
		++MoveCalls;
		if (InFromEpoch != 0)
		{
			--Holders.FindOrAdd(InFromEpoch);
		}
		if (InToEpoch != 0)
		{
			++Holders.FindOrAdd(InToEpoch);
		}
	}
	/** How many texts hold InEpoch now. */
	int32 GetHolders(uint32 InEpoch) const
	{
		const int32* Count = Holders.Find(InEpoch);
		return Count != nullptr ? *Count : 0;
	}

private:
	TMap<uint32, int32> Holders;
};
