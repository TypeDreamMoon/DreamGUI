// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "SDreamUIPrefabPalette.h"
#include "Core/DreamGUISettings.h"
#include "DreamUIPrefabEditor.h"
#include "DreamUIEditorTools.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/DreamUISpriteData.h"
#include "Core/Components/DreamLayout.h"
#include "PrefabSystem/DreamUIPrefab.h"

#include "AssetRegistry/IAssetRegistry.h"
#include "Misc/ConfigCacheIni.h"
#include "Styling/AppStyle.h"
#include "Styling/SlateIconFinder.h"
#include "Widgets/Images/SImage.h"
#include "Widgets/Input/SCheckBox.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/STableRow.h"

#define LOCTEXT_NAMESPACE "DreamUIPrefabPalette"

namespace DreamUIPalette
{
	const TCHAR* FavoritesGroupName = TEXT("Favorites");

	FString MakeFavoriteKey(const FPaletteItem& Item)
	{
		switch (Item.Kind)
		{
		case EItemKind::Category:
			return FString();
		case EItemKind::BasicWidget:
			return FString::Printf(TEXT("Basic:%s"), *Item.DisplayName);
		case EItemKind::ProjectPrefab:
			return FString::Printf(TEXT("Asset:%s"), *Item.PrefabPath);
		default:
			// Registry entries are keyed by the registry's Name, not by the label they show: relabeling
			// a control, or moving it to another category, must not drop the star off it.
			return FString::Printf(TEXT("Control:%s"), Item.NativeDescriptor.IsValid()
				? *Item.NativeDescriptor->Name.ToString() : *Item.DisplayName);
		}
	}

	bool ShouldExpandGroup(bool bFilterActive, bool bWasCollapsed)
	{
		return bFilterActive || !bWasCollapsed;
	}

	void BuildRootItems(const TArray<FItemPtr>& InAllGroups, const TSet<FString>& InFavorites,
		bool bFilterActive, TFunctionRef<bool(const FPaletteItem&)> InMatchesFilter, TArray<FItemPtr>& OutRootItems)
	{
		OutRootItems.Reset();
		FItemPtr FavoritesGroup = MakeShared<FPaletteItem>();
		FavoritesGroup->Kind = EItemKind::Category;
		FavoritesGroup->DisplayName = FavoritesGroupName;
		for (const FItemPtr& Group : InAllGroups)
		{
			if (!Group.IsValid())continue;
			FItemPtr Shown = MakeShared<FPaletteItem>();
			Shown->Kind = EItemKind::Category;
			Shown->DisplayName = Group->DisplayName;
			for (const FItemPtr& Child : Group->Children)
			{
				if (!Child.IsValid())continue;
				if (bFilterActive && !InMatchesFilter(*Child))continue;
				Shown->Children.Add(Child);
				if (!Child->FavoriteKey.IsEmpty() && InFavorites.Contains(Child->FavoriteKey))
				{
					FavoritesGroup->Children.Add(MakeShared<FPaletteItem>(*Child));
				}
			}
			if (Shown->Children.Num() > 0)
			{
				OutRootItems.Add(Shown);
			}
		}
		if (FavoritesGroup->Children.Num() > 0)
		{
			OutRootItems.Insert(FavoritesGroup, 0);
		}
	}
}

SDreamUIPrefabPalette::~SDreamUIPrefabPalette()
{
	if (RegistryChangedHandle.IsValid())
	{
		FDreamUIControlRegistry::Get().OnChanged().Remove(RegistryChangedHandle);
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

void SDreamUIPrefabPalette::Construct(const FArguments& InArgs, TSharedPtr<FDreamUIPrefabEditor> InPrefabEditor)
{
	PrefabEditorPtr = InPrefabEditor;
	RegistryChangedHandle = FDreamUIControlRegistry::Get().OnChanged().AddSP(SharedThis(this), &SDreamUIPrefabPalette::RebuildList);
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	AssetAddedHandle = AssetRegistry.OnAssetAdded().AddSP(SharedThis(this), &SDreamUIPrefabPalette::HandlePrefabAssetChanged);
	AssetRemovedHandle = AssetRegistry.OnAssetRemoved().AddSP(SharedThis(this), &SDreamUIPrefabPalette::HandlePrefabAssetChanged);
	AssetRenamedHandle = AssetRegistry.OnAssetRenamed().AddSP(SharedThis(this), &SDreamUIPrefabPalette::HandlePrefabAssetRenamed);
	FilesLoadedHandle = AssetRegistry.OnFilesLoaded().AddSP(SharedThis(this), &SDreamUIPrefabPalette::HandleAssetRegistryFilesLoaded);
	LoadPreferences();

	ChildSlot
	[
		SNew(SVerticalBox)
		+ SVerticalBox::Slot()
		.AutoHeight()
		.Padding(2, 2, 2, 4)
		[
			SNew(SSearchBox)
			.HintText(LOCTEXT("SearchHint", "Search elements"))
			.OnTextChanged(this, &SDreamUIPrefabPalette::OnSearchTextChanged)
		]
		+ SVerticalBox::Slot()
		.FillHeight(1.0f)
		[
			SAssignNew(TreeView, STreeView<FItemPtr>)
			.SelectionMode(ESelectionMode::Single)
			.TreeItemsSource(&RootItems)
			.OnGenerateRow(this, &SDreamUIPrefabPalette::OnGenerateRow)
			.OnGetChildren(this, &SDreamUIPrefabPalette::OnGetChildren)
			.OnExpansionChanged(this, &SDreamUIPrefabPalette::OnGroupExpansionChanged)
			.OnMouseButtonDoubleClick(this, &SDreamUIPrefabPalette::OnItemDoubleClick)
		]
	];

	RebuildList();
}

void SDreamUIPrefabPalette::Tick(const FGeometry& AllottedGeometry, const double InCurrentTime, const float InDeltaTime)
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

void SDreamUIPrefabPalette::RequestRebuild()
{
	bRebuildRequested = true;
}

void SDreamUIPrefabPalette::HandlePrefabAssetChanged(const FAssetData& InAssetData)
{
	if (InAssetData.IsInstanceOf(UDreamUIPrefab::StaticClass()))
	{
		RequestRebuild();
	}
}

void SDreamUIPrefabPalette::HandlePrefabAssetRenamed(const FAssetData& InAssetData, const FString& InOldObjectPath)
{
	HandlePrefabAssetChanged(InAssetData);
}

void SDreamUIPrefabPalette::HandleAssetRegistryFilesLoaded()
{
	// Every prefab-backed control fails validation against a registry that has not finished scanning.
	RequestRebuild();
}

UDreamWidget* SDreamUIPrefabPalette::GetSelectedWidget()const
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

void SDreamUIPrefabPalette::CollectBasics(TArray<FItemPtr>& Out)
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
		Item->FavoriteKey = DreamUIPalette::MakeFavoriteKey(*Item);
		return Item;
	};
	Header->Children.Add(MakeBasic(TEXT("Widget"), nullptr));
	Header->Children.Add(MakeBasic(TEXT("Text"), UDreamText::StaticClass()));
	Header->Children.Add(MakeBasic(TEXT("Image"), UDreamImage::StaticClass(), /*bDefaultSprite*/true));
	Header->Children.Add(MakeBasic(TEXT("RectBlock"), UDreamRectBlock::StaticClass()));
	Out.Add(Header);
}

void SDreamUIPrefabPalette::CollectControls(TArray<FItemPtr>& Out)
{
	TMap<FName, FItemPtr> CategoryMap;
	TArray<FName> CategoryOrder;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
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
		Item->Kind = Descriptor.CreationKind == EDreamUIControlCreationKind::WidgetClass ? EItemKind::Prefab : EItemKind::Native;
		Item->DisplayName = Descriptor.DisplayName.ToString();
		Item->PrefabPath = Descriptor.WidgetClassPath;
		Item->NativeDescriptor = MakeShared<FDreamUIControlDescriptor>(Descriptor);
		Item->bValid = FDreamUIControlRegistry::Get().Validate(Descriptor, Item->ValidationError);
		Item->FavoriteKey = DreamUIPalette::MakeFavoriteKey(*Item);
		Header->Children.Add(Item);
	}
	for (FName Category : CategoryOrder)
	{
		Out.Add(CategoryMap.FindChecked(Category));
	}
}

void SDreamUIPrefabPalette::CollectPrefabs(TArray<FItemPtr>& Out)
{
	IAssetRegistry& AssetRegistry = IAssetRegistry::GetChecked();
	TArray<FAssetData> PrefabAssets;
	AssetRegistry.GetAssetsByClass(UDreamUIPrefab::StaticClass()->GetClassPathName(), PrefabAssets, true);

	UDreamUIPrefab* EditingPrefab = nullptr;
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
		if (AssetData.PackageName.ToString().StartsWith(UDreamGUISettings::Get()->PresetControlFolder))continue;
		if (AssetData.PackageName == EditingPackage)continue;
		if (ReferencingPackages.Contains(AssetData.PackageName))
		{
			auto Candidate = Cast<UDreamUIPrefab>(AssetData.GetAsset());
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
		Item->FavoriteKey = DreamUIPalette::MakeFavoriteKey(*Item);
		// Only prefabs already in memory can be version-checked for free; the rest are rejected by
		// FDreamUIEditorTools::CanNestPrefabUnderWidget at create time, which is the guard that counts.
		if (auto Loaded = Cast<UDreamUIPrefab>(AssetData.FastGetAsset(false)))
		{
			if (Loaded->PrefabVersion <= (uint16)EDreamUIPrefabVersion::OldVersion)
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

void SDreamUIPrefabPalette::RebuildList()
{
	AllGroups.Reset();
	CollectBasics(AllGroups);
	CollectControls(AllGroups);
	CollectPrefabs(AllGroups);
	RefreshRootItems();
}

void SDreamUIPrefabPalette::RefreshRootItems()
{
	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	DreamUIPalette::BuildRootItems(AllGroups, Favorites, bFilterActive,
		[this](const FPaletteItem& Item)
		{
			return SearchFilter.TestTextFilter(FBasicStringFilterExpressionContext(Item.DisplayName));
		}, RootItems);

	if (TreeView.IsValid())
	{
		TreeView->RequestTreeRefresh();
		ApplyGroupExpansion();
	}
}

void SDreamUIPrefabPalette::ApplyGroupExpansion()
{
	const bool bFilterActive = !SearchFilter.GetFilterText().IsEmpty();
	// The tree reports every one of these back through OnGroupExpansionChanged, and a rebuild is not
	// the user speaking.
	bApplyingGroupExpansion = true;
	for (auto& Group : RootItems)
	{
		TreeView->SetItemExpansion(Group, DreamUIPalette::ShouldExpandGroup(bFilterActive, CollapsedGroups.Contains(Group->DisplayName)));
	}
	bApplyingGroupExpansion = false;
}

void SDreamUIPrefabPalette::OnGroupExpansionChanged(FItemPtr InItem, bool bExpanded)
{
	if (bApplyingGroupExpansion || !InItem.IsValid() || InItem->Kind != EItemKind::Category)return;
	// While a filter is active every group is forced open, so what the tree reports then says nothing
	// about which groups this user wants closed.
	if (!SearchFilter.GetFilterText().IsEmpty())return;
	if (bExpanded)
	{
		if (CollapsedGroups.Remove(InItem->DisplayName) == 0)return;
	}
	else
	{
		bool bAlreadyCollapsed = false;
		CollapsedGroups.Add(InItem->DisplayName, &bAlreadyCollapsed);
		if (bAlreadyCollapsed)return;
	}
	SavePreferences();
}

TSharedRef<ITableRow> SDreamUIPrefabPalette::OnGenerateRow(FItemPtr InItem, const TSharedRef<STableViewBase>& OwnerTable)
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

	const FText FavoriteTooltip = LOCTEXT("FavoriteToggleTooltip", "Keep this element in the Favorites group at the top of the palette.");

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
			: InItem->PrefabAsset.IsValid() ? UDreamUIPrefab::StaticClass() : UDreamWidget::StaticClass();
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
	TSharedRef<STableRow<FItemPtr>> Row = SNew(STableRow<FItemPtr>, OwnerTable)
		.IsEnabled(InItem->bValid)
		.Padding(FMargin(2, 2))
		.ToolTipText(Tooltip)
		.OnDragDetected(FOnDragDetected::CreateSP(this, &SDreamUIPrefabPalette::OnItemDragDetected, InItem));
	// The star shows on a favourite and under the pointer, UMG-palette style. Hover has to be asked of
	// the row: a check box only ever knows the pointer is over the check box. Hence content after
	// construction -- the row has to exist before anything inside it can ask it anything.
	const TWeakPtr<SWidget> WeakRow = Row;
	Row->SetContent(
		SNew(SHorizontalBox)
		+ SHorizontalBox::Slot()
		.AutoWidth()
		.VAlign(VAlign_Center)
		[
			SNew(SCheckBox)
			.Style(FAppStyle::Get(), "UMGEditor.Palette.FavoriteToggleStyle")
			.ToolTipText(FavoriteTooltip)
			.IsChecked(this, &SDreamUIPrefabPalette::GetFavoriteState, InItem)
			.OnCheckStateChanged(this, &SDreamUIPrefabPalette::OnFavoriteToggled, InItem)
			.Visibility_Lambda([this, InItem, WeakRow]()
			{
				const TSharedPtr<SWidget> HoverTarget = WeakRow.Pin();
				return GetFavoriteState(InItem) == ECheckBoxState::Checked || (HoverTarget.IsValid() && HoverTarget->IsHovered())
					? EVisibility::Visible : EVisibility::Hidden;
			})
		]
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
			.HighlightText(this, &SDreamUIPrefabPalette::GetSearchText)
		]);
	return Row;
}

void SDreamUIPrefabPalette::OnGetChildren(FItemPtr InItem, TArray<FItemPtr>& OutChildren)
{
	OutChildren = InItem->Children;
}

void SDreamUIPrefabPalette::OnItemDoubleClick(FItemPtr InItem)
{
	if (!InItem.IsValid() || !InItem->bValid)return;
	if (InItem->Kind == EItemKind::Category)
	{
		if (TreeView.IsValid())TreeView->SetItemExpansion(InItem, !TreeView->IsItemExpanded(InItem));
		return;
	}
	CreateItem(InItem);
}

namespace SDreamUIPrefabPaletteLocal
{
	// Favourites and collapsed groups are per user and per project, and neither is worth a settings
	// object: nothing outside this panel reads them.
	static const TCHAR* PreferencesSection = TEXT("DreamUIPrefabPalette.Preferences");
	static const TCHAR* FavoritesKey = TEXT("Favorites");
	static const TCHAR* CollapsedGroupsKey = TEXT("CollapsedGroups");

	// shared create path: the create primitives take a "get parent" function, so double-click
	// (parent = selection) and drop (parent = drop-target widget) reuse the same logic
	UDreamWidget* CreateElement(bool bIsBasicWidget, UClass* VisualClass, bool bSetDefaultSprite,
		const TSharedPtr<FDreamUIControlDescriptor>& NativeDescriptor, const FString& PrefabPath,
		bool bLinkedSubPrefab, const FString& DisplayName, TFunction<UDreamWidget*()> GetParent,
		TFunction<void(UDreamWidget*)> AfterCreate = nullptr)
	{
		if (bIsBasicWidget)
		{
			TFunction<void(UDreamWidget*)> Callback = [bSetDefaultSprite, AfterCreate](UDreamWidget* InWidget)
			{
				if (bSetDefaultSprite)
				{
					if (auto Image = Cast<UDreamImage>(InWidget->GetVisual()))
					{
						Image->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
					}
				}
				if (AfterCreate)AfterCreate(InWidget);
			};
			return FDreamUIEditorTools::CreateWidgetAndReturn(GetParent, DisplayName, VisualClass, Callback);
		}
		if (NativeDescriptor.IsValid())
		{
			return FDreamUIEditorTools::CreateRegisteredControlAndReturn(GetParent, NativeDescriptor->Name, AfterCreate);
		}
		// A project asset dropped as a flattened copy loses its override tracking and every route
		// back to the source; only the plugin's own recipes mean to be baked in.
		if (bLinkedSubPrefab)
		{
			return FDreamUIEditorTools::CreateSubPrefabAndReturn(GetParent, PrefabPath, AfterCreate);
		}
		return FDreamUIEditorTools::CreateUIControlsAndReturn(GetParent, PrefabPath, AfterCreate);
	}
}

UDreamWidget* FDreamUIPaletteDragDropOp::CreateUnder(UDreamWidget* InParentWidget, TOptional<int32> InSiblingIndex,
	TFunction<void(UDreamWidget*)> AfterCreate)const
{
	if (InParentWidget == nullptr)return nullptr;
	// Place inside the creation callback rather than after the call returns: the create primitives
	// run it while their own transaction is still open, so creating and positioning collapse into
	// one undo step instead of leaving a half-placed widget behind on Ctrl+Z.
	TFunction<void(UDreamWidget*)> PlaceThenForward =
		[InSiblingIndex, AfterCreate = MoveTemp(AfterCreate)](UDreamWidget* InWidget)
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
	return SDreamUIPrefabPaletteLocal::CreateElement(bIsBasicWidget, VisualClass.Get(), bSetDefaultSprite,
		NativeDescriptor, PrefabPath, bLinkedSubPrefab, DisplayName, [InParentWidget]() -> UDreamWidget* { return InParentWidget; },
		MoveTemp(PlaceThenForward));
}

void SDreamUIPrefabPalette::CreateItem(FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->Kind == EItemKind::Category)return;
	// With nothing selected, fall back to the prefab root -- the same "add to root" the hierarchy's
	// empty-area drop performs. Double-click used to be a no-op here, which reads as a dead panel.
	SDreamUIPrefabPaletteLocal::CreateElement(InItem->Kind == EItemKind::BasicWidget, InItem->VisualClass.Get(),
		InItem->bSetDefaultSprite, InItem->NativeDescriptor, InItem->PrefabPath, InItem->Kind == EItemKind::ProjectPrefab, InItem->DisplayName,
		[this]() -> UDreamWidget*
		{
			if (UDreamWidget* Selected = GetSelectedWidget())return Selected;
			if (auto Editor = PrefabEditorPtr.Pin())return Editor->GetLoadedRootWidget();
			return nullptr;
		});
}

FReply SDreamUIPrefabPalette::OnItemDragDetected(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent, FItemPtr InItem)
{
	if (!InItem.IsValid() || !InItem->bValid || InItem->Kind == EItemKind::Category)return FReply::Unhandled();
	auto Op = MakeShared<FDreamUIPaletteDragDropOp>();
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

void SDreamUIPrefabPalette::OnSearchTextChanged(const FText& InText)
{
	SearchFilter.SetFilterText(InText);
	RefreshRootItems();
}

FText SDreamUIPrefabPalette::GetSearchText()const
{
	return SearchFilter.GetFilterText();
}

void SDreamUIPrefabPalette::LoadPreferences()
{
	if (GConfig == nullptr)return;
	TArray<FString> Values;
	GConfig->GetArray(SDreamUIPrefabPaletteLocal::PreferencesSection, SDreamUIPrefabPaletteLocal::FavoritesKey, Values, GEditorPerProjectIni);
	Favorites.Append(Values);
	Values.Reset();
	GConfig->GetArray(SDreamUIPrefabPaletteLocal::PreferencesSection, SDreamUIPrefabPaletteLocal::CollapsedGroupsKey, Values, GEditorPerProjectIni);
	CollapsedGroups.Append(Values);
}

void SDreamUIPrefabPalette::SavePreferences()const
{
	if (GConfig == nullptr)return;
	GConfig->SetArray(SDreamUIPrefabPaletteLocal::PreferencesSection, SDreamUIPrefabPaletteLocal::FavoritesKey, Favorites.Array(), GEditorPerProjectIni);
	GConfig->SetArray(SDreamUIPrefabPaletteLocal::PreferencesSection, SDreamUIPrefabPaletteLocal::CollapsedGroupsKey, CollapsedGroups.Array(), GEditorPerProjectIni);
	GConfig->Flush(false, GEditorPerProjectIni);
}

ECheckBoxState SDreamUIPrefabPalette::GetFavoriteState(FItemPtr InItem)const
{
	return InItem.IsValid() && !InItem->FavoriteKey.IsEmpty() && Favorites.Contains(InItem->FavoriteKey)
		? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
}

void SDreamUIPrefabPalette::OnFavoriteToggled(ECheckBoxState InState, FItemPtr InItem)
{
	if (!InItem.IsValid() || InItem->FavoriteKey.IsEmpty())return;
	if (InState == ECheckBoxState::Checked)
	{
		Favorites.Add(InItem->FavoriteKey);
	}
	else
	{
		Favorites.Remove(InItem->FavoriteKey);
	}
	SavePreferences();
	// The Favorites group is derived, not stored, so it only gains or loses the entry on a re-derive.
	RefreshRootItems();
}

#undef LOCTEXT_NAMESPACE
