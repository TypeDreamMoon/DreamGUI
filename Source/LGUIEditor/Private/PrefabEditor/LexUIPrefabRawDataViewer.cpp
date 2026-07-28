// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "LexUIPrefabRawDataViewer.h"
#include "Core/Components/LexWidget.h"
#include "DetailLayoutBuilder.h"
#include "LexUIPrefabEditor.h"
#include "Modules/ModuleManager.h"
#include "PrefabSystem/LexUIPrefab.h"
#include "PrefabSystem/LexUIPrefabHelperObject.h"
#include "PropertyEditorModule.h"
#include "Styling/AppStyle.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Input/SSearchBox.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Layout/SExpandableArea.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/Text/STextBlock.h"

#define LOCTEXT_NAMESPACE "LGUIPrefabRawDataViewer"

namespace LexUIPrefabRawDataViewerLocal
{
	FString PrefabVersionToString(uint16 Version)
	{
		const TCHAR* Name = TEXT("Unknown");
		switch ((ELexUIPrefabVersion)Version)
		{
		case ELexUIPrefabVersion::OldVersion: Name = TEXT("OldVersion"); break;
		case ELexUIPrefabVersion::BuiltinFArchive: Name = TEXT("BuiltinFArchive"); break;
		case ELexUIPrefabVersion::NestedDefaultSubObject: Name = TEXT("NestedDefaultSubObject"); break;
		case ELexUIPrefabVersion::ObjectName: Name = TEXT("ObjectName"); break;
		case ELexUIPrefabVersion::CommonActor: Name = TEXT("CommonActor"); break;
		case ELexUIPrefabVersion::ActorAttachToSubPrefab: Name = TEXT("ActorAttachToSubPrefab"); break;
		case ELexUIPrefabVersion::NewObjectOnNestedPrefab: Name = TEXT("NewObjectOnNestedPrefab"); break;
		case ELexUIPrefabVersion::FTextAsReference: Name = TEXT("FTextAsReference"); break;
		default: break;
		}
		const uint16 Newest = (uint16)ELexUIPrefabVersion::NEWEST;
		return FString::Printf(TEXT("%d — %s%s"), Version, Name,
			Version == Newest ? TEXT(" (newest)") : *FString::Printf(TEXT(" (newest is %d)"), Newest));
	}

	FString SchemaVersionToString(uint16 Version)
	{
		const TCHAR* Name = TEXT("Unknown");
		switch ((ELexUIPrefabSchemaVersion)Version)
		{
		case ELexUIPrefabSchemaVersion::Unversioned: Name = TEXT("Unversioned"); break;
		case ELexUIPrefabSchemaVersion::PanelSlotOwnership: Name = TEXT("PanelSlotOwnership"); break;
		default: break;
		}
		const uint16 Newest = (uint16)ELexUIPrefabSchemaVersion::NEWEST;
		return FString::Printf(TEXT("%d — %s%s"), Version, Name,
			Version == Newest ? TEXT(" (newest)") : *FString::Printf(TEXT(" (newest is %d)"), Newest));
	}

	FString BytesToString(int64 Bytes)
	{
		if (Bytes >= 1024 * 1024)
		{
			return FString::Printf(TEXT("%.2f MB (%lld bytes)"), Bytes / (1024.0 * 1024.0), Bytes);
		}
		if (Bytes >= 1024)
		{
			return FString::Printf(TEXT("%.1f KB (%lld bytes)"), Bytes / 1024.0, Bytes);
		}
		return FString::Printf(TEXT("%lld bytes"), Bytes);
	}

	int32 CountWidgets(ULexWidget* Widget)
	{
		if (!IsValid(Widget))
		{
			return 0;
		}
		int32 Total = 1;
		for (ULexWidget* Child : Widget->GetChildren())
		{
			Total += CountWidgets(Child);
		}
		return Total;
	}

	FSlateFontInfo MonoFont() { return FCoreStyle::GetDefaultFontStyle("Mono", 9); }

	const FLinearColor AccentColor(0.55f, 0.75f, 1.0f);
	const FLinearColor DimColor(0.6f, 0.6f, 0.6f);
	const FLinearColor DeadColor(1.0f, 0.35f, 0.35f);

	/** Faint alternating background so long lists stay scannable. */
	TSharedRef<SWidget> MakeStripedRow(int32 VisibleIndex, const TSharedRef<SWidget>& Inner)
	{
		return SNew(SBorder)
			.BorderImage(FAppStyle::GetBrush("WhiteBrush"))
			.BorderBackgroundColor(FSlateColor(VisibleIndex % 2 == 0 ? FLinearColor(1, 1, 1, 0.04f) : FLinearColor::Transparent))
			.Padding(FMargin(0))
			[
				Inner
			];
	}

	TSharedRef<SWidget> MakeKeyValueRow(const FText& Key, const FString& Value, bool bMonospace = false, const FLinearColor& ValueColor = FLinearColor(0.85f, 0.85f, 0.85f))
	{
		return SNew(SHorizontalBox)
			+ SHorizontalBox::Slot()
			.AutoWidth()
			.Padding(4, 1)
			[
				SNew(SBox)
				.WidthOverride(190)
				[
					SNew(STextBlock)
					.Text(Key)
					.Font(IDetailLayoutBuilder::GetDetailFontBold())
				]
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.Padding(4, 1)
			[
				SNew(STextBlock)
				.Text(FText::FromString(Value))
				.Font(bMonospace ? MonoFont() : IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(ValueColor))
				.AutoWrapText(true)
			];
	}

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

	FString TruncateText(const FString& Value, int32 MaxLen = 80)
	{
		FString Flat = Value.Replace(TEXT("\n"), TEXT("\\n"));
		return Flat.Len() <= MaxLen ? Flat : Flat.Left(MaxLen) + TEXT("...");
	}
}

void SLexUIPrefabRawDataViewer::Construct(const FArguments& InArgs, TSharedPtr<FLexUIPrefabEditor> InPrefabEditorPtr, UObject* InObject)
{
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
	DescriptorDetailView = EditModule.CreateDetailView(DetailsViewArgs);
	DescriptorDetailView->SetObject(InObject);

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
				.ToolTipText(LOCTEXT("RefreshTip", "Re-read every section from the asset's current state (run after Apply)."))
				.OnClicked_Lambda([this]() { Rebuild(); return FReply::Handled(); })
			]
			+ SHorizontalBox::Slot()
			.FillWidth(1.0f)
			.VAlign(VAlign_Center)
			.Padding(8, 0, 0, 0)
			[
				SNew(SSearchBox)
				.HintText(LOCTEXT("SearchHint", "Filter rows in every section..."))
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
			.Text(LOCTEXT("Hint", "Decoded view of the serialized prefab data. The raw asset properties live in the last section."))
			.Font(IDetailLayoutBuilder::GetDetailFont())
			.ColorAndOpacity(FSlateColor(LexUIPrefabRawDataViewerLocal::DimColor))
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

bool SLexUIPrefabRawDataViewer::MatchesFilter(const FString& Haystack) const
{
	return FilterString.IsEmpty() || Haystack.Contains(FilterString);
}

void SLexUIPrefabRawDataViewer::Rebuild()
{
	using namespace LexUIPrefabRawDataViewerLocal;
	if (!ContentBox.IsValid())
	{
		return;
	}
	ContentBox->ClearChildren();
	ULexUIPrefab* Prefab = PrefabWeak.Get();
	if (!IsValid(Prefab))
	{
		ContentBox->AddSlot().AutoHeight().Padding(8)
		[
			SNew(STextBlock).Text(LOCTEXT("NoPrefab", "No prefab asset."))
		];
		return;
	}

	ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("Overview", "Overview"), BuildOverviewSection(Prefab))];
	ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("ReferenceLists", "Reference Lists"), BuildReferenceListsSection(Prefab))];
	ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("GuidMap", "Guid → Object Map"), BuildGuidMapSection(Prefab))];
	ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("SubPrefabs", "Sub Prefabs"), BuildSubPrefabSection(Prefab))];
	ContentBox->AddSlot().AutoHeight()[MakeSection(LOCTEXT("RawProperties", "Raw Asset Properties (advanced)"),
		SNew(SBox).MaxDesiredHeight(500)[DescriptorDetailView.ToSharedRef()], /*bInitiallyCollapsed*/true)];
}

TSharedRef<SWidget> SLexUIPrefabRawDataViewer::BuildOverviewSection(ULexUIPrefab* Prefab)
{
	using namespace LexUIPrefabRawDataViewerLocal;
	ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	const int32 WidgetCount = (Helper && IsValid(Helper->LoadedRootWidget)) ? CountWidgets(Helper->LoadedRootWidget) : -1;

	TArray<TPair<FText, FString>> Rows;
	Rows.Emplace(LOCTEXT("PrefabVersion", "Binary format version"), PrefabVersionToString(Prefab->PrefabVersion));
	Rows.Emplace(LOCTEXT("SchemaVersion", "Schema version"), SchemaVersionToString(Prefab->PrefabSchemaVersion));
	Rows.Emplace(LOCTEXT("SavedByEngine", "Saved by engine"),
		FString::Printf(TEXT("%d.%d.%d"), Prefab->EngineMajorVersion, Prefab->EngineMinorVersion, Prefab->EnginePatchVersion));
	Rows.Emplace(LOCTEXT("CreateTime", "Last saved (UTC)"), Prefab->CreateTime.ToString());
	Rows.Emplace(LOCTEXT("EditorPayload", "Editor payload (BinaryData)"), BytesToString(Prefab->BinaryData.Num()));
	Rows.Emplace(LOCTEXT("BuildPayload", "Cooked payload (BinaryDataForBuild)"),
		Prefab->BinaryDataForBuild.Num() > 0 ? BytesToString(Prefab->BinaryDataForBuild.Num()) : FString(TEXT("not cached")));
	Rows.Emplace(LOCTEXT("WidgetCount", "Widgets in loaded hierarchy"),
		WidgetCount >= 0 ? FString::FromInt(WidgetCount) : FString(TEXT("hierarchy not loaded")));
	Rows.Emplace(LOCTEXT("RefCounts", "Reference table sizes"),
		FString::Printf(TEXT("assets %d, classes %d, names %d, texts %d"),
			Prefab->ReferenceAssetList.Num(), Prefab->ReferenceClassList.Num(),
			Prefab->ReferenceNameList.Num(), Prefab->ReferenceTextList.Num()));

	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	for (const TPair<FText, FString>& Row : Rows)
	{
		if (!MatchesFilter(Row.Key.ToString() + TEXT(" ") + Row.Value))
		{
			continue;
		}
		Box->AddSlot().AutoHeight()[MakeKeyValueRow(Row.Key, Row.Value)];
	}
	return Box;
}

TSharedRef<SWidget> SLexUIPrefabRawDataViewer::BuildReferenceListsSection(ULexUIPrefab* Prefab)
{
	using namespace LexUIPrefabRawDataViewerLocal;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);

	auto AddRows = [this](const TSharedRef<SVerticalBox>& Target, const TCHAR* ListName, const TCHAR* ListDescription, int32 Num, TFunctionRef<FString(int32)> RowText, TFunctionRef<bool(int32)> RowValid)
	{
		TSharedRef<SVerticalBox> Rows = SNew(SVerticalBox);
		int32 Shown = 0;
		for (int32 i = 0; i < Num; i++)
		{
			const FString Text = FString::Printf(TEXT("[%d]  %s"), i, *RowText(i));
			if (!MatchesFilter(Text))
			{
				continue;
			}
			const bool bValid = RowValid(i);
			Rows->AddSlot().AutoHeight().Padding(8, 0, 4, 0)
			[
				MakeStripedRow(Shown,
					SNew(STextBlock)
					.Text(FText::FromString(Text))
					.Font(MonoFont())
					.ColorAndOpacity(FSlateColor(bValid ? FLinearColor(0.85f, 0.85f, 0.85f) : DeadColor)))
			];
			Shown++;
		}
		if (Shown == 0)
		{
			Rows->AddSlot().AutoHeight().Padding(12, 0, 4, 0)
			[
				SNew(STextBlock)
				.Text(Num == 0 ? LOCTEXT("EmptyList", "(empty)") : LOCTEXT("NoRowMatches", "(no rows match the filter)"))
				.Font(IDetailLayoutBuilder::GetDetailFont())
				.ColorAndOpacity(FSlateColor(DimColor))
			];
		}
		const bool bFiltering = !FilterString.IsEmpty();
		const FString CountLabel = bFiltering
			? FString::Printf(TEXT("%d/%d match"), Shown, Num)
			: FString::FromInt(Num);
		const FString Title = FString::Printf(TEXT("%s (%s)%s%s"), ListName, *CountLabel,
			ListDescription && *ListDescription ? TEXT(" — ") : TEXT(""), ListDescription);
		Target->AddSlot().AutoHeight()[MakeSection(FText::FromString(Title), Rows,
			/*bInitiallyCollapsed*/!bFiltering && Num > 20)];
	};

	AddRows(Box, TEXT("Assets"), TEXT("object references stored by index"),
		Prefab->ReferenceAssetList.Num(),
		[Prefab](int32 i) { UObject* O = Prefab->ReferenceAssetList[i]; return IsValid(O) ? FString::Printf(TEXT("%s  (%s)"), *O->GetName(), *O->GetClass()->GetName()) : FString(TEXT("MISSING — the payload references a dead asset")); },
		[Prefab](int32 i) { return IsValid(Prefab->ReferenceAssetList[i].Get()); });
	AddRows(Box, TEXT("Classes"), TEXT("widget/component classes by index"),
		Prefab->ReferenceClassList.Num(),
		[Prefab](int32 i) { UClass* C = Prefab->ReferenceClassList[i]; return IsValid(C) ? C->GetName() : FString(TEXT("MISSING — the payload references a dead class")); },
		[Prefab](int32 i) { return IsValid(Prefab->ReferenceClassList[i].Get()); });
	AddRows(Box, TEXT("Names"), TEXT(""),
		Prefab->ReferenceNameList.Num(),
		[Prefab](int32 i) { return Prefab->ReferenceNameList[i].ToString(); },
		[](int32) { return true; });
	AddRows(Box, TEXT("Texts"), TEXT("FText values, localization-gatherable"),
		Prefab->ReferenceTextList.Num(),
		[Prefab](int32 i) { return TruncateText(Prefab->ReferenceTextList[i].ToString()); },
		[](int32) { return true; });
	return Box;
}

TSharedRef<SWidget> SLexUIPrefabRawDataViewer::BuildGuidMapSection(ULexUIPrefab* Prefab)
{
	using namespace LexUIPrefabRawDataViewerLocal;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	if (!Helper)
	{
		Box->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("NoHelper", "Helper not available (hierarchy not loaded)."))
		];
		return Box;
	}
	int32 DeadCount = 0;
	int32 Shown = 0;
	for (const TPair<FGuid, TObjectPtr<UObject>>& Pair : Helper->MapGuidToObject)
	{
		const UObject* Object = Pair.Value.Get();
		const bool bValid = IsValid(Object);
		if (!bValid) { DeadCount++; }
		FString Label;
		if (bValid)
		{
			const ULexWidget* AsWidget = Cast<ULexWidget>(Object);
			Label = FString::Printf(TEXT("%s  %s (%s)"),
				*Pair.Key.ToString(EGuidFormats::DigitsWithHyphens).Left(8),
				AsWidget ? *AsWidget->GetDisplayName() : *Object->GetName(),
				*Object->GetClass()->GetName());
		}
		else
		{
			Label = FString::Printf(TEXT("%s  DEAD MAPPING — object gone; cleaned up on next save"),
				*Pair.Key.ToString(EGuidFormats::DigitsWithHyphens).Left(8));
		}
		// Match the full GUID too — the row shows only the first 8 chars but the tooltip (and any
		// log line the user copied it from) carries the whole thing.
		if (!MatchesFilter(Label + TEXT(" ") + Pair.Key.ToString(EGuidFormats::DigitsWithHyphens)))
		{
			continue;
		}
		Box->AddSlot().AutoHeight().Padding(8, 0, 4, 0)
		[
			MakeStripedRow(Shown,
				SNew(STextBlock)
				.Text(FText::FromString(Label))
				.ToolTipText(FText::FromString(Pair.Key.ToString(EGuidFormats::DigitsWithHyphens)))
				.Font(MonoFont())
				.ColorAndOpacity(FSlateColor(bValid ? FLinearColor(0.85f, 0.85f, 0.85f) : DeadColor)))
		];
		Shown++;
	}
	FString Summary = FString::Printf(TEXT("%d total, %d dead"), Helper->MapGuidToObject.Num(), DeadCount);
	if (!FilterString.IsEmpty())
	{
		Summary += FString::Printf(TEXT(" (%d match the filter)"), Shown);
	}
	Box->InsertSlot(0).AutoHeight().Padding(4, 2)
	[
		MakeKeyValueRow(LOCTEXT("GuidSummary", "Mappings"), Summary,
			false, DeadCount > 0 ? FLinearColor(1.0f, 0.6f, 0.3f) : FLinearColor(0.85f, 0.85f, 0.85f))
	];
	return Box;
}

TSharedRef<SWidget> SLexUIPrefabRawDataViewer::BuildSubPrefabSection(ULexUIPrefab* Prefab)
{
	using namespace LexUIPrefabRawDataViewerLocal;
	TSharedRef<SVerticalBox> Box = SNew(SVerticalBox);
	ULexUIPrefabHelperObject* Helper = Prefab->GetPrefabHelperObject();
	if (!Helper || Helper->SubPrefabMap.Num() == 0)
	{
		Box->AddSlot().AutoHeight().Padding(8, 2)
		[
			SNew(STextBlock).Text(LOCTEXT("NoSubPrefabs", "No nested sub prefabs.")).Font(IDetailLayoutBuilder::GetDetailFont())
		];
		return Box;
	}
	Box->AddSlot().AutoHeight().Padding(4, 2)
	[
		SNew(STextBlock)
		.Text(LOCTEXT("SubPrefabOverridesHint", "Per-object override details live in the Overrides tab."))
		.Font(IDetailLayoutBuilder::GetDetailFont())
		.ColorAndOpacity(FSlateColor(DimColor))
	];
	for (const TPair<TObjectPtr<ULexWidget>, FLexUISubPrefabData>& Pair : Helper->SubPrefabMap)
	{
		const ULexWidget* RootWidget = Pair.Key.Get();
		const FLexUISubPrefabData& Data = Pair.Value;
		const FString RootName = RootWidget ? RootWidget->GetDisplayName() : TEXT("<missing root>");
		int32 OverriddenPropertyCount = 0;
		FString OverrideHaystack;
		for (const FLexUIPrefabOverrideParameterData& Override : Data.ObjectOverrideParameterArray)
		{
			OverriddenPropertyCount += Override.MemberPropertyNames.Num();
			if (const UObject* Object = Override.Object.Get())
			{
				OverrideHaystack += TEXT(" ") + Object->GetClass()->GetName();
			}
			for (const FName& PropertyName : Override.MemberPropertyNames)
			{
				OverrideHaystack += TEXT(" ") + PropertyName.ToString();
			}
		}
		// Searching a pinned class or property name should surface the instance that pins it, and any
		// text the entry visibly renders (like the MD5) must also hit.
		if (!MatchesFilter(RootName + TEXT(" ") + GetNameSafe(Data.PrefabAsset) + OverrideHaystack
			+ TEXT(" ") + Data.OverallVersionMD5))
		{
			continue;
		}
		TSharedRef<SVerticalBox> Entry = SNew(SVerticalBox);
		Entry->AddSlot().AutoHeight()[MakeKeyValueRow(LOCTEXT("SubAsset", "Asset"), GetNameSafe(Data.PrefabAsset))];
		Entry->AddSlot().AutoHeight()[MakeKeyValueRow(LOCTEXT("SubOverrides", "Instance overrides"),
			FString::Printf(TEXT("%d object(s), %d propert(ies)"), Data.ObjectOverrideParameterArray.Num(), OverriddenPropertyCount),
			false, OverriddenPropertyCount > 0 ? FLinearColor(1.0f, 0.75f, 0.35f) : FLinearColor(0.85f, 0.85f, 0.85f))];
		Entry->AddSlot().AutoHeight()[MakeKeyValueRow(LOCTEXT("SubMappings", "Guid mappings"),
			FString::Printf(TEXT("%d in-instance, %d parent→sub, %d newly-created ids"),
				Data.MapGuidToObject.Num(), Data.MapObjectGuidFromParentPrefabToSubPrefab.Num(), Data.MapObjectIdToNewlyCreatedId.Num()))];
		Entry->AddSlot().AutoHeight()[MakeKeyValueRow(LOCTEXT("SubMD5", "Version MD5"),
			Data.OverallVersionMD5.IsEmpty() ? TEXT("(empty)") : Data.OverallVersionMD5, true)];
		Box->AddSlot().AutoHeight()
		[
			MakeSection(FText::FromString(RootName), Entry)
		];
	}
	return Box;
}

#undef LOCTEXT_NAMESPACE
