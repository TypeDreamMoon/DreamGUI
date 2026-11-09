// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Workflow/DreamUIPresetLibrary.h"

#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetBlueprintEditor.h"
#include "Designer/DreamWidgetTreeEditing.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIAst.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIReflectionPolicy.h"
#include "Text/DreamUITextBuilder.h"
#include "Text/DreamUITextPatcher.h"
#include "Text/DreamUITextWriteBack.h"
#include "Workflow/DreamUILayerStyleClipboard.h"
#include "Workflow/DreamUIStylePresets.h"

#include "Core/Components/DreamLayout.h"
#include "Core/Components/DreamLayerStyle.h"
#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamGUISettings.h"
#include "Core/DreamGradientAsset.h"
#include "Core/DreamLayerStylePreset.h"
#include "Core/DreamWidgetTree.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "ContentBrowserModule.h"
#include "Editor.h"
#include "IContentBrowserSingleton.h"
#include "Misc/PackageName.h"
#include "ScopedTransaction.h"
#include "Styling/AppStyle.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "Widgets/Layout/SBorder.h"

#define LOCTEXT_NAMESPACE "DreamUIPresetLibrary"

namespace DreamUIPresetLibraryLocal
{
	bool Fail(FText* OutError, const FText& InReason)
	{
		if (OutError != nullptr)
		{
			*OutError = InReason;
		}
		return false;
	}

	TMap<FName, FString>* SettingsMapFor(UDreamGUISettings* InSettings, EDreamUIPresetKind InKind)
	{
		switch (InKind)
		{
		case EDreamUIPresetKind::Gradient: return &InSettings->GradientPresets;
		case EDreamUIPresetKind::Style: return &InSettings->StylePresets;
		case EDreamUIPresetKind::Effect: return &InSettings->EffectPresets;
		}
		return nullptr;
	}

	FName SettingsPropertyFor(EDreamUIPresetKind InKind)
	{
		switch (InKind)
		{
		case EDreamUIPresetKind::Gradient: return GET_MEMBER_NAME_CHECKED(UDreamGUISettings, GradientPresets);
		case EDreamUIPresetKind::Style: return GET_MEMBER_NAME_CHECKED(UDreamGUISettings, StylePresets);
		case EDreamUIPresetKind::Effect: return GET_MEMBER_NAME_CHECKED(UDreamGUISettings, EffectPresets);
		}
		return NAME_None;
	}

	void SaveSettingsMap(EDreamUIPresetKind InKind)
	{
		UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
		// The whole class: a map is no property UpdateSinglePropertyInConfigFile can write.
		Settings->TryUpdateDefaultConfigFile();
		if (InKind == EDreamUIPresetKind::Gradient)
		{
			// Texts naming a project gradient by a tag paint it again.
			DreamGradientPresets::OnPresetsChanged().Broadcast();
		}
		DreamUIPresetLibrary::OnChanged().Broadcast();
	}

	// ---- where a gradient goes on a widget ------------------------------------------------------------------------------

	struct FGradientTarget
	{
		UObject* Owner = nullptr;
		/** The head property, as the write-back is told it. */
		FName Head;
		const FProperty* HeadProperty = nullptr;
		/** One or the other. */
		FDreamTextPaint* Paint = nullptr;
		FDreamGradient* Gradient = nullptr;

		bool IsValid() const { return Owner != nullptr && (Paint != nullptr || Gradient != nullptr); }
	};

	/** The first paint, else the first gradient, among InStruct's fields (structs recursed into, containers not), in declaration order. */
	bool FindIn(const UStruct* InStruct, void* InContainer, const UScriptStruct* InWanted, void*& OutValue)
	{
		for (TFieldIterator<FProperty> It(InStruct); It; ++It)
		{
			const FStructProperty* AsStruct = CastField<FStructProperty>(*It);
			if (AsStruct == nullptr || It->ArrayDim != 1 || DreamUIReflection::IsHidden(*It))
			{
				continue;
			}
			void* Value = AsStruct->ContainerPtrToValuePtr<void>(InContainer);
			if (AsStruct->Struct == InWanted)
			{
				OutValue = Value;
				return true;
			}
			if (FindIn(AsStruct->Struct, Value, InWanted, OutValue))
			{
				return true;
			}
		}
		return false;
	}

	FGradientTarget FindGradientTarget(UDreamWidget* InWidget)
	{
		FGradientTarget Target;
		if (InWidget == nullptr)
		{
			return Target;
		}
		// The visual's before the widget's own, as the builder offers a bare name to them; a paint before a bare gradient.
		UObject* Candidates[] = { InWidget->GetVisual(), InWidget };
		for (const UScriptStruct* Wanted : { FDreamTextPaint::StaticStruct(), FDreamGradient::StaticStruct() })
		{
			for (UObject* Candidate : Candidates)
			{
				if (Candidate == nullptr)
				{
					continue;
				}
				for (TFieldIterator<FProperty> It(Candidate->GetClass()); It; ++It)
				{
					if (!DreamUIReflection::IsSweepRoot(*It))
					{
						continue;
					}
					const FStructProperty* AsStruct = CastField<FStructProperty>(*It);
					if (AsStruct == nullptr || It->ArrayDim != 1)
					{
						continue;
					}
					void* HeadValue = AsStruct->ContainerPtrToValuePtr<void>(Candidate);
					void* Found = nullptr;
					if (AsStruct->Struct == Wanted)
					{
						Found = HeadValue;
					}
					else if (!FindIn(AsStruct->Struct, HeadValue, Wanted, Found))
					{
						continue;
					}
					Target.Owner = Candidate;
					Target.Head = It->GetFName();
					Target.HeadProperty = *It;
					if (Wanted == FDreamTextPaint::StaticStruct())
					{
						Target.Paint = static_cast<FDreamTextPaint*>(Found);
					}
					else
					{
						Target.Gradient = static_cast<FDreamGradient*>(Found);
					}
					return Target;
				}
			}
		}
		return Target;
	}

	/**
	 * The switch a gradient is drawn only while it is on: its head property's EditCondition when that is one bool of the
	 * owner's (a rect block's BodyGradient and bEnableBodyGradient). Null when there is none, or it is more than a name.
	 */
	const FBoolProperty* FindGradientSwitch(const FGradientTarget& InTarget)
	{
		const FString Condition = InTarget.HeadProperty != nullptr ? InTarget.HeadProperty->GetMetaData(TEXT("EditCondition")).TrimStartAndEnd() : FString();
		if (Condition.IsEmpty() || InTarget.Owner == nullptr)
		{
			return nullptr;
		}
		for (const TCHAR Character : Condition)
		{
			if (!FChar::IsAlnum(Character) && Character != TEXT('_'))
			{
				return nullptr;
			}
		}
		return FindFProperty<FBoolProperty>(InTarget.Owner->GetClass(), *Condition);
	}

	// ---- effect presets ---------------------------------------------------------------------------------------------------

	/** The first word of InText, after a leading `+`: a node's type or a behaviour's class. */
	FString FirstWord(const FString& InText, bool& bOutIsComponent)
	{
		FString Trimmed = InText.TrimStartAndEnd();
		bOutIsComponent = Trimmed.StartsWith(TEXT("+"));
		if (bOutIsComponent)
		{
			Trimmed = Trimmed.Mid(1).TrimStart();
		}
		int32 End = 0;
		while (End < Trimmed.Len() && (FChar::IsAlnum(Trimmed[End]) || Trimmed[End] == TEXT('_') || Trimmed[End] == TEXT('.')
			|| Trimmed[End] == TEXT('/')))
		{
			++End;
		}
		return Trimmed.Left(End);
	}

	FString TagForVisual(const UClass* InClass)
	{
		TArray<TPair<FString, UClass*>> Tags;
		FDreamUITextBuilder::GetVisualTags(Tags);
		for (const TPair<FString, UClass*>& Tag : Tags)
		{
			if (Tag.Value == InClass)
			{
				return Tag.Key;
			}
		}
		return FString();
	}

	/** An effect preset's text built into a node of its own: the effect's object on it, and the heads the text names. */
	struct FBuiltEffect
	{
		TStrongObjectPtr<UDreamWidgetTree> Tree;
		UObject* Object = nullptr;
		TArray<FName> Heads;
	};

	bool BuildEffect(const FString& InText, FBuiltEffect& OutEffect, FText* OutError)
	{
		bool bIsComponent = false;
		const FString Word = FirstWord(InText, bIsComponent);
		if (Word.IsEmpty())
		{
			return Fail(OutError, LOCTEXT("EffectNoType", "An effect preset starts with the effect's name: `Glass { ... }`, or `+ DreamMeshModifierWarp { ... }`."));
		}
		FString Wrapped;
		if (bIsComponent)
		{
			// On a rect block: a mesh modifier needs an element that builds a mesh.
			Wrapped = FString::Printf(TEXT("RectBlock DreamEffectPreset {\n%s\n}\n"), *InText.TrimStartAndEnd());
		}
		else
		{
			// `Glass { … }` -> `Glass DreamEffectPreset { … }`: a node of its own with the id a node needs.
			const FString Trimmed = InText.TrimStartAndEnd();
			Wrapped = FString::Printf(TEXT("%s DreamEffectPreset %s\n"), *Word, *Trimmed.Mid(Word.Len()).TrimStart());
		}
		FDreamUIAst Ast;
		FDreamUIDiagnosticBag Diagnostics;
		Diagnostics.SourceName = TEXT("EffectPreset.dui");
		UDreamWidgetTree* Tree = FDreamUITextWriteBack::BuildReferenceTree(Wrapped, Ast, Diagnostics);
		UDreamWidget* Root = Tree != nullptr ? Tree->GetRootWidget() : nullptr;
		if (Root == nullptr || !Ast.bHasRoot)
		{
			for (const FDreamUIDiagnostic& Diagnostic : Diagnostics.Diagnostics)
			{
				if (Diagnostic.IsError())
				{
					return Fail(OutError, FText::FromString(Diagnostic.ToString()));
				}
			}
			return Fail(OutError, LOCTEXT("EffectDoesNotBuild", "The effect preset does not build."));
		}
		OutEffect.Tree.Reset(Tree);
		auto AddHead = [&OutEffect](const FString& InPath)
		{
			int32 Dot = INDEX_NONE;
			OutEffect.Heads.AddUnique(FName(*(InPath.FindChar(TEXT('.'), Dot) ? InPath.Left(Dot) : InPath)));
		};
		if (bIsComponent)
		{
			if (Ast.Root.Components.Num() != 1)
			{
				return Fail(OutError, LOCTEXT("EffectOneComponent", "A behaviour's effect preset is one `+` line."));
			}
			const UClass* ComponentClass = FDreamUITextBuilder::ResolveComponentClass(Ast.Root.Components[0].ClassName);
			for (UDreamUIBehaviour* Behaviour : Root->GetAllComponents())
			{
				if (Behaviour != nullptr && Behaviour->GetClass() == ComponentClass)
				{
					OutEffect.Object = Behaviour;
					break;
				}
			}
			for (const FDreamUIProperty& Property : Ast.Root.Components[0].Properties)
			{
				AddHead(Property.Name);
			}
		}
		else
		{
			OutEffect.Object = Root->GetVisual();
			for (const FDreamUIProperty& Property : Ast.Root.Properties)
			{
				AddHead(Property.Name);
			}
		}
		if (OutEffect.Object == nullptr)
		{
			return Fail(OutError, FText::Format(LOCTEXT("EffectNoObject", "'{0}' is no effect a widget can have."), FText::FromString(Word)));
		}
		return true;
	}

	/** The object of InWidget an effect of InClass is: its visual, or its first behaviour of that class. */
	UObject* FindEffectObject(const UDreamWidget* InWidget, const UClass* InClass, bool bInIsComponent)
	{
		if (InWidget == nullptr || InClass == nullptr)
		{
			return nullptr;
		}
		if (!bInIsComponent)
		{
			UDreamVisual* Visual = InWidget->GetVisual();
			return Visual != nullptr && Visual->IsA(InClass) ? Visual : nullptr;
		}
		for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			if (Behaviour != nullptr && Behaviour->IsA(InClass))
			{
				return Behaviour;
			}
		}
		return nullptr;
	}

	/** `Tag { lines }` or `+ Class { lines }`: InObject's writable values that differ from its class's defaults. */
	FString DescribeEffectObject(const UObject* InObject, bool bInIsComponent)
	{
		const UClass* Class = InObject->GetClass();
		const FString Name = bInIsComponent ? FDreamUITextWriteBack::DescribeComponentForText(Class) : TagForVisual(Class);
		if (Name.IsEmpty())
		{
			return FString();
		}
		TArray<FString> Lines;
		const UObject* Defaults = Class->GetDefaultObject();
		for (const FString& Path : DreamUIReflection::GetWritableLeafPaths(Class))
		{
			if (!bInIsComponent && Path.StartsWith(TEXT("BlendMode")))
			{
				continue;
			}
			TArray<FString> Segments;
			Path.ParseIntoArray(Segments, TEXT("."));
			const void* Live = InObject;
			const void* Default = Defaults;
			const UStruct* Scope = Class;
			const FProperty* Leaf = nullptr;
			for (const FString& Segment : Segments)
			{
				Leaf = Scope != nullptr ? FindFProperty<FProperty>(Scope, *Segment) : nullptr;
				if (Leaf == nullptr)
				{
					break;
				}
				Live = Leaf->ContainerPtrToValuePtr<void>(Live);
				Default = Leaf->ContainerPtrToValuePtr<void>(Default);
				const FStructProperty* AsStruct = CastField<FStructProperty>(Leaf);
				Scope = AsStruct != nullptr ? AsStruct->Struct : nullptr;
			}
			FString LiveText;
			FString DefaultText;
			if (Leaf == nullptr || !FDreamUITextWriteBack::PrintValueLiteral(Leaf, Live, LiveText))
			{
				continue;
			}
			if (FDreamUITextWriteBack::PrintValueLiteral(Leaf, Default, DefaultText) && DefaultText == LiveText)
			{
				continue;
			}
			// AnchorData and the like are where the widget stands, not how its effect looks.
			if (Path.StartsWith(TEXT("AnchorData")))
			{
				continue;
			}
			Lines.Add(FString::Printf(TEXT("    %s = %s"), *Path, *LiveText));
		}
		return FString::Printf(TEXT("%s%s {\n%s\n}"), bInIsComponent ? TEXT("+ ") : TEXT(""), *Name, *FString::Join(Lines, TEXT("\n")));
	}

	/** The effect InWidget has that a preset would keep: an effect visual, else its first behaviour the text can name that is not a layer style or a panel. */
	UObject* FindDescribableEffect(const UDreamWidget* InWidget, bool& bOutIsComponent)
	{
		bOutIsComponent = false;
		if (UDreamVisualPostProcess* Effect = Cast<UDreamVisualPostProcess>(InWidget->GetVisual()))
		{
			if (!TagForVisual(Effect->GetClass()).IsEmpty())
			{
				return Effect;
			}
		}
		for (UDreamUIBehaviour* Behaviour : InWidget->GetAllComponents())
		{
			if (Behaviour == nullptr || Behaviour->IsA<UDreamLayerStyle>() || Behaviour->IsA<UDreamLayout>())
			{
				continue;
			}
			if (!FDreamUITextWriteBack::DescribeComponentForText(Behaviour->GetClass()).IsEmpty())
			{
				bOutIsComponent = true;
				return Behaviour;
			}
		}
		return nullptr;
	}

	// ---- writing onto the templates ----------------------------------------------------------------------------------------

	/** Runs InWrite on each template, in one transaction, and writes the tree to the file once. How many InWrite said it changed. */
	int32 ApplyToTemplates(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets, const FText& InName,
		TFunctionRef<bool(UDreamWidgetBlueprint*, UDreamWidget*)> InWrite)
	{
		UDreamWidgetBlueprint* Blueprint = InDesigner.GetWidgetBlueprint();
		if (!IsValid(Blueprint))
		{
			return 0;
		}
		TArray<UDreamWidget*> Templates;
		for (UDreamWidget* Preview : InPreviewWidgets)
		{
			if (UDreamWidget* Template = IsValid(Preview) ? InDesigner.GetTemplateWidget(Preview) : nullptr)
			{
				Templates.AddUnique(Template);
			}
		}
		if (Templates.IsEmpty())
		{
			return 0;
		}
		FScopedTransaction Transaction(InName);
		Blueprint->Modify();
		int32 Applied = 0;
		{
			DreamWidgetTreeEditing::FScopedStructureChangeBatch StructureBatch;
			for (UDreamWidget* Template : Templates)
			{
				Template->SetFlags(RF_Transactional);
				Template->Modify();
				if (InWrite(Blueprint, Template))
				{
					++Applied;
				}
			}
			if (Applied > 0)
			{
				DreamWidgetTreeEditing::NotifyStructureChanged(Blueprint);
			}
		}
		if (Applied == 0)
		{
			Transaction.Cancel();
			return 0;
		}
		TArray<UDreamWidget*> Previews;
		InDesigner.RepublishPreviewAndSelect(Templates, Previews);
		return Applied;
	}

	UObject* CreateAssetOf(UClass* InClass, const FString& InPackagePath, const FString& InName, FText* OutError)
	{
		FString PackagePath = InPackagePath;
		FString Name = InName;
		if (PackagePath.IsEmpty())
		{
			FSaveAssetDialogConfig Config;
			Config.DefaultPath = TEXT("/Game");
			Config.DefaultAssetName = Name;
			Config.AssetClassNames.Add(InClass->GetClassPathName());
			Config.ExistingAssetPolicy = ESaveAssetDialogExistingAssetPolicy::Disallow;
			const FString ObjectPath = FModuleManager::LoadModuleChecked<FContentBrowserModule>(TEXT("ContentBrowser")).Get().CreateModalSaveAssetDialog(Config);
			if (ObjectPath.IsEmpty())
			{
				Fail(OutError, LOCTEXT("Cancelled", "Nothing was made: the Save dialog was cancelled."));
				return nullptr;
			}
			const FString PackageName = FPackageName::ObjectPathToPackageName(ObjectPath);
			PackagePath = FPackageName::GetLongPackagePath(PackageName);
			Name = FPackageName::GetShortName(PackageName);
		}
		FString PackageName;
		FString AssetName;
		FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools")).Get().CreateUniqueAssetName(PackagePath / Name, FString(), PackageName, AssetName);
		UPackage* Package = FPackageName::IsValidLongPackageName(PackageName, /*bIncludeReadOnlyRoots*/true) ? CreatePackage(*PackageName) : nullptr;
		if (Package == nullptr)
		{
			Fail(OutError, FText::Format(LOCTEXT("BadPackage", "'{0}' is not a content path an asset can be made in."), FText::FromString(PackageName)));
			return nullptr;
		}
		UObject* Asset = NewObject<UObject>(Package, InClass, FName(*AssetName), RF_Public | RF_Standalone | RF_Transactional);
		FAssetRegistryModule::AssetCreated(Asset);
		Package->MarkPackageDirty();
		return Asset;
	}
}

// ---------------------------------------------------------------------------------------------------------------------------
// FDreamUIPreset

bool FDreamUIPreset::GetGradient(FDreamGradient& OutGradient) const
{
	if (Kind != EDreamUIPresetKind::Gradient)
	{
		return false;
	}
	if (Source == EDreamUIPresetSource::Asset)
	{
		const UDreamGradientAsset* Loaded = Cast<UDreamGradientAsset>(Asset.TryLoad());
		if (Loaded == nullptr)
		{
			return false;
		}
		OutGradient = Loaded->GetGradient();
		return true;
	}
	return FDreamGradient::ParseCss(Text, OutGradient);
}

bool FDreamUIPreset::GetStyle(FDreamUILayerStyleSnippet& OutSnippet, bool bInFollowAsset) const
{
	if (Kind != EDreamUIPresetKind::Style)
	{
		return false;
	}
	if (Source == EDreamUIPresetSource::Asset)
	{
		OutSnippet = DreamUIStylePresets::MakeSnippet(Cast<UDreamLayerStylePreset>(Asset.TryLoad()), bInFollowAsset);
		return !OutSnippet.IsEmpty();
	}
	return FDreamUILayerStyleSnippet::Parse(Text, OutSnippet);
}

bool FDreamUIPreset::GetEffectClass(UClass*& OutClass, bool& bOutIsComponent) const
{
	OutClass = nullptr;
	if (Kind != EDreamUIPresetKind::Effect)
	{
		return false;
	}
	const FString Word = DreamUIPresetLibraryLocal::FirstWord(Text, bOutIsComponent);
	if (bOutIsComponent)
	{
		OutClass = FDreamUITextBuilder::ResolveComponentClass(Word);
	}
	else
	{
		bool bKnownTag = false;
		OutClass = FDreamUITextBuilder::FindVisualClassForTag(Word, bKnownTag);
	}
	return OutClass != nullptr;
}

// ---------------------------------------------------------------------------------------------------------------------------
// The library

FSimpleMulticastDelegate& DreamUIPresetLibrary::OnChanged()
{
	static FSimpleMulticastDelegate Changed;
	static bool bForwarding = false;
	if (!bForwarding)
	{
		bForwarding = true;
		DreamUIStylePresets::OnPresetsChanged().AddLambda([]() { Changed.Broadcast(); });
		GetMutableDefault<UDreamGUISettings>()->OnSettingChanged().AddLambda([](UObject*, FPropertyChangedEvent& InEvent)
		{
			const FName Name = InEvent.GetMemberPropertyName();
			if (Name == GET_MEMBER_NAME_CHECKED(UDreamGUISettings, GradientPresets)
				|| Name == GET_MEMBER_NAME_CHECKED(UDreamGUISettings, StylePresets)
				|| Name == GET_MEMBER_NAME_CHECKED(UDreamGUISettings, EffectPresets))
			{
				Changed.Broadcast();
			}
		});
	}
	return Changed;
}

void DreamUIPresetLibrary::Collect(EDreamUIPresetKind InKind, TArray<FDreamUIPreset>& OutPresets)
{
	using namespace DreamUIPresetLibraryLocal;
	OutPresets.Reset();
	UDreamGUISettings* Settings = GetMutableDefault<UDreamGUISettings>();
	if (const TMap<FName, FString>* Texts = SettingsMapFor(Settings, InKind))
	{
		for (const TPair<FName, FString>& Pair : *Texts)
		{
			FDreamUIPreset& Preset = OutPresets.AddDefaulted_GetRef();
			Preset.Kind = InKind;
			Preset.Source = EDreamUIPresetSource::ProjectSettings;
			Preset.Name = Pair.Key;
			Preset.Text = Pair.Value;
		}
		OutPresets.Sort([](const FDreamUIPreset& A, const FDreamUIPreset& B) { return A.Name.LexicalLess(B.Name); });
	}
	const UClass* AssetClass = InKind == EDreamUIPresetKind::Gradient ? UDreamGradientAsset::StaticClass()
		: InKind == EDreamUIPresetKind::Style ? UDreamLayerStylePreset::StaticClass() : nullptr;
	if (AssetClass != nullptr)
	{
		TArray<FAssetData> Assets;
		FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssetsByClass(AssetClass->GetClassPathName(), Assets, /*bSearchSubClasses*/true);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.AssetName.LexicalLess(B.AssetName); });
		for (const FAssetData& Data : Assets)
		{
			// A transient package's asset is one a test or a tool made and will throw away, not one of the project's.
			if (Data.PackageName.ToString().StartsWith(TEXT("/Temp/")))
			{
				continue;
			}
			FDreamUIPreset& Preset = OutPresets.AddDefaulted_GetRef();
			Preset.Kind = InKind;
			Preset.Source = EDreamUIPresetSource::Asset;
			Preset.Name = Data.AssetName;
			Preset.Asset = Data.GetSoftObjectPath();
		}
	}
}

bool DreamUIPresetLibrary::CanApply(const UDreamWidget* InWidget, const FDreamUIPreset& InPreset)
{
	using namespace DreamUIPresetLibraryLocal;
	if (!IsValid(InWidget))
	{
		return false;
	}
	switch (InPreset.Kind)
	{
	case EDreamUIPresetKind::Gradient:
		return FindGradientTarget(const_cast<UDreamWidget*>(InWidget)).IsValid();
	case EDreamUIPresetKind::Style:
		return true;
	case EDreamUIPresetKind::Effect:
	{
		UClass* Class = nullptr;
		bool bIsComponent = false;
		// A behaviour's settings can be put on any widget (the behaviour is added); an effect's only on that effect.
		return InPreset.GetEffectClass(Class, bIsComponent) && (bIsComponent || FindEffectObject(InWidget, Class, false) != nullptr);
	}
	}
	return false;
}

int32 DreamUIPresetLibrary::Apply(FDreamWidgetBlueprintEditor& InDesigner, TConstArrayView<UDreamWidget*> InPreviewWidgets,
	const FDreamUIPreset& InPreset, bool bInFollowAsset, FText* OutError)
{
	using namespace DreamUIPresetLibraryLocal;
	const FText Name = FText::Format(LOCTEXT("ApplyPreset", "Apply Preset '{0}'"), FText::FromName(InPreset.Name));
	switch (InPreset.Kind)
	{
	case EDreamUIPresetKind::Style:
	{
		FDreamUILayerStyleSnippet Snippet;
		if (!InPreset.GetStyle(Snippet, bInFollowAsset))
		{
			Fail(OutError, LOCTEXT("StyleUnreadable", "The style preset could not be read."));
			return 0;
		}
		return DreamUILayerStyleClipboard::Apply(InDesigner, InPreviewWidgets, Snippet, EDreamUIStyleParts::All, Name);
	}
	case EDreamUIPresetKind::Gradient:
	{
		FDreamGradient Gradient;
		if (!InPreset.GetGradient(Gradient))
		{
			Fail(OutError, LOCTEXT("GradientUnreadable", "The gradient preset could not be read."));
			return 0;
		}
		UDreamGradientAsset* Asset = InPreset.Source == EDreamUIPresetSource::Asset && bInFollowAsset
			? Cast<UDreamGradientAsset>(InPreset.Asset.TryLoad()) : nullptr;
		const int32 Applied = ApplyToTemplates(InDesigner, InPreviewWidgets, Name, [&Gradient, Asset](UDreamWidgetBlueprint*, UDreamWidget* InTemplate)
		{
			const FGradientTarget Target = FindGradientTarget(InTemplate);
			if (!Target.IsValid())
			{
				return false;
			}
			Target.Owner->SetFlags(RF_Transactional);
			Target.Owner->Modify();
			if (Target.Paint != nullptr)
			{
				Target.Paint->bEnabled = true;
				Target.Paint->Preset = Asset;
				if (Asset == nullptr)
				{
					Target.Paint->Gradient = Gradient;
				}
			}
			else
			{
				*Target.Gradient = Gradient;
			}
			FDreamUITextWriteBack::NoteDirtyProperty(InTemplate->GetTypedOuter<UDreamWidgetTree>(), InTemplate->GetDisplayName(),
				EDreamUIPatchTarget::Node, INDEX_NONE, Target.Head.ToString());
			// A gradient its owner draws only while a switch is on has the switch turned on with it, or it would not be seen.
			if (const FBoolProperty* Switch = FindGradientSwitch(Target))
			{
				if (!Switch->GetPropertyValue_InContainer(Target.Owner))
				{
					Switch->SetPropertyValue_InContainer(Target.Owner, true);
					FDreamUITextWriteBack::NoteDirtyProperty(InTemplate->GetTypedOuter<UDreamWidgetTree>(), InTemplate->GetDisplayName(),
						EDreamUIPatchTarget::Node, INDEX_NONE, Switch->GetName());
				}
			}
			return true;
		});
		if (Applied == 0)
		{
			Fail(OutError, LOCTEXT("NoGradientTarget", "None of the widgets has a gradient to set: a text's face paint, or a gradient property."));
		}
		return Applied;
	}
	case EDreamUIPresetKind::Effect:
	{
		FBuiltEffect Effect;
		if (!BuildEffect(InPreset.Text, Effect, OutError))
		{
			return 0;
		}
		const bool bIsComponent = Effect.Object->IsA<UDreamUIBehaviour>();
		UClass* EffectClass = Effect.Object->GetClass();
		const int32 Applied = ApplyToTemplates(InDesigner, InPreviewWidgets, Name,
			[&Effect, bIsComponent, EffectClass](UDreamWidgetBlueprint* InBlueprint, UDreamWidget* InTemplate)
		{
			UObject* Target = FindEffectObject(InTemplate, EffectClass, bIsComponent);
			if (Target == nullptr && bIsComponent)
			{
				// A behaviour's settings bring the behaviour: a `+` line the file takes, checked first.
				if (DreamUITextAuthoring::IsTextAuthored(InBlueprint))
				{
					FDreamUIStructuralEdit Planned;
					Planned.Kind = EDreamUIStructuralEditKind::InsertComponent;
					Planned.NodeId = InTemplate->GetDisplayName();
					Planned.ComponentClassName = FDreamUITextWriteBack::DescribeComponentForText(EffectClass);
					FText Reason = LOCTEXT("NoBehaviourSpelling", "the .dui has no name for this behaviour");
					if (Planned.ComponentClassName.IsEmpty()
						|| !DreamUITextAuthoring::CheckStructuralEdit(InBlueprint, Planned, /*bInVerifyBuild*/false, Reason))
					{
						DreamUITextAuthoring::ReportStructuralRefusal(InBlueprint, ANSI_TO_TCHAR(__FUNCTION__), __LINE__,
							FString::Printf(TEXT("add '%s' to '%s'"), *EffectClass->GetName(), *InTemplate->GetDisplayName()), Reason);
						return false;
					}
				}
				Target = InTemplate->AddComponent(EffectClass);
			}
			if (Target == nullptr)
			{
				return false;
			}
			Target->SetFlags(RF_Transactional);
			Target->Modify();
			const UDreamWidgetTree* Tree = InTemplate->GetTypedOuter<UDreamWidgetTree>();
			const int32 Index = bIsComponent ? InTemplate->GetAllComponents().Find(Cast<UDreamUIBehaviour>(Target)) : INDEX_NONE;
			for (const FName Head : Effect.Heads)
			{
				const FProperty* Property = FindFProperty<FProperty>(EffectClass, Head);
				if (Property == nullptr)
				{
					continue;
				}
				Property->CopyCompleteValue_InContainer(Target, Effect.Object);
				FDreamUITextWriteBack::NoteDirtyProperty(Tree, InTemplate->GetDisplayName(),
					bIsComponent ? EDreamUIPatchTarget::Component : EDreamUIPatchTarget::Node, Index, Head.ToString());
			}
			return true;
		});
		if (Applied == 0)
		{
			Fail(OutError, FText::Format(LOCTEXT("NoEffectTarget", "None of the widgets is a {0}."), EffectClass->GetDisplayNameText()));
		}
		return Applied;
	}
	}
	return 0;
}

bool DreamUIPresetLibrary::DescribeWidget(const UDreamWidget* InWidget, EDreamUIPresetKind InKind, FString& OutText)
{
	using namespace DreamUIPresetLibraryLocal;
	OutText.Reset();
	if (!IsValid(InWidget))
	{
		return false;
	}
	switch (InKind)
	{
	case EDreamUIPresetKind::Gradient:
	{
		const FGradientTarget Target = FindGradientTarget(const_cast<UDreamWidget*>(InWidget));
		if (!Target.IsValid())
		{
			return false;
		}
		const FDreamGradient& Gradient = Target.Paint != nullptr ? Target.Paint->GetEffectiveGradient() : *Target.Gradient;
		OutText = Gradient.ToCss();
		return !OutText.IsEmpty();
	}
	case EDreamUIPresetKind::Style:
	{
		const FDreamUILayerStyleSnippet Snippet = FDreamUILayerStyleSnippet::FromWidget(InWidget,
			DreamUILayerStyleClipboard::GetCopyParts());
		if (!Snippet.HasPart(EDreamUIStyleParts::Style))
		{
			return false;
		}
		OutText = Snippet.GetText();
		return true;
	}
	case EDreamUIPresetKind::Effect:
	{
		bool bIsComponent = false;
		if (const UObject* Effect = FindDescribableEffect(InWidget, bIsComponent))
		{
			OutText = DescribeEffectObject(Effect, bIsComponent);
		}
		return !OutText.IsEmpty();
	}
	}
	return false;
}

bool DreamUIPresetLibrary::SaveTextPreset(EDreamUIPresetKind InKind, FName InName, const FString& InText, FText* OutError)
{
	using namespace DreamUIPresetLibraryLocal;
	if (InName.IsNone() || InName.ToString().TrimStartAndEnd().IsEmpty())
	{
		return Fail(OutError, LOCTEXT("NoName", "A text preset needs a name."));
	}
	// Read before it is kept: a preset that does not read would sit on the shelf doing nothing.
	FDreamUIPreset Probe;
	Probe.Kind = InKind;
	Probe.Text = InText;
	switch (InKind)
	{
	case EDreamUIPresetKind::Gradient:
	{
		FDreamGradient Gradient;
		FString Error;
		if (!FDreamGradient::ParseCss(InText, Gradient, &Error))
		{
			return Fail(OutError, FText::Format(LOCTEXT("BadCss", "Not a gradient: {0}"), FText::FromString(Error)));
		}
		break;
	}
	case EDreamUIPresetKind::Style:
	{
		FDreamUILayerStyleSnippet Snippet;
		if (!FDreamUILayerStyleSnippet::Parse(InText, Snippet, OutError))
		{
			return false;
		}
		break;
	}
	case EDreamUIPresetKind::Effect:
	{
		FBuiltEffect Effect;
		if (!BuildEffect(InText, Effect, OutError))
		{
			return false;
		}
		break;
	}
	}
	SettingsMapFor(GetMutableDefault<UDreamGUISettings>(), InKind)->Add(InName, InText);
	SaveSettingsMap(InKind);
	return true;
}

bool DreamUIPresetLibrary::RemoveTextPreset(EDreamUIPresetKind InKind, FName InName)
{
	using namespace DreamUIPresetLibraryLocal;
	if (SettingsMapFor(GetMutableDefault<UDreamGUISettings>(), InKind)->Remove(InName) == 0)
	{
		return false;
	}
	SaveSettingsMap(InKind);
	return true;
}

UObject* DreamUIPresetLibrary::SaveAsAsset(const FDreamUIPreset& InPreset, const FString& InPackagePath, FText* OutError)
{
	using namespace DreamUIPresetLibraryLocal;
	if (InPreset.Kind == EDreamUIPresetKind::Gradient)
	{
		FDreamGradient Gradient;
		if (!InPreset.GetGradient(Gradient))
		{
			Fail(OutError, LOCTEXT("GradientUnreadableSave", "The gradient preset could not be read."));
			return nullptr;
		}
		UDreamGradientAsset* Asset = Cast<UDreamGradientAsset>(CreateAssetOf(UDreamGradientAsset::StaticClass(), InPackagePath,
			FString::Printf(TEXT("G_%s"), *InPreset.Name.ToString()), OutError));
		if (Asset != nullptr)
		{
			Asset->SetGradient(Gradient);
			OnChanged().Broadcast();
		}
		return Asset;
	}
	if (InPreset.Kind == EDreamUIPresetKind::Style)
	{
		FDreamUILayerStyleSnippet Snippet;
		if (!InPreset.GetStyle(Snippet, /*bInFollowAsset*/false) || Snippet.GetLayerStyle() == nullptr)
		{
			Fail(OutError, LOCTEXT("StyleUnreadableSave", "The style preset could not be read."));
			return nullptr;
		}
		FDreamUIStylePresetSaveArgs Args;
		Args.Style = Snippet.GetLayerStyle()->GetStyle();
		EDreamUIBlendMode Mode = EDreamUIBlendMode::Alpha;
		float Opacity = 1.0f;
		if (Snippet.GetBlending(Mode, Opacity))
		{
			FDreamLayerStyleBlending Blending;
			Blending.BlendMode = Mode;
			Blending.Opacity = Opacity;
			Args.Blending = Blending;
		}
		Args.Name = FString::Printf(TEXT("LS_%s"), *InPreset.Name.ToString());
		Args.PackagePath = InPackagePath;
		return DreamUIStylePresets::CreatePresetAsset(Args, OutError);
	}
	Fail(OutError, LOCTEXT("EffectsAreText", "Effect presets are kept as text."));
	return nullptr;
}

bool DreamUIPresetLibrary::SaveAsText(const FDreamUIPreset& InPreset, FName InName, FText* OutError)
{
	using namespace DreamUIPresetLibraryLocal;
	if (InPreset.Kind == EDreamUIPresetKind::Gradient)
	{
		FDreamGradient Gradient;
		if (!InPreset.GetGradient(Gradient))
		{
			return Fail(OutError, LOCTEXT("GradientUnreadableText", "The gradient preset could not be read."));
		}
		return SaveTextPreset(InPreset.Kind, InName, Gradient.ToCss(), OutError);
	}
	if (InPreset.Kind == EDreamUIPresetKind::Style)
	{
		FDreamUILayerStyleSnippet Snippet;
		if (!InPreset.GetStyle(Snippet, /*bInFollowAsset*/false))
		{
			return Fail(OutError, LOCTEXT("StyleUnreadableText", "The style preset could not be read."));
		}
		return SaveTextPreset(InPreset.Kind, InName, Snippet.GetText(), OutError);
	}
	return SaveTextPreset(InPreset.Kind, InName, InPreset.Text, OutError);
}

// ---------------------------------------------------------------------------------------------------------------------------
// Dragging a preset

TSharedRef<FDreamUIPresetDragDropOp> FDreamUIPresetDragDropOp::New(const FDreamUIPreset& InPreset)
{
	TSharedRef<FDreamUIPresetDragDropOp> Operation = MakeShared<FDreamUIPresetDragDropOp>();
	Operation->Preset = InPreset;
	Operation->DefaultHoverText = FText::Format(LOCTEXT("DragPreset", "Apply '{0}'"), FText::FromName(InPreset.Name));
	Operation->CurrentHoverText = Operation->DefaultHoverText;
	Operation->DefaultHoverIcon = FAppStyle::GetBrush(InPreset.Kind == EDreamUIPresetKind::Gradient ? "Icons.Color" : "Icons.Adjust");
	Operation->CurrentIconBrush = Operation->DefaultHoverIcon;
	Operation->Construct();
	return Operation;
}

bool FDreamUIPresetDragDropOp::CanDropOnto(const FDragDropEvent& InEvent, const UDreamWidget* InPreviewWidget)
{
	const TSharedPtr<FDreamUIPresetDragDropOp> Operation = InEvent.GetOperationAs<FDreamUIPresetDragDropOp>();
	return Operation.IsValid() && DreamUIPresetLibrary::CanApply(InPreviewWidget, Operation->GetPreset());
}

bool FDreamUIPresetDragDropOp::DropOnto(const FDragDropEvent& InEvent, FDreamWidgetBlueprintEditor& InDesigner, UDreamWidget* InPreviewWidget)
{
	const TSharedPtr<FDreamUIPresetDragDropOp> Operation = InEvent.GetOperationAs<FDreamUIPresetDragDropOp>();
	if (!Operation.IsValid() || !IsValid(InPreviewWidget))
	{
		return false;
	}
	// Onto one of the selected widgets: the whole selection takes it, as a paste does. Onto any other: that one.
	TArray<UDreamWidget*> Targets;
	bool bOntoSelection = false;
	for (const TWeakObjectPtr<UDreamWidget>& Selected : InDesigner.GetSelectedWidgets())
	{
		bOntoSelection |= Selected.Get() == InPreviewWidget;
	}
	if (bOntoSelection)
	{
		for (const TWeakObjectPtr<UDreamWidget>& Selected : InDesigner.GetSelectedWidgets())
		{
			if (Selected.IsValid())
			{
				Targets.Add(Selected.Get());
			}
		}
	}
	else
	{
		Targets.Add(InPreviewWidget);
	}
	return DreamUIPresetLibrary::Apply(InDesigner, Targets, Operation->GetPreset()) > 0;
}

// ---------------------------------------------------------------------------------------------------------------------------
// A drop target for property rows

void SDreamUIPresetDropTarget::Construct(const FArguments& InArgs)
{
	Kind = InArgs._Kind;
	OnDroppedDelegate = InArgs._OnDropped;
	ChildSlot
	[
		SNew(SBorder)
		.Padding(FMargin(0.0f))
		.BorderImage_Lambda([this]()
		{
			return bDragOver ? FAppStyle::GetBrush("Brushes.Highlight") : FAppStyle::GetBrush("NoBorder");
		})
		[
			InArgs._Content.Widget
		]
	];
}

bool SDreamUIPresetDropTarget::Accepts(const FDragDropEvent& InEvent) const
{
	const TSharedPtr<FDreamUIPresetDragDropOp> Operation = InEvent.GetOperationAs<FDreamUIPresetDragDropOp>();
	return Operation.IsValid() && Operation->GetKind() == Kind;
}

void SDreamUIPresetDropTarget::OnDragEnter(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bDragOver = Accepts(DragDropEvent);
}

void SDreamUIPresetDropTarget::OnDragLeave(const FDragDropEvent& DragDropEvent)
{
	bDragOver = false;
}

FReply SDreamUIPresetDropTarget::OnDragOver(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	return Accepts(DragDropEvent) ? FReply::Handled() : FReply::Unhandled();
}

FReply SDreamUIPresetDropTarget::OnDrop(const FGeometry& MyGeometry, const FDragDropEvent& DragDropEvent)
{
	bDragOver = false;
	const TSharedPtr<FDreamUIPresetDragDropOp> Operation = DragDropEvent.GetOperationAs<FDreamUIPresetDragDropOp>();
	if (!Operation.IsValid() || Operation->GetKind() != Kind)
	{
		return FReply::Unhandled();
	}
	OnDroppedDelegate.ExecuteIfBound(*Operation);
	return FReply::Handled();
}

#undef LOCTEXT_NAMESPACE
