// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Math/Color.h"
#include "Templates/SharedPointer.h"
#include "UObject/WeakObjectPtr.h"

class UDreamWidget;

/*
 * VIEWPORT HANDLES: points of the selected widget that the designer's viewport lets you drag -- a warp's lattice, a path's
 * points, a gradient's ends and stops, and whatever else registers a provider.
 *
 * A provider says what the handles are and what a drag of one changes; the viewport does the rest the same way for all of
 * them. It asks the providers only while exactly one widget is selected, unlocked and the asset's own, every frame, and:
 *  - draws their guide lines and handles over the scene, the one under the pointer a little larger;
 *  - on a press over a handle opens one transaction and takes two snapshots of every property the provider names: the
 *    preview's value, which Esc (or the viewport losing focus) puts back, and the template's, for undo;
 *  - while the pointer moves, has the provider change the PREVIEW's objects, so what is drawn follows the hand;
 *  - on the release writes those properties to the template and the template to the .dui -- inside the same transaction, so
 *    the drag is one undo step -- or, when nothing changed, cancels the transaction;
 *  - on a double click asks the provider of the handle under the pointer, or each provider in turn when there is none, and
 *    writes what the one that took it changed the same way.
 * Game thread, editor only.
 */

/** How a handle is drawn. */
enum class EDreamDesignerHandleShape : uint8
{
	Square,
	Circle,
	Diamond,
};

/** One draggable point. */
struct DREAMGUIEDITOR_API FDreamDesignerHandle
{
	/** The provider's own number for it, handed back with every drag. */
	int32 Id = INDEX_NONE;
	/** Where it is, in the widget's local space: units from its pivot, +Y up, as the widget lays its children out. */
	FVector2D Local = FVector2D::ZeroVector;
	EDreamDesignerHandleShape Shape = EDreamDesignerHandleShape::Square;
	/** The edge it is drawn with, and its inside: a colour stop shows its own colour. Transparent inside: Outline fills it. */
	FLinearColor Outline = FLinearColor(0.95f, 0.95f, 0.95f, 0.95f);
	FLinearColor Fill = FLinearColor::Transparent;
	/** Half its width on screen, in pixels. */
	float Radius = 4.0f;
	/** Where two handles overlap, the higher one is grabbed; at the same priority, the nearer. */
	int32 Priority = 0;
};

/** A guide line, in the widget's local space. */
struct DREAMGUIEDITOR_API FDreamDesignerHandleLine
{
	FVector2D From = FVector2D::ZeroVector;
	FVector2D To = FVector2D::ZeroVector;
	FLinearColor Color = FLinearColor(0.95f, 0.95f, 0.95f, 0.85f);
	float Thickness = 1.0f;
};

/** What a provider shows for the selected widget. */
struct DREAMGUIEDITOR_API FDreamDesignerHandleSet
{
	TArray<FDreamDesignerHandle> Handles;
	TArray<FDreamDesignerHandleLine> Lines;
	/** Screen pixels per local unit, set by the viewport before it asks: for what is placed a number of pixels from something. */
	double PixelsPerUnit = 1.0;

	FDreamDesignerHandle& AddHandle(int32 InId, const FVector2D& InLocal);
	void AddLine(const FVector2D& InFrom, const FVector2D& InTo, const FLinearColor& InColor, float InThickness = 1.0f);
	void AddPolyline(TConstArrayView<FVector2D> InPoints, bool bInClosed, const FLinearColor& InColor, float InThickness = 1.0f);
	/** An ellipse of radii InRadii about InCenter, its first axis turned InAngleDegrees from +X towards -Y (clockwise on screen). */
	void AddEllipse(const FVector2D& InCenter, const FVector2D& InRadii, float InAngleDegrees, const FLinearColor& InColor, int32 InSegments = 48);
};

/** One step of a drag. */
struct DREAMGUIEDITOR_API FDreamDesignerHandleDrag
{
	int32 HandleId = INDEX_NONE;
	/** Where the pointer was pressed, where the handle was then, and where the pointer is now; local space. */
	FVector2D GrabLocal = FVector2D::ZeroVector;
	FVector2D HandleLocal = FVector2D::ZeroVector;
	FVector2D PointerLocal = FVector2D::ZeroVector;
	bool bShift = false;
	bool bCtrl = false;
	bool bAlt = false;
	/** Screen pixels per local unit, for a distance a hand measures in pixels (how far off a line counts as off it). */
	double PixelsPerUnit = 1.0;

	/** Where the handle goes: moved as far as the pointer, rather than jumping onto it. */
	FVector2D GetTarget() const { return HandleLocal + (PointerLocal - GrabLocal); }
};

/** A property a gesture writes: one on a PREVIEW object -- the widget, its visual or one of its behaviours -- by its name there. */
struct DREAMGUIEDITOR_API FDreamDesignerEditedProperty
{
	TWeakObjectPtr<UObject> Object;
	FName Property;

	FDreamDesignerEditedProperty() = default;
	FDreamDesignerEditedProperty(UObject* InObject, FName InProperty) : Object(InObject), Property(InProperty) {}
};

/** Something of the selected widget edited by dragging points in the viewport. */
class DREAMGUIEDITOR_API IDreamDesignerHandleProvider : public TSharedFromThis<IDreamDesignerHandleProvider>
{
public:
	virtual ~IDreamDesignerHandleProvider() = default;

	/** For the log and the tests. */
	virtual FName GetName() const = 0;
	/** InWidget's handles and guide lines, InWidget the preview's selected widget: nothing added, none. Asked every frame. */
	virtual void CollectHandles(UDreamWidget* InWidget, FDreamDesignerHandleSet& OutSet) const = 0;
	/** What a gesture on handle InHandleId (INDEX_NONE: a double click on no handle) writes. Nothing: the gesture does nothing. */
	virtual void GetEditedProperties(UDreamWidget* InWidget, int32 InHandleId, TArray<FDreamDesignerEditedProperty>& OutProperties) const = 0;
	/** What the undo history calls the gesture. */
	virtual FText GetTransactionName(int32 InHandleId) const = 0;
	/** The pointer moved with handle InDrag.HandleId held: change the preview's objects (through their setters). True when anything changed. */
	virtual bool UpdateDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag) = 0;
	/**
	 * The press ended: released, with bInCancelled false -- the last chance to change something, a stop dragged off its line
	 * taken out, say -- or cancelled, after the preview's values were put back. True when the release changed anything more.
	 */
	virtual bool EndDrag(UDreamWidget* InWidget, const FDreamDesignerHandleDrag& InDrag, bool bInCancelled) { return false; }
	/**
	 * A double click at InLocal, on handle InHandleId or on none (INDEX_NONE): change the preview's objects and answer true, or
	 * answer false to leave it to the next provider and the designer. InPixelsPerUnit as on FDreamDesignerHandleDrag.
	 */
	virtual bool DoubleClick(UDreamWidget* InWidget, const FVector2D& InLocal, int32 InHandleId, double InPixelsPerUnit) { return false; }
	/**
	 * InProperties were put back on the preview's objects, a drag having been cancelled: make them show it. By default each
	 * object is told its property changed (PostEditChangeProperty), as an edit in the details panel tells it.
	 */
	virtual void OnRestored(UDreamWidget* InWidget, TConstArrayView<FDreamDesignerEditedProperty> InProperties);
};

/** The providers the designer's viewport asks, and what they share. */
class DREAMGUIEDITOR_API FDreamDesignerHandles
{
public:
	/** Asked from then on, after those already registered. A module registers its providers when it starts and unregisters them when it stops. */
	static void Register(const TSharedRef<IDreamDesignerHandleProvider>& InProvider);
	static void Unregister(const TSharedRef<IDreamDesignerHandleProvider>& InProvider);
	static const TArray<TSharedRef<IDreamDesignerHandleProvider>>& GetProviders();
	static TSharedPtr<IDreamDesignerHandleProvider> FindProvider(FName InName);

	/** A point of InWidget's rect as fractions of it from its top left (0, 0) to its bottom right (1, 1), in its local space. */
	static FVector2D RectFractionToLocal(const UDreamWidget* InWidget, const FVector2D& InFromTopLeft);
	static FVector2D LocalToRectFraction(const UDreamWidget* InWidget, const FVector2D& InLocal);

	/** The colour a handle stands out in against most UIs, and the one a held or hovered handle turns. */
	static FLinearColor GetGuideColor();
	static FLinearColor GetActiveColor();
};
