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
#include "Event/DreamEventSystem.h"
#include "Interaction/UITextHyperlink.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"

/*
 * A LINK IN RICH TEXT, CLICKED AND TAPPED.
 *
 * UMG's hyperlink decorator draws a link as an SRichTextHyperlink, an SHyperlink whose button raises OnNavigate when it is
 * clicked (SHyperlink::Hyperlink_OnClicked): a press and a release on the link, once, with the link's own metadata; a click
 * on the text around it is no link's. A finger is the same press and release. Here the link is UUITextHyperlink's, which
 * announces the clicked link's id through UDreamText::OnHyperlinkClicked.
 *
 * The paragraph is set in an engine font, rasterized on the spot, so the quads the pointer is aimed at are there from the
 * first frame whatever the project's default font is.
 */
namespace DreamRichTextLinkClickInteractionTestLocal
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

	/** What a block's paragraph announced, in order. */
	struct FLinkLog
	{
		TArray<FName> Ids;
	};

	/**
	 * A rich text block reading "a <a=x>link</a> b" in Roboto at 32, with a hyperlink behaviour on its paragraph, every id it
	 * announces written to OutLog, and the pixels of the "a" before the link and of the link. False, having said why, when
	 * any of it could not be made.
	 */
	bool MakeLinkedBlock(FAutomationTestBase& InTest, FDreamDriverRig& InRig, TSharedRef<FLinkLog> OutLog, FVector2D& OutOnPlainText, FVector2D& OutOnLink)
	{
		UDreamRichTextBlock* Block = InRig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), nullptr, FVector2D(480.0, 80.0));
		UDreamText* Paragraph = (Block != nullptr && Block->TextNode != nullptr) ? Cast<UDreamText>(Block->TextNode->GetVisual()) : nullptr;
		if (!InTest.TestNotNull(TEXT("A rich text block with a paragraph came up"), Paragraph))
		{
			return false;
		}
		UDreamUIFontData_DistanceField* Font = NewObject<UDreamUIFontData_DistanceField>(InRig.GetWorld());
		Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts"), TEXT("Roboto-Regular.ttf")), false);
		Font->InitFont();
		Paragraph->SetFont(Font);
		Paragraph->SetFontSize(32.0f);
		Block->SetText(FText::FromString(TEXT("a <a=x>link</a> b")));
		if (!InTest.TestNotNull(TEXT("The paragraph took a hyperlink behaviour"), Block->TextNode->AddComponent<UUITextHyperlink>()))
		{
			return false;
		}
		Paragraph->OnHyperlinkClickedCPP.AddLambda([OutLog](FName InId)
		{
			OutLog->Ids.Add(InId);
		});
		InRig.PumpFrames(2);

		const TArray<FDreamUIText_RichTextCustomTag>& Tags = Paragraph->GetRichTextCustomTagArray();
		const int32 LinkIndex = Tags.IndexOfByPredicate([](const FDreamUIText_RichTextCustomTag& Tag)
		{
			return Tag.bHyperlink && Tag.TagName == FName(TEXT("x"));
		});
		if (!InTest.TestTrue(TEXT("The paragraph has the link"), LinkIndex != INDEX_NONE))
		{
			return false;
		}
		const TOptional<FVector2D> OnPlainText = CharactersPixel(*Paragraph, 0, 0);
		const TOptional<FVector2D> OnLink = CharactersPixel(*Paragraph, Tags[LinkIndex].CharIndexStart, Tags[LinkIndex].CharIndexEnd);
		if (!InTest.TestTrue(TEXT("The plain text and the link are on screen"), OnPlainText.IsSet() && OnLink.IsSet()))
		{
			return false;
		}
		OutOnPlainText = OnPlainText.GetValue();
		OutOnLink = OnLink.GetValue();
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextLinkClickAnnouncesTest,
	"DreamGUI.RichText.ClickingALinkAnnouncesItsIdOnceAndAClickOnThePlainTextBesideItAnnouncesNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRichTextLinkClickAnnouncesTest, "DreamGUI.RichText.ClickingALinkAnnouncesItsIdOnceAndAClickOnThePlainTextBesideItAnnouncesNothing", "[Pointer][Animated]")

/*
 * A click on the plain "a" before the link is no link's: nothing is announced. A click on the link announces it once, by
 * the id its markup gave it. A press on the link let go off it is no click (SButton clicks on a release over itself), and
 * announces nothing either.
 */
bool FDreamRichTextLinkClickAnnouncesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextLinkClickInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	TSharedRef<FLinkLog> Log = MakeShared<FLinkLog>();
	FVector2D OnPlainText = FVector2D::ZeroVector;
	FVector2D OnLink = FVector2D::ZeroVector;
	if (!MakeLinkedBlock(*this, Rig, Log, OnPlainText, OnLink))
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = Rig.EventSystem())
	{
		EventSystem->SetDoubleClickTime(0.0f);
	}

	TestTrue(TEXT("Clicking the plain text before the link completes"),
		Rig.Driver()->Sequence().MoveToPixel(OnPlainText).Press().Release().Perform());
	TestEqual(TEXT("A click on the plain text announces no link"), Log->Ids.Num(), 0);

	TestTrue(TEXT("Clicking the link completes"), Rig.Driver()->Sequence().MoveToPixel(OnLink).Press().Release().Perform());
	if (TestEqual(TEXT("A click on the link announces it once"), Log->Ids.Num(), 1))
	{
		TestEqual(TEXT("...by the id its markup gave it"), Log->Ids[0], FName(TEXT("x")));
	}

	TestTrue(TEXT("Pressing on the link and letting go on the plain text completes"),
		Rig.Driver()->Sequence().MoveToPixel(OnLink).Press().MoveToPixel(OnPlainText).Release().Perform());
	TestEqual(TEXT("A press let go off the link is no click of it"), Log->Ids.Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRichTextLinkTapAnnouncesTest,
	"DreamGUI.RichText.AFingerTapOnALinkAnnouncesItsIdOnce",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamRichTextLinkTapAnnouncesTest, "DreamGUI.RichText.AFingerTapOnALinkAnnouncesItsIdOnce", "[Touch][Animated]")

/*
 * A finger landing on the link and lifting there is SHyperlink's press and release -- Slate hands an unanswered touch on as
 * a mouse press -- so it announces the link once. A tap on the plain text announces nothing.
 */
bool FDreamRichTextLinkTapAnnouncesTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextLinkClickInteractionTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	TSharedRef<FLinkLog> Log = MakeShared<FLinkLog>();
	FVector2D OnPlainText = FVector2D::ZeroVector;
	FVector2D OnLink = FVector2D::ZeroVector;
	if (!MakeLinkedBlock(*this, Rig, Log, OnPlainText, OnLink))
	{
		return false;
	}

	TestTrue(TEXT("A tap on the plain text completes"), Rig.Driver()->Sequence().TouchDown(0, OnPlainText).TouchUp(0).Perform());
	TestEqual(TEXT("A tap on the plain text announces no link"), Log->Ids.Num(), 0);
	TestTrue(TEXT("A tap on the link completes"), Rig.Driver()->Sequence().TouchDown(1, OnLink).TouchUp(1).Perform());
	if (TestEqual(TEXT("A tap on the link announces it once"), Log->Ids.Num(), 1))
	{
		TestEqual(TEXT("...by the id its markup gave it"), Log->Ids[0], FName(TEXT("x")));
	}
	return true;
}

#endif
