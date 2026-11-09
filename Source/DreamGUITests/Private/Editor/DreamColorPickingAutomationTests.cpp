// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Designer/DreamWidgetDetailPropertyExtensionHandler.h"
#include "Widget/SDreamUIEyedropper.h"
#include "Widget/SDreamUISwatches.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"

#include "Editor.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"
#include "UObject/StrongObjectPtr.h"

/*
 * The eyedropper and the project's palette (SDreamUIEyedropper.h, SDreamUISwatches.h): the eyedropper hands a pick or a
 * cancel to its caller and writes nothing itself; the palette is added to, used and taken from, saved with the project, and
 * says so; and a colour row of the designer's details panel takes a pick through its own handle, keeping its alpha.
 *
 * The palette is a project setting: each test puts back the settings it found, the file on disk included.
 */
namespace DreamColorPickingTestLocal
{
	/** The project's palette, in memory and on disk, as the test found it. */
	struct FPaletteKeeper
	{
		TArray<FDreamUISwatch> Swatches;
		FString File;
		FString Text;
		bool bHadFile = false;

		FPaletteKeeper()
		{
			Swatches = GetDefault<UDreamGUISettings>()->ProjectSwatches;
			File = GetDefault<UDreamGUISettings>()->GetDefaultConfigFilename();
			bHadFile = FFileHelper::LoadFileToString(Text, *File);
		}
		~FPaletteKeeper()
		{
			GetMutableDefault<UDreamGUISettings>()->ProjectSwatches = Swatches;
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

	TSharedPtr<IDetailTreeNode> FindNode(const TSharedRef<IDetailTreeNode>& InNode, FName InName)
	{
		const TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle();
		if (Handle.IsValid() && Handle->GetProperty() != nullptr && Handle->GetProperty()->GetFName() == InName)
		{
			return InNode;
		}
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			if (TSharedPtr<IDetailTreeNode> Found = FindNode(Child, InName))
			{
				return Found;
			}
		}
		return nullptr;
	}

	TSharedPtr<IPropertyHandle> FindHandle(const TSharedPtr<IPropertyRowGenerator>& InRows, FName InName)
	{
		for (const TSharedRef<IDetailTreeNode>& Root : InRows->GetRootTreeNodes())
		{
			if (TSharedPtr<IDetailTreeNode> Found = FindNode(Root, InName))
			{
				return Found->CreatePropertyHandle();
			}
		}
		return nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamEyedropperPickAndCancelTest,
	"DreamGUI.Designer.ColorPicking.TheEyedropperHandsItsCallerAPickOrACancel",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Begin, pick: OnBegin, then OnPicked with the colour read -- off the screen here, where no designer viewport is under the
 * point -- and the eyedropper is down again. Begin, cancel: OnCancelled and no pick. A pick when not picking does nothing.
 */
bool FDreamEyedropperPickAndCancelTest::RunTest(const FString&)
{
	int32 Begun = 0;
	int32 Picked = 0;
	int32 Cancelled = 0;
	FDreamUIPickedColor Last;
	const TSharedRef<SDreamUIEyedropperButton> Eyedropper = SNew(SDreamUIEyedropperButton)
		.OnBegin_Lambda([&Begun]() { ++Begun; })
		.OnPicked_Lambda([&Picked, &Last](const FDreamUIPickedColor& InColor) { ++Picked; Last = InColor; })
		.OnCancelled_Lambda([&Cancelled]() { ++Cancelled; });

	TestFalse(TEXT("a pick before picking began does nothing"), Eyedropper->PickAt(FVector2D(1.0, 1.0)));
	Eyedropper->BeginPicking();
	TestTrue(TEXT("picking began"), Eyedropper->IsPicking());
	TestEqual(TEXT("and said so"), Begun, 1);
	TestTrue(TEXT("a pick is made"), Eyedropper->PickAt(FVector2D(1.0, 1.0)));
	TestEqual(TEXT("and handed over once"), Picked, 1);
	TestFalse(TEXT("the eyedropper is down again"), Eyedropper->IsPicking());
	TestEqual(TEXT("off a designer viewport it is the screen's pixel"), Last.Source, EDreamUIPickedFrom::Screen);
	TestEqual(TEXT("opaque"), Last.SRGB.A, (uint8)255);
	TestTrue(TEXT("its linear form is its sRGB one decoded"), Last.Linear.Equals(FLinearColor::FromSRGBColor(Last.SRGB), 1e-4f));

	Eyedropper->BeginPicking();
	Eyedropper->CancelPicking();
	TestEqual(TEXT("a cancel is said"), Cancelled, 1);
	TestEqual(TEXT("and picks nothing"), Picked, 1);
	TestFalse(TEXT("and puts the eyedropper down"), Eyedropper->IsPicking());

	FViewport* Viewport = nullptr;
	FIntPoint Pixel = FIntPoint::ZeroValue;
	TestFalse(TEXT("no designer viewport is under a corner of the desktop"), FDreamUIEyedropper::FindDesignerViewportPixel(FVector2D(1.0, 1.0), Viewport, Pixel));
	FDreamUIPickedColor Unused;
	TestFalse(TEXT("and no viewport has no pixel to read"), FDreamUIEyedropper::SampleViewport(nullptr, FIntPoint::ZeroValue, Unused));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamProjectPaletteTest,
	"DreamGUI.Designer.ColorPicking.TheProjectPaletteIsAddedToUsedAndTakenFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A colour added to the palette is kept once, named by its hex code, written to the project's DreamGUI settings file and
 * announced; a swatch of the grid clicked hands its colour over in linear light; the grid's + adds the colour it was given;
 * a swatch removed is gone.
 */
bool FDreamProjectPaletteTest::RunTest(const FString&)
{
	using namespace DreamColorPickingTestLocal;
	FPaletteKeeper Keeper;
	GetMutableDefault<UDreamGUISettings>()->ProjectSwatches.Reset();

	int32 Changes = 0;
	const FDelegateHandle Handle = FDreamUIProjectPalette::OnChanged().AddLambda([&Changes]() { ++Changes; });
	const FColor Gold(242, 193, 78, 255);
	FDreamUIProjectPalette::Add(Gold);
	FDreamUIProjectPalette::Add(Gold);
	TestEqual(TEXT("a colour is kept once"), FDreamUIProjectPalette::Get().Num(), 1);
	TestEqual(TEXT("and the change said once"), Changes, 1);
	if (FDreamUIProjectPalette::Get().Num() == 1)
	{
		TestEqual(TEXT("named by its hex code"), FDreamUIProjectPalette::Get()[0].Name, FString(TEXT("#F2C14E")));
		TestEqual(TEXT("holding the colour"), FDreamUIProjectPalette::Get()[0].Color, Gold);
	}
	FString OnDisk;
	FFileHelper::LoadFileToString(OnDisk, *GetDefault<UDreamGUISettings>()->GetDefaultConfigFilename());
	TestTrue(TEXT("and written to the project's settings file"), OnDisk.Contains(TEXT("ProjectSwatches")));

	FLinearColor Clicked = FLinearColor::Black;
	const FLinearColor Current(0.2f, 0.4f, 0.6f, 1.0f);
	const TSharedRef<SDreamUISwatchGrid> Grid = SNew(SDreamUISwatchGrid)
		.CurrentColor(Current)
		.OnSwatchClicked_Lambda([&Clicked](const FLinearColor& InColor) { Clicked = InColor; });
	Grid->ClickSwatch(0);
	TestTrue(TEXT("a swatch clicked hands over its colour in linear light"), Clicked.Equals(FLinearColor::FromSRGBColor(Gold), 1e-4f));
	Grid->AddCurrentColor();
	TestEqual(TEXT("the + adds the colour it was given"), FDreamUIProjectPalette::Get().Num(), 2);
	if (FDreamUIProjectPalette::Get().Num() == 2)
	{
		TestEqual(TEXT("as sRGB"), FDreamUIProjectPalette::Get()[1].Color, Current.ToFColorSRGB());
	}
	FDreamUIProjectPalette::Remove(0);
	TestEqual(TEXT("a swatch removed is gone"), FDreamUIProjectPalette::Get().Num(), 1);
	TestEqual(TEXT("every change was said"), Changes, 3);

	const TSharedRef<SDreamUIColorQuickPick> QuickPick = SNew(SDreamUIColorQuickPick).Color(Current);
	TestTrue(TEXT("a colour row's quick pick has its eyedropper"), QuickPick->GetEyedropper().IsValid());
	FDreamUIProjectPalette::OnChanged().Remove(Handle);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamColorRowPickTest,
	"DreamGUI.Designer.ColorPicking.AColourRowTakesAPickThroughItsHandleAndKeepsItsAlpha",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * The designer's details panel puts the quick pick beside every colour row -- FLinearColor and FColor alike -- and a pick
 * is written through the row's handle: an FColor row as sRGB, either keeping the alpha it had, one undo step each.
 */
bool FDreamColorRowPickTest::RunTest(const FString&)
{
	using namespace DreamColorPickingTestLocal;

	UWorld* World = UWorld::CreateWorld(EWorldType::Editor, false);
	ON_SCOPE_EXIT { World->DestroyWorld(false); };
	UDreamWidget* Widget = NewObject<UDreamWidget>(World, NAME_None, RF_Transactional);
	Widget->CreateNewVisual<UDreamRectBlock>();
	UDreamRectBlock* Block = Cast<UDreamRectBlock>(Widget->GetVisual());
	if (!TestNotNull(TEXT("the widget has a rect block"), Block)) return false;
	Block->SetFlags(RF_Transactional);
	Block->SetColor(FColor(10, 20, 30, 128));
	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	const TSharedPtr<IPropertyRowGenerator> Rows = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	Rows->SetObjects({ Block });

	const TSharedPtr<IPropertyHandle> ColorRow = FindHandle(Rows, TEXT("Color"));
	if (!TestTrue(TEXT("the block has a colour row"), ColorRow.IsValid() && ColorRow->IsValidHandle())) return false;
	TestTrue(TEXT("it is a colour row the quick pick goes beside"), FDreamWidgetDetailPropertyExtensionHandler::IsColorProperty(ColorRow->GetProperty()));
	const FDreamWidgetDetailPropertyExtensionHandler Extension(nullptr);
	TestTrue(TEXT("and the panel's extension takes the row"), Extension.IsPropertyExtendable(UDreamRectBlock::StaticClass(), *ColorRow));

	const FLinearColor Picked = FLinearColor::FromSRGBColor(FColor(200, 100, 50, 255));
	if (!TestTrue(TEXT("a pick is written"), FDreamWidgetDetailPropertyExtensionHandler::WritePickedColor(ColorRow, Picked))) return false;
	const FColor Now = Block->GetColor();
	TestEqual(TEXT("as the sRGB it was picked as"), FColor(Now.R, Now.G, Now.B, 255), FColor(200, 100, 50, 255));
	TestEqual(TEXT("keeping the row's alpha"), Now.A, (uint8)128);

	TestTrue(TEXT("one undo puts it back"), GEditor->UndoTransaction());
	TestEqual(TEXT("as it was"), Block->GetColor(), FColor(10, 20, 30, 128));

	// A row that is no colour takes no pick, and is offered no quick pick.
	const TSharedPtr<IPropertyHandle> Other = FindHandle(Rows, TEXT("BorderWidth"));
	if (TestTrue(TEXT("the block has a border width row"), Other.IsValid()))
	{
		TestFalse(TEXT("which is no colour row"), FDreamWidgetDetailPropertyExtensionHandler::IsColorProperty(Other->GetProperty()));
		TestFalse(TEXT("and takes no pick"), FDreamWidgetDetailPropertyExtensionHandler::WritePickedColor(Other, Picked));
	}
	return true;
}

#endif
