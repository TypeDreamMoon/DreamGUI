// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamWidgetEventCustomization.h"

#include "DetailLayoutBuilder.h"
#include "DetailCategoryBuilder.h"
#include "DetailWidgetRow.h"

#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetDesignerModes.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "DreamWidgetBlueprint.h"

#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/DreamWidgetTree.h"

#include "EdGraph/EdGraph.h"
#include "K2Node_CustomEvent.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "ScopedTransaction.h"

#include "Styling/AppStyle.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SWidgetSwitcher.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "DreamWidgetEventCustomization"

namespace
{
	/**
	 * The object of this route on the TEMPLATE side, resolved by name and position exactly the way
	 * the compiler and the runtime resolve it (ResolveDreamWidgetBindingTarget). Resolved fresh on
	 * every use rather than cached: the panel shows a preview that is torn down and rebuilt on every
	 * structural edit, and names are the only identity that survives that.
	 */
	const UObject* ResolveTemplateSubject(const UDreamWidgetBlueprint& InBlueprint, FName InWidgetVariableName,
		EDreamWidgetBindingTarget InTarget, int32 InBehaviourIndex, const UDreamWidget** OutTemplateWidget = nullptr)
	{
		if (OutTemplateWidget != nullptr)
		{
			*OutTemplateWidget = nullptr;
		}
		if (!IsValid(InBlueprint.WidgetTree))
		{
			return nullptr;
		}
		const UDreamWidget* TemplateWidget = InBlueprint.WidgetTree->FindWidgetByVariableName(InWidgetVariableName);
		if (!IsValid(TemplateWidget))
		{
			return nullptr;
		}
		if (OutTemplateWidget != nullptr)
		{
			*OutTemplateWidget = TemplateWidget;
		}
		switch (InTarget)
		{
		case EDreamWidgetBindingTarget::Widget:
			return TemplateWidget;
		case EDreamWidgetBindingTarget::Behaviour:
			return TemplateWidget->GetAllComponents().IsValidIndex(InBehaviourIndex)
				? static_cast<const UObject*>(TemplateWidget->GetAllComponents()[InBehaviourIndex])
				: nullptr;
		default:
			// Visual routes are authorable in text; nothing in this panel creates one.
			return nullptr;
		}
	}

	/** The BlueprintAssignable dynamic multicast delegates of one class, in declaration order. */
	void CollectAssignableEvents(const UClass* InClass, TArray<const FMulticastDelegateProperty*>& OutEvents)
	{
		for (TFieldIterator<FMulticastDelegateProperty> It(InClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
		{
			// BlueprintAssignable is the same test the text pipeline applies before writing a `->`
			// route (DreamUITextBuilder): it is what guarantees a UFUNCTION can be bound by name.
			if (It->HasAnyPropertyFlags(CPF_BlueprintAssignable))
			{
				OutEvents.Add(*It);
			}
		}
	}
}

TSharedRef<IDetailCustomization> FDreamWidgetEventCustomization::MakeInstance()
{
	return MakeShareable(new FDreamWidgetEventCustomization);
}

void FDreamWidgetEventCustomization::CustomizeDetails(IDetailLayoutBuilder& DetailBuilder)
{
	// One object only, like UMG: a route names one widget, so a multi-selection has no one thing to
	// route and offering rows that silently act on the first would be worse than offering none.
	TArray<TWeakObjectPtr<UObject>> Objects;
	DetailBuilder.GetObjectsBeingCustomized(Objects);
	if (Objects.Num() != 1 || !Objects[0].IsValid())
	{
		return;
	}

	// What is selected decides which class's events are listed and what the route's target is; the
	// OWNING WIDGET is what the route is keyed on either way, because only widgets have names.
	UDreamWidget* OwnerWidget = nullptr;
	if (UDreamWidget* AsWidget = Cast<UDreamWidget>(Objects[0].Get()))
	{
		OwnerWidget = AsWidget;
		Context.Target = EDreamWidgetBindingTarget::Widget;
		Context.BehaviourIndex = INDEX_NONE;
		SubjectClass = AsWidget->GetClass();
	}
	else if (UDreamUIBehaviour* AsBehaviour = Cast<UDreamUIBehaviour>(Objects[0].Get()))
	{
		OwnerWidget = AsBehaviour->GetWidget();
		if (!IsValid(OwnerWidget))
		{
			return;
		}
		Context.Target = EDreamWidgetBindingTarget::Behaviour;
		// By position, because behaviours have no stable name of their own -- the same correspondence
		// the runtime binder and the designer's component editing already run on. The instanced array
		// is the archetype's, in order, so a preview index addresses the template too.
		Context.BehaviourIndex = OwnerWidget->GetAllComponents().IndexOfByKey(AsBehaviour);
		if (Context.BehaviourIndex == INDEX_NONE)
		{
			return;
		}
		SubjectClass = AsBehaviour->GetClass();
	}
	else
	{
		return;
	}

	// Only inside a designer. This customization is registered globally, so it also runs for the
	// widget inspector and for live PIE widgets -- places with no Blueprint in reach. UMG's event
	// rows exist only in its own editor; outside one, no rows is the correct amount of rows.
	const TWeakPtr<FDreamWidgetBlueprintEditor> EditorPtr = FDreamWidgetBlueprintEditor::GetEditorByWorld(OwnerWidget->GetWorld());
	FDreamWidgetBlueprintEditor* Editor = EditorPtr.IsValid() ? EditorPtr.Pin().Get() : nullptr;
	UDreamWidgetBlueprint* Blueprint = Editor != nullptr ? Editor->GetWidgetBlueprint() : nullptr;
	if (Editor == nullptr || Blueprint == nullptr)
	{
		return;
	}

	TArray<const FMulticastDelegateProperty*> Events;
	CollectAssignableEvents(SubjectClass.Get(), Events);
	if (Events.Num() == 0)
	{
		return;
	}

	Context.Editor = EditorPtr;

	// The route is keyed on the TEMPLATE's variable name. The preview shares the display name, but
	// the template is the copy the compiler reads, so it is the one asked.
	const UDreamWidget* TemplateWidget = Editor->GetTemplateWidget(OwnerWidget);
	Context.WidgetVariableName = UDreamWidgetTree::MakeWidgetVariableName(TemplateWidget);

	// Why a row might be present but disabled, checked from cheapest to most specific. A disabled row
	// with a reason beats a missing one: the author can see the event exists and read what to change.
	bool bEnabled = true;
	FText DisabledReason;
	if (TemplateWidget == nullptr || Context.WidgetVariableName.IsNone())
	{
		// The root agent and anything else with no template counterpart cannot be named by a route.
		bEnabled = false;
		DisabledReason = LOCTEXT("Disabled_NoTemplate",
			"Only widgets that are part of this Blueprint's own hierarchy can have events routed.");
	}
	else if (!IsValid(Blueprint->WidgetTree)
		|| Blueprint->WidgetTree->FindWidgetByVariableName(Context.WidgetVariableName) != TemplateWidget)
	{
		// The compiler exposes one variable per display name and warns about the rest; a route from a
		// shadowed widget would bind the OTHER widget's events. Same rule here, said before the click.
		bEnabled = false;
		DisabledReason = FText::Format(LOCTEXT("Disabled_NameShadowed",
			"Another widget is also named \"{0}\", so this one has no variable of its own. Rename it first."),
			FText::FromName(Context.WidgetVariableName));
	}
	else if (Context.Target == EDreamWidgetBindingTarget::Behaviour
		&& (!TemplateWidget->GetAllComponents().IsValidIndex(Context.BehaviourIndex)
			|| TemplateWidget->GetAllComponents()[Context.BehaviourIndex] == nullptr
			|| TemplateWidget->GetAllComponents()[Context.BehaviourIndex]->GetClass() != SubjectClass.Get()))
	{
		bEnabled = false;
		DisabledReason = LOCTEXT("Disabled_BehaviourMismatch",
			"This behaviour has no counterpart on the widget's template, so a route cannot reach it.");
	}
	else if (DreamUITextAuthoring::IsTextAuthored(Blueprint))
	{
		// The .dui owns the binding lists: the next compile rebuilds them from the file, so a route
		// added here would be silently dropped -- the exact loss the text gate exists to prevent.
		bEnabled = false;
		DisabledReason = FText::Format(LOCTEXT("Disabled_TextAuthored",
			"This hierarchy is authored in {0}. Route events there instead, as a line on the widget: EventName -> YourHandler"),
			FText::FromString(DreamUITextAuthoring::GetAuthoredSourceFileName(Blueprint)));
	}

	// UMG puts every delegate in one "Events" category at the bottom, whatever category the property
	// itself claims, and it is right: these rows are about the Blueprint, not about configuring the
	// object, and one place to look beats five.
	IDetailCategoryBuilder& EventCategory = DetailBuilder.EditCategory(
		TEXT("Events"), LOCTEXT("EventsCategory", "Events"), ECategoryPriority::Uncommon);
	for (const FMulticastDelegateProperty* Event : Events)
	{
		AddEventRow(EventCategory, Event, bEnabled, DisabledReason);
	}
}

void FDreamWidgetEventCustomization::AddEventRow(IDetailCategoryBuilder& InCategory,
	const FMulticastDelegateProperty* InDelegate, bool bInEnabled, const FText& InDisabledReason)
{
	const FName EventName = InDelegate->GetFName();
	const FText EventText = InDelegate->GetDisplayNameText();
	FText EventTooltip = InDelegate->GetToolTipText();
	if (EventTooltip.IsEmpty())
	{
		EventTooltip = FText::FromName(EventName);
	}

	// UMG's row, kept verbatim so the two editors read as one: event icon, name, then a button whose
	// icon is "+" until the route exists and "view" after. The one addition is the small "x" -- UMG
	// removes a binding by deleting its bound-event node, but here the handler is an ordinary custom
	// event the route merely names, so the row needs its own way to un-route or a deleted handler
	// would leave a compile error with nothing in the panel able to clear it.
	InCategory.AddCustomRow(EventText)
		.WholeRowContent()
		[
			SNew(SHorizontalBox)
			.IsEnabled(bInEnabled)
			.ToolTipText(bInEnabled ? EventTooltip : InDisabledReason)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(0.0f, 0.0f, 5.0f, 0.0f)
			[
				SNew(SImage)
				.Image(FAppStyle::Get().GetBrush("GraphEditor.Event_16x"))
			]
			+ SHorizontalBox::Slot()
			.VAlign(VAlign_Center)
			.HAlign(HAlign_Left)
			[
				SNew(STextBlock)
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.Text(EventText)
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(0.0f)
			[
				SNew(SButton)
				.ContentPadding(FMargin(3.0f, 2.0f))
				.ToolTipText(LOCTEXT("AddOrViewTooltip", "Add or view the handler for this event in the event graph"))
				.OnClicked(this, &FDreamWidgetEventCustomization::HandleAddOrViewEvent, EventName)
				[
					SNew(SWidgetSwitcher)
					.WidgetIndex(this, &FDreamWidgetEventCustomization::GetAddOrViewIndex, EventName)
					+ SWidgetSwitcher::Slot()
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FAppStyle::Get().GetBrush("Icons.SelectInViewport"))
					]
					+ SWidgetSwitcher::Slot()
					[
						SNew(SImage)
						.ColorAndOpacity(FSlateColor::UseForeground())
						.Image(FAppStyle::Get().GetBrush("Icons.Plus"))
					]
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.HAlign(HAlign_Left)
			.VAlign(VAlign_Center)
			.Padding(4.0f, 0.0f, 0.0f, 0.0f)
			[
				SNew(SButton)
				.ButtonStyle(FAppStyle::Get(), "SimpleButton")
				.ContentPadding(FMargin(1.0f, 1.0f))
				.Visibility(this, &FDreamWidgetEventCustomization::GetRemoveVisibility, EventName)
				.ToolTipText(LOCTEXT("RemoveRouteTooltip",
					"Remove this event route. The handler stays in the event graph; delete it there if it is no longer wanted."))
				.OnClicked(this, &FDreamWidgetEventCustomization::HandleRemoveEvent, EventName)
				[
					SNew(SImage)
					.ColorAndOpacity(FSlateColor::UseForeground())
					.Image(FAppStyle::Get().GetBrush("Icons.X"))
				]
			]
		];
}

const FDreamWidgetEventBinding* FDreamWidgetEventCustomization::FindRoute(
	const UDreamWidgetBlueprint* InBlueprint, FName InEventName) const
{
	if (InBlueprint == nullptr)
	{
		return nullptr;
	}
	return InBlueprint->EventBindings.FindByPredicate([this, InEventName](const FDreamWidgetEventBinding& Route)
	{
		return Route.WidgetName == Context.WidgetVariableName
			&& Route.Target == Context.Target
			&& Route.BehaviourIndex == Context.BehaviourIndex
			&& Route.EventName == InEventName;
	});
}

int32 FDreamWidgetEventCustomization::GetAddOrViewIndex(FName InEventName) const
{
	const TSharedPtr<FDreamWidgetBlueprintEditor> Editor = Context.Editor.Pin();
	const UDreamWidgetBlueprint* Blueprint = Editor.IsValid() ? Editor->GetWidgetBlueprint() : nullptr;
	// 0 = View, 1 = Add; matches the switcher's slot order, which matches UMG's.
	return FindRoute(Blueprint, InEventName) != nullptr ? 0 : 1;
}

EVisibility FDreamWidgetEventCustomization::GetRemoveVisibility(FName InEventName) const
{
	return GetAddOrViewIndex(InEventName) == 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

void FDreamWidgetEventCustomization::FocusHandler(FDreamWidgetBlueprintEditor& InEditor, const UObject* InHandler)
{
	// Graph mode first, explicitly. The stock JumpToHyperlink starts by switching to the standard
	// Blueprint mode -- a name this editor never registers, so that call is a no-op -- and the
	// designer mode's layout has no document area to open a graph in (its own layout says so).
	// UMG makes the same explicit switch before every graph-focused gesture.
	InEditor.SetCurrentMode(FDreamWidgetBlueprintApplicationModes::GraphMode);
	FKismetEditorUtilities::BringKismetToFocusAttentionOnObject(InHandler);
}

FReply FDreamWidgetEventCustomization::HandleAddOrViewEvent(FName InEventName)
{
	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = Context.Editor.Pin();
	UDreamWidgetBlueprint* Blueprint = Editor.IsValid() ? Editor->GetWidgetBlueprint() : nullptr;
	if (Blueprint == nullptr)
	{
		return FReply::Handled();
	}

	// Re-resolved by NAME, never through the preview: the preview this row was built from may have
	// been rebuilt any number of times since, and the template tree is the copy that matters anyway.
	const UObject* Subject = ResolveTemplateSubject(*Blueprint, Context.WidgetVariableName,
		Context.Target, Context.BehaviourIndex);
	const FMulticastDelegateProperty* Delegate = Subject != nullptr
		? CastField<FMulticastDelegateProperty>(Subject->GetClass()->FindPropertyByName(InEventName))
		: nullptr;
	if (Delegate == nullptr)
	{
		return FReply::Handled();
	}

	// An existing route: put the caret on its handler. The handler may be a custom event (what this
	// button creates), a function graph (what a .dui author typically names), or -- if somebody
	// deleted the node -- nothing, in which case the handler is recreated under the recorded name
	// rather than leaving a route that can only ever produce a compile error.
	if (const FDreamWidgetEventBinding* Route = FindRoute(Blueprint, InEventName))
	{
		TArray<UEdGraph*> AllGraphs;
		Blueprint->GetAllGraphs(AllGraphs);
		for (UEdGraph* Graph : AllGraphs)
		{
			if (Graph == nullptr)
			{
				continue;
			}
			if (Graph->GetFName() == Route->FunctionName)
			{
				FocusHandler(*Editor, Graph);
				return FReply::Handled();
			}
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_CustomEvent* CustomEvent = Cast<UK2Node_CustomEvent>(Node);
				if (CustomEvent != nullptr && CustomEvent->CustomFunctionName == Route->FunctionName)
				{
					FocusHandler(*Editor, CustomEvent);
					return FReply::Handled();
				}
			}
		}

		UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
		if (EventGraph == nullptr)
		{
			return FReply::Handled();
		}
		const FScopedTransaction Transaction(LOCTEXT("RecreateEventHandler", "Recreate Event Handler"));
		const FVector2D Position = EventGraph->GetGoodPlaceForNewNode();
		UK2Node_CustomEvent* NewHandler = UK2Node_CustomEvent::CreateFromFunction(
			Position, EventGraph, Route->FunctionName.ToString(), Delegate->SignatureFunction, /*bSelectNewNode*/true);
		if (NewHandler == nullptr)
		{
			return FReply::Handled();
		}
		FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
		FocusHandler(*Editor, NewHandler);
		return FReply::Handled();
	}

	// No route yet: one transaction creates the pair -- a custom event with the delegate's own
	// signature, and the route that names it. The same authored form the text pipeline produces, so
	// the compiler's checks and the runtime's binder apply to both without knowing which wrote it.
	UEdGraph* EventGraph = FBlueprintEditorUtils::FindEventGraph(Blueprint);
	if (EventGraph == nullptr)
	{
		return FReply::Handled();
	}

	// "Confirm_OnClick": the widget the author named, the event as displayed, both spelled through
	// the same sanitizer the variable names go through. FindUniqueKismetName settles collisions with
	// anything else on the Blueprint.
	const FString HandlerBase = FString::Printf(TEXT("%s_%s"),
		*Context.WidgetVariableName.ToString(),
		*UDreamWidgetTree::SanitizeIdentifier(Delegate->GetDisplayNameText().ToString()));
	const FName HandlerName = FBlueprintEditorUtils::FindUniqueKismetName(Blueprint, HandlerBase);

	const FScopedTransaction Transaction(LOCTEXT("AddEventRoute", "Add Event Handler"));
	const FVector2D Position = EventGraph->GetGoodPlaceForNewNode();
	UK2Node_CustomEvent* Handler = UK2Node_CustomEvent::CreateFromFunction(
		Position, EventGraph, HandlerName.ToString(), Delegate->SignatureFunction, /*bSelectNewNode*/true);
	if (Handler == nullptr)
	{
		return FReply::Handled();
	}

	Blueprint->Modify();
	FDreamWidgetEventBinding& Route = Blueprint->EventBindings.AddDefaulted_GetRef();
	Route.WidgetName = Context.WidgetVariableName;
	Route.Target = Context.Target;
	Route.BehaviourIndex = Context.BehaviourIndex;
	Route.EventName = InEventName;
	Route.FunctionName = HandlerName;

	// Structurally: the class gained a member. This is also what refreshes the skeleton so the new
	// event is a function the very next compile's route validation can find.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	FocusHandler(*Editor, Handler);
	return FReply::Handled();
}

FReply FDreamWidgetEventCustomization::HandleRemoveEvent(FName InEventName)
{
	TSharedPtr<FDreamWidgetBlueprintEditor> Editor = Context.Editor.Pin();
	UDreamWidgetBlueprint* Blueprint = Editor.IsValid() ? Editor->GetWidgetBlueprint() : nullptr;
	if (Blueprint == nullptr || FindRoute(Blueprint, InEventName) == nullptr)
	{
		return FReply::Handled();
	}

	const FScopedTransaction Transaction(LOCTEXT("RemoveEventRoute", "Remove Event Route"));
	Blueprint->Modify();
	Blueprint->EventBindings.RemoveAll([this, InEventName](const FDreamWidgetEventBinding& Route)
	{
		return Route.WidgetName == Context.WidgetVariableName
			&& Route.Target == Context.Target
			&& Route.BehaviourIndex == Context.BehaviourIndex
			&& Route.EventName == InEventName;
	});
	// The same mark UMG makes when a binding is removed: the class's compiled routes changed.
	FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
