// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

#include "Controls/DreamButton.h"
#include "Controls/DreamRichTextBlock.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIGeometry.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamEventSystem.h"
#include "Event/DreamPointerEventData.h"
#include "InputCoreTypes.h"
#include "Interaction/UITextHyperlink.h"

#include "Driver/DreamDriver.h"
#include "Driver/DreamDriverElement.h"
#include "Driver/DreamDriverLocators.h"
#include "Driver/DreamDriverProjection.h"
#include "Driver/DreamDriverRig.h"
#include "Driver/DreamDriverSequence.h"
#include "Driver/DreamDriverTypes.h"
#include "Interaction/DreamPressInteractionTestTypes.h"

/*
 * A LINK IN RICH TEXT, DISABLED AND ON A SCALED CANVAS.
 *
 * UMG's hyperlink decorator draws a link as an SRichTextHyperlink, an SHyperlink, which is an SButton
 * (Slate/Public/Widgets/Input/SHyperlink.h): clicked, tapped or confirmed with the pad, it raises OnNavigate once. Inside
 * a disabled rich text block it is a disabled button: Slate's hit test cuts a click's or a finger's path at the first
 * disabled widget (FHittestGrid::GetBubblePath, SlateCore/Private/Input/HittestGrid.cpp) and the D-pad passes over it
 * (FHittestGrid::FindNextFocusableWidget skips a widget that is not enabled). And since an SButton is focusable by default
 * (SButton's IsFocusable argument), the D-pad stops on a link like on any button and the pad's confirm follows it.
 *
 * On a canvas scaled to half and to double, the click and the tap land on the link where its characters are drawn, and the
 * D-pad finds it below a button by where it is drawn.
 *
 * The paragraph is set in an engine font, rasterized on the spot, so the quads the pointer is aimed at are there from the
 * first frame whatever the project's default font is.
 */
namespace DreamRichTextCoverageTestLocal
{
	const FIntPoint ViewportSize(1280, 720);
	const double Scales[] = { 0.5, 2.0 };

	FDreamRigOptions ScaledOptions(double InScale)
	{
		FDreamRigOptions Options;
		Options.ViewportSize = ViewportSize;
		Options.CanvasScaleMode = EDreamCanvasScaleMode::ScaleWithScreenSize;
		Options.ReferenceResolution = FVector2D(ViewportSize.X / InScale, ViewportSize.Y / InScale);
		Options.MatchFromWidthToHeight = 1.0f;
		return Options;
	}

	FString At(double InScale, const FString& InWhat)
	{
		return FString::Printf(TEXT("[scale %.1f] %s"), InScale, *InWhat);
	}

	bool ComeUp(FAutomationTestBase& InTest, FDreamDriverRig& InRig, double InScale)
	{
		InRig.BindTest(&InTest);
		return InTest.TestTrue(At(InScale, TEXT("The rig came up")), InRig.IsUsable())
			&& InTest.TestNearlyEqual(*At(InScale, TEXT("The canvas is at the scale asked for")),
				static_cast<double>(InRig.RootCanvas()->GetCanvasScale()), InScale, 1.0e-3);
	}

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

	/** A block reading "a <a=x>link</a> b", announcing into a log, with the pixels of the plain "a" and of the link. */
	struct FLinkedBlock
	{
		UDreamRichTextBlock* Block = nullptr;
		TSharedRef<FLinkLog> Log = MakeShared<FLinkLog>();
		FVector2D OnPlainText = FVector2D::ZeroVector;
		FVector2D OnLink = FVector2D::ZeroVector;
	};

	/**
	 * The block, InSize, at InPosition, its paragraph in Roboto at 32 with a hyperlink behaviour on it. False, having said why,
	 * when any of it could not be made.
	 */
	bool MakeLinkedBlock(FAutomationTestBase& InTest, FDreamDriverRig& InRig, const FVector2D& InPosition, FLinkedBlock& OutBlock)
	{
		UDreamRichTextBlock* Block = InRig.MakeControl<UDreamRichTextBlock>(TEXT("Links"), nullptr, FVector2D(480.0, 80.0), InPosition);
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
		const TOptional<FVector2D> OnPlainText = CharactersPixel(*Paragraph, 0, 0);
		const TOptional<FVector2D> OnLink = CharactersPixel(*Paragraph, Tags[LinkIndex].CharIndexStart, Tags[LinkIndex].CharIndexEnd);
		if (!InTest.TestTrue(TEXT("The plain text and the link are on screen"), OnPlainText.IsSet() && OnLink.IsSet()))
		{
			return false;
		}
		OutBlock.Block = Block;
		OutBlock.OnPlainText = OnPlainText.GetValue();
		OutBlock.OnLink = OnLink.GetValue();
		return true;
	}

	/**
	 * How far across from the block's middle its link is drawn, in canvas units at InScale. The block's paragraph is
	 * left-aligned, UMG's RichTextBlock default, so the link stands well to the left of the middle; a button above or below
	 * the block is put over or under the link with it, as Slate's D-pad reaches only what crosses the focused widget's own
	 * width swept the way it goes (FHittestGrid::FindFocusableWidget, SlateCore/Private/Input/HittestGrid.cpp:430).
	 */
	double LinkAcross(FDreamDriverRig& InRig, const FLinkedBlock& InBlock, double InScale)
	{
		const TOptional<FVector2D> BlockCentre = InRig.Driver()->Find(FDreamBy::Widget(InBlock.Block))->GetCentrePixel();
		return BlockCentre.IsSet() ? (InBlock.OnLink.X - BlockCentre->X) / InScale : 0.0;
	}

	bool IsPartOf(const UDreamWidget* InWidget, const UDreamWidget* InControl)
	{
		return InWidget != nullptr && InControl != nullptr && (InWidget == InControl || InWidget->IsChildOf(InControl));
	}

	UDreamWidget* FocusOf(const FDreamDriverRig& InRig)
	{
		const UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return Services != nullptr ? Services->GetFocusedWidget(0) : nullptr;
	}

	bool FocusOn(FAutomationTestBase& InTest, FDreamDriverRig& InRig, UDreamButton* InButton, const FString& InWhat)
	{
		UDreamUIInputServices* Services = UDreamUIInputServices::Get(InRig.GetWorld());
		return InTest.TestTrue(InWhat, Services != nullptr && InButton != nullptr && Services->FocusForNavigation(InButton->FaceNode, 0));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageRichTextDisabledLinkTest,
	"DreamGUI.RichText.ALinkInADisabledBlockAnnouncesNothingForAClickOrATapUntilTheBlockIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageRichTextDisabledLinkTest, "DreamGUI.RichText.ALinkInADisabledBlockAnnouncesNothingForAClickOrATapUntilTheBlockIsEnabled", "[Pointer][Touch][Disabled]")

/*
 * The block disabled: a click on the link and a finger's tap on it announce nothing. Enabled again, the click announces the
 * link once and the tap once more.
 */
bool FDreamCoverageRichTextDisabledLinkTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	FLinkedBlock Linked;
	if (!MakeLinkedBlock(*this, Rig, FVector2D::ZeroVector, Linked))
	{
		return false;
	}
	if (UDreamEventSystem* EventSystem = Rig.EventSystem())
	{
		EventSystem->SetDoubleClickTime(0.0f);
	}
	Linked.Block->SetIsEnabled(false);
	Rig.PumpFrames(1);

	TestTrue(TEXT("Clicking the link of the disabled block completes"),
		Rig.Driver()->Sequence().MoveToPixel(Linked.OnLink).Press().Release().Perform());
	TestEqual(TEXT("The click announced nothing"), Linked.Log->Ids.Num(), 0);
	TestTrue(TEXT("A finger tapped on the link completes"), Rig.Driver()->Sequence().TouchDown(0, Linked.OnLink).TouchUp(0).Perform());
	TestEqual(TEXT("The tap announced nothing either"), Linked.Log->Ids.Num(), 0);

	Linked.Block->SetIsEnabled(true);
	Rig.PumpFrames(1);
	TestTrue(TEXT("Clicking the link once the block is enabled completes"),
		Rig.Driver()->Sequence().MoveToPixel(Linked.OnLink).Press().Release().Perform());
	if (TestEqual(TEXT("Enabled, the click announced the link once"), Linked.Log->Ids.Num(), 1))
	{
		TestEqual(TEXT("...by its id"), Linked.Log->Ids[0], FName(TEXT("x")));
	}
	TestTrue(TEXT("A finger tapped on the link completes"), Rig.Driver()->Sequence().TouchDown(1, Linked.OnLink).TouchUp(1).Perform());
	TestEqual(TEXT("...and the tap announced it once more"), Linked.Log->Ids.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageRichTextScaledLinkTest,
	"DreamGUI.RichText.OnACanvasScaledToHalfAndToDoubleAClickAndATapOnALinkAnnounceItAndOnThePlainTextNothing",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageRichTextScaledLinkTest, "DreamGUI.RichText.OnACanvasScaledToHalfAndToDoubleAClickAndATapOnALinkAnnounceItAndOnThePlainTextNothing", "[Pointer][Touch][Scaled]")

/*
 * At half and at double: a click on the plain "a" before the link announces nothing, a click on the link announces it, and
 * a finger tapped on the plain text and then on the link does the same -- the link is where its characters are drawn.
 */
bool FDreamCoverageRichTextScaledLinkTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextCoverageTestLocal;
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		FLinkedBlock Linked;
		if (!MakeLinkedBlock(*this, Rig, FVector2D::ZeroVector, Linked))
		{
			return false;
		}
		if (UDreamEventSystem* EventSystem = Rig.EventSystem())
		{
			EventSystem->SetDoubleClickTime(0.0f);
		}

		TestTrue(At(Scale, TEXT("Clicking the plain text completes")),
			Rig.Driver()->Sequence().MoveToPixel(Linked.OnPlainText).Press().Release().Perform());
		TestEqual(*At(Scale, TEXT("A click on the plain text announces no link")), Linked.Log->Ids.Num(), 0);
		TestTrue(At(Scale, TEXT("Clicking the link completes")),
			Rig.Driver()->Sequence().MoveToPixel(Linked.OnLink).Press().Release().Perform());
		if (TestEqual(*At(Scale, TEXT("A click on the link announces it once")), Linked.Log->Ids.Num(), 1))
		{
			TestEqual(*At(Scale, TEXT("...by its id")), Linked.Log->Ids[0], FName(TEXT("x")));
		}

		TestTrue(At(Scale, TEXT("A finger tapped on the plain text completes")),
			Rig.Driver()->Sequence().TouchDown(0, Linked.OnPlainText).TouchUp(0).Perform());
		TestEqual(*At(Scale, TEXT("A tap on the plain text announces no link")), Linked.Log->Ids.Num(), 1);
		TestTrue(At(Scale, TEXT("A finger tapped on the link completes")),
			Rig.Driver()->Sequence().TouchDown(1, Linked.OnLink).TouchUp(1).Perform());
		TestEqual(*At(Scale, TEXT("A tap on the link announces it once more")), Linked.Log->Ids.Num(), 2);
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageRichTextDisabledPadTest,
	"DreamGUI.RichText.TheDPadPassesOverTheLinkOfADisabledBlockAndStopsOnItOnceTheBlockIsEnabled",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageRichTextDisabledPadTest, "DreamGUI.RichText.TheDPadPassesOverTheLinkOfADisabledBlockAndStopsOnItOnceTheBlockIsEnabled", "[Nav][Disabled]")

/*
 * A button above the block's link and one below it. The block disabled, the D-pad down from the upper button passes its link over
 * and lands on the lower button, whose click the confirm makes; no link is announced. Enabled again, the D-pad down from the
 * upper button stops on the link -- a focusable button, as SHyperlink is -- and the confirm announces it.
 */
bool FDreamCoverageRichTextDisabledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextCoverageTestLocal;
	FDreamDriverRig Rig = FDreamDriverRig::Headless(ViewportSize);
	Rig.BindTest(this);
	if (!TestTrue(TEXT("The rig came up"), Rig.IsUsable()))
	{
		return false;
	}
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
	UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 120.0));
	FLinkedBlock Linked;
	if (Above == nullptr || !MakeLinkedBlock(*this, Rig, FVector2D::ZeroVector, Linked))
	{
		return false;
	}
	const double LinkX = LinkAcross(Rig, Linked, 1.0);
	Above->SetAnchoredPosition(FVector2D(LinkX, 120.0));
	UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(LinkX, -120.0));
	if (!TestNotNull(TEXT("A button below the block came up"), Below))
	{
		return false;
	}
	Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
	Linked.Block->SetIsEnabled(false);
	Rig.PumpFrames(1);

	if (!FocusOn(*this, Rig, Above, TEXT("Player 0's focus can be put on the upper button")))
	{
		return false;
	}
	TestTrue(TEXT("The D-pad down and the pad's confirm complete"),
		Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	TestEqual(TEXT("The D-pad passed the disabled block's link over: the confirm clicked the lower button"), BelowListener->ClickedCount, 1);
	TestEqual(TEXT("...and announced no link"), Linked.Log->Ids.Num(), 0);

	Linked.Block->SetIsEnabled(true);
	Rig.PumpFrames(1);
	if (!FocusOn(*this, Rig, Above, TEXT("Player 0's focus can be put back on the upper button")))
	{
		return false;
	}
	TestTrue(TEXT("The D-pad down completes"), Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).Perform());
	TestFalse(TEXT("Enabled, the D-pad stopped on the link rather than going on to the lower button"), IsPartOf(FocusOf(Rig), Below));
	TestTrue(TEXT("Pressing and releasing the pad's confirm completes"),
		Rig.Driver()->Sequence().NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
	if (TestEqual(TEXT("The confirm announced the link once"), Linked.Log->Ids.Num(), 1))
	{
		TestEqual(TEXT("...by its id"), Linked.Log->Ids[0], FName(TEXT("x")));
	}
	TestEqual(TEXT("...and did not click the lower button again"), BelowListener->ClickedCount, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamCoverageRichTextScaledPadTest,
	"DreamGUI.RichText.OnACanvasScaledToHalfAndToDoubleTheDPadStopsOnTheLinkBelowAButtonAndConfirmAnnouncesIt",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)
REGISTER_SIMPLE_AUTOMATION_TEST_TAGS(FDreamCoverageRichTextScaledPadTest, "DreamGUI.RichText.OnACanvasScaledToHalfAndToDoubleTheDPadStopsOnTheLinkBelowAButtonAndConfirmAnnouncesIt", "[Nav][Scaled]")

/*
 * The same layout at half and at double, the block enabled, the buttons over and under its link: the D-pad down from the
 * upper button stops on the link, nearer than the lower button as drawn, and the confirm announces it.
 */
bool FDreamCoverageRichTextScaledPadTest::RunTest(const FString& Parameters)
{
	using namespace DreamRichTextCoverageTestLocal;
	UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(MAX_int32);
	ON_SCOPE_EXIT { UDreamUIFontData_FreeTypeRender::SetAsyncGlyphSyncBudgetOverride(-1); };
	for (const double Scale : Scales)
	{
		FDreamDriverRig Rig = FDreamDriverRig::Headless(ScaledOptions(Scale));
		if (!ComeUp(*this, Rig, Scale))
		{
			return false;
		}
		TStrongObjectPtr<UDreamPressInteractionListener> BelowListener(NewObject<UDreamPressInteractionListener>());
		UDreamButton* Above = Rig.MakeControl<UDreamButton>(TEXT("Above"), nullptr, FVector2D(160.0, 40.0), FVector2D(0.0, 120.0));
		FLinkedBlock Linked;
		if (Above == nullptr || !MakeLinkedBlock(*this, Rig, FVector2D::ZeroVector, Linked))
		{
			return false;
		}
		const double LinkX = LinkAcross(Rig, Linked, Scale);
		Above->SetAnchoredPosition(FVector2D(LinkX, 120.0));
		UDreamButton* Below = Rig.MakeControl<UDreamButton>(TEXT("Below"), nullptr, FVector2D(160.0, 40.0), FVector2D(LinkX, -120.0));
		if (!TestNotNull(*At(Scale, TEXT("A button below the block came up")), Below))
		{
			return false;
		}
		Below->OnClicked.AddDynamic(BelowListener.Get(), &UDreamPressInteractionListener::HandleClicked);
		Rig.PumpFrames(1);
		if (!FocusOn(*this, Rig, Above, At(Scale, TEXT("Player 0's focus can be put on the upper button"))))
		{
			return false;
		}

		TestTrue(At(Scale, TEXT("The D-pad down and the pad's confirm complete")),
			Rig.Driver()->Sequence().Key(EKeys::Gamepad_DPad_Down).WaitFrames(1).NavigationTrigger(true).NavigationTrigger(false).WaitFrames(1).Perform());
		if (TestEqual(*At(Scale, TEXT("The confirm on the link the D-pad stopped on announced it once")), Linked.Log->Ids.Num(), 1))
		{
			TestEqual(*At(Scale, TEXT("...by its id")), Linked.Log->Ids[0], FName(TEXT("x")));
		}
		TestEqual(*At(Scale, TEXT("...and the lower button was not reached")), BelowListener->ClickedCount, 0);
	}
	return true;
}

#endif
