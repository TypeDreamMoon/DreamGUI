// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUILayerEffects.h"
#include "Driver/Designer/DreamDesignerDriver.h"
#include "Driver/Designer/Tests/DreamDesignerTestFixture.h"
#include "DreamUIControlRegistry.h"
#include "DreamUIEditorTools.h"
#include "Engine/World.h"
#include "Extensions/DreamMaskTransition.h"
#include "Extensions/DreamPath.h"
#include "Extensions/Effects/DreamAdjustmentLayer.h"
#include "Extensions/Effects/DreamGlass.h"
#include "Extensions/Effects/DreamHoloFoil.h"
#include "Extensions/Effects/DreamPointerLight.h"
#include "Extensions/Effects/DreamStylizeEffect.h"
#include "Extensions/Effects/DreamTiltCard.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "MeshModifier/DreamMeshModifierJelly.h"
#include "MeshModifier/DreamMeshModifierTextOnPath.h"
#include "MeshModifier/DreamMeshModifierWarp.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

/*
 * The editor's face on the layer effects: the Layer category at the top of a widget's details, the list of a widget's
 * effects with the switch of each, and the palette rows that put them down already showing.
 */
namespace DreamLayerEffectsEditorTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	void CollectUnder(const TSharedRef<IDetailTreeNode>& InNode, TSet<FName>& OutProperties)
	{
		if (TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle())
		{
			if (const FProperty* Property = Handle->GetProperty())
			{
				OutProperties.Add(Property->GetFName());
			}
		}
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			CollectUnder(Child, OutProperties);
		}
	}

	/** The properties under each top-level category of InObject's details, by category name, and the categories in order. */
	TMap<FName, TSet<FName>> RowsByCategory(UObject* InObject, TArray<FName>& OutOrder)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TSharedPtr<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetObjects({ InObject });
		TMap<FName, TSet<FName>> Out;
		for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
		{
			OutOrder.Add(Root->GetNodeName());
			CollectUnder(Root, Out.FindOrAdd(Root->GetNodeName()));
		}
		return Out;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetLayerCategoryTest,
	"DreamGUI.Editor.WidgetDetails.TheLayerCategoryComesFirstAndHoldsOpacityBlendModeClippingBelowAndTheMask",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget with a rect block: its details open on the Layer category, above the slot and the layout, holding the opacity,
 * the visual's blend mode, clipping to the layer below and the mask. None of the four is anywhere else as well -- the
 * opacity used to be under Appearance, the mask and clipping below under Appearance > Clipping, the blend mode among the
 * visual's own rows.
 */
bool FDreamWidgetLayerCategoryTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerEffectsEditorTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
	Widget->CreateNewVisual<UDreamRectBlock>();
	TArray<FName> Order;
	const TMap<FName, TSet<FName>> Rows = RowsByCategory(Widget, Order);
	const TSet<FName>* Layer = Rows.Find(TEXT("DreamLayer"));
	if (!TestNotNull(TEXT("The widget's details have a Layer category"), Layer))
	{
		return false;
	}
	TestEqual(TEXT("...and it is the first"), Order.IndexOfByKey(FName(TEXT("DreamLayer"))), 0);
	for (const TCHAR* Name : { TEXT("RenderOpacity"), TEXT("BlendMode"), TEXT("bClipToBelow"), TEXT("ClipMask"), TEXT("Shape") })
	{
		TestTrue(*FString::Printf(TEXT("The Layer category holds %s"), Name), Layer->Contains(FName(Name)));
	}
	for (const TPair<FName, TSet<FName>>& Pair : Rows)
	{
		if (Pair.Key == FName(TEXT("DreamLayer")))
		{
			continue;
		}
		for (const TCHAR* Name : { TEXT("RenderOpacity"), TEXT("bClipToBelow"), TEXT("ClipMask") })
		{
			TestFalse(*FString::Printf(TEXT("%s is not in %s as well"), Name, *Pair.Key.ToString()), Pair.Value.Contains(FName(Name)));
		}
	}
	// The visual's own rows, laid out on the visual: the blend mode is the Layer category's now.
	TArray<FName> VisualOrder;
	const TMap<FName, TSet<FName>> VisualRows = RowsByCategory(Widget->GetVisual(), VisualOrder);
	for (const TPair<FName, TSet<FName>>& Pair : VisualRows)
	{
		TestFalse(*FString::Printf(TEXT("The rect block's %s category leaves its blend mode to the Layer category"), *Pair.Key.ToString()), Pair.Value.Contains(TEXT("BlendMode")));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerEffectsListTest,
	"DreamGUI.Editor.LayerEffects.EveryEffectOnAWidgetIsListedByKindAndItsSwitchTurnsItOff",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * A widget carrying one of everything: each is listed under the kind its hierarchy mark shows, and each that has a switch
 * reads on, is turned off through it, and reads off -- with the component's own state saying the same thing, since the
 * switch is the component's own property.
 */
bool FDreamLayerEffectsListTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerEffectsEditorTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
	UDreamRectBlock* Block = Widget->CreateNewVisual<UDreamRectBlock>();
	Block->SetBlendMode(EDreamUIBlendMode::Multiply);
	Widget->SetClipping(EDreamWidgetClipping::ClipToBounds);
	FDreamClipMask Mask;
	Mask.Shape = EDreamClipMaskShape::Ellipse;
	Widget->SetClipMask(Mask);
	Widget->SetClipToBelow(true);
	Widget->AddComponent<UDreamLayerStyle>();
	Widget->AddComponent<UDreamLayerGroup>();
	Widget->AddComponent<UDreamShapeBlendGroup>();
	Widget->AddComponent<UDreamMaskTransition>();
	Widget->AddComponent<UDreamHoloFoil>();
	Widget->AddComponent<UDreamPointerLight>();
	Widget->AddComponent<UDreamTiltCard>();
	Block->AddMeshModifier(UDreamMeshModifierWarp::StaticClass());

	TArray<FDreamUILayerEffect> Effects;
	DreamUILayerEffects::Collect(Widget, Effects);
	auto Find = [&Effects](const UClass* InClass) -> const FDreamUILayerEffect*
	{
		return Effects.FindByPredicate([InClass](const FDreamUILayerEffect& E) { return E.Object.IsValid() && E.Object->IsA(InClass); });
	};
	struct FExpected { const UClass* Class; EDreamUILayerEffectKind Kind; };
	for (const FExpected& Expected : {
		FExpected{ UDreamLayerStyle::StaticClass(), EDreamUILayerEffectKind::Style },
		FExpected{ UDreamLayerGroup::StaticClass(), EDreamUILayerEffectKind::Group },
		FExpected{ UDreamShapeBlendGroup::StaticClass(), EDreamUILayerEffectKind::Group },
		FExpected{ UDreamMaskTransition::StaticClass(), EDreamUILayerEffectKind::Mask },
		FExpected{ UDreamHoloFoil::StaticClass(), EDreamUILayerEffectKind::Effect },
		FExpected{ UDreamPointerLight::StaticClass(), EDreamUILayerEffectKind::Effect },
		FExpected{ UDreamTiltCard::StaticClass(), EDreamUILayerEffectKind::Effect },
		FExpected{ UDreamMeshModifierWarp::StaticClass(), EDreamUILayerEffectKind::Warp } })
	{
		const FDreamUILayerEffect* Effect = Find(Expected.Class);
		if (!TestNotNull(*FString::Printf(TEXT("%s is listed"), *Expected.Class->GetName()), Effect))
		{
			continue;
		}
		TestEqual(*FString::Printf(TEXT("%s is marked as its kind"), *Expected.Class->GetName()), static_cast<int32>(Effect->Kind), static_cast<int32>(Expected.Kind));
		TestTrue(*FString::Printf(TEXT("%s has a switch"), *Expected.Class->GetName()), DreamUILayerEffects::HasSwitch(*Effect));
		TestTrue(*FString::Printf(TEXT("%s reads on"), *Expected.Class->GetName()), DreamUILayerEffects::IsOn(*Effect));
		TestTrue(*FString::Printf(TEXT("%s is turned off through its switch"), *Expected.Class->GetName()), DreamUILayerEffects::SetOn(*Effect, false));
		TestFalse(*FString::Printf(TEXT("%s reads off"), *Expected.Class->GetName()), DreamUILayerEffects::IsOn(*Effect));
	}
	// The components say the same as the list.
	TestFalse(TEXT("The layer group is off"), Widget->GetComponent<UDreamLayerGroup>()->IsEnabled());
	TestFalse(TEXT("The mask transition is off"), Widget->GetComponent<UDreamMaskTransition>()->IsEnabled());
	TestFalse(TEXT("The holo foil is off"), Widget->GetComponent<UDreamHoloFoil>()->IsEnabled());
	TestFalse(TEXT("The pointer light is off"), Widget->GetComponent<UDreamPointerLight>()->IsEnabled());
	TestFalse(TEXT("The tilt card is off"), Widget->GetComponent<UDreamTiltCard>()->IsEnabled());
	TestFalse(TEXT("The style's own switch is off"), Widget->GetComponent<UDreamLayerStyle>()->GetStyle().bEnabled);
	TestFalse(TEXT("The shape blend is off"), Widget->GetComponent<UDreamShapeBlendGroup>()->GetBlendEnabled());
	// What is a property of the widget or its visual is listed, and has no switch: the effect is its being set.
	int32 Masks = 0;
	bool bBlend = false;
	for (const FDreamUILayerEffect& Effect : Effects)
	{
		Masks += Effect.Kind == EDreamUILayerEffectKind::Mask && Effect.Object.Get() == Widget ? 1 : 0;
		bBlend |= Effect.Kind == EDreamUILayerEffectKind::Blend;
		if (Effect.Object.Get() == Widget || Effect.Kind == EDreamUILayerEffectKind::Blend)
		{
			TestFalse(*FString::Printf(TEXT("%s has no switch"), *Effect.Name.ToString()), DreamUILayerEffects::HasSwitch(Effect));
		}
	}
	TestEqual(TEXT("The mask and clipping to the layer below are both listed"), Masks, 2);
	TestTrue(TEXT("The blend mode is listed"), bBlend);

	// A visual that is an effect has its switch on the visual.
	UDreamWidget* GlassWidget = NewObject<UDreamWidget>(TestWorld.World);
	UDreamGlass* Glass = GlassWidget->CreateNewVisual<UDreamGlass>();
	TArray<FDreamUILayerEffect> GlassEffects;
	DreamUILayerEffects::Collect(GlassWidget, GlassEffects);
	if (TestEqual(TEXT("The glass widget lists one effect"), GlassEffects.Num(), 1))
	{
		TestTrue(TEXT("...the glass, turned off through its switch"), DreamUILayerEffects::SetOn(GlassEffects[0], false));
		TestFalse(TEXT("...which the glass reports"), Glass->IsEnabled());
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamPaletteLayerEffectsTest,
	"DreamGUI.Designer.Palette.EveryLayerEffectArrivesWithWhatMakesItShow",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/*
 * Each layer effect's palette row, created under a designer's root as a drop creates it: the widget has the effect, on
 * what it changes, set up to be seen -- an adjustment layer with an adjustment, a style with effects in it, a group with
 * something in it, a transition part way through.
 */
bool FDreamPaletteLayerEffectsTest::RunTest(const FString& Parameters)
{
	using namespace DreamTests;
	FScopedDesignerSession Session(TEXT("PaletteLayerEffects"), /*bGiveRootAPanel*/true);
	if (!TestTrue(FString::Printf(TEXT("The designer came up (%s)"), *Session.GetFailure()), Session.IsReady()))
	{
		return false;
	}
	FDreamDesignerDriver& Driver = Session.GetDriver();
	auto Create = [this, &Driver](const TCHAR* InRow) -> UDreamWidget*
	{
		UDreamWidget* Root = Driver.PreviewRoot();
		UDreamWidget* Created = FDreamUIEditorTools::CreateRegisteredControlAndReturn([Root]() { return Root; }, FName(InRow));
		TestNotNull(*FString::Printf(TEXT("The %s row makes a widget"), InRow), Created);
		return Created;
	};
	if (UDreamWidget* Made = Create(TEXT("LayerStyle")))
	{
		const UDreamLayerStyle* Style = Made->GetComponent<UDreamLayerStyle>();
		TestTrue(TEXT("Layer Style comes on a rect block with effects turned on"), Style != nullptr && Made->GetVisual() != nullptr && Style->GetEffectiveStyle().IsActive());
	}
	if (UDreamWidget* Made = Create(TEXT("LayerGroup")))
	{
		TestNotNull(TEXT("Layer Group has its group"), Made->GetComponent<UDreamLayerGroup>());
		TestEqual(TEXT("...and two layers in it"), Made->GetChildrenCount(), 2);
	}
	if (UDreamWidget* Made = Create(TEXT("ShapeBlendGroup")))
	{
		TestNotNull(TEXT("Shape Blend Group has its group"), Made->GetComponent<UDreamShapeBlendGroup>());
		TestEqual(TEXT("...and two shapes in it"), Made->GetChildrenCount(), 2);
	}
	if (UDreamWidget* Made = Create(TEXT("MaskTransition")))
	{
		const UDreamMaskTransition* Transition = Made->GetComponent<UDreamMaskTransition>();
		TestTrue(TEXT("Mask Transition comes part way through"), Transition != nullptr && Transition->GetProgress() < 1.0f);
	}
	for (const TCHAR* Row : { TEXT("HoloFoil"), TEXT("PointerLight"), TEXT("TiltCard"), TEXT("JellyModifier") })
	{
		if (UDreamWidget* Made = Create(Row))
		{
			TestTrue(FString::Printf(TEXT("%s comes on a rect block"), Row), Cast<UDreamRectBlock>(Made->GetVisual()) != nullptr);
		}
	}
	for (const TCHAR* Row : { TEXT("WarpModifier"), TEXT("TextOnPathModifier") })
	{
		if (UDreamWidget* Made = Create(Row))
		{
			const UDreamText* Text = Cast<UDreamText>(Made->GetVisual());
			TestTrue(FString::Printf(TEXT("%s comes on a text with something to bend"), Row), Text != nullptr && !Text->GetText().IsEmpty());
		}
	}
	if (UDreamWidget* Made = Create(TEXT("AdjustmentLayer")))
	{
		const UDreamAdjustmentLayer* Layer = Cast<UDreamAdjustmentLayer>(Made->GetVisual());
		TestTrue(TEXT("Adjustment Layer comes with an adjustment"), Layer != nullptr && Layer->GetAdjustments().Num() > 0);
	}
	if (UDreamWidget* Made = Create(TEXT("Stylize")))
	{
		const UDreamStylizeEffect* Effect = Cast<UDreamStylizeEffect>(Made->GetVisual());
		TestTrue(TEXT("Stylize comes with a layer"), Effect != nullptr && Effect->GetLayers().Num() > 0);
	}
	if (UDreamWidget* Made = Create(TEXT("Glass")))
	{
		TestTrue(TEXT("Glass is a glass"), Cast<UDreamGlass>(Made->GetVisual()) != nullptr);
	}
	if (UDreamWidget* Made = Create(TEXT("Path")))
	{
		TestTrue(TEXT("Path is a path"), Cast<UDreamPath>(Made->GetVisual()) != nullptr);
	}
	return true;
}

#endif
