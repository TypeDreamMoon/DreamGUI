// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DreamUIRender/DreamUIBlendMode.h"
#include "IPropertyTypeCustomization.h"
#include "UObject/WeakObjectPtr.h"

class IPropertyHandle;
class SWidget;
struct FSlateBrush;

/**
 * A small picture of each blend mode: a disc blended over a strip of hues and a ramp of greys by the mode's own
 * formula -- FDreamUIBlendMath::ReferenceComposite, which the shaders are held to -- so every picture is what the mode
 * does, made the first time one is asked for and kept until the editor module goes.
 */
class DREAMGUIEDITOR_API FDreamBlendModeThumbnails
{
public:
	static FIntPoint GetSize();
	static const FSlateBrush* Get(EDreamUIBlendMode InMode);
	/** The pixels of InMode's picture, GetSize() across, row by row. */
	static void Render(EDreamUIBlendMode InMode, TArray<FColor>& OutPixels);
	/** Lets the pictures go: from the module's shutdown, while the object system is still up. */
	static void Shutdown();
};

/**
 * Shows the objects a property handle edits with another value for a moment, and puts them back.
 *
 * The value is written straight into each object and the object told as an interactive edit -- so it draws with it --
 * but no transaction is opened and the details panel's hook is not called: nothing is recorded for undo, nothing is
 * carried to a designer's template or written to a .dui. End puts back what each object held when Begin first ran.
 */
class DREAMGUIEDITOR_API FDreamPropertyPreview
{
public:
	~FDreamPropertyPreview();
	/** InValueText (as a property row would import it) on every object InHandle edits. A second Begin keeps the first originals. */
	void Begin(const TSharedRef<IPropertyHandle>& InHandle, const FString& InValueText);
	void End();
	bool IsActive() const { return Originals.Num() > 0; }

private:
	void Write(const TSharedRef<IPropertyHandle>& InHandle, TFunctionRef<FString(int32)> InTextFor);
	TWeakPtr<IPropertyHandle> Handle;
	TArray<FString> Originals;
};

/**
 * Every EDreamUIBlendMode property: the mode's picture beside its name, and a menu of all 27 with theirs, in
 * Photoshop's order and groups. Hovering a mode in the menu shows the widgets being edited with it until the pointer
 * moves on or the menu closes (FDreamPropertyPreview); choosing one sets it as any property row does.
 */
class DREAMGUIEDITOR_API FDreamBlendModeCustomization : public IPropertyTypeCustomization
{
public:
	static TSharedRef<IPropertyTypeCustomization> MakeInstance();

	virtual void CustomizeHeader(TSharedRef<IPropertyHandle> PropertyHandle, class FDetailWidgetRow& HeaderRow, IPropertyTypeCustomizationUtils& CustomizationUtils) override;
	virtual void CustomizeChildren(TSharedRef<IPropertyHandle> PropertyHandle, class IDetailChildrenBuilder& ChildBuilder, IPropertyTypeCustomizationUtils& CustomizationUtils) override {}

	/** The menu's groups, in order: normal, darken, lighten, contrast, inversion and component. Every mode once. */
	static const TArray<TArray<EDreamUIBlendMode>>& GetGroups();
	static FText GetModeName(EDreamUIBlendMode InMode);

private:
	TSharedRef<SWidget> MakeMenu();
	TOptional<EDreamUIBlendMode> GetCurrent() const;
	void Choose(EDreamUIBlendMode InMode);

	TSharedPtr<IPropertyHandle> Handle;
	FDreamPropertyPreview Preview;
};
