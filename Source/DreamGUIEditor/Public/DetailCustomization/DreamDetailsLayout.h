// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DetailCustomization/DreamPropertyMeta.h"
#include "PropertyEditorDelegates.h"

class IDetailCategoryBuilder;
class IDetailGroup;
class IDetailLayoutBuilder;
class IDetailPropertyRow;
class IPropertyHandle;

/**
 * Lays a details panel out from its properties' DreamGUI_ meta tags (DreamPropertyMeta).
 *
 * A customization builds the rows it draws itself, then calls PlaceTagged last: every property of the objects being
 * customized that the customization has not placed or hidden already, and that says where it goes, is put there -- into
 * its section, its group, in its order, under the advanced expander when it asks to be -- and every one tagged
 * DreamGUI_Hidden is hidden. A property without tags is left alone, so the engine shows it under its own Category.
 * Nothing is hidden by category: a panel built this way cannot lose a property it was not told to hide.
 *
 *   void FMyCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
 *   {
 *       FDreamDetailsLayout Layout(DetailBuilder);
 *       Layout.Section(DreamPropertyMeta::ESection::Layer).AddCustomRow(...);   // rows of its own
 *       Layout.PlaceTagged();
 *   }
 *
 * A customization that hand-places a property and a tag on the same property do not mix: tag it, or place it, not both.
 * The DreamGUI base classes' customizations call PlaceTagged, and a DreamGUI class with tags and no customization in its
 * hierarchy gets one that does (FDreamGUIEditorModule), so tags work on an extension's class as they do on DreamGUI's.
 */
class DREAMGUIEDITOR_API FDreamDetailsLayout
{
public:
	explicit FDreamDetailsLayout(IDetailLayoutBuilder& InBuilder);

	/** The category a section is drawn as, made with its name, title and place in the panel. */
	IDetailCategoryBuilder& Section(DreamPropertyMeta::ESection InSection);

	/** The group at InPath ("Mask", "Glass/Refraction") of a section, made the way a tagged property's is: a row of its own goes in the same group. */
	IDetailGroup& Group(DreamPropertyMeta::ESection InSection, const FString& InPath, bool bInAdvanced = false);

	/**
	 * The tagged properties of one section, placed now rather than last: those of the objects being customized and those
	 * of InHosted -- objects shown inside this panel whose own layout leaves that section to it (IsDrawnByHost), such as a
	 * widget's visual -- merged in their order. Returns the rows made, by property name, to rename or adjust.
	 */
	TMap<FName, IDetailPropertyRow*> PlaceSection(DreamPropertyMeta::ESection InSection, const TArray<UObject*>& InHosted = TArray<UObject*>());

	/** Every tagged property of the objects being customized not placed or hidden yet, placed; DreamGUI_Hidden ones hidden. */
	void PlaceTagged();

	/**
	 * InProperty's row where it belongs, shown only while InApplies says it applies -- for what decides it that is not
	 * another property (whether a canvas is a root, how it is rendered), which EditCondition cannot say. What HideProperty
	 * was used for, without taking the property out of the panel. Nothing when the property is placed or hidden already.
	 */
	IDetailPropertyRow* ShowWhen(FName InProperty, TAttribute<bool> InApplies);

	/** Whether a section of InClass's properties is drawn by the panel InClass is shown inside: a visual's Layer rows are the widget's. */
	static bool IsDrawnByHost(const UClass* InClass, DreamPropertyMeta::ESection InSection);

	/** The editable properties of InClass, from its first base down, in the order they are declared. */
	static TArray<const FProperty*> PropertiesInOrder(const UClass* InClass);

	/** InCustomization, then PlaceTagged on the panel it laid out: how each DreamGUI class customization is registered. */
	static FOnGetDetailCustomizationInstance Wrap(FOnGetDetailCustomizationInstance InCustomization);
	/**
	 * A customization that only places tags, registered for every class that has tags and no customization of its own or of
	 * a base -- now, and for the classes of each module loaded after. Registered last, by the editor module.
	 */
	static void RegisterForTaggedClasses();
	static void UnregisterForTaggedClasses();

private:
	struct FPlacement
	{
		const FProperty* Property = nullptr;
		/** The objects of a property of InHosted; empty for the objects being customized. */
		TArray<UObject*> Objects;
	};

	IDetailCategoryBuilder& CategoryFor(const FProperty* InProperty);
	IDetailGroup& GroupIn(IDetailCategoryBuilder& InCategory, const TArray<FString>& InPath, bool bInAdvanced);
	void Place(TArray<FPlacement>& InPlacements, TMap<FName, IDetailPropertyRow*>* OutRows);

	IDetailLayoutBuilder& Builder;
	TMap<FString, IDetailGroup*> Groups;
};
