// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIComponentVisualizerModule.h"

#include "UnrealEdGlobals.h"
#include "Editor/UnrealEdEngine.h"

#define LOCTEXT_NAMESPACE "FLGUIComponentVisualizerModule"
DEFINE_LOG_CATEGORY(LGUIComponentVisualizer);

void FLGUIComponentVisualizerModule::StartupModule()
{
	//component visualizer
	{
		if (GUnrealEd)
		{
			// TSharedPtr<FUIItemComponentVisualizer> UIItemVisualizer = MakeShareable(new FUIItemComponentVisualizer);
			// GUnrealEd->RegisterComponentVisualizer(ULexWidget::StaticClass()->GetFName(), UIItemVisualizer);
			// UIItemVisualizer->OnRegister();
		}
	}
}

void FLGUIComponentVisualizerModule::ShutdownModule()
{
	//unregister component visualizer
	{
		if (GUnrealEd)
		{
			// GUnrealEd->UnregisterComponentVisualizer(ULexWidget::StaticClass()->GetFName());
		}
	}
}

IMPLEMENT_MODULE(FLGUIComponentVisualizerModule, LGUIComponentVisualizer)

#undef LOCTEXT_NAMESPACE