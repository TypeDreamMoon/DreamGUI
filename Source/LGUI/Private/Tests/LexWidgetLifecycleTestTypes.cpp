// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Tests/LexWidgetLifecycleTestTypes.h"

#include "Core/Components/LexWidget.h"

void ULexWidgetHierarchyMutationBehaviour::OnUnregister()
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
