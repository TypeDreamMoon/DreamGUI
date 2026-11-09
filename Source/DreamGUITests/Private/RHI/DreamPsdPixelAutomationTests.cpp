// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Engine/Texture2D.h"
#include "UObject/StrongObjectPtr.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPropertyBinding.h"
#include "Core/DreamWidgetTree.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"

#include "Import/DreamPsdDuiModel.h"
#include "Import/DreamPsdImporter.h"
#include "Import/DreamPsdMapper.h"
#include "Psd/DreamPsdReader.h"
#include "Psd/DreamPsdTestDocuments.h"

#include "DreamGalleryStage.h"
#include "DreamPixelProbe.h"

/*
 * An import drawn: the gallery document (every layer original, see DreamPsdTestDocuments) read, mapped, printed as a
 * .dui, built and put on a render-target stage, then held pixel by pixel to the merged image the document carries.
 * That merged image is what DreamGUI draws of those layers into an sRGB target (normal layers over in linear light,
 * the rest by the blend formulas), so a difference is the mapping's -- a layer in the wrong place, a mask on the wrong
 * rectangle, a group not isolating -- and not the two renderers'. A document from the editor itself blends normal
 * layers in encoded space; how close an import of one comes is for the documents listed in the import's report.
 */
namespace DreamPsdPixelTestLocal
{
	using namespace DreamGalleryStage;

	/** Pixels further apart than this on any channel count as different. */
	constexpr uint8 Tolerance = 6;
	/** At most this share may differ: the anti-aliased edges a block's and a mask's distance fields draw, against the path the document filled. */
	constexpr double AllowedFraction = 0.01;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPsdImportPixelTest,
	"DreamGUI.RHI.Psd.AnImportDrawsWhatTheDocumentsMergedImageShows",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter | EAutomationTestFlags::NonNullRHI)

bool FDreamPsdImportPixelTest::RunTest(const FString& Parameters)
{
	using namespace DreamPsdPixelTestLocal;
	const FColor StageBackdrop = DreamGalleryStage::Backdrop;
	FDreamPsdWriteDocument Write = DreamPsdTestDocuments::MakeGallery();
	const TArray<uint8> Bytes = DreamPsdTestDocuments::WriteWithComposite(Write, StageBackdrop);
	TSharedRef<FDreamPsdDocument> Document = MakeShared<FDreamPsdDocument>();
	FDreamPsdMessages Messages;
	if (!TestTrue(FString::Printf(TEXT("the gallery reads (%s)"), *Messages.ToString()), DreamPsdReader::Read(Bytes, *Document, Messages) && !Document->Composite.IsEmpty()))
	{
		return false;
	}
	FDreamPsdMapOptions Options;
	Options.DocumentName = TEXT("PixelGallery");
	Options.TextureFolder = TEXT("/Temp/DreamGUITests/PsdPixel/PixelGallery");
	FDreamPsdMapper Mapper(*Document, Options);
	FDreamPsdMapResult Map;
	Mapper.Map(Map);
	TArray<UTexture2D*> Textures = DreamPsdImporter::MakeTextures(Map.Textures, false, Messages);
	const FString Text = DreamPsdDui::Print(Map.Root, FString(), TEXT("PixelGallery.psd"));
	FDreamUIDiagnosticBag Diagnostics;
	FDreamUIAst Ast;
	TArray<FDreamWidgetPropertyBinding> Bindings;
	const bool bParsed = FDreamUISourceFile::Parse(Text, TEXT("PixelGallery.dui"), Ast, Diagnostics);
	TStrongObjectPtr<UDreamWidgetTree> Tree(bParsed ? FDreamUITextBuilder::Build(Ast, GetTransientPackage(), Diagnostics, Bindings) : nullptr);
	if (!TestTrue(FString::Printf(TEXT("the import's .dui builds: %s"), *Diagnostics.ToString()), Tree.IsValid() && !Diagnostics.HasErrors()))
	{
		return false;
	}
	FStageRef Stage = BeginStage(*this, FIntPoint(Document->Width, Document->Height), StageBackdrop);
	if (!Stage->IsUsable())
	{
		Stage->TearDown();
		return false;
	}
	Stage->KeepAlive(Tree.Get());
	for (UTexture2D* Texture : Textures)
	{
		Stage->KeepAlive(Texture);
	}
	UDreamWidget* Scene = DuplicateDreamWidgetHierarchy(Stage->GetWorld(), Tree->RootWidget.Get(), Stage->GetRoot());
	if (!TestNotNull(TEXT("the import comes to life on the stage"), Scene))
	{
		Stage->TearDown();
		return false;
	}
	EnqueueFrames(Stage, 3);
	EnqueueFramesUntilDrawn(Stage, Document->Width * Document->Height / 2);
	EnqueueFramesUntilSettled(Stage);
	EnqueueDo([this, Stage, Document]()
	{
		TArray<FColor> Pixels;
		FIntPoint Size;
		if (!Stage->ReadBack(Pixels, Size))
		{
			AddError(TEXT("the stage could not be read back"));
			return;
		}
		TArray<FColor> Merged = Document->Composite.Pixels;
		for (FColor& P : Merged)
		{
			P.A = 255;
		}
		for (FColor& P : Pixels)
		{
			P.A = 255;
		}
		// The difference, four times over, kept beside the two pictures whatever the outcome.
		TArray<FColor> Difference;
		int32 Worst = 0;
		int32 Over = 0;
		double Sum = 0.0;
		const int32 Count = FMath::Min(Pixels.Num(), Merged.Num());
		Difference.SetNum(Count);
		for (int32 i = 0; i < Count; ++i)
		{
			const int32 R = FMath::Abs(Pixels[i].R - Merged[i].R), G = FMath::Abs(Pixels[i].G - Merged[i].G), B = FMath::Abs(Pixels[i].B - Merged[i].B);
			const int32 Max = FMath::Max3(R, G, B);
			Worst = FMath::Max(Worst, Max);
			Over += Max > Tolerance ? 1 : 0;
			Sum += (R + G + B) / 3.0;
			Difference[i] = FColor((uint8)FMath::Min(255, R * 4), (uint8)FMath::Min(255, G * 4), (uint8)FMath::Min(255, B * 4), 255);
		}
		FDreamPixelProbe::SaveCapture(Merged, Size, TEXT("Psd_Gallery_Merged"));
		FDreamPixelProbe::SaveCapture(Difference, Size, TEXT("Psd_Gallery_Difference"));
		AddInfo(FString::Printf(TEXT("the import against the document's merged image: %d of %d pixels past %d, the worst %d, the mean %.3f"),
			Over, Count, Tolerance, Worst, Count > 0 ? Sum / Count : 0.0));
		FDreamPixelProbe::ExpectPicturesMatch(*this, Pixels, Size, Merged, Size, TEXT("Psd_Gallery_AgainstMerged"), Tolerance, AllowedFraction);
		CheckGolden(*this, Pixels, Size, TEXT("Psd_Gallery"), EMissingGolden::Fail);
	});
	EnqueueTearDown(Stage);
	return true;
}

#endif
