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

	// GetMemberPropertyName, not GetPropertyName: the file picker edits the struct's inner FilePath
	// FString, so the innermost name here is "FilePath" and the member name is "SourceFile". The
	// original guard compared the innermost name and therefore NEVER fired for a details-panel pick,
	// which is how absolute paths ended up stored: the picker stores the path verbatim, this was the
	// normalisation, and it was dead on exactly the path that needed it.
	if (PropertyChangedEvent.GetMemberPropertyName()
		== GET_MEMBER_NAME_CHECKED(UDreamTextUserWidget, SourceFile))
	{
		// Assigned unconditionally rather than only when it changes: MakePortablePath returns its
		// input for anything already portable, so the no-op case costs a compare that PostEditChange
		// would do anyway, and the guard it replaces is one more place to get the condition wrong.
		SourceFile.FilePath = DreamUIPaths::MakePortablePath(SourceFile.FilePath);
	}
}
#endif
