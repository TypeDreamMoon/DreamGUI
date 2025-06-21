// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIFontData_BaseObject.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"

#define LOCTEXT_NAMESPACE "LGUIFontData_BaseObject"

ULexUIFontData_BaseObject* ULexUIFontData_BaseObject::GetDefaultFont()
{
	static auto defaultFont = LoadObject<ULexUIFontData_BaseObject>(NULL, TEXT("/LGUI/DefaultSDFFont"));
	if (defaultFont == nullptr)
	{
		auto errMsg = FText::Format(LOCTEXT("MissingDefaultContent", "{0} Load default font error! Missing some content of LGUI plugin, reinstall this plugin may fix the issue.")
			, FText::FromString(FString::Printf(TEXT("[%s].%d"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__)));
		UE_LOG(LGUI, Error, TEXT("%s"), *errMsg.ToString());
#if WITH_EDITOR
		FLexUIUtils::EditorNotification(errMsg, 10);
#endif
		return nullptr;
	}
	return defaultFont;
}

#undef LOCTEXT_NAMESPACE