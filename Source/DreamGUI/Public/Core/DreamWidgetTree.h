// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamWidgetTree.generated.h"

class UDreamWidget;

/**
 * Owns one widget hierarchy: the object every widget in that hierarchy is outered to.
 *
 * DreamUI used to outer widgets flat to the UWorld and keep the hierarchy only inside the prefab
 * blob (FDreamUIPrefabSaveData::MapWidgetToParent), rebuilding it at load. That left no object
 * graph for the engine to serialize, duplicate or instance -- which is why a widget tree could not
 * live on a class template. The tree is that anchor.
 *
 * Widgets stay outered FLAT to the tree rather than nested under their parents, matching UMG, whose
 * UWidgetTree::ConstructWidget outers every widget to the tree and expresses the hierarchy purely
 * through Instanced properties. FObjectInstancingGraph follows those properties, not the outer
 * chain, so a flat outer costs nothing -- and it means reparenting never has to move an outer,
 * which keeps UObject::Rename (unsafe once an object has begun destruction) out of the attach path.
 *
 * UDreamWidget::GetWorld is GetTypedOuter<UWorld>(), so an instance tree must be outered somewhere
 * that reaches a world. A tree held as a class template deliberately is not: it returns no world,
 * never registers (registration is an explicit OnRegister call, never PostInitProperties) and is
 * never ticked.
 */
UCLASS(ClassGroup = (DreamGUI), DisplayName = "DreamUI Widget Tree")
class DREAMGUI_API UDreamWidgetTree : public UObject
{
	GENERATED_BODY()

public:
	virtual UWorld* GetWorld() const override;

	/**
	 * Root of the hierarchy. Instanced: this is where FObjectInstancingGraph enters the tree, and
	 * from here UDreamWidget::Children (also Instanced) carries it the rest of the way down.
	 */
	UPROPERTY(Instanced)
	TObjectPtr<UDreamWidget> RootWidget = nullptr;

	/** Create a widget owned by this tree. Every widget in a tree is outered to the tree itself. */
	UDreamWidget* ConstructWidget(TSubclassOf<UDreamWidget> InWidgetClass, FName InName = NAME_None);

	template<typename WidgetT>
	WidgetT* ConstructWidget(TSubclassOf<UDreamWidget> InWidgetClass = WidgetT::StaticClass(), FName InName = NAME_None)
	{
		static_assert(TPointerIsConvertibleFromTo<WidgetT, const UDreamWidget>::Value,
			"'WidgetT' template parameter to ConstructWidget must be derived from UDreamWidget");
		return Cast<WidgetT>(ConstructWidget(InWidgetClass, InName));
	}

	/**
	 * Restore the transient Parent back-pointers from the persistent Children arrays. Required after
	 * any path that produces a tree without going through the attach functions -- package load, class
	 * template instancing, subtree duplication -- and before the tree is registered.
	 */
	void RebuildParentLinks();

	/** Visit every widget in the tree, root first, parents before children. Skips nothing. */
	void ForEachWidget(TFunctionRef<void(UDreamWidget*)> InPredicate) const;

	/** Total widget count, root included. Walks the tree; not cached. */
	int32 CountWidgets() const;
};
