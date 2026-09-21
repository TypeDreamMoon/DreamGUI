// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Commandlets/Commandlet.h"
#include "DreamGUIReferenceDocsCommandlet.generated.h"

/**
 * Writes the control library's reference pages from reflection.
 *
 *     UnrealEditor-Cmd.exe <project>.uproject -run=DreamGUIReferenceDocs [-Out=<directory>]
 *
 * A hand-written property reference is wrong the day after it is written: the header gains a knob,
 * the page does not, and nothing fails. These pages are printed from the same reflection data the
 * details panel and the Blueprint menus read, so what they say a control offers is what it offers.
 * The comment above a property is its description here, which is one more reason to write it for the
 * person using the control rather than the person maintaining it.
 *
 * Each page ends with the class's UMG comparison, read from Resources/UMGParity: the same tables the
 * automation suite holds against UMG's own reflection, so "how does this differ from UScrollBox" has
 * one answer and it is a tested one.
 *
 * Output is deterministic -- members in declaration order, everything else sorted -- so that the diff
 * of a regenerated page is the diff of the API.
 */
UCLASS()
class UDreamGUIReferenceDocsCommandlet : public UCommandlet
{
	GENERATED_BODY()

public:
	UDreamGUIReferenceDocsCommandlet();

	virtual int32 Main(const FString& Params) override;
};
