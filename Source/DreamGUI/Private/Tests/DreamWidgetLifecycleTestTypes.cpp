// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/DreamWidgetLifecycleTestTypes.h"

#include "Core/Components/DreamWidget.h"

void UDreamWidgetHierarchyMutationBehaviour::OnUnregister()
{
	Super::OnUnregister();
	if (IsValid(WidgetToDetach) && IsValid(ExternalParent))
	{
		WidgetToDetach->SetParent(ExternalParent, false);
	}
	if (IsValid(WidgetToAttach) && IsValid(GetWidget()))
	{
		WidgetToAttach->SetParent(GetWidget(), false);
	}
}
