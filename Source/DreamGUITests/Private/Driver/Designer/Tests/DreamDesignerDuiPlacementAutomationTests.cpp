// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerDuiFixture.h"
#include "Driver/Designer/Tests/DreamDesignerInputTestSupport.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"

/*
 * Where a designer gesture puts a widget, as the .dui on disk says it afterwards.
 *
 * A widget under a group with no panel is placed by its own anchors, as on a UMG Canvas Panel, so what a move or a drop
 * writes is the anchored position that, under the widget's anchors and pivot, stands it where the pointer let go -- and
 * nothing else: an axis the pointer did not travel along keeps the number the file had, and every other line keeps its
 * bytes. The group is where the widget stays or lands, not the nearest ancestor that happens to have a panel.
 *
 * The file is laid out like a settings panel: a root with an overlay, a fixed-size content group centred in it, rows
 * hung from the group's top edge by anchors.
 */
namespace DreamDesignerDuiPlacementTestLocal
{
	using namespace DreamTests;

	/** The lines that differ between two texts of the same line count, as "before -> after"; a different count is said so. */
	TArray<FString> ChangedLines(const FString& InBefore, const FString& InAfter)
	{
		TArray<FString> Before;
		TArray<FString> After;
		InBefore.ParseIntoArrayLines(Before, /*bCullEmpty*/ false);
		InAfter.ParseIntoArrayLines(After, /*bCullEmpty*/ false);
		TArray<FString> Changed;
		if (Before.Num() != After.Num())
		{
			Changed.Add(FString::Printf(TEXT("%d lines became %d"), Before.Num(), After.Num()));
			return Changed;
		}
		for (int32 Index = 0; Index < Before.Num(); ++Index)
		{
			if (!Before[Index].Equals(After[Index], ESearchCase::CaseSensitive))
			{
				Changed.Add(FString::Printf(TEXT("%s -> %s"), *Before[Index].TrimStart(), *After[Index].TrimStart()));
			}
		}
		return Changed;
	}

	/** Where the widget's pivot is on the inner pixel grid: its projected rect, read the way the widget's own up runs. */
	TOptional<FVector2D> PivotPixel(const FDreamDesignerDriver& InDriver, const UDreamWidget* InPreview)
	{
		const TOptional<FBox2D> Rect = InDriver.WidgetPixelRect(InPreview);
		if (!Rect.IsSet() || !::IsValid(InPreview))
		{
			return TOptional<FVector2D>();
		}
		const FVector2D Pivot = InPreview->GetPivot();
		// Screen Y runs down and a widget's runs up, so the pivot's share of the height is measured from the bottom.
		return FVector2D(FMath::Lerp(Rect->Min.X, Rect->Max.X, Pivot.X), FMath::Lerp(Rect->Max.Y, Rect->Min.Y, Pivot.Y));
	}

	/**
	 * The layer a panel's effect stands in over everything, holding nothing -- the settings panel's pixel sort, its
	 * pointer light: in front of every rect, so the first thing under the pointer wherever it is.
	 */
	TArray<FString> EffectLayerLines()
	{
		return {
			TEXT(""),
			TEXT("    Widget Sort {"),
			TEXT("        @slot HorizontalAlignment = Fill"),
			TEXT("        @slot VerticalAlignment = Fill"),
			TEXT("    }"),
		};
	}

	/**
	 * The content group and its two rows, filling a root whose overlay lays an effect over everything. The header row is
	 * a box holding its title at its left end, so most of it is empty.
	 */
	TArray<FString> SettingsPanelLines()
	{
		TArray<FString> Lines = {
			TEXT("Widget Root {"),
			TEXT("    AnchorData.SizeDelta = (720, 440)"),
			TEXT("    + Overlay {}"),
			TEXT(""),
			TEXT("    Widget Content {"),
			TEXT("        @slot HorizontalAlignment = Fill"),
			TEXT("        @slot VerticalAlignment = Fill"),
			TEXT(""),
			TEXT("        HorizontalBox Header {"),
			TEXT("            AnchorData.AnchorMin = (0, 1)"),
			TEXT("            AnchorData.AnchorMax = (1, 1)"),
			TEXT("            AnchorData.Pivot = (0.5, 1)"),
			TEXT("            AnchorData.AnchoredPosition = (0, -58)"),
			TEXT("            AnchorData.SizeDelta = (-88, 58)"),
			TEXT(""),
			TEXT("            Widget Title {"),
			TEXT("                AnchorData.SizeDelta = (96, 58)"),
			TEXT("                @slot SizeRule = Auto"),
			TEXT("            }"),
			TEXT("        }"),
			TEXT("        Widget Footer {"),
			TEXT("            AnchorData.AnchorMin = (0, 1)"),
			TEXT("            AnchorData.AnchorMax = (1, 1)"),
			TEXT("            AnchorData.Pivot = (0.5, 1)"),
			TEXT("            AnchorData.AnchoredPosition = (0, -366)"),
			TEXT("            AnchorData.SizeDelta = (-88, 46)"),
			TEXT("        }"),
			TEXT("    }"),
		};
		Lines.Append(EffectLayerLines());
		Lines.Add(TEXT("}"));
		return Lines;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerSidewaysDragWritesOnlyXTest,
	"DreamGUI.Designer.Driver.DraggingAnAnchoredRowSidewaysWritesOnlyItsXIntoTheFile",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A row hung from its group's top edge -- stretched across it -- grabbed in the empty part of it right of its title and
 * dragged sixty pixels to the right with the grid on. The row stays in its group: the group has no panel, the nearest
 * ancestor that has one is not where it was dropped, and the effect layer in front of everything holds nothing to drop
 * into. The file changes on one line: the anchored position's X takes the travel and its Y keeps -58, which a grid of
 * ten would otherwise have pulled to -60 although the pointer never moved that way.
 */
bool FDreamDesignerSidewaysDragWritesOnlyXTest::RunTest(const FString&)
{
	using namespace DreamDesignerDuiPlacementTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, 10.0f);
	Preferences.SetGuides(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerSidewaysDragDui"), SettingsPanelLines());
	if (!Scoped.IsReady())
	{
		AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *Scoped.Failure));
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FBox2D WorkArea(ForceInit);
	if (!TestTrue(TEXT("The class is text-authored"), DreamUITextAuthoring::IsTextAuthored(Scoped.Blueprint))
		|| !PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Header = Scoped.FindTemplate(TEXT("Header"));
	UDreamWidget* Content = Scoped.FindTemplate(TEXT("Content"));
	if (!TestNotNull(TEXT("The file's row is in the asset"), Header) || !TestNotNull(TEXT("and so is its group"), Content))
	{
		return false;
	}
	// Half a pixel a unit: the group fills the design canvas, which runs past the viewport at one to one, and the row
	// hangs from its top edge.
	Driver.Toolkit()->SetDesignerPixelsPerUnit(0.5f);
	Driver.PumpFrame();
	SelectOnlyInDesigner(Driver, Header);
	Driver.PumpFrame();
	UDreamWidget* HeaderPreview = Driver.PreviewFor(Header);
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(HeaderPreview);
	const FIntPoint Viewport = Driver.ViewportPixelSize();
	const FBox2D Screen(FVector2D(20.0, 20.0), FVector2D(Viewport.X - 20.0, Viewport.Y - 20.0));
	const FVector2D PixelsPerUnit = PixelsPerUnitFor(Driver, HeaderPreview);
	if (!TestTrue(TEXT("The row is on screen"), Rect.IsSet() && Screen.IsInside(Rect->GetCenter()) && PixelsPerUnit.X > 0.0))
	{
		return false;
	}
	const FString TextBefore = Scoped.ReadFile();
	// Right of the title, three quarters of the way along and halfway down: the row's empty part, the Move handle, away
	// from its edges and its pivot.
	const FIntPoint From = PointInBox(Rect.GetValue(), 0.75, 0.5);
	// Sixty pixels, onto the grid of ten.
	const int32 ExpectedX = FMath::RoundToInt32(FMath::GridSnap(60.0 / PixelsPerUnit.X, 10.0));

	TestTrue(TEXT("The drag completes"), Driver.DragFromTo(From, From + FIntPoint(60, 0), /*Steps*/ 6));
	Driver.PumpFrame();

	Header = Scoped.FindTemplate(TEXT("Header"));
	Content = Scoped.FindTemplate(TEXT("Content"));
	TestTrue(TEXT("The row is still in its group"), Header != nullptr && Header->GetParent() == Content);
	const FString TextAfter = Scoped.ReadFile();
	const TArray<FString> Changed = ChangedLines(TextBefore, TextAfter);
	const bool bOneLine = Changed.Num() == 1 && Changed[0] == FString::Printf(
		TEXT("AnchorData.AnchoredPosition = (0, -58) -> AnchorData.AnchoredPosition = (%d, -58)"), ExpectedX);
	if (!TestTrue(FString::Printf(TEXT("The file changes on the anchored position's line, X to %d and Y kept"), ExpectedX), bOneLine))
	{
		AddInfo(FString::Printf(TEXT("Changed: %s"), *FString::Join(Changed, TEXT(" | "))));
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
	}
	if (Header != nullptr)
	{
		TestTrue(FString::Printf(TEXT("The asset agrees: %s"), *Header->GetAnchoredPosition().ToString()),
			Header->GetAnchoredPosition().Equals(FVector2D(ExpectedX, -58.0), 1e-4));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerPaletteDropIntoGroupTest,
	"DreamGUI.Designer.Driver.AWidgetDroppedOnAGroupLandsInItWhereThePointerLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A Text dragged from the palette and let go over the empty middle of the content group, the grid off. It is the group's
 * child -- not the root overlay's, which would have stood it in its top-left corner -- and the anchored position the file
 * gets puts its pivot, under its own anchors, on the pixel the pointer let go at once the file has been compiled again.
 */
bool FDreamDesignerPaletteDropIntoGroupTest::RunTest(const FString&)
{
	using namespace DreamDesignerDuiPlacementTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(false);
	Preferences.SetGuides(false);
	FScopedDuiDesigner Scoped(TEXT("DesignerPaletteDropDui"), SettingsPanelLines());
	if (!Scoped.IsReady())
	{
		AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *Scoped.Failure));
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Content = Scoped.FindTemplate(TEXT("Content"));
	const TOptional<FBox2D> ContentRect = Content != nullptr ? Driver.WidgetPixelRect(Driver.PreviewFor(Content)) : TOptional<FBox2D>();
	if (!TestTrue(TEXT("The group is on screen"), ContentRect.IsSet()))
	{
		return false;
	}
	// Left of the middle, between the two rows, where nothing but the group is under the pointer.
	const FIntPoint Drop = PointInBox(ContentRect.GetValue(), 0.36, 0.55);
	const FString TextBefore = Scoped.ReadFile();

	TestTrue(TEXT("The Text row is dropped"), Driver.DropBasicFromPalette(UDreamText::StaticClass(), Drop));
	Driver.PumpFrame();
	Driver.PumpFrame();

	Content = Scoped.FindTemplate(TEXT("Content"));
	UDreamWidget* Dropped = nullptr;
	if (Content != nullptr)
	{
		for (UDreamWidget* Child : Content->GetChildren())
		{
			if (::IsValid(Child) && Child->GetDisplayName() != TEXT("Header") && Child->GetDisplayName() != TEXT("Footer"))
			{
				Dropped = Child;
			}
		}
	}
	const FString TextAfter = Scoped.ReadFile();
	if (!TestNotNull(TEXT("The new widget is the group's child"), Dropped))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
		return false;
	}
	const TOptional<FVector2D> Pivot = PivotPixel(Driver, Driver.PreviewFor(Dropped));
	if (!TestTrue(FString::Printf(TEXT("Its pivot is where the pointer let go, (%d, %d): it is at %s"), Drop.X, Drop.Y,
		Pivot.IsSet() ? *Pivot->ToString() : TEXT("nowhere")),
		Pivot.IsSet() && FVector2D::Distance(Pivot.GetValue(), FVector2D(Drop)) <= 1.0))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
	}
	TestFalse(TEXT("A group arranges nothing, so the file gives the new widget no slot line"), TextAfter.Contains(TEXT("@slot HorizontalAlignment = Left")));
	TestTrue(TEXT("and the rows' lines are as they were"),
		TextAfter.Contains(TEXT("            AnchorData.AnchoredPosition = (0, -58)\n"))
		&& TextAfter.Contains(TEXT("            AnchorData.AnchoredPosition = (0, -366)\n")));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDesignerDragIntoAnotherGroupTest,
	"DreamGUI.Designer.Driver.AWidgetDraggedIntoAnotherGroupIsWrittenWhereItWasLetGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Two frames side by side in the content group -- groups, each holding a widget -- a chip in the left one, dragged
 * across onto the right one's empty middle with the grid on. The chip is moved in the file under the right frame, and the
 * position the file gives it there puts it where the pointer let go: the anchored position is measured in the frame it
 * landed in, and it is written with the move rather than lost to the rebuild the move causes.
 */
bool FDreamDesignerDragIntoAnotherGroupTest::RunTest(const FString&)
{
	using namespace DreamDesignerDuiPlacementTestLocal;
	FScopedDesignerPreferences Preferences;
	Preferences.SetGridSnap(true, 10.0f);
	Preferences.SetGuides(false);
	TArray<FString> Lines = {
		TEXT("Widget Root {"),
		TEXT("    AnchorData.SizeDelta = (720, 440)"),
		TEXT("    + Overlay {}"),
		TEXT(""),
		TEXT("    Widget Content {"),
		TEXT("        AnchorData.SizeDelta = (720, 440)"),
		TEXT("        @slot HorizontalAlignment = Center"),
		TEXT("        @slot VerticalAlignment = Center"),
		TEXT(""),
		TEXT("        Widget Left {"),
		TEXT("            AnchorData.AnchoredPosition = (-180, 0)"),
		TEXT("            AnchorData.SizeDelta = (300, 300)"),
		TEXT(""),
		TEXT("            Widget Chip {"),
		TEXT("                AnchorData.SizeDelta = (120, 80)"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("        Widget Right {"),
		TEXT("            AnchorData.AnchoredPosition = (180, 0)"),
		TEXT("            AnchorData.SizeDelta = (300, 300)"),
		TEXT(""),
		TEXT("            Widget Badge {"),
		TEXT("                AnchorData.AnchoredPosition = (0, 110)"),
		TEXT("                AnchorData.SizeDelta = (40, 20)"),
		TEXT("            }"),
		TEXT("        }"),
		TEXT("    }"),
	};
	Lines.Append(EffectLayerLines());
	Lines.Add(TEXT("}"));
	FScopedDuiDesigner Scoped(TEXT("DesignerDragIntoGroupDui"), Lines);
	if (!Scoped.IsReady())
	{
		AddError(FString::Printf(TEXT("No text-authored designer to drive: %s."), *Scoped.Failure));
		return false;
	}
	FDreamDesignerDriver& Driver = *Scoped.Driver;
	FBox2D WorkArea(ForceInit);
	if (!PrepareDesignerOneToOne(*this, Driver, WorkArea))
	{
		return false;
	}
	UDreamWidget* Chip = Scoped.FindTemplate(TEXT("Chip"));
	if (!TestNotNull(TEXT("The chip is in the asset"), Chip))
	{
		return false;
	}
	SelectOnlyInDesigner(Driver, Chip);
	Driver.PumpFrame();
	const TOptional<FBox2D> Rect = Driver.WidgetPixelRect(Driver.PreviewFor(Chip));
	if (!TestTrue(TEXT("The chip is on screen"), Rect.IsSet()))
	{
		return false;
	}
	const FVector2D CentreBefore = Rect->GetCenter();
	// Near the left edge, halfway down: the Move handle, clear of the edge's own handle and of the anchor markers, which
	// stand in a cluster at the chip's middle -- the left frame's centre, where its point anchors are.
	const FIntPoint From = PointInBox(Rect.GetValue(), 0.15, 0.5);
	// The two frames' middles stand 360 apart, so this lets the chip go 20 right of and 30 below the right one's: not where
	// it stood in the left one, which is what it would be given back if the move kept its old numbers.
	const FIntPoint Travel(380, 30);

	TestTrue(TEXT("The drag completes"), Driver.DragFromTo(From, From + Travel, /*Steps*/ 8));
	Driver.PumpFrame();
	Driver.PumpFrame();

	Chip = Scoped.FindTemplate(TEXT("Chip"));
	UDreamWidget* Right = Scoped.FindTemplate(TEXT("Right"));
	const FString TextAfter = Scoped.ReadFile();
	if (!TestTrue(TEXT("The chip is the right frame's now"), Chip != nullptr && Chip->GetParent() == Right))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
		return false;
	}
	const TOptional<FBox2D> After = Driver.WidgetPixelRect(Driver.PreviewFor(Chip));
	const FVector2D Expected = CentreBefore + FVector2D(Travel);
	if (!TestTrue(FString::Printf(TEXT("and it stands where it was let go, %s: it is at %s"), *Expected.ToString(),
		After.IsSet() ? *After->GetCenter().ToString() : TEXT("nowhere")),
		After.IsSet() && FVector2D::Distance(After->GetCenter(), Expected) <= 1.0))
	{
		AddInfo(FString::Printf(TEXT("The file reads:\n%s"), *TextAfter));
	}
	return true;
}

#endif
