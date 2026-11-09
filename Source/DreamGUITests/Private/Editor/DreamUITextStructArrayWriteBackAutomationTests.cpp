// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Text/DreamUITextWriteBack.h"

#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetTree.h"

#include "UObject/StrongObjectPtr.h"

/*
 * A list of structs -- a layer style's drop shadows -- written back the way a `.dui` reads one: each element a quoted string
 * of its fields that differ from the struct's defaults, `Style.DropShadows = ["(Angle=45,Distance=8)"]`. Written, it reads
 * back as the same list; and once the file says it, writing again changes nothing.
 */
namespace DreamUITextStructArrayWriteBackTestLocal
{
	FString Source()
	{
		return FString::Join(TArray<FString>{
			TEXT("Widget Root {"),
			TEXT("    RectBlock Card {"),
			TEXT("        + DreamLayerStyle { Style.OuterGlow.bEnabled = true }"),
			TEXT("    }"),
			TEXT("}")
		}, TEXT("\n"));
	}

	UDreamLayerStyle* FindStyle(UDreamWidgetTree* InTree)
	{
		UDreamLayerStyle* Found = nullptr;
		if (InTree != nullptr)
		{
			InTree->ForEachWidget([&Found](UDreamWidget* Widget)
			{
				for (UDreamUIBehaviour* Behaviour : Widget->GetAllComponents())
				{
					if (UDreamLayerStyle* Style = Cast<UDreamLayerStyle>(Behaviour))
					{
						Found = Found != nullptr ? Found : Style;
					}
				}
			});
		}
		return Found;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUITextStructArrayWriteBackTest,
	"DreamGUI.Text.AListOfStructsIsWrittenBackAsTheQuotedTextItIsReadFrom",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUITextStructArrayWriteBackTest::RunTest(const FString&)
{
	using namespace DreamUITextStructArrayWriteBackTestLocal;

	FDreamUIAst Ast;
	FDreamUIDiagnosticBag Diagnostics;
	TStrongObjectPtr<UDreamWidgetTree> Live(FDreamUITextWriteBack::BuildReferenceTree(Source(), Ast, Diagnostics));
	UDreamLayerStyle* Style = FindStyle(Live.Get());
	if (!TestNotNull(TEXT("the fixture builds a layer style"), Style)) return false;

	FDreamLayerEffects Effects = Style->GetStyle();
	FDreamStyleDropShadow& Shadow = Effects.DropShadows.AddDefaulted_GetRef();
	Shadow.Angle = 45.0f;
	Shadow.Distance = 8.0f;
	Effects.DropShadows.AddDefaulted();
	FProperty* StyleProperty = FindFProperty<FProperty>(UDreamLayerStyle::StaticClass(), TEXT("Style"));
	StyleProperty->CopyCompleteValue(StyleProperty->ContainerPtrToValuePtr<void>(Style), &Effects);

	FString Produced;
	FDreamUIDiagnosticBag ProduceDiagnostics;
	if (!TestTrue(TEXT("the write-back computes an answer"), FDreamUITextWriteBack::ProduceText(Source(), Live.Get(), Produced, ProduceDiagnostics))) return false;
	TestTrue(FString::Printf(TEXT("the list is written as quoted elements:\n%s"), *Produced), Produced.Contains(TEXT("Style.DropShadows = [\"(")));
	TestTrue(TEXT("an element at its defaults is an empty list of fields"), Produced.Contains(TEXT("\"()\"")));

	FDreamUIAst BackAst;
	FDreamUIDiagnosticBag BackDiagnostics;
	TStrongObjectPtr<UDreamWidgetTree> Back(FDreamUITextWriteBack::BuildReferenceTree(Produced, BackAst, BackDiagnostics));
	const UDreamLayerStyle* BackStyle = FindStyle(Back.Get());
	if (TestNotNull(TEXT("what was written builds"), BackStyle) && TestEqual(TEXT("with both shadows"), BackStyle->GetStyle().DropShadows.Num(), 2))
	{
		TestEqual(TEXT("the first one's angle"), BackStyle->GetStyle().DropShadows[0].Angle, 45.0f);
		TestEqual(TEXT("and its distance"), BackStyle->GetStyle().DropShadows[0].Distance, 8.0f);
		TestTrue(TEXT("the second one at its defaults"), FDreamStyleDropShadow::StaticStruct()->CompareScriptStruct(
			&BackStyle->GetStyle().DropShadows[1], &Style->GetStyle().DropShadows[1], PPF_None));
	}

	FString Again;
	FDreamUIDiagnosticBag AgainDiagnostics;
	TestTrue(TEXT("writing again computes an answer"), FDreamUITextWriteBack::ProduceText(Produced, Live.Get(), Again, AgainDiagnostics));
	TestEqual(TEXT("and changes nothing once the file says it"), Again, Produced);
	return true;
}

#endif
