// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DreamControlTestScope.h"

#include "Controls/DreamControlStyles.h"
#include "Controls/DreamRichTextBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The rich text block's knobs, which until now were authored-only.
 *
 * Every property on this control was pushed exactly once, at build, and the control offered a single
 * setter (SetText). So a screen could not change the case policy, the wrap, the alignment or which
 * assets the markup resolves against without rebuilding the widget. The tests below write each one
 * after the block is built and read the PARAGRAPH back, because reading the control's own field
 * would pass with the push deleted.
 *
 * Two of them are about re-parsing rather than about a value. Which tags are acted on, and what they
 * mean, are decisions the parser makes WHILE it parses -- so a setter that only wrote the flag would
 * leave the prose showing what the old rules produced, with the new rules sitting unused beside it.
 */
namespace DreamRichTextParityTestLocal
{
	template<class T>
	T* Author(float InWidth = 320.0f, float InHeight = 120.0f)
	{
		T* Control = NewObject<T>(GetTransientPackage());
		Control->StyleSource = EDreamUIStyleSource::Inline;
		Control->SetWidth(InWidth);
		Control->SetHeight(InHeight);
		return Control;
	}

	UDreamText* ParagraphOf(const UDreamRichTextBlock* InBlock)
	{
		return InBlock != nullptr && InBlock->TextNode != nullptr
			? Cast<UDreamText>(InBlock->TextNode->GetVisual())
			: nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextRuntimeSettersReachTheParagraphTest,
	"DreamGUI.RichText.Parity.EveryAuthoredKnobCanBeWrittenAfterTheBlockIsBuilt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextRuntimeSettersReachTheParagraphTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextParityTestLocal;

	TDreamTestControl<UDreamRichTextBlock> Block(Author<UDreamRichTextBlock>());
	Block->Text = FText::AsCultureInvariant(TEXT("plain prose"));
	Block->Initialize();

	UDreamText* Paragraph = ParagraphOf(Block.Get());
	if (!TestNotNull(TEXT("the block built a paragraph"), Paragraph))
	{
		return false;
	}
	TestTrue(TEXT("and it reads markup, which is what makes it a rich text block"), Paragraph->GetRichText());

	Block->SetHorizontalAlignment(EDreamUITextParagraphHorizontalAlign::Center);
	TestEqual(TEXT("the horizontal alignment reaches the paragraph"),
		(int32)Paragraph->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Center);
	// UMG calls the same decision Justification, and it must not become a second field.
	Block->SetJustification(EDreamUITextParagraphHorizontalAlign::Right);
	TestEqual(TEXT("and UMG's name for it writes the same one"),
		(int32)Paragraph->GetParagraphHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);
	TestEqual(TEXT("which the control agrees with"),
		(int32)Block->GetHorizontalAlignment(), (int32)EDreamUITextParagraphHorizontalAlign::Right);

	Block->SetVerticalAlignment(EDreamUITextParagraphVerticalAlign::Bottom);
	TestEqual(TEXT("the vertical alignment reaches the paragraph"),
		(int32)Paragraph->GetParagraphVerticalAlignment(), (int32)EDreamUITextParagraphVerticalAlign::Bottom);

	Block->SetOverflowType(EDreamUITextOverflowType::Ellipsis);
	TestEqual(TEXT("the overflow type reaches the paragraph"),
		(int32)Paragraph->GetOverflowType(), (int32)EDreamUITextOverflowType::Ellipsis);
	Block->SetTextOverflowPolicy(EDreamUITextOverflowType::Truncate);
	TestEqual(TEXT("and UMG's name for it writes the same one"),
		(int32)Paragraph->GetOverflowType(), (int32)EDreamUITextOverflowType::Truncate);

	Block->SetAutoWrapText(true);
	TestTrue(TEXT("auto wrap reaches the paragraph"), Paragraph->GetAutoWrapText());

	Block->SetTextTransformPolicy(EDreamUITextTransformPolicy::ToUpper);
	TestEqual(TEXT("the case policy reaches the paragraph"),
		(int32)Paragraph->GetTextTransform(), (int32)EDreamUITextTransformPolicy::ToUpper);

	Block->SetMinDesiredWidth(140.0f);
	TestEqual(TEXT("the minimum desired width reaches the paragraph"),
		Paragraph->GetMinDesiredWidth(), 140.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextStyleCarriesTheShadowTest,
	"DreamGUI.RichText.Parity.TheStyleNowCarriesTheOutlineAndDropShadowAndDefaultsToNeither",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextStyleCarriesTheShadowTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextParityTestLocal;

	// A block that states nothing has to look exactly as it did before the style could describe
	// effects at all -- every effect colour ships with alpha zero, which is how "no effect" is said.
	TDreamTestControl<UDreamRichTextBlock> Plain(Author<UDreamRichTextBlock>());
	Plain->Initialize();
	if (UDreamText* Paragraph = ParagraphOf(Plain.Get()))
	{
		TestFalse(TEXT("a block with a default style draws no effects"), Paragraph->GetTextStyle().HasEffects());
	}

	// The drop shadow, which is this library's underlay: UMG's SetDefaultShadowColorAndOpacity and
	// SetDefaultShadowOffset are two fields of one struct here, and the struct is theme.
	TDreamTestControl<UDreamRichTextBlock> Shadowed(Author<UDreamRichTextBlock>());
	Shadowed->Style.TextStyle.UnderlayColor = FColor(0, 0, 0, 255);
	Shadowed->Style.TextStyle.UnderlayOffset = FVector2f(0.1f, 0.1f);
	Shadowed->Initialize();
	if (UDreamText* Paragraph = ParagraphOf(Shadowed.Get()))
	{
		TestEqual(TEXT("the shadow colour reached the paragraph"),
			Paragraph->GetTextStyle().UnderlayColor, FColor(0, 0, 0, 255));
		TestEqual(TEXT("and its offset"),
			Paragraph->GetTextStyle().UnderlayOffset.X, 0.1f);
		TestTrue(TEXT("so the paragraph knows it has something to draw"), Paragraph->GetTextStyle().HasEffects());
	}

	// And a whole style pushed at runtime lands the same way, which is the road SetStyle exists for.
	FDreamRichTextStyle Restyled = Plain->GetStyle();
	Restyled.TextColor = FColor(10, 20, 30, 255);
	Restyled.TextStyle.OutlineColor = FColor(255, 0, 0, 255);
	Restyled.TextStyle.OutlineWidth = 0.05f;
	Plain->SetStyle(Restyled);
	if (UDreamText* Paragraph = ParagraphOf(Plain.Get()))
	{
		TestEqual(TEXT("a restyle reaches the colour"), Paragraph->GetColor(), FColor(10, 20, 30, 255));
		TestEqual(TEXT("and the outline"), Paragraph->GetTextStyle().OutlineColor, FColor(255, 0, 0, 255));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextMarkupRulesReParseTest,
	"DreamGUI.RichText.Parity.ChangingWhatTheMarkupResolvesAgainstRunsTheProseThroughTheParserAgain",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextMarkupRulesReParseTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextParityTestLocal;

	// The half that is easy to get wrong: which tags are acted on, and what they mean, are read
	// WHILE parsing. A setter that wrote the flag and stopped would leave the prose showing what the
	// old rules produced, with the new rules sitting unused beside it.
	TDreamTestControl<UDreamRichTextBlock> Block(Author<UDreamRichTextBlock>());
	Block->Text = FText::AsCultureInvariant(TEXT("<b>bold</b> and plain"));
	Block->Initialize();

	UDreamText* Paragraph = ParagraphOf(Block.Get());
	if (!TestNotNull(TEXT("the block built a paragraph"), Paragraph))
	{
		return false;
	}

	// Every tag acted on is the shipped state; the drawn result of that is layout output and there is
	// no layout here, so what is asserted is the INPUT the parser reads and that the prose survives.
	TestEqual(TEXT("the shipped filter lets every tag through"),
		Paragraph->GetRichTextTagFilterFlags(), Block->GetTagFilterFlags());

	Block->SetTagFilterFlags(0);
	TestEqual(TEXT("the filter reached the paragraph"), Paragraph->GetRichTextTagFilterFlags(), 0);
	// The prose itself never changed, so whatever the filter did, it did through a re-parse.
	TestEqual(TEXT("and the control still holds the prose it was given"),
		Block->GetText().ToString(), FString(TEXT("<b>bold</b> and plain")));

	// The style asset is the other resolve-time input, and it takes the same road.
	TStrongObjectPtr<UDreamUIRichTextCustomStyleData> Styles(
		NewObject<UDreamUIRichTextCustomStyleData>(GetTransientPackage()));
	Block->SetCustomStyleData(Styles.Get());
	TestTrue(TEXT("the style asset reached the paragraph"),
		(UObject*)Paragraph->GetRichTextCustomStyleData() == (UObject*)Styles.Get());
	TestTrue(TEXT("and the control agrees"),
		(UObject*)Block->GetCustomStyleData() == (UObject*)Styles.Get());

	// A refresh is the same re-parse with nothing changed, which is how a style asset edited in
	// place is picked up. It must not disturb the prose.
	Block->RefreshTextLayout();
	TestEqual(TEXT("a refresh leaves the prose alone"),
		Block->GetText().ToString(), FString(TEXT("<b>bold</b> and plain")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextDefaultDynamicMaterialTest,
	"DreamGUI.RichText.Parity.ABlockWithNoMaterialOfItsOwnRefusesToHandOutTheSharedOne",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextDefaultDynamicMaterialTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextParityTestLocal;

	// The case worth pinning, because the tempting implementation is wrong: with no material named,
	// instancing "whatever the paragraph is currently drawn with" would hand back an instance of the
	// library's shared text material -- and a caller editing that edits every paragraph in the
	// project. Null is the honest answer.
	TDreamTestControl<UDreamRichTextBlock> Block(Author<UDreamRichTextBlock>());
	Block->Initialize();
	TestNull(TEXT("a block whose style names no material has none to instance"),
		Block->GetDefaultDynamicMaterial());

	if (UDreamText* Paragraph = ParagraphOf(Block.Get()))
	{
		TestNull(TEXT("and its paragraph is left on the built-in material"), Paragraph->GetOverrideMaterial());
	}
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
