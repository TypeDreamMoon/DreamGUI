// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUserWidget.generated.h"

class UDreamWidgetTree;

/**
 * A widget hierarchy that is a CLASS rather than an asset instance -- the counterpart to UMG's
 * UUserWidget, and the thing a DreamUI prefab is becoming.
 *
 * It is itself a UDreamWidget, exactly as UUserWidget is a UWidget. That is what makes nesting fall
 * out for free: one of these placed inside another hierarchy is just a widget in that hierarchy,
 * while its own contents come from its own class.
 *
 * Its contents live behind WidgetTree rather than directly in Children, and that indirection is not
 * decoration. Without it "the class of one widget" and "the class of a hierarchy" would be the same
 * thing, and a UDreamWidget subclass (they are Blueprintable) could not be told apart from a
 * hierarchy template. UMG buys the same separation with UWidgetTree, and it is not optional here
 * either.
 *
 * WidgetTree is Transient: a class template must never carry an expanded subtree, or a nested user
 * widget would be baked into its parent's template and stop tracking its own class. It is built at
 * Initialize, from the class, every time.
 */
UCLASS(ClassGroup = (DreamGUI), BlueprintType, Blueprintable, DisplayName = "DreamUI User Widget")
class DREAMGUI_API UDreamUserWidget : public UDreamWidget
{
	GENERATED_BODY()

public:
	/**
	 * This instance's own hierarchy, instanced from the class template. Transient and
	 * DuplicateTransient: it is regenerated from the class, never persisted and never copied.
	 */
	UPROPERTY(Transient, DuplicateTransient, TextExportTransient)
	TObjectPtr<UDreamWidgetTree> WidgetTree = nullptr;

	/**
	 * Instance the class's template into this widget, resolve the by-name widget bindings, and attach
	 * the resulting root beneath this widget.
	 *
	 * Idempotent, and a no-op on a class template (a CDO has no instance to build). Called for you by
	 * CreateDreamWidget; call it directly only when you constructed the object yourself.
	 */
	void Initialize();

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	bool IsInitialized() const { return bInitialized; }

	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	UDreamWidgetTree* GetWidgetTree() const { return WidgetTree; }

	/** The root of this widget's own contents -- the tree's root, not this widget. Null before Initialize. */
	UFUNCTION(BlueprintPure, Category = "DreamGUI|UserWidget")
	UDreamWidget* GetContentRoot() const;

private:
	uint8 bInitialized : 1 = false;
};

/**
 * Create and initialize a user widget of InClass.
 *
 * A widget needs a tree to belong to, so a fresh UDreamWidgetTree is minted and outered to the world,
 * with the new widget as its root -- the same ownership a prefab load produces. Pass InParent to put
 * the widget into an existing hierarchy instead, in which case it joins that hierarchy's tree.
 *
 * InCallbackBeforeAlive runs after the hierarchy exists and is parented, but before it is registered
 * and before any behaviour's Awake. It is the counterpart of the prefab loader's CallbackBeforeAwake
 * and exists for the same reason: a caller that has to reshape what was built -- swapping the root
 * canvas, for one -- must do it before anything observes the old shape and caches it.
 *
 * Returns null if InClass is not a UDreamUserWidget, or if the world is invalid.
 */
DREAMGUI_API UDreamUserWidget* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass, UDreamWidget* InParent = nullptr,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive = nullptr);

template<typename WidgetT>
WidgetT* CreateDreamWidget(UWorld* InWorld, TSubclassOf<UDreamUserWidget> InClass = WidgetT::StaticClass(), UDreamWidget* InParent = nullptr,
	const TFunction<void(UDreamUserWidget*)>& InCallbackBeforeAlive = nullptr)
{
	static_assert(TPointerIsConvertibleFromTo<WidgetT, const UDreamUserWidget>::Value,
		"'WidgetT' template parameter to CreateDreamWidget must be derived from UDreamUserWidget");
	return Cast<WidgetT>(CreateDreamWidget(InWorld, InClass, InParent, InCallbackBeforeAlive));
}
