// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Core/DreamTextUserWidget.h"

#include "Text/DreamUIPaths.h"

FString UDreamTextUserWidget::ResolveDuiFilePath(const FString& InPath)
{
	return DreamUIPaths::Resolve(InPath);
}

#if WITH_EDITOR
void UDreamTextUserWidget::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
	Super::PostEditChangeProperty(PropertyChangedEvent);

	if (PropertyChangedEvent.GetPropertyName()
		== GET_MEMBER_NAME_CHECKED(UDreamTextUserWidget, SourceFile))
	{
		// Assigned unconditionally rather than only when it changes: MakePortablePath returns its
		// input for anything already portable, so the no-op case costs a compare that PostEditChange
		// would do anyway, and the guard it replaces is one more place to get the condition wrong.
		SourceFile.FilePath = DreamUIPaths::MakePortablePath(SourceFile.FilePath);
	}
}
#endif
