// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamListView.h"
#include "Controls/DreamProgressBar.h"
#include "Controls/DreamScrollBar.h"
#include "Controls/DreamScrollBox.h"
#include "Controls/DreamSlider.h"
#include "Controls/DreamTextInput.h"
#include "Controls/DreamTreeView.h"
#include "Controls/DreamUIControl.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamUIControlRegistry.h"
#include "DreamUIEditorTools.h"
#include "Textures/SlateIcon.h"

/*
 * The Controls category, after the Blueprint presets stopped being what it offers.
 *
 * The registry could describe two kinds of thing and the control library was neither. WidgetClass
 * names a Blueprint by asset path; Native is a recipe -- a visual plus a behaviour plus a layout --
 * for a widget the palette assembles. A UDreamUIControl is a class that BUILDS its own parts, so
 * describing it as a recipe would have meant the palette naming parts it has no business knowing,
 * and describing it as a WidgetClass would have meant giving it an asset path it does not have.
 *
 * What that cost, for as long as it lasted: `.dui` had `Native.Button` and the palette handed out
 * BP_Button, an asset that carries no UIButton at all. The two roads into one project disagreed
 * about what a button is, and the disagreement was invisible from either end.
 */

namespace DreamControlClassPaletteTestsLocal
{
	const FDreamUIControlDescriptor* Find(FName InName)
	{
		return FDreamUIControlRegistry::Get().GetDescriptors().FindByPredicate(
			[InName](const FDreamUIControlDescriptor& Item) { return Item.Name == InName; });
	}

	const FName ControlsCategory(TEXT("Controls"));
	const FName LegacyCategory(TEXT("Legacy DreamGUI Controls"));

	/** Place one, the way a double-click does, and hand back the widget under a scoped root. */
	UDreamWidget* MakeRoot(UDreamWidgetTree*& OutTree)
	{
		OutTree = NewObject<UDreamWidgetTree>(GetTransientPackage());
		return OutTree->ConstructWidget(UDreamWidget::StaticClass(), TEXT("PaletteHost"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteControlsAreAllControlClassesTest,
	"DreamGUI.Palette.NothingInTheControlsCategoryIsABlueprintPresetAnyMore",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaletteControlsAreAllControlClassesTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlClassPaletteTestsLocal;

	// The structural half of the retirement. Twelve rows in this category used to resolve to
	// /DreamGUI/Controls/BP_*, and an asset cannot be fixed for the copies already placed -- which
	// is how two of them shipped inert for months. Asserting the KIND rather than listing the twelve
	// names is what makes a thirteenth preset impossible to add here by accident.
	int32 Controls = 0;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		if (Descriptor.Category != ControlsCategory)
		{
			continue;
		}
		++Controls;
		TestNotEqual(*FString::Printf(TEXT("'%s' is not a Blueprint preset"), *Descriptor.Name.ToString()),
			Descriptor.CreationKind, EDreamUIControlCreationKind::WidgetClass);
	}
	TestTrue(TEXT("the category is not empty"), Controls >= 15);

	// And the other half: the presets are still registered, because a project has them placed and a
	// row that vanished would take with it the only way to recognise one.
	const TCHAR* Retired[] =
	{
		TEXT("ButtonPreset"), TEXT("CheckBoxPreset"), TEXT("ToggleGroupPreset"),
		TEXT("HorizontalSliderPreset"), TEXT("VerticalSliderPreset"),
		TEXT("HorizontalScrollbarPreset"), TEXT("VerticalScrollbarPreset"),
		TEXT("ComboBoxPreset"), TEXT("TextInputPreset"), TEXT("TextInputMultilinePreset"),
		TEXT("HorizontalScrollViewPreset"), TEXT("VerticalScrollViewPreset"),
	};
	for (const TCHAR* Name : Retired)
	{
		const FDreamUIControlDescriptor* Descriptor = Find(Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s is still registered"), Name), Descriptor))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s sits in the legacy category"), Name),
			Descriptor->Category, LegacyCategory);
		TestEqual(*FString::Printf(TEXT("%s is still the Blueprint road"), Name),
			Descriptor->CreationKind, EDreamUIControlCreationKind::WidgetClass);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlClassRegistrationTest,
	"DreamGUI.Palette.EveryControlClassEntryNamesAConcreteUserWidgetAndValidates",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlClassRegistrationTest::RunTest(const FString& Parameters)
{
	int32 Checked = 0;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		if (Descriptor.CreationKind != EDreamUIControlCreationKind::ControlClass)
		{
			continue;
		}
		++Checked;
		const FString Name = Descriptor.Name.ToString();
		if (!TestTrue(*FString::Printf(TEXT("%s names a class"), *Name), Descriptor.ControlClass.IsValid()))
		{
			continue;
		}
		TestTrue(*FString::Printf(TEXT("%s is a user widget"), *Name),
			Descriptor.ControlClass->IsChildOf(UDreamUserWidget::StaticClass()));
		TestFalse(*FString::Printf(TEXT("%s can be instanced"), *Name),
			Descriptor.ControlClass->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated));
		// The palette shows an invalid entry disabled with its reason, which for a shipped control
		// is a row nobody can use and everybody can see.
		FText ValidationError;
		TestTrue(*FString::Printf(TEXT("%s validates: %s"), *Name, *ValidationError.ToString()),
			FDreamUIControlRegistry::Get().Validate(Descriptor, ValidationError));
	}
	TestTrue(TEXT("the control library is registered"), Checked >= 15);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteIconsResolveTest,
	"DreamGUI.Palette.EveryEntrysIconResolvesToARealBrush",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaletteIconsResolveTest::RunTest(const FString& Parameters)
{
	// A mistyped style name does not fail, it draws the missing-texture box -- which is how a row
	// once shipped showing "registered texture missing" after a rename. GetOptionalIcon is the only
	// question that separates "found" from "fell back", and asking it of EVERY entry means the next
	// icon name is checked by the suite rather than by whoever happens to open the palette.
	int32 Checked = 0;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		if (!Descriptor.Icon.IsSet())
		{
			continue;
		}
		++Checked;
		TestNotNull(*FString::Printf(TEXT("'%s' has a real icon (%s)"),
			*Descriptor.Name.ToString(), *Descriptor.Icon.GetStyleName().ToString()),
			Descriptor.Icon.GetOptionalIcon());
	}
	TestTrue(TEXT("there were icons to check"), Checked >= 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamOrientationPairsShareOneClassTest,
	"DreamGUI.Palette.AnOrientationPairIsOneClassAndTwoPropertyWrites",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamOrientationPairsShareOneClassTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlClassPaletteTestsLocal;

	// Four pairs of Blueprint presets existed for one reason: an asset cannot branch on a property.
	// The rows survive because that is how an author looks for them; what must NOT survive is two
	// definitions of what a slider is. Same class, different configure -- and the configure has to
	// actually be there, or both rows produce the default orientation and only one of them is right.
	struct FPair { const TCHAR* A; const TCHAR* B; UClass* Class; };
	const FPair Pairs[] =
	{
		{ TEXT("HorizontalSlider"),     TEXT("VerticalSlider"),     UDreamSlider::StaticClass() },
		{ TEXT("HorizontalScrollbar"),  TEXT("VerticalScrollbar"),  UDreamScrollBar::StaticClass() },
		{ TEXT("HorizontalScrollView"), TEXT("VerticalScrollView"), UDreamScrollBox::StaticClass() },
		{ TEXT("TextInput"),            TEXT("TextInputMultiline"), UDreamTextInput::StaticClass() },
	};

	for (const FPair& Pair : Pairs)
	{
		const FDreamUIControlDescriptor* A = Find(Pair.A);
		const FDreamUIControlDescriptor* B = Find(Pair.B);
		if (!TestNotNull(*FString::Printf(TEXT("%s is registered"), Pair.A), A)
			|| !TestNotNull(*FString::Printf(TEXT("%s is registered"), Pair.B), B))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s names its class"), Pair.A), A->ControlClass.Get(), Pair.Class);
		TestEqual(*FString::Printf(TEXT("%s names the same class"), Pair.B), B->ControlClass.Get(), Pair.Class);
		// At least one of the pair must write the property; the row matching the class default may
		// legitimately leave it alone.
		TestTrue(*FString::Printf(TEXT("%s or %s carries the property write"), Pair.A, Pair.B),
			static_cast<bool>(A->NativeConfigure) || static_cast<bool>(B->NativeConfigure));
	}

	// And the writes do what they say, run against a real instance rather than trusted by name.
	UDreamWidgetTree* Tree = nullptr;
	MakeRoot(Tree);
	if (const FDreamUIControlDescriptor* Vertical = Find(TEXT("VerticalSlider")))
	{
		UDreamSlider* Slider = NewObject<UDreamSlider>(Tree, UDreamSlider::StaticClass());
		if (Vertical->NativeConfigure)
		{
			Vertical->NativeConfigure(Slider);
		}
		TestEqual(TEXT("the vertical row leaves a vertical slider"),
			Slider->Direction, EUISliderDirectionType::BottomToTop);
	}
	if (const FDreamUIControlDescriptor* Multiline = Find(TEXT("TextInputMultiline")))
	{
		UDreamTextInput* Input = NewObject<UDreamTextInput>(Tree, UDreamTextInput::StaticClass());
		if (Multiline->NativeConfigure)
		{
			Multiline->NativeConfigure(Input);
		}
		TestTrue(TEXT("the multiline row leaves a multiline input"), Input->bMultiLine);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamControlClassValidationTest,
	"DreamGUI.Palette.AControlClassEntryRefusesWhatCannotBeInstanced",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamControlClassValidationTest::RunTest(const FString& Parameters)
{
	FDreamUIControlDescriptor Descriptor;
	Descriptor.Name = TEXT("TestOnly_ControlClassValidation");
	Descriptor.CreationKind = EDreamUIControlCreationKind::ControlClass;
	FText Error;

	// Placement instances the class, so each of these would fail at the point of a double-click --
	// as a log line in the output log, which is where a palette entry goes to be ignored.
	TestFalse(TEXT("no class at all is refused"), FDreamUIControlRegistry::Get().Validate(Descriptor, Error));

	// UDreamUIControl itself: the family base, abstract, and the easiest one to register by mistake.
	Descriptor.ControlClass = UDreamUIControl::StaticClass();
	TestFalse(TEXT("an abstract control is refused"), FDreamUIControlRegistry::Get().Validate(Descriptor, Error));

	// A class that is not a user widget at all. The placement path casts on this and would have
	// nothing to instance under the selected widget.
	Descriptor.ControlClass = UObject::StaticClass();
	TestFalse(TEXT("a non-user-widget class is refused"), FDreamUIControlRegistry::Get().Validate(Descriptor, Error));

	Descriptor.ControlClass = UDreamProgressBar::StaticClass();
	TestTrue(TEXT("a concrete control is accepted"), FDreamUIControlRegistry::Get().Validate(Descriptor, Error));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLegacyControlCategoryTest,
	"DreamGUI.Palette.AReplacedBehaviourMovesToTheLegacyCategoryRatherThanDisappearing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLegacyControlCategoryTest::RunTest(const FString& Parameters)
{
	using namespace DreamControlClassPaletteTestsLocal;

	// The LGUI-era behaviours the control library replaced. Still registered for the same reason the
	// presets are: an asset built on one of these has to stay explicable.
	const TCHAR* Replaced[] = { TEXT("ProgressBar"), TEXT("ListView"), TEXT("TreeView") };
	for (const TCHAR* Name : Replaced)
	{
		const FDreamUIControlDescriptor* Descriptor = Find(Name);
		if (!TestNotNull(*FString::Printf(TEXT("%s is still registered"), Name), Descriptor))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s sits in the legacy category"), Name),
			Descriptor->Category, LegacyCategory);
	}

	// The one that did NOT move, and the reason it is worth asserting: the control library has no
	// tile view, so retiring this entry would be deleting a feature rather than replacing one. If a
	// Native.TileView ever lands, this assertion is the reminder to move it.
	const FDreamUIControlDescriptor* TileView = Find(TEXT("TileView"));
	if (TestNotNull(TEXT("the tile view is registered"), TileView))
	{
		TestEqual(TEXT("and stays in Controls, having no replacement"),
			TileView->Category, ControlsCategory);
	}

	// The toggle group is the odd one out in the other direction: its replacement is not a control
	// at all but the bare behaviour, which the palette can now offer because a component entry is a
	// thing. Asserting the KIND is what says the preset is no longer the only road to one.
	const FDreamUIControlDescriptor* ToggleGroup = Find(TEXT("ToggleGroup"));
	if (TestNotNull(TEXT("a toggle group is offered"), ToggleGroup))
	{
		TestEqual(TEXT("as a behaviour rather than a preset"),
			ToggleGroup->CreationKind, EDreamUIControlCreationKind::Native);
		TestEqual(TEXT("in the controls category"), ToggleGroup->Category, ControlsCategory);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteNamesAreIdentifiersTest,
	"DreamGUI.Palette.EveryRegistryKeyIsUsableAsAWidgetName",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPaletteNamesAreIdentifiersTest::RunTest(const FString& Parameters)
{
	// A new widget is named from the registry KEY, and its display name is what the compiler
	// declares the Blueprint variable from -- so a key with a space in it produces a widget whose
	// own name the details panel then refuses, in red, forever, for a name the author never typed.
	//
	// The labels are where the spaces legitimately live ("UMG Size Box"), and one creation path was
	// naming from the label instead of the key. Asserting the keys is what makes that class of
	// mistake impossible to reintroduce at the source rather than caught downstream.
	int32 Checked = 0;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		const FString Key = Descriptor.Name.ToString();
		++Checked;
		TestEqual(*FString::Printf(TEXT("'%s' survives being used as a widget name"), *Key),
			UDreamWidgetTree::SanitizeIdentifier(Key), Key);
	}
	TestTrue(TEXT("there were keys to check"), Checked >= 20);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUniqueNameSanitisesTest,
	"DreamGUI.Palette.ANameHandedToTheEditorComesBackUsable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUniqueNameSanitisesTest::RunTest(const FString& Parameters)
{
	// The second half of the guarantee, for the paths that do not name from the registry at all.
	// Sanitising here costs nothing on a name that was already valid, and it is variable-name
	// PRESERVING: the compiler has always derived the variable through the same filter, so a widget
	// called "UMG Size Box" already compiled to UMG_Size_Box. Fixing the display name makes the two
	// agree rather than changing what the graph refers to.
	const FString Cleaned = FDreamUIEditorTools::MakeUniqueWidgetDisplayName(nullptr, TEXT("UMG Size Box"));
	TestEqual(TEXT("a label with spaces comes back as an identifier"), Cleaned, FString(TEXT("UMG_Size_Box")));
	TestEqual(TEXT("and it is what the compiler would have derived anyway"),
		UDreamWidgetTree::SanitizeIdentifier(TEXT("UMG Size Box")), Cleaned);

	// An already-valid name is returned untouched -- this must not start mangling what authors type.
	TestEqual(TEXT("a valid name is left alone"),
		FDreamUIEditorTools::MakeUniqueWidgetDisplayName(nullptr, TEXT("OkButton")), FString(TEXT("OkButton")));
	TestEqual(TEXT("and so is one that is already suffixed"),
		FDreamUIEditorTools::MakeUniqueWidgetDisplayName(nullptr, TEXT("Row_2")), FString(TEXT("Row_2")));
	return true;
}

#endif
