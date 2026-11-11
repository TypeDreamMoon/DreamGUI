// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Editor/DreamUITextStructureTestFixtures.h"
#include "RHI/DreamPixelProbe.h"

#include "Workflow/DreamUILayerStyleDialog.h"
#include "Workflow/SDreamUILayerStyleDialog.h"

#include "DreamUICaptureLibrary.h"

#include "Core/Components/DreamWidget.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "IDetailsView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "RenderingThread.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

/*
 * The Layer Style dialog drawn by Slate on a real RHI, off screen, the way it opens on a styled widget: its effects listed
 * with their ticks, the drop shadow selected and its parameters shown, OK, Cancel, New Style and Preview beside them. The
 * picture is written to Saved/DreamGUITests/Captures (Workflow_LayerStyleDialog.png) for a reader with no editor open, and
 * it has to have drawn its list, its parameters and its buttons -- something in each third of it. Its parameters are drawn
 * by the editing controls a widget's details use: the shadow's angle a dial on the style's global light, its contour a
 * curve, its blend mode the blend mode picker.
 *
 * A widget's details, drawn so that their rows are made: the Edit... beside the Layer section's layer style switch opens
 * the dialog.
 */
namespace DreamStyleDialogPixelTestLocal
{
	using namespace DreamUITextStructureFixtures;

	const FColor Background(24, 26, 31, 255);

	bool Draw(const TSharedRef<SWidget>& InContent, FIntPoint InSize, TArray<FColor>& OutPixels)
	{
		const TSharedRef<SWidget> Root = SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::FromSRGBColor(Background))
			.Padding(FMargin(0.0f))
			[
				InContent
			];
		TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
		Target->ClearColor = FLinearColor::Transparent;
		Target->InitCustomFormat(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y), EPixelFormat::PF_B8G8R8A8, true);
		Target->UpdateResourceImmediate(true);
		FWidgetRenderer* Renderer = new FWidgetRenderer(true, true);
		for (int32 Pass = 0; Pass < 5; ++Pass)
		{
			Renderer->DrawWidget(Target.Get(), Root, 1.0f, FVector2D(InSize), 0.016f);
			// Icons and glyphs drawn for the first time are rasterised by the application's own Slate frame.
			FSlateApplication::Get().Tick();
			FlushRenderingCommands();
		}
		FIntPoint Size = FIntPoint::ZeroValue;
		const bool bRead = UDreamUICaptureLibrary::ReadRenderTargetPixels(Target.Get(), OutPixels, Size);
		BeginCleanup(Renderer);
		return bRead && Size == InSize;
	}

	/**
	 * The type of every widget under InRoot, and the text of every text block as written (FText::BuildSourceString): the
	 * English, whatever language the editor shows it in.
	 */
	void CollectWidgets(const TSharedRef<SWidget>& InRoot, TArray<FString>& OutTypes, TArray<FString>& OutTexts)
	{
		OutTypes.Add(InRoot->GetTypeAsString());
		if (InRoot->GetType() == TEXT("STextBlock"))
		{
			OutTexts.Add(StaticCastSharedRef<STextBlock>(InRoot)->GetText().BuildSourceString());
		}
		FChildren* Children = InRoot->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			CollectWidgets(Children->GetChildAt(Index), OutTypes, OutTexts);
		}
	}

	/** The widget under InRoot tagged InTag, or null. */
	TSharedPtr<SWidget> FindTagged(const TSharedRef<SWidget>& InRoot, FName InTag)
	{
		if (InRoot->GetTag() == InTag)
		{
			return InRoot;
		}
		FChildren* Children = InRoot->GetChildren();
		for (int32 Index = 0; Children != nullptr && Index < Children->Num(); ++Index)
		{
			if (TSharedPtr<SWidget> Found = FindTagged(Children->GetChildAt(Index), InTag))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** Pixels between columns InFromX and InToX that are not the background: what was drawn there. */
	int32 DrawnPixels(const TArray<FColor>& InPixels, FIntPoint InSize, int32 InFromX, int32 InToX)
	{
		int32 Count = 0;
		for (int32 Y = 0; Y < InSize.Y; ++Y)
		{
			for (int32 X = InFromX; X < InToX; ++X)
			{
				const FColor& Pixel = InPixels[Y * InSize.X + X];
				Count += (FMath::Abs(Pixel.R - Background.R) + FMath::Abs(Pixel.G - Background.G) + FMath::Abs(Pixel.B - Background.B)) > 30 ? 1 : 0;
			}
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleDialogPixelTest,
	"DreamGUI.Designer.RHI.TheLayerStyleDialogDraws",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamStyleDialogPixelTest::RunTest(const FString&)
{
	using namespace DreamStyleDialogPixelTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogPicture.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    RectBlock Card {"),
		TEXT("        + DreamLayerStyle {"),
		TEXT("            Style.DropShadows = [\"(Distance=6,Size=12,Opacity=0.5)\", \"(Distance=1,Size=2)\"]"),
		TEXT("            Style.Strokes = [\"(Size=2)\"]"),
		TEXT("            Style.OuterGlow.bEnabled = true"),
		TEXT("            Style.BevelEmboss.bEnabled = true"),
		TEXT("        }"),
		TEXT("    }"),
		TEXT("}") }))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogPicture"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const TSharedPtr<FDreamUILayerStyleSession> Session = FDreamUILayerStyleSession::Begin(*Scoped.Designer, { Scoped.FindPreview(TEXT("Card")) });
	if (!TestTrue(TEXT("a session begins on the card"), Session.IsValid())) return false;

	const TSharedRef<SDreamUILayerStyleDialog> Dialog = SNew(SDreamUILayerStyleDialog, Session.ToSharedRef());
	Dialog->SelectRow({ EDreamUIStyleDialogPage::DropShadow, 0 });
	const FIntPoint Size(860, 540);
	TArray<FColor> Pixels;
	const bool bDrawn = TestTrue(TEXT("the dialog is drawn and read"),
		Draw(SNew(SBox).WidthOverride(static_cast<float>(Size.X)).HeightOverride(static_cast<float>(Size.Y))[Dialog], Size, Pixels));
	// The editing controls registered for a field's type or tags are the dialog's too, and find their way about the style.
	TArray<FString> Types;
	TArray<FString> Texts;
	CollectWidgets(Dialog, Types, Texts);
	TestTrue(TEXT("the shadow's angle is a dial"), Types.Contains(TEXT("SDreamAngleWheel")));
	TestTrue(TEXT("which turns the style's global light, as in a widget's details"), Texts.Contains(TEXT("Global")));
	TestTrue(TEXT("its contour a curve"), Types.Contains(TEXT("SDreamCurveEditor")));
	TestFalse(TEXT("and its blend mode no plain list of names"), Types.Contains(TEXT("SPropertyEditorCombo")));
	Dialog->Cancel();
	if (!bDrawn) return false;

	AddInfo(FString::Printf(TEXT("Workflow_LayerStyleDialog: %s"), *FDreamPixelProbe::SaveCapture(Pixels, Size, TEXT("Workflow_LayerStyleDialog"))));
	const int32 Third = Size.X / 3;
	const int32 Enough = Third * Size.Y / 100;
	TestTrue(TEXT("its list is drawn"), DrawnPixels(Pixels, Size, 0, Third) > Enough);
	TestTrue(TEXT("its parameters are drawn"), DrawnPixels(Pixels, Size, Third, 2 * Third) > Enough);
	TestTrue(TEXT("its buttons are drawn"), DrawnPixels(Pixels, Size, 2 * Third, Size.X) > Enough / 4);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamStyleDialogLayerSectionTest,
	"DreamGUI.Designer.RHI.TheLayerSectionsEditOpensTheLayerStyleDialog",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamStyleDialogLayerSectionTest::RunTest(const FString&)
{
	using namespace DreamStyleDialogPixelTestLocal;
	FScopedDuiFile File(TEXT("StyleDialogLayerSection.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    RectBlock Card {"),
		TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = true }"),
		TEXT("    }"),
		TEXT("    RectBlock Plain { }"),
		TEXT("}") }))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_StyleDialogLayerSection"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	UDreamWidget* Card = Scoped.FindPreview(TEXT("Card"));
	if (!TestNotNull(TEXT("the card has a preview"), Card)) return false;
	Scoped.Designer->SelectWidgets({ Card }, /*bAppendOrToggle*/false);

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	FDetailsViewArgs Args;
	Args.bAllowSearch = false;
	Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	const TSharedRef<IDetailsView> View = PropertyEditor.CreateDetailView(Args);
	View->SetObject(Card);
	const FIntPoint Size(480, 640);
	TArray<FColor> Pixels;
	if (!TestTrue(TEXT("the details are drawn"), Draw(SNew(SBox).WidthOverride(480.0f).HeightOverride(640.0f)[View], Size, Pixels))) return false;

	const TSharedPtr<SButton> Edit = StaticCastSharedPtr<SButton>(FindTagged(View, TEXT("DreamGUI.LayerSection.EditLayerStyle")));
	if (!TestTrue(TEXT("the Layer section has the layer style's Edit..."), Edit.IsValid())) return false;
	TestTrue(TEXT("which can be pressed"), Edit->IsEnabled());
	Edit->SimulateClick();
	const TSharedPtr<SDreamUILayerStyleDialog> Dialog = DreamUILayerStyleDialog::Find(*Scoped.Designer);
	TestTrue(TEXT("pressed, it opens the Layer Style dialog"), Dialog.IsValid());
	TestTrue(TEXT("on the widget"), Dialog.IsValid() && Dialog->GetSession()->GetTargetNames() == TArray<FString>({ TEXT("Card") }));
	if (Dialog.IsValid())
	{
		Dialog->Cancel();
	}
	return true;
}

#endif
