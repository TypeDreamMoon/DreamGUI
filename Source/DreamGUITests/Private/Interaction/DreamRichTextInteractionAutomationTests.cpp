// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"

#include "Controls/DreamRichTextBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIGeometry.h"
#include "Interaction/UITextHyperlink.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A RICH TEXT BLOCK'S LINKS, POINTED AT THE WAY A PLAYER POINTS.
 *
 * The pointer goes through the rig's input module and raycaster like any other, and what is asserted is what the text
 * would draw: the colour override a link is painted with, read off the paragraph, and whether the paragraph had to be
 * laid out again for it. The paragraph is set in an engine font, rasterized on the spot, so the quads the pointer is
 * aimed at -- and the link hit test reads -- are there from the first frame whatever the project's default font is.
 */
namespace DreamRichTextInteractionTestLocal
{
	const FIntPoint ViewportSize(1280, 720);

	/** The pixel at the middle of the quads of visible characters InFirst to InLast of a text, or unset without any. */
	TOptional<FVector2D> CharactersPixel(const UDreamText& InText, int32 InFirst, int32 InLast)
	{
		const TArray<FDreamUITextCharProperty>& Chars = InText.GetCharPropertyArray();
		const FDreamUIGeometry* Geometry = InText.GetGeometry();
		if (Geometry == nullptr)
		{
			return TOptional<FVector2D>();
		}
		// The quads are in the text widget's own 2D space (Y across, Z up), which WidgetLocalPointToPixel reads.
		FBox2D Bounds(ForceInit);
		for (int32 CharIndex = FMath::Max(0, InFirst); CharIndex <= InLast && Chars.IsValidIndex(CharIndex); CharIndex++)
		{
			const FDreamUITextCharProperty& Char = Chars[CharIndex];
			for (int32 Vertex = Char.StartVertIndex; Vertex < Char.StartVertIndex + Char.VertCount; Vertex++)
			{
				if (Geometry->OriginVertices.IsValidIndex(Vertex))
				{
					const FVector3f& Position = Geometry->OriginVertices[Vertex].Position;
					Bounds += FVector2D(Position.Y, Position.Z);
				}
			}
		}
		if (!Bounds.bIsValid)
		{
			return TOptional<FVector2D>();
		}
		return FDreamDriverProjection::WidgetLocalPointToPixel(InText.GetWidget(), Bounds.GetCenter());
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextLinkHoverAndPressColoursTest,
	"DreamGUI.RichText.ALinkTakesItsHoverAndPressedColoursWithoutALayoutAndGivesThemBack",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRichTextLinkHoverAndPressColoursTest, "DreamGUI.RichText.ALinkTakesItsHoverAndPressedColoursWithoutALayoutAndGivesThemBack", "[Pointer][Animated]")

/*
 * A link in rich text could be clicked and nothing more: UMG's hyperlink decorator styles a link hovered and pressed, and
 * this one looked the same whatever the pointer did. UUITextHyperlink colours the link under the pointer now, through the
 * text's tag colour override, which is applied as the text is painted. Here the pointer comes onto the plain text before
 * the link, moves along onto the link -- which raises no event, since the pointer never leaves the text -- presses and
 * lets go there, and moves away. The link's colour is read off the paragraph at each step, and the paragraph is never
 * laid out again for any of it.
 */
bool FDreamRichTextLinkHoverAndPressColoursTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamRichTextBlock* Block = Rig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), nullptr, FVector2D(480.0, 80.0));
	UDreamText* Paragraph = (Block != nullptr && Block->TextNode != nullptr) ? Cast<UDreamText>(Block->TextNode->GetVisual()) : nullptr;
	if (!TestTrue(TEXT("The rig and the block came up"), Rig.IsUsable() && Paragraph != nullptr))
	{
		return false;
	}

	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(Rig.GetWorld());
	Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), TEXT("Roboto-Regular.ttf")), false);
	Font->InitFont();
	Paragraph->SetFont(Font);
	Paragraph->SetFontSize(32.0f);
	Block->SetText(FText::FromString(TEXT("a <a=x>link</a> b")));
	UUITextHyperlink* Link = Block->TextNode->AddComponent<UUITextHyperlink>();
	if (!TestNotNull(TEXT("The paragraph took a hyperlink behaviour"), Link))
	{
		return false;
	}
	const FColor HoverColor(255, 64, 0, 255);
	const FColor PressedColor(0, 128, 255, 255);
	Link->SetUseHoverColor(true);
	Link->SetHoverColor(HoverColor);
	Link->SetUsePressedColor(true);
	Link->SetPressedColor(PressedColor);
	Rig.PumpFrames(2);

	const TArray<FDreamUIText_RichTextCustomTag>& Tags = Paragraph->GetRichTextCustomTagArray();
	const int32 LinkIndex = Tags.IndexOfByPredicate([](const FDreamUIText_RichTextCustomTag& Tag)
	{
		return Tag.bHyperlink && Tag.TagName == FName(TEXT("x"));
	});
	if (!TestTrue(TEXT("The paragraph has the link"), LinkIndex != INDEX_NONE))
	{
		return false;
	}
	// The first visible character is the "a" before the link.
	const TOptional<FVector2D> OnPlainText = CharactersPixel(*Paragraph, 0, 0);
	const TOptional<FVector2D> OnLink = CharactersPixel(*Paragraph, Tags[LinkIndex].CharIndexStart, Tags[LinkIndex].CharIndexEnd);
	const TOptional<FBox2D> BlockRect = FDreamDriverProjection::WidgetToPixelRect(Block);
	if (!TestTrue(TEXT("The plain text, the link and the block are on screen"), OnPlainText.IsSet() && OnLink.IsSet() && BlockRect.IsSet()))
	{
		return false;
	}
	const FVector2D Away(BlockRect->Max.X + 60.0, BlockRect->GetCenter().Y);

	const int32 Laid = Paragraph->GetCacheTextGeometryData().GetLayoutRunCount();
	FColor Shown = FColor::White;
	TestTrue(TEXT("Moving onto the plain text completes"), Rig.Driver()->Sequence().MoveToPixel(OnPlainText.GetValue()).WaitFrames(1).Perform());
	TestFalse(TEXT("Over the plain text, the link keeps its own colour"), Paragraph->GetTagColorOverride(LinkIndex, Shown));

	TestTrue(TEXT("Moving along onto the link completes"), Rig.Driver()->Sequence().MoveToPixel(OnLink.GetValue()).WaitFrames(1).Perform());
	TestTrue(TEXT("Over the link, it is drawn in the hover colour"), Paragraph->GetTagColorOverride(LinkIndex, Shown) && Shown == HoverColor);

	TestTrue(TEXT("Pressing on the link completes"), Rig.Driver()->Sequence().Press().WaitFrames(1).Perform());
	TestTrue(TEXT("Held down, it is drawn in the pressed colour"), Paragraph->GetTagColorOverride(LinkIndex, Shown) && Shown == PressedColor);
	TestTrue(TEXT("Letting go completes"), Rig.Driver()->Sequence().Release().WaitFrames(1).Perform());
	TestTrue(TEXT("Let go with the pointer still on it, it is back in the hover colour"), Paragraph->GetTagColorOverride(LinkIndex, Shown) && Shown == HoverColor);

	TestTrue(TEXT("Moving away completes"), Rig.Driver()->Sequence().MoveToPixel(Away).WaitFrames(1).Perform());
	TestFalse(TEXT("Away from the text, the link has its own colour back"), Paragraph->GetTagColorOverride(LinkIndex, Shown));
	TestEqual(TEXT("And none of it laid the paragraph out again"), Paragraph->GetCacheTextGeometryData().GetLayoutRunCount(), Laid);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextLinkColourFollowsTheLinkThroughAnEditTest,
	"DreamGUI.RichText.AnEditUnderAStillPointerTakesTheHoverColourOffTheTagThatTookTheLinksIndex",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRichTextLinkColourFollowsTheLinkThroughAnEditTest, "DreamGUI.RichText.AnEditUnderAStillPointerTakesTheHoverColourOffTheTagThatTookTheLinksIndex", "[Pointer][Animated]")

/*
 * A tag colour override names its tag by index, and it outlived a change of the text: an edit that put another tag at the
 * hovered link's index had the painter colour that tag, and the hyperlink, which looked again only when the pointer moved,
 * left it so for as long as the pointer stayed still. Here the pointer rests on the link of "a <a=x>link</a> b", and the
 * text becomes the same words with a plain tag round the "a" -- so the plain tag takes the link's index and the link moves
 * up one. One frame later, with the pointer where it was, the plain tag has no colour of the hyperlink's and the link,
 * still under the pointer at its new index, has the hover colour.
 */
bool FDreamRichTextLinkColourFollowsTheLinkThroughAnEditTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	UDreamRichTextBlock* Block = Rig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), nullptr, FVector2D(480.0, 80.0));
	UDreamText* Paragraph = (Block != nullptr && Block->TextNode != nullptr) ? Cast<UDreamText>(Block->TextNode->GetVisual()) : nullptr;
	if (!TestTrue(TEXT("The rig and the block came up"), Rig.IsUsable() && Paragraph != nullptr))
	{
		return false;
	}

	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(Rig.GetWorld());
	Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), TEXT("Roboto-Regular.ttf")), false);
	Font->InitFont();
	Paragraph->SetFont(Font);
	Paragraph->SetFontSize(32.0f);
	Block->SetText(FText::FromString(TEXT("a <a=x>link</a> b")));
	UUITextHyperlink* Link = Block->TextNode->AddComponent<UUITextHyperlink>();
	if (!TestNotNull(TEXT("The paragraph took a hyperlink behaviour"), Link))
	{
		return false;
	}
	const FColor HoverColor(255, 64, 0, 255);
	Link->SetUseHoverColor(true);
	Link->SetHoverColor(HoverColor);
	Rig.PumpFrames(2);

	auto IndexOfTag = [Paragraph](const TCHAR* InName, bool bInHyperlink)
	{
		return Paragraph->GetRichTextCustomTagArray().IndexOfByPredicate([InName, bInHyperlink](const FDreamUIText_RichTextCustomTag& Tag)
		{
			return Tag.bHyperlink == bInHyperlink && Tag.TagName == FName(InName);
		});
	};
	const int32 LinkIndex = IndexOfTag(TEXT("x"), true);
	if (!TestTrue(TEXT("The paragraph has the link"), LinkIndex != INDEX_NONE))
	{
		return false;
	}
	const FDreamUIText_RichTextCustomTag LinkTag = Paragraph->GetRichTextCustomTagArray()[LinkIndex];
	const TOptional<FVector2D> OnLink = CharactersPixel(*Paragraph, LinkTag.CharIndexStart, LinkTag.CharIndexEnd);
	FColor Shown = FColor::White;
	if (!TestTrue(TEXT("The link is on screen"), OnLink.IsSet())
		|| !TestTrue(TEXT("Moving onto the link completes"), Rig.Driver()->Sequence().MoveToPixel(OnLink.GetValue()).WaitFrames(1).Perform())
		|| !TestTrue(TEXT("Over the link, it is drawn in the hover colour"), Paragraph->GetTagColorOverride(LinkIndex, Shown) && Shown == HoverColor))
	{
		return false;
	}

	// The same words, so the same glyphs under the same pointer, with a plain tag in front of the link.
	Block->SetText(FText::FromString(TEXT("<plain>a</plain> <a=x>link</a> b")));
	TestTrue(TEXT("A frame passes with the pointer where it was"), Rig.Driver()->Sequence().WaitFrames(1).Perform());
	const int32 PlainIndex = IndexOfTag(TEXT("plain"), false);
	const int32 MovedLinkIndex = IndexOfTag(TEXT("x"), true);
	if (!TestEqual(TEXT("The plain tag took the index the link had"), PlainIndex, LinkIndex)
		|| !TestTrue(TEXT("And the link has another"), MovedLinkIndex != INDEX_NONE && MovedLinkIndex != LinkIndex))
	{
		return false;
	}
	TestFalse(TEXT("The plain tag is not drawn in the link's hover colour"), Paragraph->GetTagColorOverride(PlainIndex, Shown));
	TestTrue(TEXT("The link, still under the pointer, is"), Paragraph->GetTagColorOverride(MovedLinkIndex, Shown) && Shown == HoverColor);
	return true;
}

#endif
