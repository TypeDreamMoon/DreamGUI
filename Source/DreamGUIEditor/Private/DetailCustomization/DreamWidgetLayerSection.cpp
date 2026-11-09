// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamWidgetLayerSection.h"

#include "Core/Components/DreamLayerGroup.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Designer/DreamUILayerEffects.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/SDreamWidgetDesignerDetails.h"
#include "DetailCustomization/DreamDetailsLayout.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "DetailWidgetRow.h"
#include "Editor.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "IPropertyUtilities.h"
#include "PropertyHandle.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "TimerManager.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SHyperlink.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWidgetLayerSection"

namespace DreamWidgetLayerSectionLocal
{
	using FWidgets = TArray<TWeakObjectPtr<UDreamWidget>>;

	template<typename T>
	T* FirstComponentOf(const UDreamWidget* InWidget)
	{
		return IsValid(InWidget) ? InWidget->GetComponent<T>() : nullptr;
	}

	/** Each widget's first T, or nothing at all when one of them has none. */
	template<typename T>
	TArray<UObject*> ComponentOnEach(const FWidgets& InWidgets)
	{
		TArray<UObject*> Out;
		for (const TWeakObjectPtr<UDreamWidget>& Widget : InWidgets)
		{
			T* Component = FirstComponentOf<T>(Widget.Get());
			if (Component == nullptr)
			{
				return {};
			}
			Out.Add(Component);
		}
		return Out;
	}

	/**
	 * InWork on the next editor tick. What a row's click does to the panel the row is in -- rebuilding it, showing a component
	 * in its place -- has to wait until the click has returned, or it destroys the button whose click is still running.
	 */
	void Defer(TFunction<void()> InWork)
	{
		if (GEditor != nullptr)
		{
			GEditor->GetTimerManager()->SetTimerForNextTick(MoveTemp(InWork));
		}
		else
		{
			InWork();
		}
	}

	void ShowComponent(UDreamUIBehaviour* InComponent)
	{
		UDreamWidget* Widget = IsValid(InComponent) ? InComponent->GetWidget() : nullptr;
		if (FDreamWidgetBlueprintEditor* Designer = Widget != nullptr ? FDreamWidgetBlueprintEditor::FindDesignerForWidget(Widget) : nullptr)
		{
			if (const TSharedPtr<SDreamWidgetDesignerDetails> Details = Designer->GetDesignerDetailsWidget())
			{
				Details->ShowComponent(InComponent);
			}
		}
	}

	/** The switch of InWidget's component of class InClass, as an effect, when it has one. */
	TOptional<FDreamUILayerEffect> EffectOf(const UDreamWidget* InWidget, const UClass* InClass)
	{
		TArray<FDreamUILayerEffect> Effects;
		DreamUILayerEffects::Collect(InWidget, Effects);
		for (const FDreamUILayerEffect& Effect : Effects)
		{
			if (Effect.Object.IsValid() && Effect.Object->GetClass()->IsChildOf(InClass))
			{
				return Effect;
			}
		}
		return {};
	}

	/** Checked when every widget has an InClass that is on, unchecked when none does, undetermined between. */
	ECheckBoxState ComponentSwitchState(const FWidgets& InWidgets, const UClass* InClass)
	{
		int32 On = 0;
		int32 Count = 0;
		for (const TWeakObjectPtr<UDreamWidget>& Widget : InWidgets)
		{
			if (!Widget.IsValid())
			{
				continue;
			}
			++Count;
			const TOptional<FDreamUILayerEffect> Effect = EffectOf(Widget.Get(), InClass);
			On += Effect.IsSet() && DreamUILayerEffects::IsOn(Effect.GetValue()) ? 1 : 0;
		}
		return On == 0 ? ECheckBoxState::Unchecked : On == Count ? ECheckBoxState::Checked : ECheckBoxState::Undetermined;
	}

	/**
	 * Turn InClass on across the selection, adding one to each widget that has none -- as the component list's Add does, on
	 * the template, so that it is saved and written to the .dui -- or off on each that has one, keeping it and its settings.
	 */
	void SetComponentSwitch(const FWidgets& InWidgets, UClass* InClass, bool bInOn, TWeakPtr<IPropertyUtilities> InUtilities)
	{
		Defer([InWidgets, InClass, bInOn, InUtilities]()
		{
			const FScopedTransaction Transaction(bInOn ? LOCTEXT("TurnOnComponent", "Turn Layer Effect On") : LOCTEXT("TurnOffComponent", "Turn Layer Effect Off"));
			UDreamUIBehaviour* Added = nullptr;
			bool bStructural = false;
			for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : InWidgets)
			{
				UDreamWidget* Widget = WeakWidget.Get();
				if (!IsValid(Widget))
				{
					continue;
				}
				if (const TOptional<FDreamUILayerEffect> Effect = EffectOf(Widget, InClass))
				{
					DreamUILayerEffects::SetOn(Effect.GetValue(), bInOn);
				}
				else if (bInOn)
				{
					if (FDreamWidgetBlueprintEditor* Designer = FDreamWidgetBlueprintEditor::FindDesignerForWidget(Widget))
					{
						UClass* Classes[] = { InClass };
						Added = Designer->DesignerAddComponents(Widget, Classes);
						bStructural = true;
					}
				}
			}
			if (bStructural)
			{
				// One added on one widget opens in the panel, where its settings are; several leave the selection as it was.
				if (Added != nullptr && InWidgets.Num() == 1)
				{
					Defer([WeakAdded = TWeakObjectPtr<UDreamUIBehaviour>(Added)]() { ShowComponent(WeakAdded.Get()); });
				}
				else if (const TSharedPtr<IPropertyUtilities> Utilities = InUtilities.Pin())
				{
					Utilities->RequestForceRefresh();
				}
			}
		});
	}

	/** A switch for a component of InClass, and a button that opens the first one's own properties. */
	TSharedRef<SWidget> MakeComponentSwitch(const FWidgets& InWidgets, UClass* InClass, TWeakPtr<IPropertyUtilities> InUtilities, FText InOpenToolTip)
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SCheckBox)
				.IsChecked_Lambda([InWidgets, InClass]() { return ComponentSwitchState(InWidgets, InClass); })
				.OnCheckStateChanged_Lambda([InWidgets, InClass, InUtilities](ECheckBoxState InState)
				{
					SetComponentSwitch(InWidgets, InClass, InState == ECheckBoxState::Checked, InUtilities);
				})
				.ToolTipText(LOCTEXT("ComponentSwitchTip", "On adds one to a widget that has none. Off turns it off and keeps its settings."))
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(6.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.Text(LOCTEXT("EditComponent", "Edit..."))
				.ToolTipText(InOpenToolTip)
				.IsEnabled_Lambda([InWidgets, InClass]()
				{
					return InWidgets.Num() == 1 && InWidgets[0].IsValid() && InWidgets[0]->GetComponent(InClass) != nullptr;
				})
				.OnClicked_Lambda([InWidgets, InClass]()
				{
					if (InWidgets.Num() == 1 && InWidgets[0].IsValid())
					{
						TWeakObjectPtr<UDreamUIBehaviour> Component = InWidgets[0]->GetComponent(InClass);
						Defer([Component]() { ShowComponent(Component.Get()); });
					}
					return FReply::Handled();
				})
			];
	}

	FText ShapeName(EDreamClipMaskShape InShape)
	{
		return StaticEnum<EDreamClipMaskShape>()->GetDisplayNameTextByValue(static_cast<int64>(InShape));
	}

	/** What the quick shape button reads for the selection: None, a shape, or Multiple Values. */
	FText MaskShapeText(const FWidgets& InWidgets)
	{
		TOptional<FText> Found;
		for (const TWeakObjectPtr<UDreamWidget>& Widget : InWidgets)
		{
			if (!Widget.IsValid())
			{
				continue;
			}
			const FDreamClipMask& Mask = Widget->GetClipMask();
			const FText This = Mask.IsActive() ? ShapeName(Mask.Shape) : LOCTEXT("NoMask", "None");
			if (Found.IsSet() && !Found->EqualTo(This))
			{
				return NSLOCTEXT("PropertyEditor", "MultipleValues", "Multiple Values");
			}
			Found = This;
		}
		return Found.Get(LOCTEXT("NoMask", "None"));
	}
}

void DreamWidgetLayerSection::Build(IDetailLayoutBuilder& InDetailBuilder, const TArray<TWeakObjectPtr<UDreamWidget>>& InWidgets, TSharedPtr<IPropertyUtilities> InUtilities)
{
	using namespace DreamWidgetLayerSectionLocal;
	FDreamDetailsLayout Layout(InDetailBuilder);
	// Above the slot: what a layer is comes before where it sits.
	IDetailCategoryBuilder& Layer = Layout.Section(DreamPropertyMeta::ESection::Layer);
	const TWeakPtr<IPropertyUtilities> WeakUtilities = InUtilities;

	// What its tags put in the section, in their order: the widget's opacity, its visual's blend mode (where every selected
	// widget's visual is of one class), clipping to the layer below, the mask.
	TArray<UObject*> Visuals;
	for (const TWeakObjectPtr<UDreamWidget>& Widget : InWidgets)
	{
		UDreamVisual* Visual = Widget.IsValid() ? Widget->GetVisual() : nullptr;
		if (Visual == nullptr || (Visuals.Num() > 0 && Visuals[0]->GetClass() != Visual->GetClass()))
		{
			Visuals.Reset();
			break;
		}
		Visuals.Add(Visual);
	}
	const TMap<FName, IDetailPropertyRow*> Rows = Layout.PlaceSection(DreamPropertyMeta::ESection::Layer, Visuals);
	if (IDetailPropertyRow* const* Opacity = Rows.Find(TEXT("RenderOpacity")))
	{
		(*Opacity)->DisplayName(LOCTEXT("Opacity", "Opacity"));
	}
	if (IDetailPropertyRow* const* Blend = Rows.Find(TEXT("BlendMode")))
	{
		(*Blend)->DisplayName(LOCTEXT("BlendMode", "Blend Mode"));
	}
	if (IDetailPropertyRow* const* MaskRow = Rows.Find(TEXT("ClipMask")))
	{
		(*MaskRow)->DisplayName(LOCTEXT("MaskSettings", "Mask Settings"));
	}

	// The mask group's header: a shape to pick at once, over the whole mask.
	{
		const TSharedRef<IPropertyHandle> Mask = InDetailBuilder.GetProperty(TEXT("ClipMask"), UDreamWidget::StaticClass());
		const TSharedRef<IPropertyHandle> Clipping = InDetailBuilder.GetProperty(TEXT("Clipping"), UDreamWidget::StaticClass());
		const TSharedPtr<IPropertyHandle> Shape = Mask->GetChildHandle(TEXT("Shape"));
		IDetailGroup& MaskGroup = Layout.Group(DreamPropertyMeta::ESection::Layer, TEXT("Mask"));
		const FWidgets Widgets = InWidgets;
		auto PickShape = [Widgets, Mask, Clipping, Shape](TOptional<EDreamClipMaskShape> InShape)
		{
			const FScopedTransaction Transaction(LOCTEXT("PickMaskShape", "Set Mask Shape"));
			if (!InShape.IsSet())
			{
				Mask->ResetToDefault();
				return;
			}
			// A mask applies to a widget that clips to its bounds, so picking one turns that on where it is off, rather than
			// leaving the author to find the switch that greys the mask out.
			for (const TWeakObjectPtr<UDreamWidget>& Widget : Widgets)
			{
				const EDreamWidgetClipping Current = Widget.IsValid() ? Widget->GetClipping() : EDreamWidgetClipping::ClipToBounds;
				if (Current != EDreamWidgetClipping::ClipToBounds && Current != EDreamWidgetClipping::ClipToBoundsWithoutIntersecting)
				{
					Clipping->SetValueFromFormattedString(TEXT("ClipToBounds"));
					break;
				}
			}
			if (Shape.IsValid())
			{
				Shape->SetValueFromFormattedString(StaticEnum<EDreamClipMaskShape>()->GetNameStringByValue(static_cast<int64>(InShape.GetValue())));
			}
		};
		MaskGroup.HeaderRow()
			.NameContent()
			[
				SNew(STextBlock)
				.Text(LOCTEXT("Mask", "Mask"))
				.ToolTipText(LOCTEXT("MaskTip", "A shape this widget and everything under it are cut to, with a feathered edge, a texture or a pattern. Picking one makes the widget clip to its bounds."))
				.Font(IDetailLayoutBuilder::GetDetailFont())
			]
			.ValueContent()
			.MinDesiredWidth(160.0f)
			[
				SNew(SComboButton)
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text_Lambda([Widgets]() { return MaskShapeText(Widgets); })
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.OnGetMenuContent_Lambda([PickShape]()
				{
					FMenuBuilder Menu(true, nullptr);
					Menu.AddMenuEntry(LOCTEXT("NoMask", "None"), LOCTEXT("NoMaskTip", "No mask: the mask put back to its defaults."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([PickShape]() { PickShape(TOptional<EDreamClipMaskShape>()); })));
					const UEnum* Enum = StaticEnum<EDreamClipMaskShape>();
					for (int32 Index = 0; Index < Enum->NumEnums() - 1; ++Index)
					{
						if (Enum->HasMetaData(TEXT("Hidden"), Index))
						{
							continue;
						}
						const EDreamClipMaskShape Value = static_cast<EDreamClipMaskShape>(Enum->GetValueByIndex(Index));
						Menu.AddMenuEntry(Enum->GetDisplayNameTextByIndex(Index), Enum->GetToolTipTextByIndex(Index), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([PickShape, Value]() { PickShape(Value); })));
					}
					return Menu.MakeWidget();
				})
			];
	}

	// The layer style and the layer group: a switch each, and a way to their own settings.
	Layer.AddCustomRow(LOCTEXT("LayerStyle", "Layer Style"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("LayerStyle", "Layer Style"))
			.ToolTipText(LOCTEXT("LayerStyleTip", "Drop shadows, glows, a bevel, strokes and overlays, drawn round what this widget draws."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			MakeComponentSwitch(InWidgets, UDreamLayerStyle::StaticClass(), WeakUtilities, LOCTEXT("EditStyleTip", "Open the layer style's own settings"))
		];
	Layer.AddCustomRow(LOCTEXT("LayerGroup", "Layer Group"))
		.NameContent()
		[
			SNew(STextBlock)
			.Text(LOCTEXT("LayerGroup", "Layer Group"))
			.ToolTipText(LOCTEXT("LayerGroupTip", "Makes this widget and everything under it one layer: faded, blended and masked as one."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
		]
		.ValueContent()
		[
			MakeComponentSwitch(InWidgets, UDreamLayerGroup::StaticClass(), WeakUtilities, LOCTEXT("EditGroupTip", "Open the layer group's own settings"))
		];
	{
		const TArray<UObject*> Groups = ComponentOnEach<UDreamLayerGroup>(InWidgets);
		if (Groups.Num() > 0)
		{
			if (IDetailPropertyRow* Row = Layer.AddExternalObjectProperty(Groups, TEXT("Isolation")))
			{
				Row->DisplayName(LOCTEXT("GroupMode", "Group Mode"));
			}
		}
	}

	// The effects on the widget, each with its eye and a way to its own settings.
	if (InWidgets.Num() != 1 || !InWidgets[0].IsValid())
	{
		return;
	}
	TArray<FDreamUILayerEffect> Effects;
	DreamUILayerEffects::Collect(InWidgets[0].Get(), Effects);
	if (Effects.Num() == 0)
	{
		return;
	}
	IDetailGroup& EffectsGroup = Layer.AddGroup(TEXT("LayerEffects"), FText::Format(LOCTEXT("EffectsHeader", "Effects ({0})"), Effects.Num()), /*bForAdvanced*/false, /*bStartExpanded*/true);
	for (const FDreamUILayerEffect& Effect : Effects)
	{
		const bool bHasSwitch = DreamUILayerEffects::HasSwitch(Effect);
		const TWeakObjectPtr<UDreamUIBehaviour> AsComponent = Cast<UDreamUIBehaviour>(Effect.Object.Get());
		EffectsGroup.AddWidgetRow()
			.FilterString(Effect.Name)
			.NameContent()
			[
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				[
					SNew(SButton)
					.ButtonStyle(FAppStyle::Get(), "HoverHintOnly")
					.ContentPadding(FMargin(2.0f, 0.0f))
					.Visibility(bHasSwitch ? EVisibility::Visible : EVisibility::Hidden)
					.IsEnabled(Effect.SwitchUnavailable.IsEmpty())
					.ToolTipText(Effect.SwitchUnavailable.IsEmpty() ? LOCTEXT("EyeTip", "Turn this effect off or on, to compare with and without it. Saved like any other setting.") : Effect.SwitchUnavailable)
					.OnClicked_Lambda([Effect]()
					{
						const bool bOn = !DreamUILayerEffects::IsOn(Effect);
						Defer([Effect, bOn]() { DreamUILayerEffects::SetOn(Effect, bOn); });
						return FReply::Handled();
					})
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image_Lambda([Effect]()
						{
							return FAppStyle::GetBrush(DreamUILayerEffects::IsOn(Effect) ? TEXT("Icons.Visible") : TEXT("Icons.Hidden"));
						})
					]
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4.0f, 0.0f)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(DreamUILayerEffects::KindIcon(Effect.Kind))
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				[
					AsComponent.IsValid()
					? StaticCastSharedRef<SWidget>(SNew(SHyperlink)
						.Text(Effect.Name)
						.ToolTipText(LOCTEXT("OpenEffectTip", "Open its settings"))
						.OnNavigate_Lambda([AsComponent]() { Defer([AsComponent]() { ShowComponent(AsComponent.Get()); }); }))
					: StaticCastSharedRef<SWidget>(SNew(STextBlock)
						.Text(Effect.Name)
						.Font(IDetailLayoutBuilder::GetDetailFont()))
				]
			]
			.ValueContent()
			[
				SNew(STextBlock)
				.Text(Effect.Detail)
				.ToolTipText(Effect.Detail)
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
				.Font(IDetailLayoutBuilder::GetDetailFont())
			];
	}
}

#undef LOCTEXT_NAMESPACE
