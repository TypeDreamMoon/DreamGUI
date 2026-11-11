// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamUITextStructureTestFixtures.h"
#include "DreamUserWidgetTestTypes.h"
#include "DreamWidgetBlueprintTestTypes.h"

#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetPropertyBindingExtension.h"
#include "Text/DreamUIBlueprintConverter.h"

#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Core/DreamUserWidget.h"
#include "Core/Components/DreamVisual.h"
#include "UObject/UnrealType.h"

#include <limits>

/*
 * A hand-authored Blueprint turned into one authored in a `.dui`: the file made by the write-back, the class pointed at
 * it, the tree rebuilt from it equal to the one it came from -- and the refusals, the loss, the reparenting.
 */

namespace DreamUIConverterTestLocal
{
	using namespace DreamUITextStructureFixtures;

	/** A file path under Saved/DreamGUITests that does not exist yet, and is gone again when the test returns. */
	FString TargetPath(const TCHAR* InName)
	{
		FString Path = FPaths::ConvertRelativePathToFull(FPaths::Combine(FPaths::ProjectSavedDir(), TEXT("DreamGUITests"), TEXT("Converted"), InName));
		FPaths::NormalizeFilename(Path);
		IFileManager::Get().Delete(*Path, false, true, true);
		return Path;
	}

	struct FScopedFileCleanup
	{
		FString Path;
		~FScopedFileCleanup() { IFileManager::Get().Delete(*Path, false, true, true); }
	};

	TArray<FString> SplitLines(const FString& InText)
	{
		TArray<FString> Lines;
		InText.ParseIntoArrayLines(Lines, /*bCullEmpty*/false);
		return Lines;
	}

	/** Detach a text-authored Blueprint from its file: from here on its tree is its own, as a hand-authored one's is. */
	bool Detach(UDreamWidgetBlueprint* InBlueprint)
	{
		return DreamUITextAuthoring::SetAuthoredSourcePath(InBlueprint, FString()) && !DreamUITextAuthoring::IsTextAuthored(InBlueprint);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIConverterRoundTripTest,
	"DreamGUI.Designer.Text.AHandAuthoredBlueprintConvertsToADuiThatBuildsTheSameTree",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIConverterRoundTripTest::RunTest(const FString&)
{
	using namespace DreamUIConverterTestLocal;

	FScopedDuiFile Source(TEXT("ConverterSource.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { Spacing = 6 }"),
		TEXT("    Text Title {"),
		TEXT("        Text <- GetTitleText()"),
		TEXT("        FontSize = 28"),
		TEXT("        @slot Padding = (4, 8, 4, 8)"),
		TEXT("    }"),
		TEXT("    Widget Play {"),
		TEXT("        + UIButton { }"),
		TEXT("    }"),
		TEXT("    Text Caption {"),
		TEXT("        Text = \"Hello\""),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_ConverterRoundTrip"), Source.FilePath, /*bInOpenDesigner*/false,
		UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	UDreamWidgetBlueprint* Blueprint = Scoped.Blueprint;
	if (!TestTrue(TEXT("detached, the Blueprint authors its own tree"), Detach(Blueprint))) return false;
	if (!TestNotNull(TEXT("and still has it"), Scoped.FindTemplate(TEXT("Title")))) return false;

	// An animation made in the animation editor, on the root: the asset keeps it, the file lists it.
	UDreamWidget* Root = Blueprint->WidgetTree->RootWidget;
	UDreamWidgetAnimationComponent* Animator = Root->GetComponent<UDreamWidgetAnimationComponent>();
	if (Animator == nullptr)
	{
		Animator = Root->AddComponent<UDreamWidgetAnimationComponent>();
	}
	UDreamWidgetAnimation* Pulse = Animator != nullptr ? Animator->AddNewAnimation() : nullptr;
	if (!TestNotNull(TEXT("an animation was made on the root"), Pulse)) return false;
	Pulse->SetDisplayNameString(TEXT("Pulse"));

	const FString Target = TargetPath(TEXT("ConverterRoundTrip.dui"));
	FScopedFileCleanup Cleanup{ Target };
	FDreamUIConversionReport Report;
	const bool bConverted = FDreamUIBlueprintConverter::Convert(Blueprint, Target, /*bInAllowLoss*/false, Report);
	if (!TestTrue(*FString::Printf(TEXT("the Blueprint converts:\n%s"), *Report.ToString()), bConverted)) return false;
	TestTrue(*FString::Printf(TEXT("with nothing left out:\n%s"), *Report.ToString()), Report.Residue.IsEmpty());
	TestTrue(*FString::Printf(TEXT("and the rebuilt tree equal to the one it came from:\n%s"), *Report.ToString()), Report.Mismatches.IsEmpty());

	FString Text;
	FFileHelper::LoadFileToString(Text, *Target);
	TestTrue(TEXT("the file was written"), !Text.IsEmpty());
	TestTrue(TEXT("with the binding as its line"), Text.Contains(TEXT("Text <- GetTitleText()")));
	TestTrue(TEXT("the values"), Text.Contains(TEXT("FontSize = 28")) && Text.Contains(TEXT("\"Hello\"")));
	TestTrue(TEXT("the behaviour"), Text.Contains(TEXT("+ UIButton")));
	TestTrue(TEXT("and the animation the asset keeps"), Text.Contains(TEXT("timeline Pulse external")));

	TestTrue(TEXT("the class builds from the file now"), DreamUITextAuthoring::IsTextAuthored(Blueprint));
	TestTrue(TEXT("a backup was made beside it"), FindObject<UDreamWidgetBlueprint>(nullptr, *Report.BackupPath) != nullptr
		&& Report.BackupPath.Contains(TEXT("BP_ConverterRoundTrip_PreDui")));
	FCompilerResultsLog Results;
	FKismetEditorUtilities::CompileBlueprint(Blueprint, EBlueprintCompileOptions::SkipGarbageCollection, &Results);
	TestEqual(TEXT("and compiles clean"), Results.NumErrors, 0);
	UDreamWidget* Title = Scoped.FindTemplate(TEXT("Title"));
	if (TestTrue(TEXT("the rebuilt tree has the text"), Title != nullptr && Title->GetVisual() != nullptr))
	{
		TestNotNull(TEXT("with its binding, read back from the file"), DreamWidgetPropertyBindingExtension::FindBinding(Blueprint,
			DreamWidgetPropertyBindingExtension::ResolveBindingSite(Title->GetVisual()), TEXT("Text")));
	}
	const UDreamWidgetAnimationComponent* NewAnimator = IsValid(Blueprint->WidgetTree) && IsValid(Blueprint->WidgetTree->RootWidget)
		? Blueprint->WidgetTree->RootWidget->GetComponent<UDreamWidgetAnimationComponent>() : nullptr;
	TestTrue(TEXT("the animation came across"), NewAnimator != nullptr && NewAnimator->GetSequenceArray().ContainsByPredicate(
		[](const UDreamWidgetAnimation* InAnimation) { return InAnimation != nullptr && InAnimation->GetDisplayNameString() == TEXT("Pulse"); }));

	// One edit in the designer after converting writes one line: the file says everything already.
	UDreamWidget* Caption = Scoped.FindTemplate(TEXT("Caption"));
	FProperty* FontSize = Caption != nullptr && Caption->GetVisual() != nullptr ? Caption->GetVisual()->GetClass()->FindPropertyByName(TEXT("FontSize")) : nullptr;
	if (TestNotNull(TEXT("the caption has a font size"), FontSize))
	{
		FontSize->ImportText_Direct(TEXT("19"), FontSize->ContainerPtrToValuePtr<void>(Caption->GetVisual()), Caption->GetVisual(), PPF_None);
		TestTrue(TEXT("the edit is written"), DreamUITextAuthoring::WriteStructureToText(Blueprint));
		FString After;
		FFileHelper::LoadFileToString(After, *Target);
		const TArray<FString> Before = SplitLines(Text);
		const TArray<FString> Now = SplitLines(After);
		int32 Added = 0;
		for (const FString& Line : Now)
		{
			Added += Before.Contains(Line) ? 0 : 1;
		}
		TestEqual(*FString::Printf(TEXT("as exactly one new line:\n%s"), *After), Added, 1);
		TestTrue(TEXT("the font size"), After.Contains(TEXT("FontSize = 19")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIConverterShorthandsTest,
	"DreamGUI.Designer.Text.AConvertedBlueprintWritesItsTagsSizesAndItemsInTheShortSpellings",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIConverterShorthandsTest::RunTest(const FString&)
{
	using namespace DreamUIConverterTestLocal;

	FScopedDuiFile Source(TEXT("ConverterShorthands.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), Source.Write({
		TEXT("Widget Root {"),
		TEXT("    + VerticalBox { }"),
		TEXT("    Widget Trim {"),
		TEXT("        @tag Trim, UI.Glow"),
		TEXT("        @slot Size = (120, 48)"),
		TEXT("    }"),
		TEXT("    Native.RingMenu Wheel {"),
		TEXT("        Items = [{ \"Lamp\", Tag: Lamp }, { \"Door\", Tag: Door, bEnabled: false }]"),
		TEXT("    }"),
		TEXT("}")
	})))
	{
		return false;
	}
	FScopedTextBlueprint Scoped(TEXT("BP_ConverterShorthands"), Source.FilePath, /*bInOpenDesigner*/false,
		UDreamTextUserWidgetBindingBase::StaticClass());
	if (!TestNotNull(TEXT("the Blueprint was created"), Scoped.Blueprint)) return false;
	if (!TestEqual(TEXT("and its .dui compiled clean"), Scoped.CompileErrors, 0)) return false;
	if (!TestTrue(TEXT("detached, the Blueprint authors its own tree"), Detach(Scoped.Blueprint))) return false;

	const FString Target = TargetPath(TEXT("ConverterShorthands.dui"));
	FScopedFileCleanup Cleanup{ Target };
	FDreamUIConversionReport Report;
	if (!TestTrue(*FString::Printf(TEXT("the Blueprint converts:\n%s"), *Report.ToString()),
		FDreamUIBlueprintConverter::Convert(Scoped.Blueprint, Target, /*bInAllowLoss*/false, Report)))
	{
		return false;
	}
	TestTrue(*FString::Printf(TEXT("with nothing left out:\n%s"), *Report.ToString()), Report.Residue.IsEmpty());
	TestTrue(*FString::Printf(TEXT("and the rebuilt tree equal to the one it came from:\n%s"), *Report.ToString()), Report.Mismatches.IsEmpty());

	FString Text;
	FFileHelper::LoadFileToString(Text, *Target);
	TestTrue(*FString::Printf(TEXT("the tags as a tag line:\n%s"), *Text), Text.Contains(TEXT("@tag Trim, UI.Glow")) && !Text.Contains(TEXT("Tags =")));
	TestTrue(TEXT("the fixed size as '@slot Size'"), Text.Contains(TEXT("@slot Size = (120, 48)")) && !Text.Contains(TEXT("DesiredSize")));
	TestTrue(TEXT("the items as maps"), Text.Contains(TEXT("Items = [{ \"Lamp\", Tag: Lamp }, { \"Door\", Tag: Door, bEnabled: false }]")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUIConverterRefusalTest,
	"DreamGUI.Designer.Text.AConversionIsRefusedOrReparentsAndSaysWhatItLoses",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUIConverterRefusalTest::RunTest(const FString&)
{
	using namespace DreamUIConverterTestLocal;

	// ---- a parent class that is neither UDreamUserWidget nor a text user widget
	{
		FScopedTextBlueprint Custom(TEXT("BP_ConverterCustomParent"), FString(), /*bInOpenDesigner*/false, UDreamUserWidgetBindFixture::StaticClass());
		if (TestNotNull(TEXT("a Blueprint of a custom C++ class was made"), Custom.Blueprint))
		{
			Custom.Blueprint->GetOrCreateWidgetTree(/*bEnsureRootWidget*/true);
			FDreamUIConversionReport Report;
			TestFalse(TEXT("it is refused"), FDreamUIBlueprintConverter::Preflight(Custom.Blueprint, TargetPath(TEXT("CustomParent.dui")), Report));
			TestTrue(*FString::Printf(TEXT("naming the class (%s)"), *Report.ToString()), Report.ToString().Contains(TEXT("UDreamTextUserWidget")));
			TestFalse(TEXT("and the Content Browser does not offer it"), FDreamUIBlueprintConverter::IsConvertible(Custom.Blueprint));
		}
	}

	// ---- UDreamUserWidget: reparented, and a value with no spelling is the loss the author has to allow
	FScopedTextBlueprint Plain(TEXT("BP_ConverterPlain"), FString(), /*bInOpenDesigner*/false, UDreamUserWidget::StaticClass());
	if (!TestNotNull(TEXT("a plain DreamUI Blueprint was made"), Plain.Blueprint)) return false;
	UDreamWidgetTree* Tree = Plain.Blueprint->GetOrCreateWidgetTree(/*bEnsureRootWidget*/true);
	if (!TestTrue(TEXT("with a root"), Tree != nullptr && Tree->RootWidget != nullptr)) return false;
	TestTrue(TEXT("it is offered for conversion"), FDreamUIBlueprintConverter::IsConvertible(Plain.Blueprint));
	FFloatProperty* Opacity = CastField<FFloatProperty>(Tree->RootWidget->GetClass()->FindPropertyByName(TEXT("RenderOpacity")));
	if (!TestNotNull(TEXT("the root has an opacity"), Opacity)) return false;
	Opacity->SetPropertyValue_InContainer(Tree->RootWidget, std::numeric_limits<float>::quiet_NaN());

	const FString Target = TargetPath(TEXT("ConverterPlain.dui"));
	FScopedFileCleanup Cleanup{ Target };
	{
		FDreamUIConversionReport Report;
		TestFalse(TEXT("a loss the author did not allow stops it"), FDreamUIBlueprintConverter::Convert(Plain.Blueprint, Target, /*bInAllowLoss*/false, Report));
		TestTrue(*FString::Printf(TEXT("and the report names it (%s)"), *Report.ToString()),
			Report.Residue.ContainsByPredicate([](const FString& InEntry) { return InEntry.Contains(TEXT("RenderOpacity")); }));
		TestFalse(TEXT("nothing was written"), FPaths::FileExists(Target));
		TestTrue(TEXT("nor reparented"), Plain.Blueprint->ParentClass == UDreamUserWidget::StaticClass());
	}
	{
		FDreamUIConversionReport Report;
		TestTrue(*FString::Printf(TEXT("allowed, it converts (%s)"), *Report.ToString()), FDreamUIBlueprintConverter::Convert(Plain.Blueprint, Target, /*bInAllowLoss*/true, Report));
		TestTrue(TEXT("the file was written"), FPaths::FileExists(Target));
		TestTrue(TEXT("the class is a text user widget now"), Plain.Blueprint->ParentClass == UDreamTextUserWidget::StaticClass());
		TestTrue(TEXT("built from the file"), DreamUITextAuthoring::IsTextAuthored(Plain.Blueprint));
		TestFalse(TEXT("and no longer offered for conversion"), FDreamUIBlueprintConverter::IsConvertible(Plain.Blueprint));
	}

	// ---- a file that is there already is never written over
	{
		FScopedTextBlueprint Other(TEXT("BP_ConverterExisting"), FString(), /*bInOpenDesigner*/false, UDreamUserWidget::StaticClass());
		if (TestNotNull(TEXT("another plain Blueprint was made"), Other.Blueprint))
		{
			Other.Blueprint->GetOrCreateWidgetTree(/*bEnsureRootWidget*/true);
			FString Before;
			FFileHelper::LoadFileToString(Before, *Target);
			FDreamUIConversionReport Report;
			TestFalse(TEXT("converting into an existing file is refused"), FDreamUIBlueprintConverter::Convert(Other.Blueprint, Target, true, Report));
			FString After;
			FFileHelper::LoadFileToString(After, *Target);
			TestEqual(TEXT("and the file is as it was"), After, Before);
		}
	}
	return true;
}

#endif
