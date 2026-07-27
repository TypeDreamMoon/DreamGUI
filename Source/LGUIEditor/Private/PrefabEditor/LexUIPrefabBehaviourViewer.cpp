// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LexUIPrefabBehaviourViewer.h"
#include "Core/Components/LexVisual.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "DetailLayoutBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "Framework/Notifications/NotificationManager.h"
#include "LexUIBehaviourEditorBackend.h"
#include "LexUIPrefabBehaviourUtils.h"
#include "LexUIPrefabEditor.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Notifications/SNotificationList.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabBehaviourViewer"

namespace LexUIPrefabBehaviourViewerLocal
{
	FSlateFontInfo MonoFont() { return FCoreStyle::GetDefaultFontStyle("Mono", 9); }

	const FLinearColor ValueColor(0.85f, 0.85f, 0.85f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor WarnColor(1.0f, 0.75f, 0.35f);
	const FLinearColor AccentColor(0.55f, 0.75f, 1.0f);
	const FLinearColor BoundColor(0.4f, 0.85f, 0.5f);

	TSharedRef<SWidget> MakeSection(const FText& Title, const TSharedRef<SWidget>& Body, bool bInitiallyCollapsed = false)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("NoBorder"))
			.Padding(FMargin(0, 0, 0, 2))
			[
				SNew(SExpandableArea)
				.InitiallyCollapsed(bInitiallyCollapsed)
				.BorderBackgroundColor(FLinearColor(1, 1, 1, 0.05f))
				.Padding(FMargin(8, 4))
				.HeaderContent()
				[
					SNew(STextBlock)
					.Text(Title)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.ColorAndOpacity(FSlateColor(AccentColor))
				]
				.BodyContent()
				[
					Body
				]
			];
	}

	TSharedRef<SWidget> MakeStripedRow(int32 VisibleIndex, const TSharedRef<SWidget>& Inner)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(VisibleIndex % 2 == 0 ? FLinearColor(1, 1, 1, 0.04f) : FLinearColor::Transparent))
			.Padding(FMargin(4, 2))
			[
				Inner
			];
	}

	void ShowNotification(const FText& Message, bool bSuccess)
	{
		FNotificationInfo Info(Message);
		Info.ExpireDuration = 4.0f;
		if (TSharedPtr<SNotificationItem> Item = FSlateNotificationManager::Get().AddNotification(Info))
		{
			Item->SetCompletionState(bSuccess ? SNotificationItem::CS_Success : SNotificationItem::CS_Fail);
		}
	}

	/** "Tick(float DeltaTime)"-style signature from a UFunction. */
	FString DescribeFunctionSignature(UFunction* Function)
	{
		FString Params;
		for (TFieldIterator<FProperty> It(Function); It && It->HasAnyPropertyFlags(CPF_Parm); ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_ReturnParm))
			{
				continue;
			}
			Params += (Params.IsEmpty() ? TEXT("") : TEXT(", "));
			Params += It->GetCPPType() + TEXT(" ") + It->GetName();
		}
		return Function->GetName() + TEXT("(") + Params + TEXT(")");
	}

	/** Display label for a bound reference: the owning widget's name plus what the object is. */
	FString DescribeBoundValue(UObject* Value)
	{
		if (!IsValid(Value))
		{
			return TEXT("(unbound)");
		}
		if (const ULexWidget* AsWidget = Cast<ULexWidget>(Value))
		{
			return AsWidget->GetDisplayName();
		}
		if (const ULexWidget* Owner = Value->GetTypedOuter<ULexWidget>())
		{
			return FString::Printf(TEXT("%s · %s"), *Owner->GetDisplayName(), *Value->GetClass()->GetName());
		}
		return Value->GetName();
	}
}

void SLexUIPrefabBehaviourViewer::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject)
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	PrefabEditorPtr = InPrefabEditorPtr;
	PrefabWeak = Cast<ULexUIPrefab>(InObject);

	if (InPrefabEditorPtr.IsValid())
	{
		InPrefabEditorPtr->OnSelectionChanged.AddSP(this, &SLexUIPrefabBehaviourViewer::HandleSelectionChanged);
	}

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(4)
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			[
				SNew(SButton)
				.Text(LOCTEXT("OpenBehaviour", "Open"))
				.ToolTipText(LOCTEXT("OpenBehaviourTip", "Open the companion behaviour blueprint (created next to the prefab if there is none yet)."))
				.OnClicked_Lambda([this]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin())
					{
						Editor->CreateOrOpenBehaviourBlueprint();
						Rebuild();
					}
					return FReply::Handled();
				})
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("AutoBind", "Auto Bind"))
				.ToolTipText(LOCTEXT("AutoBindTip", "Run the save-time name-matching pass now: bind every null Instance-Editable widget/visual/behaviour property to the descendant widget with the matching display name."))
				.OnClicked_Lambda([this]() { RunAutoBind(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 0, 0, 0)
			[
				SNew(SButton)
				.Text(LOCTEXT("RefreshBehaviour", "Refresh"))
				.OnClicked_Lambda([this]() { Rebuild(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter properties, events, and functions..."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					FilterString = Text.ToString();
					Rebuild();
				})
			]
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SScrollBox)
			+ SScrollBox::Slot()
			[
				SAssignNew(ContentBox, SVerticalBox)
			]
		]
	];
	Rebuild();
}

bool SLexUIPrefabBehaviourViewer::MatchesFilter(const FString& Haystack) const
{
	return FilterString.IsEmpty() || Haystack.Contains(FilterString);
}

void SLexUIPrefabBehaviourViewer::HandleSelectionChanged()
{
	Rebuild();
}

void SLexUIPrefabBehaviourViewer::RunAutoBind()
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	ULexUIPrefab* Prefab = PrefabWeak.Get();
	ULexWidget* RootWidget = Editor.IsValid() ? Editor->GetLoadedRootWidget() : nullptr;
	if (!Editor.IsValid() || !IsValid(Prefab) || !IsValid(RootWidget))
	{
		return;
	}
	TArray<FString> BoundDetails;
	TArray<FString> Problems;
	// The pass only assigns object properties on the companion, so it undoes cleanly -- but the
	// Modify() calls inside it record nothing unless a transaction is open here.
	const FScopedTransaction Transaction(LOCTEXT("AutoBindTransaction", "Auto Bind Widget References"));
	LexUIPrefabBehaviourUtils::AutoBindAndValidate(RootWidget, Prefab, BoundDetails, Problems, /*bPerformAutoBind*/true);
	if (BoundDetails.Num() > 0)
	{
		if (ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject())
		{
			Helper->Modify();
			Helper->SetAnythingDirty();
		}
	}
	FString Message = FString::Printf(TEXT("Auto bind: %d bound, %d problem(s)."), BoundDetails.Num(), Problems.Num());
	for (const FString& Detail : BoundDetails)
	{
		Message += TEXT("\n") + Detail;
	}
	for (const FString& Problem : Problems)
	{
		Message += TEXT("\n") + Problem;
	}
	ShowNotification(FText::FromString(Message), Problems.Num() == 0);
	Rebuild();
}

void SLexUIPrefabBehaviourViewer::CollectBindCandidates(FObjectProperty* Property, TArray<TPair<UObject*, FString>>& OutCandidates) const
{
	TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	ULexWidget* RootWidget = Editor.IsValid() ? Editor->GetLoadedRootWidget() : nullptr;
	if (!IsValid(RootWidget) || !Property)
	{
		return;
	}
	UClass* TargetClass = Property->PropertyClass;
	TArray<ULexWidget*> Stack{ RootWidget };
	while (Stack.Num() > 0)
	{
		ULexWidget* Widget = Stack.Pop();
		if (!IsValid(Widget))
		{
			continue;
		}
		for (ULexWidget* Child : Widget->GetChildren())
		{
			Stack.Push(Child);
		}
		if (TargetClass->IsChildOf(ULexWidget::StaticClass()))
		{
			if (Widget->IsA(TargetClass))
			{
				OutCandidates.Emplace(Widget, Widget->GetDisplayName());
			}
		}
		else if (TargetClass->IsChildOf(ULexVisual::StaticClass()))
		{
			if (ULexVisual* Visual = Widget->GetVisual(); IsValid(Visual) && Visual->IsA(TargetClass))
			{
				OutCandidates.Emplace(Visual, FString::Printf(TEXT("%s · %s"), *Widget->GetDisplayName(), *Visual->GetClass()->GetName()));
			}
		}
		else if (TargetClass->IsChildOf(ULexUIBehaviour::StaticClass()))
		{
			if (ULexUIBehaviour* Behaviour = Widget->GetComponent(TargetClass))
			{
				OutCandidates.Emplace(Behaviour, FString::Printf(TEXT("%s · %s"), *Widget->GetDisplayName(), *Behaviour->GetClass()->GetName()));
			}
		}
	}
	// Stable, readable menu order.
	OutCandidates.Sort([](const TPair<UObject*, FString>& A, const TPair<UObject*, FString>& B) { return A.Value < B.Value; });
}

void SLexUIPrefabBehaviourViewer::BuildWidgetReferenceSection(UClass* BehaviourClass, ULexUIBehaviour* Primary)
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Shown = 0;
	int32 Total = 0;
	int32 Unbound = 0;
	for (TFieldIterator<FObjectProperty> It(BehaviourClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FObjectProperty* Property = *It;
		if (Property->HasAnyPropertyFlags(CPF_Parm))
		{
			continue;
		}
		UClass* TargetClass = Property->PropertyClass;
		if (!TargetClass->IsChildOf(ULexWidget::StaticClass())
			&& !TargetClass->IsChildOf(ULexVisual::StaticClass())
			&& !TargetClass->IsChildOf(ULexUIBehaviour::StaticClass()))
		{
			continue;
		}
		// The base class's own cached pointers (owner widget, animation player) are not references.
		if (Property->GetOwnerClass() == ULexUIBehaviour::StaticClass())
		{
			continue;
		}
		Total++;

		const bool bTransient = Property->HasAnyPropertyFlags(CPF_Transient);
		const bool bInstanceEditable = Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_DisableEditOnInstance);
		const bool bSavable = bInstanceEditable && !bTransient;
		UObject* BoundValue = IsValid(Primary) ? Property->GetObjectPropertyValue_InContainer(Primary) : nullptr;
		const bool bBound = IsValid(BoundValue);
		if (bSavable && !bBound)
		{
			Unbound++;
		}

		FString Status;
		FLinearColor StatusColor = ValueColor;
		if (bTransient)
		{
			Status = TEXT("runtime-bound (native, by display name)");
			StatusColor = DimColor;
		}
		else if (!bSavable)
		{
			Status = TEXT("NOT Instance-Editable — value would not save");
			StatusColor = WarnColor;
		}
		else
		{
			Status = DescribeBoundValue(BoundValue);
			StatusColor = bBound ? BoundColor : WarnColor;
		}

		const FString RowText = FString::Printf(TEXT("%s %s %s"), *Property->GetName(), *TargetClass->GetName(), *Status);
		if (!MatchesFilter(RowText))
		{
			continue;
		}

		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(200)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Property->GetName()))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
					.ToolTipText(FText::FromString(TargetClass->GetName()))
				]
			]
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(160)
				[
					SNew(STextBlock)
					.Text(FText::FromString(TargetClass->GetName()))
					.Font(MonoFont())
					.ColorAndOpacity(FSlateColor(DimColor))
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Status))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(StatusColor))
			];

		if (bSavable && IsValid(Primary))
		{
			const TWeakObjectPtr<ULexUIBehaviour> PrimaryWeak = Primary;
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(SComboButton)
				.ToolTipText(LOCTEXT("BindMenuTip", "Bind this property to a type-compatible widget from the hierarchy."))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("BindMenu", "Bind"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.OnGetMenuContent_Lambda([this, Property, PrimaryWeak]()
				{
					FMenuBuilder MenuBuilder(true, nullptr);
					TArray<TPair<UObject*, FString>> Candidates;
					CollectBindCandidates(Property, Candidates);
					auto AssignValue = [this, Property, PrimaryWeak](UObject* NewValue)
					{
						ULexUIBehaviour* LocalPrimary = PrimaryWeak.Get();
						if (!IsValid(LocalPrimary))
						{
							return;
						}
						const FScopedTransaction Transaction(NewValue != nullptr
							? LOCTEXT("BindReferenceTransaction", "Bind Widget Reference")
							: LOCTEXT("UnbindReferenceTransaction", "Unbind Widget Reference"));
						LocalPrimary->Modify();
						Property->SetObjectPropertyValue_InContainer(LocalPrimary, NewValue);
						if (ULexUIPrefab* LocalPrefab = PrefabWeak.Get())
						{
							if (ULexUIPrefabHelperObject* Helper = LocalPrefab->GetPrefabHelperObject())
							{
								Helper->Modify();
								Helper->SetAnythingDirty();
							}
						}
						Rebuild();
					};
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("BindCandidatesSection", "Bind to"));
					for (const TPair<UObject*, FString>& Candidate : Candidates)
					{
						UObject* Target = Candidate.Key;
						MenuBuilder.AddMenuEntry(FText::FromString(Candidate.Value), FText::GetEmpty(), FSlateIcon(),
							FUIAction(FExecuteAction::CreateLambda([AssignValue, Target]() { AssignValue(Target); })));
					}
					if (Candidates.Num() == 0)
					{
						MenuBuilder.AddMenuEntry(LOCTEXT("NoCandidates", "(no type-compatible widget in this prefab)"),
							FText::GetEmpty(), FSlateIcon(), FUIAction(FExecuteAction(), FCanExecuteAction::CreateLambda([]() { return false; })));
					}
					MenuBuilder.EndSection();
					MenuBuilder.BeginSection(NAME_None, LOCTEXT("BindClearSection", "Clear"));
					MenuBuilder.AddMenuEntry(LOCTEXT("ClearBinding", "Unbind"),
						LOCTEXT("ClearBindingTip", "Reset this property to null."), FSlateIcon(),
						FUIAction(FExecuteAction::CreateLambda([AssignValue]() { AssignValue(nullptr); })));
					MenuBuilder.EndSection();
					return MenuBuilder.MakeWidget();
				})
			];
		}

		Rows->AddSlot().AutoHeight()[MakeStripedRow(Shown, Row)];
		Shown++;
	}
	if (Total == 0)
	{
		Rows->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoWidgetRefs", "The behaviour class declares no widget/visual/behaviour references. Promote a widget to a variable, or declare Instance-Editable properties and let Auto Bind match them by name."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(LexUIPrefabBehaviourViewerLocal::DimColor))
			.AutoWrapText(true)
		];
	}
	const FString Header = Unbound > 0
		? FString::Printf(TEXT("Widget References (%d, %d unbound)"), Total, Unbound)
		: FString::Printf(TEXT("Widget References (%d)"), Total);
	ContentBox->AddSlot().AutoHeight()[MakeSection(FText::FromString(Header), Rows)];
}

void SLexUIPrefabBehaviourViewer::BuildProvidesSection(UClass* BehaviourClass)
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
	int32 Shown = 0;
	int32 Total = 0;

	auto AddRow = [this, &Rows, &Shown, &Total](const TCHAR* Kind, const FLinearColor& KindColor, const FString& Signature, const FString& OwnerName)
	{
		Total++;
		if (!MatchesFilter(FString::Printf(TEXT("%s %s %s"), Kind, *Signature, *OwnerName)))
		{
			return;
		}
		Rows->AddSlot().AutoHeight()
		[
			LexUIPrefabBehaviourViewerLocal::MakeStripedRow(Shown,
				SNew(SHorizontalBox)
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 1)
				[
					SNew(SBox)
					.WidthOverride(90)
					[
						SNew(STextBlock)
						.Text(FText::FromString(Kind))
						.Font(IDetailLayoutBuilder::GetDetailFontBold())
						.ColorAndOpacity(FSlateColor(KindColor))
					]
				]
				+ SHorizontalBox::Slot()
				.FillWidth(1.0f)
				.VAlign(VAlign_Center)
				.Padding(4, 1)
				[
					SNew(STextBlock)
					.Text(FText::FromString(Signature))
					.Font(LexUIPrefabBehaviourViewerLocal::MonoFont())
					.ColorAndOpacity(FSlateColor(LexUIPrefabBehaviourViewerLocal::ValueColor))
				]
				+ SHorizontalBox::Slot()
				.AutoWidth()
				.VAlign(VAlign_Center)
				.Padding(4, 1)
				[
					SNew(STextBlock)
					.Text(FText::FromString(OwnerName))
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(LexUIPrefabBehaviourViewerLocal::DimColor))
				])
		];
		Shown++;
	};

	for (TFieldIterator<UFunction> It(BehaviourClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		UFunction* Function = *It;
		UClass* Owner = Function->GetOwnerClass();
		if (!Owner || !Owner->IsChildOf(ULexUIBehaviour::StaticClass()))
		{
			continue;
		}
		const FString OwnerName = Owner == BehaviourClass ? FString() : Owner->GetName();
		// Same masks the kismet compiler uses to classify events.
		constexpr uint32 EventMask = FUNC_Event | FUNC_BlueprintEvent | FUNC_Native;
		const uint32 Masked = Function->FunctionFlags & EventMask;
		if (Masked == (FUNC_Event | FUNC_BlueprintEvent))
		{
			AddRow(TEXT("Event"), AccentColor, DescribeFunctionSignature(Function), OwnerName);
		}
		else if (Masked == EventMask)
		{
			AddRow(TEXT("NativeEvent"), AccentColor, DescribeFunctionSignature(Function), OwnerName);
		}
		else if (Function->HasAllFunctionFlags(FUNC_BlueprintCallable) && !Function->HasAnyFunctionFlags(FUNC_Delegate))
		{
			AddRow(Function->HasAllFunctionFlags(FUNC_BlueprintPure) ? TEXT("Pure") : TEXT("Function"),
				ValueColor, DescribeFunctionSignature(Function), OwnerName);
		}
	}
	for (TFieldIterator<FMulticastDelegateProperty> It(BehaviourClass, EFieldIteratorFlags::IncludeSuper); It; ++It)
	{
		FMulticastDelegateProperty* Property = *It;
		UClass* Owner = Property->GetOwnerClass();
		if (!Owner || !Owner->IsChildOf(ULexUIBehaviour::StaticClass())
			|| Property->HasAnyPropertyFlags(CPF_Parm)
			|| !Property->HasAllPropertyFlags(CPF_BlueprintAssignable))
		{
			continue;
		}
		AddRow(TEXT("Delegate"), BoundColor, Property->GetName(),
			Owner == BehaviourClass ? FString() : Owner->GetName());
	}

	if (Total == 0)
	{
		Rows->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoProvides", "Nothing exposed."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
		];
	}
	ContentBox->AddSlot().AutoHeight()[MakeSection(
		FText::FromString(FString::Printf(TEXT("Provides — Events, Functions, Delegates (%d)"), Total)), Rows, /*collapsed*/true)];
}

void SLexUIPrefabBehaviourViewer::BuildSelectedWidgetSection()
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return;
	}
	ULexWidget* SelectedWidget = Editor->GetSelectedWidgets().Num() > 0 ? Editor->GetSelectedWidgets()[0].Get() : nullptr;
	TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);

	if (!IsValid(SelectedWidget))
	{
		Rows->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSelection", "Select a widget to see its bindable events and promote it to a variable."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
		];
		ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("SelectedWidgetSectionEmpty", "Selected Widget"), Rows)];
		return;
	}

	const TWeakObjectPtr<ULexWidget> SelectedWeak = SelectedWidget;
	Rows->AddSlot().AutoHeight().Padding(4, 2)
	[
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SButton)
			.Text(LOCTEXT("PromoteToVariable", "Promote To Variable"))
			.ToolTipText(LOCTEXT("PromoteToVariableTip", "Add an Instance-Editable variable on the companion behaviour typed to this widget and bind it (UMG \"Is Variable\")."))
			.IsEnabled(Editor->CanAuthorBehaviour())
			.OnClicked_Lambda([this, SelectedWeak]()
			{
				if (TSharedPtr<FLexUIPrefabEditor> LocalEditor = PrefabEditorPtr.Pin(); LocalEditor.IsValid() && SelectedWeak.IsValid())
				{
					LocalEditor->PromoteToBehaviourVariable(SelectedWeak.Get());
					Rebuild();
				}
				return FReply::Handled();
			})
		]
	];

	TArray<LexUIPrefabBehaviourUtils::FDiscoveredEvent> Events;
	LexUIPrefabBehaviourUtils::DiscoverEvents(SelectedWidget, Events);
	int32 Shown = 0;
	for (const LexUIPrefabBehaviourUtils::FDiscoveredEvent& Event : Events)
	{
		const FString OwnerClassName = IsValid(Event.Component) ? Event.Component->GetClass()->GetName() : TEXT("?");
		if (!MatchesFilter(OwnerClassName + TEXT(" ") + Event.DisplayName))
		{
			continue;
		}
		TSharedRef<SHorizontalBox> Row = SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(240)
				[
					SNew(STextBlock)
					.Text(FText::FromString(FString::Printf(TEXT("%s · %s"), *OwnerClassName, *Event.DisplayName)))
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(Event.bIsBound ? LOCTEXT("EventBound", "bound") : LOCTEXT("EventUnbound", "no handler"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(Event.bIsBound ? BoundColor : DimColor))
			];
		for (ELexUIBehaviourHandlerType HandlerType : { ELexUIBehaviourHandlerType::Function, ELexUIBehaviourHandlerType::Event })
		{
			const FText Label = HandlerType == ELexUIBehaviourHandlerType::Function
				? LOCTEXT("AddHandlerFunction", "+ Function")
				: LOCTEXT("AddHandlerEvent", "+ Event");
			Row->AddSlot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(4, 1)
			[
				SNew(SButton)
				.Text(Label)
				.ToolTipText(LOCTEXT("AddHandlerTip", "Generate a matching handler on the companion behaviour and wire this event to it (UMG \"Event +\")."))
				.IsEnabled(Editor->CanAddEventHandler(HandlerType))
				.OnClicked_Lambda([this, Event, HandlerType]()
				{
					if (TSharedPtr<FLexUIPrefabEditor> LocalEditor = PrefabEditorPtr.Pin())
					{
						LocalEditor->AddEventHandler(Event, HandlerType);
						Rebuild();
					}
					return FReply::Handled();
				})
			];
		}
		Rows->AddSlot().AutoHeight()[MakeStripedRow(Shown, Row)];
		Shown++;
	}
	if (Events.Num() == 0)
	{
		Rows->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoEvents", "No bindable events on this widget's behaviours."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
		];
	}
	ContentBox->AddSlot().AutoHeight()[MakeSection(
		FText::FromString(FString::Printf(TEXT("Selected Widget — %s"), *SelectedWidget->GetDisplayName())), Rows)];
}

void SLexUIPrefabBehaviourViewer::Rebuild()
{
	using namespace LexUIPrefabBehaviourViewerLocal;
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->ClearChildren();
	TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin();
	if (!Editor.IsValid())
	{
		return;
	}
	UClass* BehaviourClass = Editor->GetEffectiveBehaviourClass();
	ULexUIBehaviour* Primary = Editor->GetPrimaryBehaviour();

	FString Summary;
	if (BehaviourClass)
	{
		const bool bIsBlueprint = BehaviourClass->ClassGeneratedBy != nullptr;
		Summary = FString::Printf(TEXT("%s (%s)"), *BehaviourClass->GetName(), bIsBlueprint ? TEXT("Blueprint") : TEXT("Native"));
	}
	else
	{
		Summary = TEXT("No behaviour class — Open creates the companion blueprint.");
	}
	ContentBox->AddSlot().AutoHeight().Padding(8, 4)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Summary))
		.Font(IDetailLayoutBuilder::GetDetailFontBold())
		.ColorAndOpacity(FSlateColor(BehaviourClass ? ValueColor : WarnColor))
	];
	if (!BehaviourClass)
	{
		return;
	}

	BuildWidgetReferenceSection(BehaviourClass, Primary);
	BuildProvidesSection(BehaviourClass);
	BuildSelectedWidgetSection();
}

#undef LOCTEXT_NAMESPACE
