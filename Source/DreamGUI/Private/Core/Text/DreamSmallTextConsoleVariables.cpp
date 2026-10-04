// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamGUISettings.h"
#include "Async/Async.h"
#include "HAL/IConsoleManager.h"

/*
 * The console variables small text is switched by for an A/B run -- coverage on or off, and the size it stops at -- with the
 * answers UDreamGUISettings gives with them. They are UDreamGUISettings's statics, defined here beside the variables they
 * read: every font asks them (UDreamUIFontData_BaseObject::SupportsCoverageGlyphs, GetCoverageMaxPixelSize), and every text
 * that has painted is repainted when either variable changes (UDreamText, through GetOnSmallTextCoverageChanged).
 */
namespace DreamSmallTextConsoleVariablesLocal
{
	/** Either variable changed: told on the game thread, where the texts and the fonts that listen live. */
	void OnSmallTextConsoleVariableChanged(IConsoleVariable* /*InVariable*/)
	{
		if (IsInGameThread())
		{
			UDreamGUISettings::GetOnSmallTextCoverageChanged().Broadcast();
			return;
		}
		AsyncTask(ENamedThreads::GameThread, []()
		{
			UDreamGUISettings::GetOnSmallTextCoverageChanged().Broadcast();
		});
	}

	TAutoConsoleVariable<int32> CVarDreamGUISmallTextCoverage(
		TEXT("DreamGUI.Text.SmallTextCoverage"),
		-1,
		TEXT("Whether small screen text draws from hinted coverage glyphs. -1: as the project says (Project Settings > DreamGUI > ")
		TEXT("Small Text Coverage). 0: never, for every font, those set to On included. 1: for every font that leaves it to the ")
		TEXT("project. Changing it repaints every text."),
		FConsoleVariableDelegate::CreateStatic(&OnSmallTextConsoleVariableChanged),
		ECVF_Default);

	TAutoConsoleVariable<float> CVarDreamGUISmallTextMaxPixelSize(
		TEXT("DreamGUI.Text.SmallTextMaxPixelSize"),
		0.0f,
		TEXT("Above 0: the device pixels per em up to which small text draws from coverage glyphs, in place of the project's ")
		TEXT("Small Text Max Pixel Size, for every font with no limit of its own -- to force coverage onto larger labels for a ")
		TEXT("measurement. 0: the project's. Changing it repaints every text."),
		FConsoleVariableDelegate::CreateStatic(&OnSmallTextConsoleVariableChanged),
		ECVF_Default);
}

bool UDreamGUISettings::IsSmallTextCoverageEnabled()
{
	// Read on any thread: a font may be asked from its glyph worker.
	const int32 Override = DreamSmallTextConsoleVariablesLocal::CVarDreamGUISmallTextCoverage.GetValueOnAnyThread();
	return Override < 0 ? Get()->bSmallTextCoverage : Override != 0;
}

float UDreamGUISettings::GetSmallTextMaxPixelSize()
{
	const float Override = DreamSmallTextConsoleVariablesLocal::CVarDreamGUISmallTextMaxPixelSize.GetValueOnAnyThread();
	return Override > 0.0f ? Override : Get()->SmallTextMaxPixelSize;
}

FSimpleMulticastDelegate& UDreamGUISettings::GetOnSmallTextCoverageChanged()
{
	static FSimpleMulticastDelegate OnSmallTextCoverageChanged;
	return OnSmallTextCoverageChanged;
}
