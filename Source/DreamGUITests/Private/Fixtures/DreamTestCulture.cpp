// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamTestCulture.h"

#include "Internationalization/Culture.h"

namespace DreamTests
{
	FScopedEnglish::FScopedEnglish()
	{
		FInternationalization& I18N = FInternationalization::Get();
		if (I18N.GetCurrentLanguage()->GetTwoLetterISOLanguageName() == TEXT("en"))
		{
			return;
		}
		// The language only, as the text tests that switch it do: the locale formats numbers and dates, which no
		// message matched here depends on, and the snapshot puts back both either way.
		I18N.BackupCultureState(Before);
		bSwitched = I18N.SetCurrentLanguage(TEXT("en"));
	}

	FScopedEnglish::~FScopedEnglish()
	{
		if (bSwitched)
		{
			FInternationalization::Get().RestoreCultureState(Before);
		}
	}
}
