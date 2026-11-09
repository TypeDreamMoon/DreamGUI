// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"
#include "DetailCustomization/PropertyType/DreamGradientCustomization.h"
#include "Engine/World.h"
#include "Lifecycle/DreamLifecycleFixtures.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUISourceFile.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUITextWriteBack.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

/*
 * A rect block's gradients in the editor: a .dui written with the two-colour lines flushes as it is until the gradient is
 * written with stops, and then the gradient's line takes the place of the old ones; and the details panel shows a gradient
 * that has no stops as the two-colour one it draws, which its first edit writes as stops.
 */
namespace DreamRectBlockGradientEditorTestLocal
{
	struct FBuiltTree
	{
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		TStrongObjectPtr<UDreamWidgetTree> Tree;

		UDreamRectBlock* FindBlock(const TCHAR* InNodeId) const
		{
			UDreamRectBlock* Found = nullptr;
			if (Tree.IsValid())
			{
				Tree->ForEachWidget([InNodeId, &Found](UDreamWidget* InWidget)
				{
					if (Found == nullptr && IsValid(InWidget) && InWidget->GetDisplayName() == InNodeId)
					{
						Found = Cast<UDreamRectBlock>(InWidget->GetVisual());
					}
				});
			}
			return Found;
		}
	};

	FBuiltTree BuildTree(const FString& InText)
	{
		FBuiltTree Built;
		Built.Diagnostics.SourceName = TEXT("Gradient.dui");
		if (FDreamUISourceFile::Parse(InText, Built.Diagnostics.SourceName, Built.Ast, Built.Diagnostics))
		{
			TArray<FDreamWidgetPropertyBinding> Bindings;
			Built.Tree.Reset(FDreamUITextBuilder::Build(Built.Ast, GetTransientPackage(), Built.Diagnostics, Bindings));
		}
		return Built;
	}

	/** A rect block on a registered widget of InWorld. */
	UDreamRectBlock* MakeBlock(UWorld* InWorld, const TCHAR* InName)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient | RF_Transactional);
		Widget->SetDisplayName(InName);
		Widget->SetWidth(200.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		return Widget->CreateNewVisual<UDreamRectBlock>();
	}

	/** A file with a rect block written the two-colour way, one line per property, and one more property after them. */
	FString TwoColourFile()
	{
		return FString::Join(TArray<FString>{
			TEXT("Widget Root {"),
			TEXT("    RectBlock Card {"),
			TEXT("        AnchorData.SizeDelta = (200, 100)"),
			TEXT("        bEnableBodyGradient = true"),
			TEXT("        BodyGradientColor = #FF8000FF"),
			TEXT("        BodyGradientCenter = (0.25, 0.75)"),
			TEXT("        BodyGradientRotation = 30"),
			TEXT("        Color = #FFFFFFFF"),
			TEXT("    }"),
			TEXT("}")
		}, TEXT("\n"));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockGradientWriteBackTest,
	"DreamGUI.Designer.AGradientWrittenWithStopsTakesThePlaceOfItsTwoColourLines",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockGradientWriteBackTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientEditorTestLocal;
	const FString Source = TwoColourFile();
	FBuiltTree Live = BuildTree(Source);
	UDreamRectBlock* Block = Live.FindBlock(TEXT("Card"));
	if (!TestNotNull(TEXT("the file builds, with its rect block"), Block))
	{
		AddInfo(Live.Diagnostics.ToString());
		return false;
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	TestEqual(TEXT("the two-colour lines are read"), Block->GetBodyGradientColor(), FColor(255, 128, 0, 255));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	TestTrue(TEXT("its gradient has no stops of its own"), Block->GetBodyGradient().Stops.IsEmpty());
	TestEqual(TEXT("and draws two"), Block->GetEffectiveBodyGradient().Stops.Num(), 2);

	// Untouched, the file is left exactly as it was.
	FString Produced;
	FDreamUIDiagnosticBag Diagnostics;
	if (!TestTrue(TEXT("the write-back computes an answer"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}
	TestEqualSensitive(TEXT("an untouched two-colour file is not rewritten"), Produced, Source);

	// Its gradient written with stops -- what the first edit in the details panel or with a handle does.
	FDreamGradient Edited = Block->GetEffectiveBodyGradient();
	Edited.Stops.Insert(FDreamGradientStop(0.5f, FColor(0, 128, 255)), 1);
	Block->SetBodyGradient(Edited);
	Diagnostics = FDreamUIDiagnosticBag();
	if (!TestTrue(TEXT("the write-back computes an answer"), FDreamUITextWriteBack::ProduceText(Source, Live.Tree.Get(), Produced, Diagnostics)))
	{
		return false;
	}
	TestTrue(TEXT("the gradient's line is written"), Produced.Contains(TEXT("BodyGradient = \"radial-gradient(")));
	for (const TCHAR* Old : { TEXT("BodyGradientColor"), TEXT("BodyGradientCenter"), TEXT("BodyGradientRotation") })
	{
		TestFalse(*FString::Printf(TEXT("%s's line is gone"), Old), Produced.Contains(FString(Old) + TEXT(" =")));
	}
	TestTrue(TEXT("the switch and the lines around stay"), Produced.Contains(TEXT("bEnableBodyGradient = true")) && Produced.Contains(TEXT("Color = #FFFFFFFF")));
	if (!TestFalse(TEXT("and nothing is refused"), Diagnostics.HasErrors()))
	{
		AddInfo(Diagnostics.ToString());
	}

	// Read again, the file draws the same gradient, and a flush of it changes nothing more.
	FBuiltTree Again = BuildTree(Produced);
	UDreamRectBlock* AgainBlock = Again.FindBlock(TEXT("Card"));
	if (TestNotNull(TEXT("the new file builds"), AgainBlock))
	{
		TestTrue(TEXT("with the gradient as written"), AgainBlock->GetBodyGradient() == Edited);
		FString Twice;
		FDreamUIDiagnosticBag TwiceDiagnostics;
		FDreamUITextWriteBack::ProduceText(Produced, Again.Tree.Get(), Twice, TwiceDiagnostics);
		TestEqualSensitive(TEXT("which says it once and for all"), Twice, Produced);
	}
	AddInfo(Produced);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockGradientDetailsTest,
	"DreamGUI.Editor.GradientDetails.ARectBlocksTwoColourGradientIsShownAsItsStopsAndItsFirstEditWritesThem",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockGradientDetailsTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockGradientEditorTestLocal;
	// On a widget: a rect block's setters and its edit hooks tell its widget.
	DreamTests::Lifecycle::FScopedWorld World{ EWorldType::Editor };
	UDreamRectBlock* Block = MakeBlock(World.World, TEXT("Block"));
	UDreamRectBlock* Other = MakeBlock(World.World, TEXT("Other"));
	if (!TestTrue(TEXT("two rect blocks"), Block != nullptr && Other != nullptr))
	{
		return false;
	}
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Block->SetEnableBodyGradient(true);
	Block->SetBodyColor(FColor(0, 0, 255));
	Block->SetBodyGradientColor(FColor(255, 0, 0));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS

	FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
	TSharedRef<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
	Generator->SetObjects({ Block });
	TSharedPtr<IPropertyHandle> GradientHandle;
	TFunction<void(const TSharedRef<IDetailTreeNode>&)> Find = [&](const TSharedRef<IDetailTreeNode>& InNode)
	{
		if (const TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle())
		{
			if (!GradientHandle.IsValid() && Handle->GetProperty() != nullptr && Handle->GetProperty()->GetFName() == FName(TEXT("BodyGradient")))
			{
				GradientHandle = Handle;
			}
		}
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			Find(Child);
		}
	};
	for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
	{
		Find(Root);
	}
	if (!TestTrue(TEXT("the body gradient has a row"), GradientHandle.IsValid()))
	{
		return false;
	}

	// Shown as it is drawn: two stops, red to blue, though the property holds none.
	FDreamGradient Shown;
	TestTrue(TEXT("the row reads a gradient"), DreamGradientDetails::ReadDrawnGradient(GradientHandle.ToSharedRef(), Shown));
	TestTrue(TEXT("the two-colour gradient's two stops"), Shown.Stops.Num() == 2 && Shown.Stops[0].Color == FColor(255, 0, 0) && Shown.Stops[1].Color == FColor(0, 0, 255));
	TestTrue(TEXT("a radial one"), Shown.Type == EDreamPaintType::Radial);

	// The first edit -- the type picker's -- writes them, with the edit.
	TestTrue(TEXT("the edit is made"), DreamGradientDetails::EditGradients(GradientHandle.ToSharedRef(), INVTEXT("Change Gradient Type"),
		[](FDreamGradient& InOutGradient) { InOutGradient.Type = EDreamPaintType::Diamond; }));
	const FDreamGradient& Written = Block->GetBodyGradient();
	TestTrue(TEXT("the property now holds the two stops"), Written.Stops.Num() == 2 && Written.Stops[0].Color == FColor(255, 0, 0));
	TestTrue(TEXT("and the new type"), Written.Type == EDreamPaintType::Diamond);

	// A field edited on its own, by the property system, writes them too.
	PRAGMA_DISABLE_DEPRECATION_WARNINGS
	Other->SetEnableBodyGradient(true);
	Other->SetBodyGradientColor(FColor(0, 255, 0));
	PRAGMA_ENABLE_DEPRECATION_WARNINGS
	FProperty* GradientProperty = FindFProperty<FProperty>(UDreamRectBlock::StaticClass(), FName(TEXT("BodyGradient")));
	FProperty* AngleProperty = FindFProperty<FProperty>(FDreamGradient::StaticStruct(), GET_MEMBER_NAME_CHECKED(FDreamGradient, Angle));
	FEditPropertyChain Chain;
	Chain.AddHead(GradientProperty);
	Chain.AddTail(AngleProperty);
	Chain.SetActiveMemberPropertyNode(GradientProperty);
	Chain.SetActivePropertyNode(AngleProperty);
	Other->PreEditChange(Chain);
	FDreamGradient& Raw = *GradientProperty->ContainerPtrToValuePtr<FDreamGradient>(Other);
	Raw.Angle = 45.0f;
	FPropertyChangedEvent Event(AngleProperty, EPropertyChangeType::ValueSet);
	Event.SetActiveMemberProperty(GradientProperty);
	FPropertyChangedChainEvent ChainEvent(Chain, Event);
	Other->PostEditChangeChainProperty(ChainEvent);
	TestTrue(TEXT("its two stops are written"), Other->GetBodyGradient().Stops.Num() == 2 && Other->GetBodyGradient().Stops[0].Color == FColor(0, 255, 0));
	TestEqual(TEXT("with the field as it was set"), Other->GetBodyGradient().Angle, 45.0f);
	TestTrue(TEXT("and the rest of the two-colour gradient"), Other->GetBodyGradient().Type == EDreamPaintType::Radial
		&& Other->GetBodyGradient().Interpolation == EDreamPaintInterpolation::Linear);
	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
