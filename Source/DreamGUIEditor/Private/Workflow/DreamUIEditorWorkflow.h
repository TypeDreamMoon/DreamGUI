// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class FDreamGUIEditorModule;

/**
 * The designer's workflow tools -- copying and pasting layer styles, the eyedropper and the project's swatches, the preset
 * library -- put up with the editor session and taken down with it. One call each way from the editor module.
 */
namespace DreamUIEditorWorkflow
{
	void Startup(FDreamGUIEditorModule& InModule);
	void Shutdown(FDreamGUIEditorModule& InModule);
}
