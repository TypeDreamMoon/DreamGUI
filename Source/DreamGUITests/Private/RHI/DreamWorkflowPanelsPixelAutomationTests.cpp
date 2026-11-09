// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Editor/DreamUITextStructureTestFixtures.h"
#include "RHI/DreamPixelProbe.h"

#include "DreamGUIEditorModule.h"
#include "Widget/SDreamUISwatches.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/SDreamUIPresetLibrary.h"

#include "Core/DreamGUISettings.h"
#include "DreamUICaptureLibrary.h"

#include "Engine/TextureRenderTarget2D.h"
#include "Framework/Application/SlateApplication.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "RenderingThread.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Slate/WidgetRenderer.h"
#include "Styling/AppStyle.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"

/*
 * The designer's workflow panels drawn by Slate on a real RHI, off screen: the preset library's style and gradient
 * shelves, a colour row's eyedropper and palette, the widget menu's Layer Style section. Each picture is written to
 * Saved/DreamGUITests/Captures (Workflow_*.png) -- what the panels look like, for a reader who has no editor open -- and
 * each has to have drawn something on its background.
 *
 * The presets and the palette are the project's: each is put back as it was found. The menu's copy goes to a clipboard of
 * the editor's own, not the desktop's.
 */
namespace DreamWorkflowPanelsPixelTestLocal
{
	using namespace DreamUITextStructureFixtures;

	struct FSettingsKeeper
	{
		TMap<FName, FString> Gradients;
		TMap<FName, FString> Styles;
		TArray<FDreamUISwatch> Swatches;
		FString File;
		FString Text;
		bool bHadFile = false;
		/** The menu's copy goes to a clipboard of the editor's own, not the desktop's. */
		DreamUILayerStyleClipboard::FScopedClipboard Clipboard;

		FSettingsKeeper()
		{
			const UDreamGUISettings* Settings = GetDefault<UDreamGUISettings>();
			Gradients = Settings->GradientPresets;
			Styles = Settings->StylePresets;
			Swatches = Settings->ProjectSwatches;
			File = Settings->GetDefaultConfigFilename();
			bHadFile = FFileHelper::LoadFileToString(Text, *File);
		}
		~FSettingsKeeper()
		{
			UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
			Settings->GradientPresets = Gradients;
			Settings->StylePresets = Styles;
			Settings->ProjectSwatches = Swatches;
			if (bHadFile)
			{
				FFileHelper::SaveStringToFile(Text, *File);
			}
			else
			{
				// The test made the file by saving a setting: it goes again.
				IFileManager::Get().Delete(*File, /*RequireExists*/false, /*EvenReadOnly*/true, /*Quiet*/true);
			}
		}
	};

	const FColor Background(24, 26, 31, 255);

	/** InContent on the background, InSize big, drawn four times with Slate ticking between (glyphs and icons are rasterised on the first) and read. */
	bool Draw(const TSharedRef<SWidget>& InContent, FIntPoint InSize, TArray<FColor>& OutPixels)
	{
		const TSharedRef<SWidget> Root = SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FLinearColor::FromSRGBColor(Background))
			.Padding(FMargin(8.0f))
			[
				InContent
			];
		TStrongObjectPtr<UTextureRenderTarget2D> Target(NewObject<UTextureRenderTarget2D>(GetTransientPackage(), NAME_None, RF_Transient));
		Target->ClearColor = FLinearColor::Transparent;
		Target->InitCustomFormat(static_cast<uint32>(InSize.X), static_cast<uint32>(InSize.Y), EPixelFormat::PF_B8G8R8A8, true);
		Target->UpdateResourceImmediate(true);
		FWidgetRenderer* Renderer = new FWidgetRenderer(true, true);
		for (int32 Pass = 0; Pass < 4; ++Pass)
		{
			Renderer->DrawWidget(Target.Get(), Root, 1.0f, FVector2D(InSize), 0.016f);
			// An icon drawn for the first time is rasterised by the application's own Slate frame, not by this renderer's.
			FSlateApplication::Get().Tick();
			FlushRenderingCommands();
		}
		FIntPoint Size = FIntPoint::ZeroValue;
		const bool bRead = UDreamUICaptureLibrary::ReadRenderTargetPixels(Target.Get(), OutPixels, Size);
		BeginCleanup(Renderer);
		return bRead && Size == InSize;
	}

	/** How many pixels are not the background: what was drawn on it. */
	int32 DrawnPixels(const TArray<FColor>& InPixels)
	{
		int32 Count = 0;
		for (const FColor& Pixel : InPixels)
		{
			Count += (FMath::Abs(Pixel.R - Background.R) + FMath::Abs(Pixel.G - Background.G) + FMath::Abs(Pixel.B - Background.B)) > 30 ? 1 : 0;
		}
		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWorkflowPanelsPixelTest,
	"DreamGUI.Designer.RHI.ThePresetLibraryTheColourRowsAndTheLayerStyleMenuDraw",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::NonNullRHI | EAutomationTestFlags::EngineFilter)

bool FDreamWorkflowPanelsPixelTest::RunTest(const FString&)
{
	using namespace DreamWorkflowPanelsPixelTestLocal;
	FSettingsKeeper Keeper;
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	Settings->StylePresets.Add(TEXT("Button Bevel"), TEXT("+ DreamLayerStyle { Style.BevelEmboss.bEnabled = true  Style.BevelEmboss.Size = 6 }"));
	Settings->StylePresets.Add(TEXT("Soft Shadow"), TEXT("+ DreamLayerStyle { Style.DropShadows = [\"(Distance=6,Size=12,Opacity=0.5)\"] }"));
	Settings->StylePresets.Add(TEXT("Title Glow"), TEXT("+ DreamLayerStyle { Style.OuterGlow.bEnabled = true  Style.OuterGlow.Size = 14 }"));
	Settings->GradientPresets.Add(TEXT("Sunset"), TEXT("linear-gradient(90deg, #FF8A5B, #7B4AE2, #2B5FD9)"));
	Settings->GradientPresets.Add(TEXT("Gold"), TEXT("linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)"));
	Settings->ProjectSwatches = {
		FDreamUISwatch(TEXT("Brand Gold"), FColor(242, 193, 78)), FDreamUISwatch(TEXT("Brand Violet"), FColor(123, 74, 226)),
		FDreamUISwatch(TEXT("Ink"), FColor(27, 31, 39)), FDreamUISwatch(TEXT("Paper"), FColor(230, 233, 240)),
		FDreamUISwatch(TEXT("Alert"), FColor(232, 93, 74)) };

	FScopedDuiFile File(TEXT("WorkflowPanels.dui"));
	if (!TestTrue(TEXT("the fixture wrote a .dui"), File.Write({
		TEXT("Widget Root {"),
		TEXT("    + CanvasPanel"),
		TEXT("    RectBlock Card {"),
		TEXT("        BlendMode = Multiply"),
		TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = true }"),
		TEXT("    }"),
		TEXT("}") }))) return false;
	FScopedTextBlueprint Scoped(TEXT("BP_WorkflowPanels"), File.FilePath);
	if (!TestNotNull(TEXT("the designer opened"), Scoped.Designer)) return false;
	const TSharedRef<FDreamWidgetBlueprintEditor> Designer = StaticCastSharedRef<FDreamWidgetBlueprintEditor>(Scoped.Designer->AsShared());
	Designer->SelectWidgets({ Scoped.FindPreview(TEXT("Card")) }, /*bAppendOrToggle*/false);

	struct FPicture { const TCHAR* Name; TSharedRef<SWidget> Widget; FIntPoint Size; };
	TArray<FPicture> Pictures;
	{
		const TSharedRef<SDreamUIPresetLibrary> Styles = SNew(SDreamUIPresetLibrary, Designer);
		Styles->SetKind(EDreamUIPresetKind::Style);
		Pictures.Add({ TEXT("Workflow_PresetLibrary_Styles"), SNew(SBox).WidthOverride(320.0f).HeightOverride(300.0f)[Styles], FIntPoint(336, 316) });
		const TSharedRef<SDreamUIPresetLibrary> Gradients = SNew(SDreamUIPresetLibrary, Designer);
		Gradients->SetKind(EDreamUIPresetKind::Gradient);
		Pictures.Add({ TEXT("Workflow_PresetLibrary_Gradients"), SNew(SBox).WidthOverride(320.0f).HeightOverride(300.0f)[Gradients], FIntPoint(336, 316) });
	}
	Pictures.Add({ TEXT("Workflow_ColorRow"),
		SNew(SVerticalBox)
		+ SVerticalBox::Slot().AutoHeight()
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot().AutoWidth().VAlign(VAlign_Center).Padding(0.0f, 0.0f, 8.0f, 0.0f)
			[
				SNew(STextBlock).Text(FText::FromString(TEXT("Color")))
			]
			+ SHorizontalBox::Slot().AutoWidth()
			[
				SNew(SDreamUIColorQuickPick).Color(FLinearColor::FromSRGBColor(FColor(242, 193, 78)))
			]
		]
		+ SVerticalBox::Slot().AutoHeight().Padding(0.0f, 8.0f, 0.0f, 0.0f)
		[
			SNew(SBox).WidthOverride(200.0f)
			[
				SNew(SDreamUISwatchGrid).CurrentColor(FLinearColor::FromSRGBColor(FColor(242, 193, 78)))
			]
		], FIntPoint(240, 90) });

	DreamUILayerStyleClipboard::Copy(Scoped.FindTemplate(TEXT("Card")), EDreamUIStyleParts::All);
	FMenuBuilder Menu(/*bShouldCloseWindowAfterMenuSelection*/true, nullptr);
	FModuleManager::GetModuleChecked<FDreamGUIEditorModule>(TEXT("DreamGUIEditor")).ExtendWidgetContextMenu(Menu, Designer);
	Pictures.Add({ TEXT("Workflow_LayerStyleMenu"), Menu.MakeWidget(), FIntPoint(380, 176) });

	for (const FPicture& Picture : Pictures)
	{
		TArray<FColor> Pixels;
		if (!TestTrue(*FString::Printf(TEXT("%s is drawn and read"), Picture.Name), Draw(Picture.Widget, Picture.Size, Pixels)))
		{
			continue;
		}
		const FString Saved = FDreamPixelProbe::SaveCapture(Pixels, Picture.Size, Picture.Name);
		AddInfo(FString::Printf(TEXT("%s: %s"), Picture.Name, *Saved));
		TestTrue(*FString::Printf(TEXT("%s drew on its background"), Picture.Name), DrawnPixels(Pixels) > Picture.Size.X * Picture.Size.Y / 200);
	}
	return true;
}

#endif
