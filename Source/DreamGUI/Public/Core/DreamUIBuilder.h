// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/Function.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamText.h"

class UDreamPanelSlot;
class UDreamUserWidget;
class UDreamWidgetTree;

/**
 * A tree that does not exist yet.
 *
 * The expressions below describe a hierarchy; nothing is constructed until Realize walks the
 * description. That is not a stylistic choice, it buys two things:
 *
 * A node expression carries no context, so it is a value -- returnable from a helper, storable in a
 * variable, composable. The tree it will be built into is named once, at the root. This is the
 * property that lets SNew read the way it does, and threading an Outer through every node would
 * take it away.
 *
 * And nothing is ever an unreferenced UObject. C++ evaluates arguments before the call, so eager
 * construction would create every child before its parent existed to hold it -- and an Outer does
 * not keep a UObject alive, so a collection in that window would take the subtree with it. Realize
 * goes top-down and attaches each widget the statement after it is made.
 *
 * The vocabulary deliberately mirrors .dui, because they describe the same thing:
 *
 *     Image ButtonA {              |   Image("ButtonA")
 *         Brush.TintColor = ...    |       .Visual([](UDreamImage& I){ ... })
 *         @slot SizeRule = Fill    |       .Slot([](UDreamPanelSlot& S){ ... })
 *         + UIButton { ... }       |       .With<UUIButton>([](UUIButton& B){ ... })
 *         Text Face { ... }        |       .Children(Text("Face"))
 *     }                            |
 *
 * What has no counterpart -- `<-`, `<->`, `each`, `-> Handler` -- has none on purpose. Those are
 * authoring features that compile into a generated class, and code that builds its own tree has
 * C++ instead. A native control's consumers still use all four ON it from .dui, because its knobs
 * are UPROPERTYs either way.
 */
struct DREAMGUI_API FDreamUINodeSpec
{
	/**
	 * The name the node will answer to.
	 *
	 * It becomes the widget's DisplayName, not its object name. Everything downstream matches on
	 * DisplayName -- the by-name class binding, MakeWidgetVariableName, FindWidgetByVariableName --
	 * and object names have to be unique within the tree, which a hierarchy with two nodes called
	 * "BG" would violate.
	 */
	FName Name;

	/** Null means a plain UDreamWidget. A UDreamUserWidget subclass here is what Nested() writes. */
	UClass* WidgetClass = nullptr;

	/** Null means a node that draws nothing -- .dui's `Widget` tag. */
	UClass* VisualClass = nullptr;

	/** One `+ Something { }`: a behaviour, a layout container, or a layout-self. */
	struct FComponent
	{
		UClass* Class = nullptr;
		TFunction<void(UObject&)> Init;
	};
	TArray<FComponent> Components;

	TArray<TFunction<void(UDreamWidget&)>> WidgetInit;
	TArray<TFunction<void(UDreamVisual&)>> VisualInit;

	/** Runs after the node is attached, because only then does it have a slot to configure. */
	TArray<TFunction<void(UDreamPanelSlot&)>> SlotInit;

	/** Runs after the WHOLE tree exists. See TDreamUINode::Then. */
	TArray<TFunction<void(UDreamWidget&)>> Deferred;

	TArray<TFunction<void(UDreamWidget*)>> Captures;

	TArray<FDreamUINodeSpec> ChildSpecs;
};

/** Tag type, never defined: the visual parameter of a node that has no visual. */
struct FDreamUINoVisual;

/**
 * The fluent half. Templated on the visual type purely so `.Visual` can take a lambda over the
 * concrete visual without the author restating it -- a node built by Image() will not accept a
 * UDreamText lambda, and that is a compile error rather than a cast that fails on a Tuesday.
 *
 * It adds no data members. Children are stored as the base, and that slice is exactly right: the
 * typing exists to check the expression, not to survive it.
 */
template<class VisualT>
struct TDreamUINode : FDreamUINodeSpec
{
	using ThisType = TDreamUINode<VisualT>;

	/** The widget itself. Size/Pos/Stretch below are the three of these worth a shorter spelling. */
	ThisType&& Self(TFunction<void(UDreamWidget&)> InFn)
	{
		WidgetInit.Emplace(MoveTemp(InFn));
		return MoveTemp(*this);
	}

	/** The thing that draws. `.dui`'s `Brush.TintColor = ...` lives here. */
	ThisType&& Visual(TFunction<void(VisualT&)> InFn)
	{
		static_assert(!std::is_same_v<VisualT, FDreamUINoVisual>,
			"This node draws nothing, so it has no visual to configure. Make it with Image(), Text() or Node<T>().");
		VisualInit.Emplace([Fn = MoveTemp(InFn)](UDreamVisual& InVisual)
		{
			Fn(static_cast<VisualT&>(InVisual));
		});
		return MoveTemp(*this);
	}

	/** `+ UIButton {}`. Behaviours, layout containers and layout-selfs all arrive this way, as in .dui. */
	template<class T>
	ThisType&& With()
	{
		Components.Add({ T::StaticClass(), nullptr });
		return MoveTemp(*this);
	}

	template<class T>
	ThisType&& With(TFunction<void(T&)> InFn)
	{
		Components.Add({ T::StaticClass(), [Fn = MoveTemp(InFn)](UObject& InObject)
		{
			Fn(static_cast<T&>(InObject));
		} });
		return MoveTemp(*this);
	}

	/**
	 * `@slot ...`: what the PARENT's layout is told about this child.
	 *
	 * There is one slot class carrying every layout's fields, so unlike Slate this needs no type --
	 * and unlike the rest of the node it cannot run until the child is attached, which Realize
	 * handles. A node whose parent lays out no slots says so in the log rather than writing to
	 * nothing.
	 */
	ThisType&& Slot(TFunction<void(UDreamPanelSlot&)> InFn)
	{
		SlotInit.Emplace(MoveTemp(InFn));
		return MoveTemp(*this);
	}

	/**
	 * Runs once the whole tree exists.
	 *
	 * For everything that has to name a node other than itself -- a toggle pointing its checked
	 * transition at a knob three levels down, a slider handed its fill and its handle. Parents are
	 * built before children, so that wiring cannot happen inline; this is the same deferred pass,
	 * and for the same reason, that .dui resolves its node references in.
	 */
	ThisType&& Then(TFunction<void(UDreamWidget&)> InFn)
	{
		Deferred.Emplace(MoveTemp(InFn));
		return MoveTemp(*this);
	}

	/**
	 * Hand the built widget back to a member. SAssignNew's counterpart.
	 *
	 * Point it at a UPROPERTY, not a bare pointer: a part nothing reflects is a part the designer,
	 * .dui, the write-back and the bindings cannot see. The target has to outlive the Realize call,
	 * which for a member of the widget being built it does.
	 */
	template<class T>
	ThisType&& Out(T*& OutPtr)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamWidget>::Value,
			"Out hands back the node itself. For the thing that draws it -- which is what a transition or a "
			"tint is aimed at -- use OutVisual.");
		Captures.Emplace([&OutPtr](UDreamWidget* InWidget) { OutPtr = Cast<T>(InWidget); });
		return MoveTemp(*this);
	}

	template<class T>
	ThisType&& Out(TObjectPtr<T>& OutPtr)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamWidget>::Value,
			"Out hands back the node itself. For the thing that draws it, use OutVisual.");
		Captures.Emplace([&OutPtr](UDreamWidget* InWidget) { OutPtr = Cast<T>(InWidget); });
		return MoveTemp(*this);
	}

	/**
	 * Hand back what DRAWS the node, rather than the node.
	 *
	 * The distinction is not pedantry: the properties a control actually wires -- every transition
	 * target, every tint -- are typed UDreamVisual, and a node is not one. Out into a UDreamImage*
	 * would compile as a cast of a widget to a visual and quietly yield null.
	 */
	template<class T>
	ThisType&& OutVisual(T*& OutPtr)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamVisual>::Value,
			"OutVisual hands back the visual. For the node itself, use Out.");
		Captures.Emplace([&OutPtr](UDreamWidget* InWidget)
		{
			OutPtr = InWidget != nullptr ? Cast<T>(InWidget->GetVisual()) : nullptr;
		});
		return MoveTemp(*this);
	}

	template<class T>
	ThisType&& OutVisual(TObjectPtr<T>& OutPtr)
	{
		static_assert(TPointerIsConvertibleFromTo<T, const UDreamVisual>::Value,
			"OutVisual hands back the visual. For the node itself, use Out.");
		Captures.Emplace([&OutPtr](UDreamWidget* InWidget)
		{
			OutPtr = InWidget != nullptr ? Cast<T>(InWidget->GetVisual()) : nullptr;
		});
		return MoveTemp(*this);
	}

	ThisType&& Size(float InWidth, float InHeight)
	{
		return Self([InWidth, InHeight](UDreamWidget& InWidget)
		{
			InWidget.SetWidth(InWidth);
			InWidget.SetHeight(InHeight);
		});
	}

	ThisType&& Pos(float InX, float InY)
	{
		return Self([InX, InY](UDreamWidget& InWidget)
		{
			InWidget.SetAnchoredPosition(FVector2D(InX, InY));
		});
	}

	/** Fill the parent exactly: anchors to its corners, no offsets. `.dui`'s (0,0)-(1,1) plus a zero SizeDelta. */
	ThisType&& Stretch()
	{
		return Self([](UDreamWidget& InWidget)
		{
			InWidget.SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0f, 0.0f), FVector2D(1.0f, 1.0f), false, false);
			InWidget.SetAnchoredPositionAndSizeDelta(FVector2D::ZeroVector, FVector2D::ZeroVector);
		});
	}

	ThisType&& Anchors(FVector2D InMin, FVector2D InMax)
	{
		return Self([InMin, InMax](UDreamWidget& InWidget)
		{
			InWidget.SetHorizontalAndVerticalAnchorMinMax(InMin, InMax, false, false);
		});
	}

	/**
	 * Nesting. One spelling, not Slate's two: its `[ ]` and `+ Slot()` split exists because its slots
	 * are typed per panel, and these are not.
	 *
	 * Children are consumed. A node expression is a temporary that describes one place in one tree.
	 */
	template<typename... TChildren>
	ThisType&& Children(TChildren&&... InChildren)
	{
		ChildSpecs.Reserve(ChildSpecs.Num() + sizeof...(InChildren));
		(ChildSpecs.Emplace(MoveTemp(static_cast<FDreamUINodeSpec&>(InChildren))), ...);
		return MoveTemp(*this);
	}
};

namespace DreamUI
{
	/** A node that draws nothing: a container, a spacer, an area another widget is measured against. */
	inline TDreamUINode<FDreamUINoVisual> Widget(FName InName)
	{
		TDreamUINode<FDreamUINoVisual> Node;
		Node.Name = InName;
		return Node;
	}

	/** A node drawn by any UDreamVisual. Image() and Text() are this with the two common ones filled in. */
	template<class VisualT>
	TDreamUINode<VisualT> Node(FName InName)
	{
		TDreamUINode<VisualT> Result;
		Result.Name = InName;
		Result.VisualClass = VisualT::StaticClass();
		return Result;
	}

	inline TDreamUINode<UDreamImage> Image(FName InName) { return Node<UDreamImage>(InName); }
	inline TDreamUINode<UDreamText> Text(FName InName) { return Node<UDreamText>(InName); }

	/**
	 * Another user widget class as a node, which is what `/Script/DreamGUI.DreamToggle Foo { }` means
	 * in .dui. Its contents come from its own class; this only places it.
	 */
	template<class WidgetT>
	TDreamUINode<FDreamUINoVisual> Nested(FName InName)
	{
		TDreamUINode<FDreamUINoVisual> Result;
		Result.Name = InName;
		Result.WidgetClass = WidgetT::StaticClass();
		return Result;
	}

	/**
	 * Build the description into InTree and return its root.
	 *
	 * The tree comes back whole but UNREGISTERED and unparented -- registration is the caller's, and
	 * has to be, because a subtree registered before it is attached inherits nothing from where it
	 * ends up. Attach first, then RegisterDreamWidgetHierarchy.
	 */
	DREAMGUI_API UDreamWidget* Realize(UDreamWidgetTree* InTree, FDreamUINodeSpec&& InRoot);

	/**
	 * Build a native user widget's own contents, tree and all.
	 *
	 * For a UDreamUserWidget subclass that declares its hierarchy in code rather than in an asset:
	 * call it from NativeOnInitialized, which runs whether or not the class has an archetype and
	 * before anything reads the widget.
	 */
	DREAMGUI_API UDreamWidget* Realize(UDreamUserWidget* InOwner, FDreamUINodeSpec&& InRoot);
}
