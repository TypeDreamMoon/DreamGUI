// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamUIWidgetRegistry.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamProgressBar.h"
#include "Controls/DreamRadioButton.h"
#include "Controls/DreamSpinBox.h"
#include "Controls/DreamDropdown.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamTextInput.h"
#include "Controls/DreamToggle.h"

// The library's tags, gathered rather than scattered FOR NOW: the idiomatic home for each line is
// the foot of its control's own .cpp, but those files are mid-edit elsewhere and a registration is
// position-independent. Move each line home when its file next opens for other reasons.
DECLARE_DREAM_GUI_WIDGET("Native", "Button", UDreamButton)
DECLARE_DREAM_GUI_WIDGET("Native", "Toggle", UDreamToggle)
DECLARE_DREAM_GUI_WIDGET("Native", "Slider", UDreamSlider)
DECLARE_DREAM_GUI_WIDGET("Native", "TextInput", UDreamTextInput)
DECLARE_DREAM_GUI_WIDGET("Native", "Dropdown", UDreamDropdown)
DECLARE_DREAM_GUI_WIDGET("Native", "ProgressBar", UDreamProgressBar)
DECLARE_DREAM_GUI_WIDGET("Native", "RadioButton", UDreamRadioButton)
DECLARE_DREAM_GUI_WIDGET("Native", "SpinBox", UDreamSpinBox)
