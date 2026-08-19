// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

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
	// May already be gone during editor shutdown, hence Get() rather than GetChecked().
	if (IAssetRegistry* AssetRegistry = IAssetRegistry::Get())
	{
		AssetRegistry->OnAssetAdded().Remove(AssetAddedHandle);
		AssetRegistry->OnAssetRemoved().Remove(AssetRemovedHandle);
		AssetRegistry->OnAssetRenamed().Remove(AssetRenamedHandle);
		AssetRegistry->OnFilesLoaded().Remove(FilesLoadedHandle);
	}
}

void SLexUIPrefabPalette::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
	RegistryChangedHandle = FLexUIControlRegistry::Get().OnChanged().AddSP(SharedThis(this), &SLexUIPrefabPalette::RebuildList);
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(SharedThis(this), &SLexUIPrefabPalette::HandlePrefabAssetChanged);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(SharedThis(this), &SLexUIPrefabPalette::HandlePrefabAssetChanged);
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddSP(SharedThis(this), &SLexUIPrefabPalette::HandlePrefabAssetRenamed);
	FilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddSP(SharedThis(this), &SLexUIPrefabPalette::HandleAssetRegistryFilesLoaded);

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

void SLexUIPrefabPalette::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
{
	SCompoundWidget::Tick(AllottedGeometry, InCurrentTime, InDeltaTime);
	// Importing or deleting a folder of prefabs fires one event per asset; rebuilding the whole tree
	// for each of them would stall the panel, so a burst collapses into a single rebuild.
	if (bRebuildRequested && InCurrentTime >= NextRebuildTime)
	{
		bRebuildRequested = false;
		NextRebuildTime = InCurrentTime + 0.25;
		RebuildList();
	}
}

void SLexUIPrefabPalette::RequestRebuild()
{
	bRebuildRequested = true;
}

void SLexUIPrefabPalette::HandlePrefabAssetChanged(const FAssetData& InAssetData)
{
	if (InAssetData.IsInstanceOf(ULexUIPrefab::StaticClass()))
	{
		RequestRebuild();
	}
}

void SLexUIPrefabPalette::HandlePrefabAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	HandlePrefabAssetChanged(InAssetData);
}

void SLexUIPrefabPalette::HandleAssetRegistryFilesLoaded()
{
	// Every prefab-backed control fails validation against a registry that has not finished scanning.
	RequestRebuild();
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
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	TArray<FAssetData> PrefabAssets;
	AssetRegistry.GetAssetsByClass(ULexUIPrefab::StaticClass()->GetClassPathName(), PrefabAssets, true);

	ULexUIPrefab* EditingPrefab = nullptr;
	if (auto Editor = PrefabEditorPtr.Pin())EditingPrefab = Editor->GetPrefabBeingEdited();
	const FName EditingPackage = IsValid(EditingPrefab) ? EditingPrefab->GetOutermost()->GetFName() : NAME_None;

	// Whether a candidate already nests the prefab being edited is a question only the loaded asset
	// can answer, and loading every prefab in the project to ask it would stall the panel. Nesting
	// implies a package reference, so the registry's referencer closure narrows the candidates that
	// are worth a load down to the handful that could possibly be cyclic.
	TSet<FName> ReferencingPackages;
	if (EditingPackage != NAME_None)
	{
		TArray<FName> Pending;
		Pending.Add(EditingPackage);
		while (Pending.Num() > 0)
		{
			TArray<FName> Referencers;
			AssetRegistry.GetReferencers(Pending.Pop(), Referencers, UE::AssetRegistry::EDependencyCategory::Package);
			for (FName Referencer : Referencers)
			{
				bool bAlreadyKnown = false;
				ReferencingPackages.Add(Referencer, &bAlreadyKnown);
				if (!bAlreadyKnown)Pending.Add(Referencer);
			}
		}
	}

	FItemPtr Header;
	for (auto& AssetData : PrefabAssets)
	{
		// the plugin's built-in preset prefabs already live in the Controls section
		if (AssetData.PackageName.ToString().StartsWith(TEXT("/LGUI/Prefabs/")))continue;
		if (AssetData.PackageName == EditingPackage)continue;
		if (ReferencingPackages.Contains(AssetData.PackageName))
		{
			auto Candidate = Cast<ULexUIPrefab>(AssetData.GetAsset());
			if (Candidate == nullptr || Candidate->IsPrefabBelongsToThisSubPrefab(EditingPrefab, true))continue;
		}

		if (!Header.IsValid())
		{
			Header = MakeShared<FPaletteItem>();
			Header->Kind = EItemKind::Category;
			Header->DisplayName = TEXT("Prefabs");
		}
		auto Item = MakeShared<FPaletteItem>();
		Item->Kind = EItemKind::ProjectPrefab;
		Item->DisplayName = AssetData.AssetName.ToString();
		Item->PrefabPath = AssetData.GetSoftObjectPath().ToString();
		Item->PrefabAsset = AssetData;
		// Only prefabs already in memory can be version-checked for free; the rest are rejected by
		// FLexUIEditorTools::CanNestPrefabUnderWidget at create time, which is the guard that counts.
		if (auto Loaded = Cast<ULexUIPrefab>(AssetData.FastGetAsset(false)))
		{
			if (Loaded->PrefabVersion <= (uint16)ELexUIPrefabVersion::OldVersion)
			{
				Item->bValid = false;
				Item->ValidationError = FText::Format(LOCTEXT("OldPrefabVersion", "{0}\nThis prefab's version is too old. Open it and hit \"Save\" to upgrade it."), FText::FromString(Item->PrefabPath));
			}
		}
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
	// Two gestures on one asset used to mean opposite things with nothing saying so. Now the palette
	// links like the Content Browser does, and the row says which of the two it is.
	const FText Tooltip = !InItem->bValid
		? InItem->ValidationError
		: InItem->Kind == EItemKind::ProjectPrefab
		? FText::Format(LOCTEXT("ProjectPrefabRowTooltip", "{0}\nDouble-click, or drag onto a Hierarchy widget, to add it as a linked sub-prefab."), FText::FromString(InItem->PrefabPath))
		: InItem->Kind == EItemKind::Prefab
		? FText::Format(LOCTEXT("PrefabRowTooltip", "{0}\nDouble-click to add a copy under the selected widget."), FText::FromString(InItem->PrefabPath))
		: FText::Format(LOCTEXT("BasicRowTooltip", "{0}\nDouble-click, or drag onto a Hierarchy widget, to add it under that widget."), FText::FromString(InItem->DisplayName));
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
		bool bLinkedSubPrefab, const FString& DisplayName, TFunction<ULexWidget*()> GetParent,
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
		// A project asset dropped as a flattened copy loses its override tracking and every route
		// back to the source; only the plugin's own recipes mean to be baked in.
		if (bLinkedSubPrefab)
		{
			return FLexUIEditorTools::CreateSubPrefabAndReturn(GetParent, PrefabPath, AfterCreate);
		}
		return FLexUIEditorTools::CreateUIControlsAndReturn(GetParent, PrefabPath, AfterCreate);
	}
}

ULexWidget* FLexUIPaletteDragDropOp::CreateUnder(ULexWidget* InParentWidget, TOptional<int32> InSiblingIndex,
	TFunction<void(ULexWidget*)> AfterCreate)const
{
	if (InParentWidget == nullptr)return nullptr;
	// Place inside the creation callback rather than after the call returns: the create primitives
	// run it while their own transaction is still open, so creating and positioning collapse into
	// one undo step instead of leaving a half-placed widget behind on Ctrl+Z.
	TFunction<void(ULexWidget*)> PlaceThenForward =
		[InSiblingIndex, AfterCreate = MoveTemp(AfterCreate)](ULexWidget* InWidget)
		{
			if (InWidget != nullptr && InSiblingIndex.IsSet())
			{
				InWidget->SetSiblingIndex(InSiblingIndex.GetValue());
			}
			if (AfterCreate)
			{
				AfterCreate(InWidget);
			}
		};
	return SLexUIPrefabPaletteLocal::CreateElement(bIsBasicWidget, VisualClass.Get(), bSetDefaultSprite,
		NativeDescriptor, PrefabPath, bLinkedSubPrefab, DisplayName, [InParentWidget]() -> ULexWidget* { return InParentWidget; },
		MoveTemp(PlaceThenForward));
}

void SLexUIPrefabPalette::CreateItem(FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->Kind == EItemKind::Category)return;
	// With nothing selected, fall back to the prefab root -- the same "add to root" the hierarchy's
	// empty-area drop performs. Double-click used to be a no-op here, which reads as a dead panel.
	SLexUIPrefabPaletteLocal::CreateElement(InItem->Kind == EItemKind::BasicWidget, InItem->VisualClass.Get(),
		InItem->bSetDefaultSprite, InItem->NativeDescriptor, InItem->PrefabPath, InItem->Kind == EItemKind::ProjectPrefab, InItem->DisplayName,
		[this]() -> ULexWidget*
		{
			if (ULexWidget* Selected = GetSelectedWidget())return Selected;
			if (auto Editor = PrefabEditorPtr.Pin())return Editor->GetLoadedRootWidget();
			return nullptr;
		});
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
	Op->bLinkedSubPrefab = (InItem->Kind == EItemKind::ProjectPrefab);
	Op->DisplayName = InItem->DisplayName;
	Op->Construct();
	Op->SetToolTip(FText::FromString(InItem->DisplayName), nullptr);
	// Without a default recorded, ResetToDefaultToolTip clears the decorator, so a handler that
	// writes CurrentHoverText on hover has nothing to fall back to when the pointer leaves it.
	Op->SetupDefaults();
	return FReply::Handled().BeginDragDrop(Op);
}

void SLexUIPrefabPalette::OnSearchTextChanged(const FText& InText)
{
	SearchFilter.SetFilterText(InText);
	RebuildList();
}

#undef LOCTEXT_NAMESPACE
