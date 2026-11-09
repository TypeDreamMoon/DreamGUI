// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamShapeBlendGroup.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/SDreamWidgetComponentEditor.h"
#include "DetailCustomization/DreamDetailsLayout.h"
#include "DreamNiagaraSystem.h"
#include "DreamNiagaraTrigger.h"
#include "DreamUIComponentSupport.h"
#include "DreamUIControlRegistry.h"
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
#include "Text/DreamUIReflectionPolicy.h"

/*
 * Whether each layer effect can be found and edited in the editor at all.
 *
 * For every effect class: every property it declares editable has a row in the details panel the designer shows for it --
 * a component's own panel, or the widget's Visual category for a visual -- including the fields of the structs it holds;
 * it can be put on a widget from the palette, and a component from Add Component as well; and nothing it lets a .dui file
 * write is missing from the panel. A row an EditCondition hides until its switch is on still counts as a row: it shows
 * the moment the switch does.
 */
namespace DreamLayerEffectsDetailsAuditTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	enum class EKind : uint8 { Visual, Component, Modifier };

	struct FSubject
	{
		const TCHAR* Name;
		UClass* Class;
		EKind Kind;
		/** The visual the widget needs for the component to be supported on it, when it needs one. */
		UClass* HostVisual = nullptr;
	};

	TArray<FSubject> Subjects()
	{
		return {
			{ TEXT("Layer Style"), UDreamLayerStyle::StaticClass(), EKind::Component, UDreamRectBlock::StaticClass() },
			{ TEXT("Layer Group"), UDreamLayerGroup::StaticClass(), EKind::Component },
			{ TEXT("Shape Blend Group"), UDreamShapeBlendGroup::StaticClass(), EKind::Component },
			{ TEXT("Mask Transition"), UDreamMaskTransition::StaticClass(), EKind::Component, UDreamRectBlock::StaticClass() },
			{ TEXT("Holo Foil"), UDreamHoloFoil::StaticClass(), EKind::Component, UDreamRectBlock::StaticClass() },
			{ TEXT("Pointer Light"), UDreamPointerLight::StaticClass(), EKind::Component, UDreamRectBlock::StaticClass() },
			{ TEXT("Tilt Card"), UDreamTiltCard::StaticClass(), EKind::Component, UDreamRectBlock::StaticClass() },
			{ TEXT("Niagara Trigger"), UDreamNiagaraTrigger::StaticClass(), EKind::Component },
			{ TEXT("Warp"), UDreamMeshModifierWarp::StaticClass(), EKind::Modifier, UDreamRectBlock::StaticClass() },
			{ TEXT("Jelly"), UDreamMeshModifierJelly::StaticClass(), EKind::Modifier, UDreamRectBlock::StaticClass() },
			{ TEXT("Text On Path"), UDreamMeshModifierTextOnPath::StaticClass(), EKind::Modifier, UDreamText::StaticClass() },
			{ TEXT("Glass"), UDreamGlass::StaticClass(), EKind::Visual },
			{ TEXT("Adjustment Layer"), UDreamAdjustmentLayer::StaticClass(), EKind::Visual },
			{ TEXT("Stylize"), UDreamStylizeEffect::StaticClass(), EKind::Visual },
			{ TEXT("Path"), UDreamPath::StaticClass(), EKind::Visual },
			{ TEXT("Niagara System"), UDreamNiagaraSystem::StaticClass(), EKind::Visual },
		};
	}

	void CollectShown(const TSharedRef<IDetailTreeNode>& InNode, TSet<const FProperty*>& OutShown, TSet<FName>& OutGroupNames)
	{
		if (TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle())
		{
			if (const FProperty* Property = Handle->GetProperty())
			{
				OutShown.Add(Property);
			}
		}
		// A group whose header is a property (IDetailGroup::HeaderProperty) is named after it and lists no node for it.
		OutGroupNames.Add(InNode->GetNodeName());
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			CollectShown(Child, OutShown, OutGroupNames);
		}
	}

	TSet<const FProperty*> ShownFor(UObject* InObject, TSet<FName>* OutGroupNames = nullptr)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		FPropertyRowGeneratorArgs Args;
		TSharedPtr<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(Args);
		Generator->SetObjects({ InObject });
		TSet<const FProperty*> Shown;
		TSet<FName> GroupNames;
		for (const TSharedRef<IDetailTreeNode>& Root : Generator->GetRootTreeNodes())
		{
			CollectShown(Root, Shown, GroupNames);
		}
		if (OutGroupNames)
		{
			*OutGroupNames = MoveTemp(GroupNames);
		}
		return Shown;
	}

	bool IsEditable(const FProperty* InProperty)
	{
		return InProperty->HasAnyPropertyFlags(CPF_Edit) && !InProperty->HasAnyPropertyFlags(CPF_Deprecated)
#if WITH_EDITORONLY_DATA
			&& !InProperty->HasMetaData(TEXT("HideInDetailPanel"))
#endif
			;
	}

	/** Every editable property of InStruct the panel ought to have a row for, and the fields of the plain structs among them. */
	void CollectExpected(const UStruct* InStruct, const UStruct* InStopAt, const FString& InPrefix, int32 InDepth,
		TArray<TPair<FString, const FProperty*>>& OutExpected)
	{
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			const FProperty* Property = *It;
			if (InStopAt != nullptr && Property->GetOwnerStruct() != nullptr && InStopAt->IsChildOf(Property->GetOwnerStruct()) && InDepth == 0)
			{
				// Declared by a base the audit does not own (UDreamUIBehaviour, UDreamVisual and their parents).
				continue;
			}
			if (!IsEditable(Property))
			{
				continue;
			}
#if WITH_EDITORONLY_DATA
			// A toggle drawn inline on the row whose edit condition it is: a check box, not a row of its own.
			if (Property->HasMetaData(TEXT("InlineEditConditionToggle")))
			{
				continue;
			}
#endif
			const FString Path = InPrefix.IsEmpty() ? Property->GetName() : InPrefix + TEXT(".") + Property->GetName();
			OutExpected.Emplace(Path, Property);
			const FStructProperty* AsStruct = CastField<FStructProperty>(Property);
			// The fields of a struct with a layout of its own (a brush, a gradient) are that layout's to show or not.
			FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			if (AsStruct && InDepth < 3 && Property->ArrayDim == 1 && !PropertyEditor.IsCustomizedStruct(AsStruct->Struct, FCustomPropertyTypeLayoutMap()))
			{
				CollectExpected(AsStruct->Struct, nullptr, Path, InDepth + 1, OutExpected);
			}
		}
	}

	FString EditConditionOf(const FProperty* InProperty)
	{
#if WITH_EDITORONLY_DATA
		const FString Condition = InProperty->GetMetaData(TEXT("EditCondition"));
		if (!Condition.IsEmpty())
		{
			return FString::Printf(TEXT("%s%s"), *Condition, InProperty->HasMetaData(TEXT("EditConditionHides")) ? TEXT(" (hidden)") : TEXT(" (greyed)"));
		}
#endif
		return FString();
	}

	UClass* BaseOf(EKind InKind)
	{
		return InKind == EKind::Visual ? UDreamVisual::StaticClass() : UDreamUIBehaviour::StaticClass();
	}

	bool InPalette(const FSubject& InSubject, FString& OutRow)
	{
		for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
		{
			const bool bMatch = (InSubject.Kind == EKind::Visual && Descriptor.VisualClass.Get() == InSubject.Class)
				|| (InSubject.Kind == EKind::Component && Descriptor.BehaviourClass.Get() == InSubject.Class)
				|| (InSubject.Kind == EKind::Modifier && Descriptor.MeshModifierClass.Get() == InSubject.Class);
			if (bMatch)
			{
				OutRow = FString::Printf(TEXT("%s / %s"), *Descriptor.Category.ToString(), *Descriptor.DisplayName.ToString());
				return true;
			}
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamLayerEffectsDetailsAuditTest,
	"DreamGUI.Editor.LayerEffects.EveryEffectHasItsRowsAPlaceInThePaletteAndNothingOnlyTextCanWrite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamLayerEffectsDetailsAuditTest::RunTest(const FString& Parameters)
{
	using namespace DreamLayerEffectsDetailsAuditTestLocal;
	FScopedTestWorld TestWorld;
	for (const FSubject& Subject : Subjects())
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
		Widget->SetWidth(200.0f);
		Widget->SetHeight(100.0f);
		UObject* Object = nullptr;
		if (Subject.Kind == EKind::Visual)
		{
			Object = Widget->CreateNewVisual(Subject.Class);
		}
		else
		{
			if (Subject.HostVisual)
			{
				Widget->CreateNewVisual(Subject.HostVisual);
			}
			Object = Widget->AddComponent(Subject.Class);
		}
		if (!TestNotNull(*FString::Printf(TEXT("%s was made on a widget"), Subject.Name), Object))
		{
			continue;
		}

		// Rows: a component in its own panel; a visual in its widget's Visual category, which the details view lays out from
		// the visual's own class customizations. A row generator has no details view behind it and lays out no object added
		// to a category from outside (FDetailPropertyRow asks for the view to make that layout), so the visual's rows are
		// generated on the visual itself, which is the same layout.
		TSet<FName> GroupNames;
		const TSet<const FProperty*> Shown = ShownFor(Object, &GroupNames);
		TArray<TPair<FString, const FProperty*>> Expected;
		CollectExpected(Subject.Class, BaseOf(Subject.Kind), FString(), 0, Expected);
		TArray<FString> Missing, Conditional;
		for (const TPair<FString, const FProperty*>& Pair : Expected)
		{
			// A visual's Layer rows (its blend mode) are the widget's Layer section's, not its own.
			if (FDreamDetailsLayout::IsDrawnByHost(Subject.Class, DreamPropertyMeta::GetSection(Pair.Value)))
			{
				continue;
			}
			if (!Shown.Contains(Pair.Value) && !(Pair.Key == Pair.Value->GetName() && GroupNames.Contains(Pair.Value->GetFName())))
			{
				Missing.Add(Pair.Key);
			}
			const FString Condition = EditConditionOf(Pair.Value);
			if (!Condition.IsEmpty() && !Pair.Key.Contains(TEXT(".")))
			{
				Conditional.Add(FString::Printf(TEXT("%s <- %s"), *Pair.Key, *Condition));
			}
		}
		AddInfo(FString::Printf(TEXT("[%s] %d of %d editable properties have a row%s%s"), Subject.Name, Expected.Num() - Missing.Num(), Expected.Num(),
			Missing.Num() ? *FString::Printf(TEXT("; missing: %s"), *FString::Join(Missing, TEXT(", "))) : TEXT(""),
			Conditional.Num() ? *FString::Printf(TEXT("; until a switch is on: %s"), *FString::Join(Conditional, TEXT("; "))) : TEXT("")));
		TestEqual(*FString::Printf(TEXT("Every editable property of %s has a row in its details panel"), Subject.Name), Missing.Num(), 0);

		// What only a .dui file can write: settable from text, with no row to set it from.
		TArray<FString> TextOnly;
		for (TFieldIterator<FProperty> It(Subject.Class); It; ++It)
		{
			if (It->GetOwnerStruct() != nullptr && BaseOf(Subject.Kind)->IsChildOf(It->GetOwnerStruct()))
			{
				continue;
			}
			if (DreamUIReflection::IsSweepRoot(*It) && !It->HasAnyPropertyFlags(CPF_Edit))
			{
				TextOnly.Add(It->GetName());
			}
		}
		if (TextOnly.Num())
		{
			AddInfo(FString::Printf(TEXT("[%s] only a .dui can write: %s"), Subject.Name, *FString::Join(TextOnly, TEXT(", "))));
		}
		TestEqual(*FString::Printf(TEXT("Nothing on %s is writable from a .dui and missing from the panel"), Subject.Name), TextOnly.Num(), 0);

		// The ways onto a widget.
		FString Row;
		const bool bInPalette = InPalette(Subject, Row);
		FText Reason;
		const bool bAddComponent = Subject.Kind != EKind::Visual
			&& FDreamWidgetComponentClassFilter::IsComponentClassAllowed(Subject.Class)
			&& FDreamUIComponentSupport::Get().IsSupported(Subject.Class, Widget, &Reason);
		AddInfo(FString::Printf(TEXT("[%s] palette: %s; Add Component: %s"), Subject.Name, bInPalette ? *Row : TEXT("no"),
			Subject.Kind == EKind::Visual ? TEXT("n/a (a visual)") : bAddComponent ? TEXT("yes") : *FString::Printf(TEXT("no (%s)"), *Reason.ToString())));
		TestTrue(*FString::Printf(TEXT("%s is in the palette"), Subject.Name), bInPalette);
		if (Subject.Kind != EKind::Visual)
		{
			TestTrue(*FString::Printf(TEXT("%s is offered by Add Component on a widget it works on"), Subject.Name), bAddComponent);
		}
	}

	// The widget's own layer rows: the mask and clipping to the layer below.
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
		const TSet<const FProperty*> Shown = ShownFor(Widget);
		for (const TCHAR* Name : { TEXT("ClipMask"), TEXT("bClipToBelow"), TEXT("RenderOpacity") })
		{
			const FProperty* Property = FindFProperty<FProperty>(UDreamWidget::StaticClass(), Name);
			if (TestNotNull(*FString::Printf(TEXT("UDreamWidget declares %s"), Name), Property))
			{
				TestTrue(*FString::Printf(TEXT("The widget's %s has a row"), Name), Shown.Contains(Property));
			}
		}
	}
	return true;
}

#endif
