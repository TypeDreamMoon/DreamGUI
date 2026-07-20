// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "SLexUIPrefabPalette.h"
#include "LexUIPrefabEditor.h"
#include "LexUIEditorTools.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexRectBlock.h"
#include "Core/LexUISpriteData.h"
#include "Core/Components/LexLayout.h"
#include "PrefabSystem/LexUIPrefab.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "LexUIPrefabPalette"

SLexUIPrefabPalette::~SLexUIPrefabPalette()
{
	if (RegistryChangedHandle.IsValid())
	{
		FLexUIControlRegistry::Get().OnChanged().Remove(RegistryChangedHandle);
	}
}

void SLexUIPrefabPalette::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
	RegistryChangedHandle = FLexUIControlRegistry::Get().OnChanged().AddSP(SharedThis(this), &SLexUIPrefabPalette::RebuildList);

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 2, 2, 4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search elements"))
			.OnTextChanged(this, &SLexUIPrefabPalette::OnSearchTextChanged)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TreeView, STreeView<FItemPtr>)
			.SelectionMode(ESelectionMode::Single)
			.TreeItemsSource(&RootItems)
			.OnGenerateRow(this, &SLexUIPrefabPalette::OnGenerateRow)
			.OnGetChildren(this, &SLexUIPrefabPalette::OnGetChildren)
			.OnMouseButtonDoubleClick(this, &SLexUIPrefabPalette::OnItemDoubleClick)
		]
	];

	RebuildList();
}

ULexWidget* SLexUIPrefabPalette::GetSelectedWidget()const
{
	if (auto Editor = PrefabEditorPtr.Pin())
	{
		for (auto& Weak : Editor->GetSelectedWidgets())
		{
			if (Weak.IsValid())return Weak.Get();
		}
	}
	return nullptr;
}

void SLexUIPrefabPalette::CollectBasics(TArray<FItemPtr>& Out)
{
	auto Header = MakeShared<FPaletteItem>();
	Header->Kind = EItemKind::Category;
	Header->DisplayName = TEXT("Basic");

	auto MakeBasic = [](const FString& Name, UClass* VisualClass, bool bDefaultSprite = false)
	{
		auto Item = MakeShared<FPaletteItem>();
		Item->Kind = EItemKind::BasicWidget;
		Item->DisplayName = Name;
		Item->VisualClass = VisualClass;
		Item->bSetDefaultSprite = bDefaultSprite;
		return Item;
	};
	Header->Children.Add(MakeBasic(TEXT("Widget"), nullptr));
	Header->Children.Add(MakeBasic(TEXT("Text"), ULexText::StaticClass()));
	Header->Children.Add(MakeBasic(TEXT("Image"), ULexImage::StaticClass(), /*bDefaultSprite*/true));
	Header->Children.Add(MakeBasic(TEXT("RectBlock"), ULexRectBlock::StaticClass()));
	Out.Add(Header);
}

void SLexUIPrefabPalette::CollectControls(TArray<FItemPtr>& Out)
{
	TMap<FName, FItemPtr> CategoryMap;
	TArray<FName> CategoryOrder;
	for (const FLexUIControlDescriptor& Descriptor : FLexUIControlRegistry::Get().GetDescriptors())
	{
		FItemPtr& Header = CategoryMap.FindOrAdd(Descriptor.Category);
		if (!Header.IsValid())
		{
			Header = MakeShared<FPaletteItem>();
			Header->Kind = EItemKind::Category;
			Header->DisplayName = Descriptor.Category.ToString();
			CategoryOrder.Add(Descriptor.Category);
		}
		auto Item = MakeShared<FPaletteItem>();
		Item->Kind = Descriptor.CreationKind == ELexUIControlCreationKind::Prefab ? EItemKind::Prefab : EItemKind::Native;
		Item->DisplayName = Descriptor.DisplayName.ToString();
		Item->PrefabPath = Descriptor.PrefabPath;
		Item->NativeDescriptor = MakeShared<FLexUIControlDescriptor>(Descriptor);
		Item->bValid = FLexUIControlRegistry::Get().Validate(Descriptor, Item->ValidationError);
		Header->Children.Add(Item);
	}
	for (FName Category : CategoryOrder)
	{
		Out.Add(CategoryMap.FindChecked(Category));
	}
}

void SLexUIPrefabPalette::CollectPrefabs(TArray<FItemPtr>& Out)
{
	TArray<FAssetData> PrefabAssets;
	IAssetRegistry::GetChecked().GetAssetsByClass(ULexUIPrefab::StaticClass()->GetClassPathName(), PrefabAssets, true);

	FItemPtr Header;
	for (auto& AssetData : PrefabAssets)
	{
		// the plugin's built-in preset prefabs already live in the Controls section
		if (AssetData.PackageName.ToString().StartsWith(TEXT("/LGUI/Prefabs/")))continue;

		if (!Header.IsValid())
		{
			Header = MakeShared<FPaletteItem>();
			Header->Kind = EItemKind::Category;
			Header->DisplayName = TEXT("Prefabs");
		}
		auto Item = MakeShared<FPaletteItem>();
		Item->Kind = EItemKind::Prefab;
		Item->DisplayName = AssetData.AssetName.ToString();
		Item->PrefabPath = AssetData.GetSoftObjectPath().ToString();
		Item->PrefabAsset = AssetData;
		Header->Children.Add(Item);
	}
	if (Header.IsValid())
	{
		Header->Children.Sort([](const FItemPtr& A, const FItemPtr& B) { return A->DisplayName < B->DisplayName; });
		Out.Add(Header);
	}
}

void SLexUIPrefabPalette::RebuildList()
{
	RootItems.Reset();

	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	TArray<FItemPtr> AllGroups;
	CollectBasics(AllGroups);
	CollectControls(AllGroups);
	CollectPrefabs(AllGroups);

	if (!bFilterActive)
	{
		RootItems = MoveTemp(AllGroups);
	}
	else
	{
		// keep only children matching the search; drop empty groups
		for (auto& Group : AllGroups)
		{
			FItemPtr Filtered = MakeShared<FPaletteItem>();
			Filtered->Kind = EItemKind::Category;
			Filtered->DisplayName = Group->DisplayName;
			for (auto& Child : Group->Children)
			{
				if (SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(Child->DisplayName)))
				{
					Filtered->Children.Add(Child);
				}
			}
			if (Filtered->Children.Num() > 0)
			{
				RootItems.Add(Filtered);
			}
		}
	}

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		for (auto& Group : RootItems)
		{
			TreeView->SetItemExpansion(Group, true);
		}
	}
}

TSharedRef<ITableRow> SLexUIPrefabPalette::OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
{
	if (InItem->Kind == EItemKind::Category)
	{
		return SNew(STableRow<FItemPtr>, OwnerTable)
			.ShowSelection(false)
			.Padding(FMargin(4, 3))
			[
				SNew(STextBlock)
				.Font(FAppStyle::Get().GetFontStyle("SmallFontBold"))
				.Text(FText::FromString(InItem->DisplayName.ToUpper()))
				.ColorAndOpacity(FSlateColor::UseSubduedForeground())
			];
	}

	// Registry entries carry an explicit semantic icon. Basics and project prefabs
	// still resolve through their native class, matching UMG's palette behavior.
	const FSlateBrush* IconBrush = nullptr;
	if (InItem->NativeDescriptor.IsValid() && InItem->NativeDescriptor->Icon.IsSet())
	{
		IconBrush = InItem->NativeDescriptor->Icon.GetOptionalIcon();
	}
	if (!IconBrush)
	{
		UClass* IconClass = InItem->VisualClass.IsValid() ? InItem->VisualClass.Get()
			: InItem->PrefabAsset.IsValid() ? ULexUIPrefab::StaticClass() : ULexWidget::StaticClass();
		IconBrush = FSlateIconFinder::FindIconBrushForClass(IconClass);
	}
	const FText Tooltip = !InItem->bValid
		? InItem->ValidationError
		: InItem->Kind == EItemKind::Prefab
		? FText::Format(LOCTEXT("PrefabRowTooltip", "{0}\nDouble-click to add under the selected widget."), FText::FromString(InItem->PrefabPath))
		: FText::Format(LOCTEXT("BasicRowTooltip", "{0}\nDouble-click, or drag onto an Outliner widget, to add it under that widget."), FText::FromString(InItem->DisplayName));
	return SNew(STableRow<FItemPtr>, OwnerTable)
		.IsEnabled(InItem->bValid)
		.Padding(FMargin(2, 2))
		.ToolTipText(Tooltip)
		.OnDragDetected(FOnDragDetected::CreateSP(this, &SLexUIPrefabPalette::OnItemDragDetected, InItem))
		[
			SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.VAlign(VAlign_Center)
			.Padding(2, 0, 6, 0)
			[
				SNew(SImage)
				.Image(IconBrush)
				.ColorAndOpacity(FSlateColor::UseForeground())
				.DesiredSizeOverride(FVector2D(16, 16))
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			[
				SNew(STextBlock)
				.Text(FText::FromString(InItem->DisplayName))
			]
		];
}

void SLexUIPrefabPalette::OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SLexUIPrefabPalette::OnItemDoubleClick(FItemPtr InItem)
{
	if (!InItem.IsValid() || !InItem->bValid)return;
	if (InItem->Kind == EItemKind::Category)
	{
		if (TreeView.IsValid())TreeView->SetItemExpansion(InItem, !TreeView->IsItemExpanded(InItem));
		return;
	}
	CreateItem(InItem);
}

namespace SLexUIPrefabPaletteLocal
{
	// shared create path: the create primitives take a "get parent" function, so double-click
	// (parent = selection) and drop (parent = drop-target widget) reuse the same logic
	ULexWidget* CreateElement(bool bIsBasicWidget, UClass* VisualClass, bool bSetDefaultSprite,
		const TSharedPtr<FLexUIControlDescriptor>& NativeDescriptor, const FString& PrefabPath,
		const FString& DisplayName, TFunction<ULexWidget*()> GetParent,
		TFunction<void(ULexWidget*)> AfterCreate = nullptr)
	{
		if (bIsBasicWidget)
		{
			TFunction<void(ULexWidget*)> Callback = [bSetDefaultSprite, AfterCreate](ULexWidget* InWidget)
			{
				if (bSetDefaultSprite)
				{
					if (auto Image = Cast<ULexImage>(InWidget->GetVisual()))
					{
						Image->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
					}
				}
				if (AfterCreate)AfterCreate(InWidget);
			};
			return FLexUIEditorTools::CreateWidgetAndReturn(GetParent, DisplayName, VisualClass, Callback);
		}
		if (NativeDescriptor.IsValid())
		{
			return FLexUIEditorTools::CreateRegisteredControlAndReturn(GetParent, NativeDescriptor->Name, AfterCreate);
		}
		return FLexUIEditorTools::CreateUIControlsAndReturn(GetParent, PrefabPath, AfterCreate);
	}
}

ULexWidget* FLexUIPaletteDragDropOp::CreateUnder(ULexWidget* InParentWidget, TFunction<void(ULexWidget*)> AfterCreate)const
{
	if (InParentWidget == nullptr)return nullptr;
	return SLexUIPrefabPaletteLocal::CreateElement(bIsBasicWidget, VisualClass.Get(), bSetDefaultSprite,
		NativeDescriptor, PrefabPath, DisplayName, [InParentWidget]() -> ULexWidget* { return InParentWidget; }, AfterCreate);
}

void SLexUIPrefabPalette::CreateItem(FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->Kind == EItemKind::Category)return;
	// the create primitives no-op (and warn) when nothing suitable is selected
	SLexUIPrefabPaletteLocal::CreateElement(InItem->Kind == EItemKind::BasicWidget, InItem->VisualClass.Get(),
		InItem->bSetDefaultSprite, InItem->NativeDescriptor, InItem->PrefabPath, InItem->DisplayName,
		[this]() -> ULexWidget* { return GetSelectedWidget(); });
}

FReply SLexUIPrefabPalette::OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem)
{
	if (!InItem.IsValid() || !InItem->bValid || InItem->Kind == EItemKind::Category)return FReply::Unhandled();
	auto Op = MakeShared<FLexUIPaletteDragDropOp>();
	Op->bIsBasicWidget = (InItem->Kind == EItemKind::BasicWidget);
	Op->VisualClass = InItem->VisualClass;
	Op->bSetDefaultSprite = InItem->bSetDefaultSprite;
	Op->NativeDescriptor = InItem->NativeDescriptor;
	Op->PrefabPath = InItem->PrefabPath;
	Op->DisplayName = InItem->DisplayName;
	Op->Construct();
	Op->SetToolTip(FText::FromString(InItem->DisplayName), nullptr);
	return FReply::Handled().BeginDragDrop(Op);
}

void SLexUIPrefabPalette::OnSearchTextChanged(const FText& InText)
{
	SearchFilter.SetFilterText(InText);
	RebuildList();
}

#undef LOCTEXT_NAMESPACE
