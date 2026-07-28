// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "LexUIPrefabOverridesViewer.h"
#include "Core/Components/LexWidget.h"
#include "Core/LexUIBehaviour.h"
#include "DetailLayoutBuilder.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LexUIPrefabEditor.h"
#include "Modules/ModuleManager.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SComboButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Layout/SSplitter.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabOverridesViewer"

namespace LexUIPrefabOverridesViewerLocal
{
	FSlateFontInfo MonoFont() { return FCoreStyle::GetDefaultFontStyle("Mono", 9); }

	const FLinearColor ValueColor(0.85f, 0.85f, 0.85f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor DeadColor(1.0f, 0.35f, 0.35f);
	const FLinearColor AccentColor(0.55f, 0.75f, 1.0f);
	const FLinearColor WarnColor(1.0f, 0.75f, 0.35f);
	const FLinearColor SelectedRowColor(0.25f, 0.45f, 0.75f, 0.35f);

	/** "WidgetName · ClassName" — enough to find the object in the hierarchy and know what kind it is. */
	FString DescribeOverrideObject(UObject* Object)
	{
		if (!IsValid(Object))
		{
			return TEXT("DEAD OBJECT — target gone; entry is stale");
		}
		if (const ULexWidget* AsWidget = Cast<ULexWidget>(Object))
		{
			return FString::Printf(TEXT("%s · %s"), *AsWidget->GetDisplayName(), *Object->GetClass()->GetName());
		}
		if (const ULexWidget* OwnerWidget = Object->GetTypedOuter<ULexWidget>())
		{
			return FString::Printf(TEXT("%s · %s"), *OwnerWidget->GetDisplayName(), *Object->GetClass()->GetName());
		}
		return FString::Printf(TEXT("%s · %s"), *Object->GetName(), *Object->GetClass()->GetName());
	}

	FString JoinPropertyNames(const TArray<FName>& Names)
	{
		return FString::JoinBy(Names, TEXT(", "), [](const FName& Name) { return Name.ToString(); });
	}
}

void SLexUIPrefabOverridesViewer::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject)
{
	using namespace LexUIPrefabOverridesViewerLocal;
	PrefabEditorPtr = InPrefabEditorPtr;
	PrefabWeak = Cast<ULexUIPrefab>(InObject);

	FPropertyEditorModule& EditModule = FModuleManager::Get().GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	FDetailsViewArgs DetailsViewArgs;
	{
		DetailsViewArgs.bAllowSearch = true;
		DetailsViewArgs.bShowOptions = false;
		DetailsViewArgs.bAllowMultipleTopLevelObjects = false;
		DetailsViewArgs.bAllowFavoriteSystem = false;
		DetailsViewArgs.bHideSelectionTip = true;
	}
	ObjectDetailView = EditModule.CreateDetailView(DetailsViewArgs);

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
				.Text(LOCTEXT("Refresh", "Refresh"))
				.ToolTipText(LOCTEXT("RefreshTip", "Re-read the override table from the asset's current state."))
				.OnClicked_Lambda([this]() { Rebuild(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter by widget, class, or property name..."))
				.OnTextChanged_Lambda([this](const FText& Text)
				{
					FilterString = Text.ToString();
					Rebuild();
				})
			]
		]
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(8, 0, 8, 4)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("Hint", "A pinned property shadows any later edit made in the sub-prefab asset itself. Click an object to select it and edit its properties below; use a row's Revert menu to un-pin."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
			.AutoWrapText(true)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SNew(SSplitter)
			.Orientation(Orient_Vertical)
			+ SSplitter::Slot()
			.Value(0.6f)
			[
				SNew(SScrollBox)
				+ SScrollBox::Slot()
				[
					SAssignNew(ContentBox, SVerticalBox)
				]
			]
			+ SSplitter::Slot()
			.Value(0.4f)
			[
				SNew(SVerticalBox)
				+ SVerticalBox::Slot()
				.AutoHeight()
				.Padding(8, 4, 8, 2)
				[
					SNew(STextBlock)
					.Text_Lambda([this]()
					{
						UObject* Object = SelectedObject.Get();
						return Object
							? FText::Format(LOCTEXT("SelectedObjectFmt", "Editing: {0} — changes on a nested instance are recorded as overrides."),
								FText::FromString(LexUIPrefabOverridesViewerLocal::DescribeOverrideObject(Object)))
							: LOCTEXT("NoSelectedObject", "Click an object above to inspect and edit its properties here.");
					})
					.Font(IDetailLayoutBuilder::GetDetailFont())
					.ColorAndOpacity(FSlateColor(DimColor))
					.AutoWrapText(true)
				]
				+ SVerticalBox::Slot()
				.FillHeight(1.0f)
				[
					ObjectDetailView.ToSharedRef()
				]
			]
		]
	];
	Rebuild();
}

bool SLexUIPrefabOverridesViewer::MatchesFilter(const FString& Haystack) const
{
	return FilterString.IsEmpty() || Haystack.Contains(FilterString);
}

ULexWidget* SLexUIPrefabOverridesViewer::ResolveOwnerWidget(UObject* Object)
{
	if (!IsValid(Object))
	{
		return nullptr;
	}
	if (ULexWidget* AsWidget = Cast<ULexWidget>(Object))
	{
		return AsWidget;
	}
	if (ULexWidget* OuterWidget = Object->GetTypedOuter<ULexWidget>())
	{
		return OuterWidget;
	}
	if (ULexUIBehaviour* AsBehaviour = Cast<ULexUIBehaviour>(Object))
	{
		return AsBehaviour->GetWidget();
	}
	return nullptr;
}

void SLexUIPrefabOverridesViewer::SelectOverrideObject(UObject* Object)
{
	SelectedObject = Object;
	if (ObjectDetailView.IsValid())
	{
		ObjectDetailView->SetObject(Object);
	}
	if (ULexWidget* Widget = ResolveOwnerWidget(Object))
	{
		if (TSharedPtr<FLexUIPrefabEditor> Editor = PrefabEditorPtr.Pin())
		{
			Editor->SelectWidgets(TSet<ULexWidget*>{ Widget }, false);
		}
	}
}

void SLexUIPrefabOverridesViewer::Rebuild()
{
	using namespace LexUIPrefabOverridesViewerLocal;
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->ClearChildren();
	ULexUIPrefab* Prefab = PrefabWeak.Get();
	ULexUIPrefabHelperObject* Helper = IsValid(Prefab) ? Prefab->GetPrefabHelperObject() : nullptr;
	if (!Helper)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8)
		[
			SNew(STextBlock).Text(LOCTEXT("NoHelper", "Prefab hierarchy not loaded."))
		];
		return;
	}

	int32 TotalObjects = 0;
	int32 TotalProperties = 0;
	int32 DeadObjects = 0;
	for (const TPair<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& Pair : Helper->SubPrefabMap)
	{
		for (const FLexUIPrefabOverrideParameterData& Override : Pair.Value.ObjectOverrideParameterArray)
		{
			TotalObjects++;
			TotalProperties += Override.MemberPropertyNames.Num();
			if (!IsValid(Override.Object.Get()))
			{
				DeadObjects++;
			}
		}
	}

	FString Summary = FString::Printf(TEXT("%d sub-prefab instance(s) — %d pinned object(s), %d pinned propert(ies)"),
		Helper->SubPrefabMap.Num(), TotalObjects, TotalProperties);
	if (DeadObjects > 0)
	{
		Summary += FString::Printf(TEXT(", %d DEAD"), DeadObjects);
	}
	ContentBox->AddSlot().AutoHeight().Padding(8, 4)
	[
		SNew(STextBlock)
		.Text(FText::FromString(Summary))
		.Font(IDetailLayoutBuilder::GetDetailFontBold())
		.ColorAndOpacity(FSlateColor(DeadObjects > 0 ? WarnColor : ValueColor))
	];

	if (Helper->SubPrefabMap.Num() == 0)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(LOCTEXT("NoSubPrefabs", "This prefab has no nested sub-prefab instances, so nothing can be pinned."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
		];
		return;
	}

	const bool bFiltering = !FilterString.IsEmpty();
	int32 ShownInstances = 0;
	for (const TPair<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& Pair : Helper->SubPrefabMap)
	{
		const ULexWidget* InstanceRoot = Pair.Key.Get();
		const FLexUISubPrefabData& Data = Pair.Value;
		const FString RootName = InstanceRoot ? InstanceRoot->GetDisplayName() : TEXT("<missing root>");
		const FString AssetName = GetNameSafe(Data.PrefabAsset);
		// A filter hit on the instance itself keeps every entry visible; otherwise entries filter individually.
		const bool bInstanceMatches = MatchesFilter(RootName + TEXT(" ") + AssetName);

		int32 InstanceProperties = 0;
		for (const FLexUIPrefabOverrideParameterData& Override : Data.ObjectOverrideParameterArray)
		{
			InstanceProperties += Override.MemberPropertyNames.Num();
		}

		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		int32 ShownRows = 0;
		for (int32 Index = 0; Index < Data.ObjectOverrideParameterArray.Num(); Index++)
		{
			const FLexUIPrefabOverrideParameterData& Override = Data.ObjectOverrideParameterArray[Index];
			UObject* Object = Override.Object.Get();
			const bool bDead = !IsValid(Object);
			const FString ObjectLabel = DescribeOverrideObject(Object);
			const FString PropertyLabel = JoinPropertyNames(Override.MemberPropertyNames);
			if (!bInstanceMatches && !MatchesFilter(ObjectLabel + TEXT(" ") + PropertyLabel))
			{
				continue;
			}
			const TWeakObjectPtr<UObject> ObjectWeak = Object;
			const int32 StripeIndex = ShownRows;
			Rows->AddSlot().AutoHeight()
			[
				SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
				.BorderBackgroundColor_Lambda([this, ObjectWeak, StripeIndex]()
				{
					if (ObjectWeak.IsValid() && SelectedObject == ObjectWeak)
					{
						return FSlateColor(SelectedRowColor);
					}
					return FSlateColor(StripeIndex % 2 == 0 ? FLinearColor(1, 1, 1, 0.04f) : FLinearColor::Transparent);
				})
				.Padding(FMargin(4, 2))
				[
					SNew(SHorizontalBox)
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 1)
					[
						SNew(SBox)
						.WidthOverride(260)
						[
							SNew(SButton)
							.ButtonStyle(FAppStyle::Get(), "SimpleButton")
							.ToolTipText(LOCTEXT("SelectObjectTip", "Select this object in the prefab editor and load it into the property panel below."))
							.IsEnabled(!bDead)
							.OnClicked_Lambda([this, ObjectWeak]()
							{
								SelectOverrideObject(ObjectWeak.Get());
								return FReply::Handled();
							})
							[
								SNew(STextBlock)
								.Text(FText::FromString(ObjectLabel))
								.Font(IDetailLayoutBuilder::GetDetailFontBold())
								.ColorAndOpacity(FSlateColor(bDead ? DeadColor : ValueColor))
							]
						]
					]
					+ SHorizontalBox::Slot()
					.FillWidth(1.0f)
					.VAlign(VAlign_Center)
					.Padding(4, 1)
					[
						SNew(STextBlock)
						.Text(FText::FromString(PropertyLabel.IsEmpty() ? TEXT("(no properties recorded)") : PropertyLabel))
						.Font(MonoFont())
						.ColorAndOpacity(FSlateColor(bDead ? DeadColor : DimColor))
						.AutoWrapText(true)
					]
					+ SHorizontalBox::Slot()
					.AutoWidth()
					.VAlign(VAlign_Center)
					.Padding(4, 1)
					[
						SNew(SComboButton)
						.ToolTipText(LOCTEXT("RevertMenuTip", "Un-pin properties on this object so the sub-prefab asset's values apply again."))
						.ButtonContent()
						[
							SNew(STextBlock)
							.Text(LOCTEXT("RevertMenu", "Revert"))
							.Font(IDetailLayoutBuilder::GetDetailFont())
						]
						.OnGetMenuContent_Lambda([this, ObjectWeak, Names = Override.MemberPropertyNames]()
						{
							FMenuBuilder MenuBuilder(/*bInShouldCloseWindowAfterMenuSelection*/true, nullptr);
							MenuBuilder.BeginSection(NAME_None, LOCTEXT("RevertSingleSection", "Revert single property"));
							for (const FName& PropertyName : Names)
							{
								MenuBuilder.AddMenuEntry(
									FText::FromName(PropertyName),
									FText::Format(LOCTEXT("RevertPropertyTip", "Un-pin {0}; the value from the sub-prefab asset applies again."), FText::FromName(PropertyName)),
									FSlateIcon(),
									FUIAction(FExecuteAction::CreateLambda([this, ObjectWeak, PropertyName]()
									{
										ULexUIPrefab* LocalPrefab = PrefabWeak.Get();
										ULexUIPrefabHelperObject* LocalHelper = IsValid(LocalPrefab) ? LocalPrefab->GetPrefabHelperObject() : nullptr;
										if (LocalHelper && ObjectWeak.IsValid())
										{
											LocalHelper->RevertPrefabOverride(ObjectWeak.Get(), { PropertyName });
										}
										Rebuild();
									})));
							}
							MenuBuilder.EndSection();
							MenuBuilder.BeginSection(NAME_None, LOCTEXT("RevertAllSection", "Whole object"));
							MenuBuilder.AddMenuEntry(
								LOCTEXT("RevertAllOnObject", "Revert all on this object"),
								LOCTEXT("RevertAllOnObjectTip", "Un-pin every property recorded on this object."),
								FSlateIcon(),
								FUIAction(FExecuteAction::CreateLambda([this, ObjectWeak]()
								{
									ULexUIPrefab* LocalPrefab = PrefabWeak.Get();
									ULexUIPrefabHelperObject* LocalHelper = IsValid(LocalPrefab) ? LocalPrefab->GetPrefabHelperObject() : nullptr;
									if (LocalHelper && ObjectWeak.IsValid())
									{
										LocalHelper->RevertAllPrefabOverride(ObjectWeak.Get());
									}
									Rebuild();
								})));
							MenuBuilder.EndSection();
							return MenuBuilder.MakeWidget();
						})
					]
				]
			];
			ShownRows++;
		}
		// An instance whose name matched (or an unfiltered view) still deserves its section when it
		// has nothing pinned — "exists, no overrides" is the answer, not "nothing matches".
		if (ShownRows == 0 && !bInstanceMatches)
		{
			continue;
		}
		if (ShownRows == 0)
		{
			Rows->AddSlot().AutoHeight().Padding(8, 2)
			[
				SNew(STextBlock)
				.Text(LOCTEXT("NoPinnedOverrides", "(no pinned overrides)"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(DimColor))
			];
		}
		ShownInstances++;

		const int32 TotalRows = Data.ObjectOverrideParameterArray.Num();
		const FString CountLabel = (bFiltering && ShownRows < TotalRows)
			? FString::Printf(TEXT("%d/%d object(s) match, %d propert(ies) total"), ShownRows, TotalRows, InstanceProperties)
			: FString::Printf(TEXT("%d object(s), %d propert(ies)"), TotalRows, InstanceProperties);
		const FString Header = FString::Printf(TEXT("%s — %s   (%s)"),
			*RootName, *AssetName, *CountLabel);
		ContentBox->AddSlot().AutoHeight().Padding(0, 0, 0, 2)
		[
			SNew(SExpandableArea)
			.InitiallyCollapsed(false)
			.BorderBackgroundColor(FLinearColor(1, 1, 1, 0.05f))
			.Padding(FMargin(8, 4))
			.HeaderContent()
			[
				SNew(STextBlock)
				.Text(FText::FromString(Header))
				.Font(IDetailLayoutBuilder::GetDetailFontBold())
				.ColorAndOpacity(FSlateColor(AccentColor))
			]
			.BodyContent()
			[
				Rows
			]
		];
	}
	if (bFiltering && ShownInstances == 0)
	{
		ContentBox->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock)
			.Text(FText::Format(LOCTEXT("NoMatchesFmt", "Nothing matches \"{0}\"."), FText::FromString(FilterString)))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(DimColor))
		];
	}
}

#undef LOCTEXT_NAMESPACE
