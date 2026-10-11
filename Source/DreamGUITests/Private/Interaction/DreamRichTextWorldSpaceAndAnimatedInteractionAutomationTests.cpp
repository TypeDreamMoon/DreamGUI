// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamRichTextBlock.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIGeometry.h"
#include "Event/DreamEventSystem.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextHyperlink.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Driver/DreamDriverWorldSpace.h"
#include "Interaction/DreamPressInteractionTestTypes.h"
#include "Interaction/DreamTextNavInfraWorldInteractionTestHelpers.h"

/*
 * A LINK IN RICH TEXT IN THE WORLD, AND THE PAD ON A LINK AS SHIPPED.
 *
 * UMG's hyperlink decorator draws a link as an SRichTextHyperlink, an SHyperlink, which is an SButton
 * (Slate/Public/Widgets/Input/SHyperlink.h): clicked, tapped or confirmed with the pad, it raises OnNavigate once, and a
 * press on the plain text beside it raises nothing. In a widget component the press comes through the interaction's ray
 * and lands on the glyphs it hits, as on the screen. SButton is focusable by default (SButton's IsFocusable), so the D-pad
 * stops on a link like on any button; DreamGUI gives every drawn link a stop of its own in a game (UUITextHyperlink::
 * bLinksAreFocusStops), which on a panel is one of the world-space widgets its directional scan compares.
 *
 * The paragraph is set in an engine font, rasterized on the spot, so the quads the pointer is aimed at are there from the
 * first frame whatever the project's default font is.
 */
namespace DreamRichTextWorldTestLocal
{
	using namespace DreamTextNavInfraWorldTest;

	/** The pixel the camera sees at the middle of the quads of visible characters InFirst to InLast of a text, or unset. */
	TOptional<FVector2D> CharactersPixel(const FDreamDriverRig& InRig, const UDreamText& InText, int32 InFirst, int32 InLast)
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
		return LocalPointPixel(InRig, InText.GetWidget(), Bounds.GetCenter());
	}

	struct FLinkLog
	{
		TArray<FName> Ids;
	};

	/** A block reading "a <a=x>link</a> b", announcing into a log, with the pixels of the plain "a" and of the link. */
	struct FLinkedBlock
	{
		UDreamRichTextBlock* Block = nullptr;
		UUITextHyperlink* Hyperlink = nullptr;
		int32 LinkIndex = INDEX_NONE;
		TSharedRef<FLinkLog> Log = MakeShared<FLinkLog>();
		FVector2D OnPlainText = FVector2D::ZeroVector;
		FVector2D OnLink = FVector2D::ZeroVector;
	};

	/**
	 * The block, 480 by 80, under InParent (the rig's root when null), its paragraph in Roboto at 32 with a hyperlink
	 * behaviour on it. False, having said why, when any of it could not be made.
	 */
	bool MakeLinkedBlock(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamWidget* InParent, FLinkedBlock& OutBlock)
	{
		UDreamRichTextBlock* Block = InRig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), InParent, FVector2D(480.0, 80.0));
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
		UUITextHyperlink* Hyperlink = Block->TextNode->AddComponent<UUITextHyperlink>();
		if (!InTest.TestNotNull(TEXT("The paragraph took a hyperlink behaviour"), Hyperlink))
		{
			return false;
		}
		TSharedRef<FLinkLog> Log = OutBlock.Log;
		Paragraph->OnHyperlinkClickedCPP.AddLambda([Log](FName InId)
		{
			Log->Ids.Add(InId);
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
		const TOptional<FVector2D> OnPlainText = CharactersPixel(InRig, *Paragraph, 0, 0);
		const TOptional<FVector2D> OnLink = CharactersPixel(InRig, *Paragraph, Tags[LinkIndex].CharIndexStart, Tags[LinkIndex].CharIndexEnd);
		if (!InTest.TestTrue(TEXT("The plain text and the link are seen"), OnPlainText.IsSet() && OnLink.IsSet()))
		{
			return false;
		}
		OutBlock.Block = Block;
		OutBlock.Hyperlink = Hyperlink;
		OutBlock.LinkIndex = LinkIndex;
		OutBlock.OnPlainText = OnPlainText.GetValue();
		OutBlock.OnLink = OnLink.GetValue();
		return true;
	}

	/**
	 * How far across from the block's middle its link is, in the units of the block's parent: the pixels between them over
	 * the pixels a unit is seen as. The paragraph is left-aligned, UMG's RichTextBlock default, so the link stands well to the
	 * left of the middle; the buttons over and under the block are put over and under the link with it.
	 */
	double LinkAcross(const FDreamDriverRig& InRig, const FLinkedBlock& InBlock)
	{
		const TOptional<FBox2D> BlockRect = RectOf(InRig, InBlock.Block);
		if (!BlockRect.IsSet() || InBlock.Block->GetWidth() <= 0.0f)
		{
			return 0.0;
		}
		const double PixelsPerUnit = BlockRect->GetSize().X / InBlock.Block->GetWidth();
		return PixelsPerUnit > 0.0 ? (InBlock.OnLink.X - BlockRect->GetCenter().X) / PixelsPerUnit : 0.0;
	}

	/**
	 * A button over the block's link and one under it, under InParent (the rig's root when null). With the focus on the upper
	 * button, the D-pad down stops on the link -- nearer than the lower button -- and the pad's confirm announces it once; the
	 * lower button is never clicked.
	 */
	bool RunPadOntoLink(FAutomationTestBase& InTest, bool bInOnPanel)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
		Rig.BindTest(&InTest);
		if (!InTest.TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
		{
			return false;
		}
		UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
		ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
		UDreamWidget* Parent = nullptr;
		if (bInOnPanel)
		{
			const FWorldStage Stage = SetUpPanel(InTest, Rig);
			if (!Stage.IsReady())
			{
				return false;
			}
			Parent = Stage.Panel;
		}
		TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), Parent, FVector2D(160.0, 40.0), FVector2D(0.0, 120.0));
		FLinkedBlock Linked;
		if (!InTest.TestNotNull(TEXT("A button above the block came up"), Above) || !MakeLinkedBlock(InTest, Rig, Parent, Linked))
		{
			return false;
		}
		const double LinkX = LinkAcross(Rig, Linked);
		Above->SetAnchoredPosition(FVector2D(LinkX, 120.0));
		UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), Parent, FVector2D(160.0, 40.0), FVector2D(LinkX, -120.0));
		if (!InTest.TestNotNull(TEXT("A button below the block came up"), Below))
		{
			return false;
		}
		Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(2);
		if (!InTest.TestTrue(TEXT("Player 0's focus can be put on the upper button"), PutFocusOn(Rig, Above->FaceNode)))
		{
			return false;
		}

		InTest.TestTrue(TEXT("The D-pad down completes"), PressKey(Rig, EKeys::Gamepad_DPad_Down));
		InTest.TestFalse(TEXT("The D-pad down left the upper button"), FocusIsIn(Rig, Above));
		InTest.TestFalse(TEXT("...and stopped short of the lower button"), FocusIsIn(Rig, Below));
		if (const UDreamWidget* LinkStop = Linked.Hyperlink->GetLinkStop(Linked.LinkIndex))
		{
			InTest.TestTrue(TEXT("...on the link's own stop"), FocusIsIn(Rig, LinkStop));
		}
		InTest.TestTrue(TEXT("Pressing and releasing the pad's confirm completes"), PadConfirm(Rig));
		if (InTest.TestEqual(TEXT("The confirm on the link announced it once"), Linked.Log->Ids.Num(), 1))
		{
			InTest.TestEqual(TEXT("...by its id"), Linked.Log->Ids[0], FName(TEXT("x")));
		}
		InTest.TestEqual(TEXT("...and the lower button was not reached"), BelowListener->ClickedCount, 0);
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRichTextLinkPressTest,
	"DreamGUI.RichText.OnAWorldPanelAClickAndATapOnALinkAnnounceItAndOnThePlainTextNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldRichTextLinkPressTest, "DreamGUI.RichText.OnAWorldPanelAClickAndATapOnALinkAnnounceItAndOnThePlainTextNothing", "[Pointer][Touch][World]")

/*
 * The block on a panel. A click through the world pointer on the plain "a" before the link announces nothing, a click on the
 * link announces it once; a finger tapped on the plain text and then on the link does the same.
 */
bool FDreamWorldRichTextLinkPressTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextWorldTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(RigOptions());
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	const FWorldStage Stage = SetUpPanel(*this, Rig);
	FLinkedBlock Linked;
	if (!Stage.IsReady() || !MakeLinkedBlock(*this, Rig, Stage.Panel, Linked))
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = Rig.EventSystem())
	{
		EventSystem->SetDoubleClickTime(0.0f);
	}

	TestTrue(TEXT("Clicking the plain text on the panel completes"),
		Rig.Driver()->Sequence().MoveToPixel(Linked.OnPlainText).Press().Release().Perform());
	TestEqual(TEXT("A click on the plain text announces no link"), Linked.Log->Ids.Num(), 0);
	TestTrue(TEXT("Clicking the link on the panel completes"),
		Rig.Driver()->Sequence().MoveToPixel(Linked.OnLink).Press().Release().Perform());
	if (TestEqual(TEXT("A click on the link announces it once"), Linked.Log->Ids.Num(), 1))
	{
		TestEqual(TEXT("...by its id"), Linked.Log->Ids[0], FName(TEXT("x")));
	}

	TestTrue(TEXT("A finger tapped on the plain text completes"),
		Rig.Driver()->Sequence().TouchDown(0, Linked.OnPlainText).TouchUp(0).Perform());
	TestEqual(TEXT("A tap on the plain text announces no link"), Linked.Log->Ids.Num(), 1);
	TestTrue(TEXT("A finger tapped on the link completes"),
		Rig.Driver()->Sequence().TouchDown(1, Linked.OnLink).TouchUp(1).Perform());
	TestEqual(TEXT("A tap on the link announces it once more"), Linked.Log->Ids.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorldRichTextPadTest,
	"DreamGUI.RichText.OnAWorldPanelTheDPadStopsOnTheLinkBelowAButtonAndConfirmAnnouncesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamWorldRichTextPadTest, "DreamGUI.RichText.OnAWorldPanelTheDPadStopsOnTheLinkBelowAButtonAndConfirmAnnouncesIt", "[Nav][World]")

/* The link's stop is on the panel with the buttons, and the D-pad's scan there finds it as it finds them. */
bool FDreamWorldRichTextPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextWorldTestLocal;
	return RunPadOntoLink(*this, true);
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAnimatedRichTextPadTest,
	"DreamGUI.RichText.TheDPadStopsOnTheLinkBelowAButtonAndThePadsConfirmAnnouncesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamAnimatedRichTextPadTest, "DreamGUI.RichText.TheDPadStopsOnTheLinkBelowAButtonAndThePadsConfirmAnnouncesIt", "[Nav][Animated]")

/* As shipped on the screen: SHyperlink is a focusable SButton, so the D-pad stops on the link and the accept follows it. */
bool FDreamAnimatedRichTextPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextWorldTestLocal;
	return RunPadOntoLink(*this, false);
}

#endif
