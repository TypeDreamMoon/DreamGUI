// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamUIPrefabEdMode.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabEdMode"

const FEditorModeID UDreamUIPrefabEdMode::EM_DreamUIPrefab = TEXT("EM_DreamUIPrefab");

UDreamUIPrefabEdMode::UDreamUIPrefabEdMode()
{
	// bVisible false: this is not a mode anyone picks from a toolbar, it is what the prefab editor's
	// viewport activates on itself.
	Info = FEditorModeInfo(
		EM_DreamUIPrefab,
		LOCTEXT("DreamUIPrefabEdModeName", "DreamGUI Prefab"),
		FSlateIcon(),
		false);
}

#undef LOCTEXT_NAMESPACE
