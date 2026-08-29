// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
// FWeakObjectPtr itself, not just the template that names it: TWeakObjectPtr<UDreamWidget> is a
// member below, so the pointee type has to be complete here.
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class FDreamWidgetPreviewHost;
class UDreamWidget;
struct FDreamWidgetReference;

/**
 * One widget's identity, held indirectly so a reinstance can repoint it.
 *
 * Nothing outside the preview host constructs these: the host owns the pool and is the only thing
 * that can keep it honest when the editor replaces objects underneath it.
 */
struct FDreamWidgetHandle
{
	friend struct FDreamWidgetReference;
	friend class FDreamWidgetPreviewHost;

private:
	explicit FDreamWidgetHandle(UDreamWidget* InWidget)
		: Widget(InWidget)
	{
	}

	TWeakObjectPtr<UDreamWidget> Widget;
};

/**
 * A widget as the designer sees it: the TEMPLATE that gets saved, plus the PREVIEW that gets drawn.
 *
 * The two exist because a template is inert. Template widgets live on the UDreamWidgetBlueprint,
 * outside any world, so they have no render state, no layout pass and no behaviour lifecycle --
 * asking one for its drawn geometry gets whatever it was last told, not what it looks like. The
 * preview is an ordinary CreateDreamWidget instance of the compiled class, registered in the
 * designer's world, and it is the only one of the pair that can answer a question about pixels.
 *
 * So: structure is edited on the template (and the preview rebuilt from it), while properties and
 * geometry are edited on the preview (and mirrored back). That split is UMG's, for the same reason.
 *
 * Never cache GetPreview(): the preview is destroyed and rebuilt whenever the hierarchy changes.
 * Holding a reference is how you survive that; holding the pointer is how you get a dangling one.
 */
struct DREAMGUIEDITOR_API FDreamWidgetReference
{
	friend class FDreamWidgetPreviewHost;

public:
	FDreamWidgetReference() = default;

	/** True when the template is still alive AND the preview has a counterpart for it. */
	bool IsValid() const;

	/**
	 * The widget that is serialized -- the one to edit structure and to write authored values onto.
	 * Answers even when there is no preview, which is what identity and hashing compare on.
	 */
	UDreamWidget* GetTemplate() const;

	/** The live counterpart in the designer world. Transient; do not cache. */
	UDreamWidget* GetPreview() const;

	/** Identity is the template. Two references to the same template are the same reference. */
	bool operator==(const FDreamWidgetReference& Other) const
	{
		return GetTemplate() == Other.GetTemplate();
	}
	bool operator!=(const FDreamWidgetReference& Other) const { return !operator==(Other); }

private:
	FDreamWidgetReference(TSharedPtr<FDreamWidgetPreviewHost> InHost, TSharedPtr<FDreamWidgetHandle> InTemplateHandle);

	TWeakPtr<FDreamWidgetPreviewHost> Host;
	TSharedPtr<FDreamWidgetHandle> TemplateHandle;
};

inline uint32 GetTypeHash(const FDreamWidgetReference& InReference)
{
	return ::GetTypeHash(reinterpret_cast<void*>(InReference.GetTemplate()));
}
