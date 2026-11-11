// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Internationalization/Internationalization.h"

namespace DreamTests
{
	/**
	 * The editor in English for as long as this lives, and back in the language it was in when it ends.
	 *
	 * DreamGUI's editor text is translated, and the editor shows it in the machine's language. A test that waits for a
	 * message the product has already turned into a string -- a compiler error, a refusal in the log -- and knows it by
	 * its English words finds a Chinese editor saying the same thing in Chinese, and its expected error never matches.
	 * Such a test holds one of these around what logs and around reading what was logged. A message that is still an
	 * FText needs none of this: FText::BuildSourceString gives back the English it was written in, in any language.
	 *
	 * In an English editor it does nothing. In any other, the switch there and back costs about three quarters of a
	 * second -- the editor reloads its localization each way -- so it goes around what logs rather than a whole test.
	 */
	struct FScopedEnglish
	{
		FScopedEnglish();
		~FScopedEnglish();
		FScopedEnglish(const FScopedEnglish&) = delete;
		FScopedEnglish& operator=(const FScopedEnglish&) = delete;

	private:
		FInternationalization::FCultureStateSnapshot Before;
		bool bSwitched = false;
	};
}
