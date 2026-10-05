// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "AssetToolsModule.h"
#include "Factories/Factory.h"
#include "IAssetTools.h"
#include "Modules/ModuleManager.h"
#include "UObject/UObjectIterator.h"

#include "DataFactory/DreamUIAssetFactory.h"
#include "DataFactory/DreamWidgetBlueprintFactory.h"

/*
 * The Content Browser's Add menu: the DreamGUI widget offered at its top, beside the engine's Blueprint Class, and the
 * DreamGUI submenu in sections with short labels and one-line tooltips -- not the flat list of type names it was, with
 * the widget last and the classes' programmer comments as tooltips.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAddMenuEntriesTest,
	"DreamGUI.Editor.AddMenu.EveryDreamGUIAssetIsOfferedWithALabelATipAndASection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAddMenuEntriesTest::RunTest(const FString& Parameters)
{
	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
	const EAssetTypeCategories::Type DreamCategory = AssetTools.FindAdvancedAssetCategory(FName(TEXT("DreamUI")));
	if (!TestTrue(TEXT("the DreamGUI category is registered"), DreamCategory != EAssetTypeCategories::Misc))
	{
		return false;
	}

	// Every factory of DreamGUI's editor module that offers a new asset is a UDreamUIAssetFactory, so none can come
	// back as a raw type name filed under the menu's Basic heading.
	const FString EditorPackage = TEXT("/Script/DreamGUIEditor");
	int32 Offered = 0;
	for (TObjectIterator<UClass> It; It; ++It)
	{
		UClass* Class = *It;
		if (!Class->IsChildOf(UFactory::StaticClass()) || Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
			|| Class->GetOutermost()->GetName() != EditorPackage)
		{
			continue;
		}
		const UFactory* Factory = Class->GetDefaultObject<UFactory>();
		if (!Factory->CanCreateNew() || !Factory->ShouldShowInNewMenu())
		{
			continue;
		}
		++Offered;
		const UDreamUIAssetFactory* DreamFactory = Cast<UDreamUIAssetFactory>(Factory);
		if (!TestNotNull(*FString::Printf(TEXT("%s is a UDreamUIAssetFactory"), *Class->GetName()), DreamFactory))
		{
			continue;
		}
		const FString Label = DreamFactory->GetDisplayName().ToString();
		TestFalse(*FString::Printf(TEXT("%s has a label"), *Class->GetName()), Label.IsEmpty());
		TestFalse(*FString::Printf(TEXT("%s's label does not repeat the type name (%s)"), *Class->GetName(), *Label),
			Label.StartsWith(TEXT("DreamUI ")));
		TestFalse(*FString::Printf(TEXT("%s has a tooltip"), *Class->GetName()), DreamFactory->GetToolTip().IsEmpty());
		TestTrue(*FString::Printf(TEXT("%s is in the DreamGUI submenu"), *Class->GetName()),
			(DreamFactory->GetMenuCategories() & DreamCategory) != 0);
	}
	TestTrue(FString::Printf(TEXT("the DreamGUI assets are all there (%d)"), Offered), Offered >= 11);

	// The widget at the top of the menu, and first in the submenu's Basic section, under the name it is asked for by.
	const UDreamWidgetBlueprintFactory* WidgetFactory = GetDefault<UDreamWidgetBlueprintFactory>();
	TestTrue(TEXT("the widget Blueprint is offered at the top of the Add menu"),
		(WidgetFactory->GetMenuCategories() & EAssetTypeCategories::Basic) != 0);
	TestTrue(TEXT("...and in the DreamGUI submenu"), (WidgetFactory->GetMenuCategories() & DreamCategory) != 0);
	TestEqual(TEXT("...as the DreamGUI Widget"), WidgetFactory->GetDisplayName().ToString(), FString(TEXT("DreamGUI Widget")));
	// The category alone, with no section under it, is what the menu files under its own Basic section.
	bool bInBasicSection = true;
	for (const FAssetCategoryPath& Path : WidgetFactory->GetAssetMenuPathsForCategory(FName(TEXT("DreamGUI"))))
	{
		bInBasicSection &= !Path.HasSubCategory();
	}
	TestTrue(TEXT("...in the submenu's Basic section, which the menu names and puts first itself"), bInBasicSection);

	// A filed entry names its section, as a section rather than a further submenu.
	// By path: the font's factory is not exported from the editor module, and its menu entry is all that is asked of it.
	const UClass* FontFactoryClass = FindObject<UClass>(nullptr, TEXT("/Script/DreamGUIEditor.DreamUIFontDataDistanceFieldFactory"));
	const UDreamUIAssetFactory* FontFactory = FontFactoryClass != nullptr ? Cast<UDreamUIAssetFactory>(FontFactoryClass->GetDefaultObject()) : nullptr;
	if (!TestNotNull(TEXT("the distance field font's factory"), FontFactory))
	{
		return false;
	}
	const TArray<FAssetCategoryPath> FontPaths = FontFactory->GetAssetMenuPathsForCategory(FName(TEXT("DreamGUI")));
	if (TestEqual(TEXT("the distance field font has one place in the menu"), FontPaths.Num(), 1))
	{
		TArray<FCategoryPath> SubCategories;
		FontPaths[0].GetSubCategoriesInfo(SubCategories);
		if (TestEqual(TEXT("one level below the DreamGUI submenu"), SubCategories.Num(), 1))
		{
			TestTrue(TEXT("the Fonts section"), SubCategories[0].GetSubMenuName().EqualTo(UDreamUIAssetFactory::FontsSection()));
			TestTrue(TEXT("a section, not a submenu"), SubCategories[0].GetCategoryMenuType() == ECategoryMenuType::Section);
		}
	}
	return true;
}

#endif
