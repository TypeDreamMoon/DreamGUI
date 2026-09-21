// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Core/DreamUIBehaviour.h"
#include "DreamContentWidget.generated.h"

class UDreamWidget;

/** Single-child host equivalent to UContentWidget. */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamContentWidget : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(VisibleAnywhere, Transient, BlueprintReadOnly, Category = "ContentWidget", meta = (AllowPrivateAccess = true))
	TObjectPtr<UDreamWidget> Content = nullptr;
	virtual void OnRegister() override;
	virtual void OnWidgetChildAttached(UDreamWidget* Child) override;
	virtual void OnWidgetChildDetached(UDreamWidget* Child) override;
	void SynchronizeContentFromChildren();

public:
	virtual int32 GetMaxWidgetChildren() const override { return 1; }
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	UDreamWidget* GetContent()const;
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	bool SetContent(UDreamWidget* NewContent);
	/** ContentWidget always removes its actual child; bDetach is retained for Blueprint compatibility. */
	UFUNCTION(BlueprintCallable, Category = "ContentWidget")
	void ClearContent(bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "ContentWidget")
	bool CanAcceptChild(const UDreamWidget* Child)const;
};

/** Named child attachment points equivalent to INamedSlotInterface. */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamNamedSlotHost : public UDreamUIBehaviour
{
	GENERATED_BODY()

protected:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NamedSlots", meta = (AllowPrivateAccess = true))
	TMap<FName, TObjectPtr<UDreamWidget>> NamedSlots;
	virtual void OnRegister() override;
	virtual void OnWidgetChildDetached(UDreamWidget* Child) override;
	void SynchronizeNamedSlots();

public:
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	bool SetContentForSlot(FName SlotName, UDreamWidget* Content);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	UDreamWidget* GetContentForSlot(FName SlotName)const;
	UFUNCTION(BlueprintCallable, Category = "NamedSlots")
	void ClearSlot(FName SlotName, bool bDetach = true);
	UFUNCTION(BlueprintPure, Category = "NamedSlots")
	TArray<FName> GetSlotNames()const;
};

/**
 * Marks its widget as a hole in a widget blueprint that whoever PLACES that blueprint fills in.
 *
 * The other half of the boundary DreamWidget_ShouldEditorExpandContents draws. Folding a nested
 * instance into one row is right for a Button or a Slider, which are finished things; it makes a
 * Card, a Panel or a dialogue shell impossible, because the whole point of those is that the parent
 * supplies the middle. This is the sanctioned opening: the class says WHERE, the host says WHAT.
 *
 * The name is the widget's display name, which is what the author already types and already sees in
 * the hierarchy -- the same choice UMG makes for UNamedSlot. One child, like UContentWidget: a slot
 * that took several would be a panel, and panels are a thing the class can put here itself.
 *
 * Not to be confused with UDreamNamedSlotHost above, which is the INamedSlotInterface analogue: a
 * runtime name->child map inside ONE hierarchy, with no other asset involved.
 */
UCLASS(ClassGroup = (DreamGUI), Blueprintable, meta = (BlueprintSpawnableComponent))
class DREAMGUI_API UDreamNamedSlot : public UDreamUIBehaviour
{
	GENERATED_BODY()

public:
	/**
	 * Whether this hole takes more than one widget.
	 *
	 * One is the default and the norm -- UMG's UNamedSlot, our own UContentWidget -- for the reason
	 * above: a hole that took several would be a panel, and a panel is a thing the class can put
	 * here itself. A class that ALREADY put one here says so with this. The expander's content
	 * column is a vertical box and its hole is that box; without this, `Native.ExpandableArea {
	 * Text {} Text {} }` would place the first line and refuse the second.
	 *
	 * Only NESTED content can arrive in numbers. NamedSlotContent maps a name to one widget, so a
	 * designer drop or an explicit binding is one either way -- this widens what an author can nest,
	 * not what the binding map can hold.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "NamedSlot")
	bool bAcceptsSeveral = false;

	virtual int32 GetMaxWidgetChildren() const override { return bAcceptsSeveral ? INDEX_NONE : 1; }

	/** The name the host binds content to: this widget's display name. */
	FName GetSlotName() const;

	/** The content the host put here, or null while the slot is empty. */
	UFUNCTION(BlueprintPure, Category = "NamedSlot")
	UDreamWidget* GetSlotContent() const;
};
