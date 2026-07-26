// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR

#include "Core/LexUIAnchorData.h"

class ULexWidget;
class ULexPanelSlot;

/**
 * While alive, every widget in the hierarchy whose rect was written by a panel pass holds its AUTHORED
 * anchor data instead of the arranged result, and the slot bookkeeping reads "nothing applied".
 *
 * This is what makes arranged geometry transient with respect to the asset: UMG never persists
 * arrangement (it is per-frame FGeometry), while this system arranges by writing into the same
 * serialized AnchorData the user authors. Wrapping serialization in this scope means the asset only
 * ever stores what the user authored — panel arrangement is re-derived by the first layout pass after
 * load — so re-saves stop churning arranged values and a squeezed rect can never be baked into data.
 *
 * The swap is raw field access with no setters and no invalidation: nothing observes the hierarchy
 * between construction and destruction, and destruction restores the exact live state. Nesting is a
 * no-op (an inner scope sees nothing applied and collects no entries).
 */
class LGUI_API FLexUIAuthoredGeometrySaveScope
{
public:
	explicit FLexUIAuthoredGeometrySaveScope(ULexWidget* RootWidget);
	~FLexUIAuthoredGeometrySaveScope();

	FLexUIAuthoredGeometrySaveScope(const FLexUIAuthoredGeometrySaveScope&) = delete;
	FLexUIAuthoredGeometrySaveScope& operator=(const FLexUIAuthoredGeometrySaveScope&) = delete;

	int32 NumSwappedWidgets() const { return Entries.Num(); }

private:
	struct FEntry
	{
		TWeakObjectPtr<ULexWidget> Widget;
		TWeakObjectPtr<ULexPanelSlot> Slot;
		FLexUIAnchorData LiveAnchorData;
		uint8 LiveControlMask = 0;
	};
	TArray<FEntry> Entries;
};

#endif
