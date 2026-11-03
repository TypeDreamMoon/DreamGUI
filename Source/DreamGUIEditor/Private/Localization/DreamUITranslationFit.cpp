// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Localization/DreamUITranslationFit.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_BaseObject.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "DreamWidgetBlueprint.h"
#include "Text/DreamUIDiagnostics.h"
#include "Text/DreamUIPaths.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUITextBuilder.h"

#include "Misc/Paths.h"

#if WITH_DREAMLOC
#include "DreamLocCheck.h"
#include "DreamLocContextProvider.h"
#include "DreamLocQuery.h"
#include "Features/IModularFeatures.h"
#endif

namespace DreamUITranslationFitLocal
{
	FString LastId(const FString& InNodePath)
	{
		FString Id = InNodePath;
		int32 Slash = INDEX_NONE;
		if (Id.FindLastChar(TEXT('/'), Slash))
		{
			Id.RightChopInline(Slash + 1);
		}
		return Id;
	}
}

bool DreamUITranslationFit::Measure(const UDreamWidgetTree* InTree, const FString& InNodePath, const FString& InNamespace, const FString& InKey,
	const FString& InText, const FString& InCulture, FFit& OutFit)
{
	using namespace DreamUITranslationFitLocal;
	OutFit = FFit();
	if (InTree == nullptr)
	{
		return false;
	}
	const FString Id = LastId(InNodePath);
	const UDreamText* Text = nullptr;
	const_cast<UDreamWidgetTree*>(InTree)->ForEachWidget([&Text, &Id](UDreamWidget* InWidget)
	{
		if (Text == nullptr && InWidget != nullptr && InWidget->GetDisplayName() == Id)
		{
			Text = Cast<UDreamText>(InWidget->GetVisual());
		}
	});
	if (Text == nullptr)
	{
		return false;
	}
	const TOptional<FString> Namespace = FTextInspector::GetNamespace(Text->GetText());
	const TOptional<FString> Key = FTextInspector::GetKey(Text->GetText());
	if (!Key.IsSet() || Key.GetValue() != InKey || !Namespace.IsSet() || Namespace.GetValue() != InNamespace)
	{
		return false;
	}
	const FDreamTextFit Fit = Text->MeasureFit(InText, InCulture);
	if (!Fit.bMeasured)
	{
		return false;
	}
	OutFit.bMeasured = true;
	OutFit.bOverflows = Fit.bOverflows;
	OutFit.Needed = Fit.Needed;
	OutFit.Box = Fit.Box;
	OutFit.MissingGlyphs = Text->FindMissingGlyphs(InText);
	OutFit.Where = FString::Printf(TEXT("'%s' (%s, %g)"), *Id, Text->GetFont() != nullptr ? *Text->GetFont()->GetName() : TEXT("no font"), Text->GetFontSize());
	return true;
}

#if WITH_DREAMLOC

void DreamUITranslationFit::ReportTranslations(const UDreamWidgetTree* InTree, const FDreamUILocalization& InLocalization, FDreamUIDiagnosticBag& OutDiagnostics)
{
	using namespace DreamUITranslationFitLocal;
	if (InTree == nullptr || InLocalization.Texts.Num() == 0)
	{
		return;
	}
	TArray<FString> Keys;
	for (const FDreamUILocalizableText& Text : InLocalization.Texts)
	{
		Keys.Add(Text.Key);
	}
	const TMap<FString, TArray<DreamLocQuery::FTranslation>> Translations = DreamLocQuery::FindTranslations(InLocalization.Namespace, Keys);
	for (const FDreamUILocalizableText& Text : InLocalization.Texts)
	{
		const TArray<DreamLocQuery::FTranslation>* Found = Translations.Find(Text.Key);
		for (const DreamLocQuery::FTranslation& Translation : Found != nullptr ? *Found : TArray<DreamLocQuery::FTranslation>())
		{
			FFit Fit;
			if (!Measure(InTree, Text.NodePath, InLocalization.Namespace, Text.Key, Translation.Text, Translation.Culture, Fit))
			{
				continue;
			}
			const FString Where = FString::Printf(TEXT("%s(%d)"), *Translation.File, Translation.Line);
			if (Fit.bOverflows)
			{
				OutDiagnostics.AddWarning(EDreamUIDiagnosticCode::TranslationDoesNotFit, Text.Location, FString::Printf(
					TEXT("the %s translation of %s does not fit: it needs %.0f x %.0f, the text has %.0f x %.0f (%s)"),
					*Translation.Culture, *Fit.Where, Fit.Needed.X, Fit.Needed.Y, Fit.Box.X, Fit.Box.Y, *Where));
			}
			if (Fit.MissingGlyphs.Num() > 0)
			{
				OutDiagnostics.AddWarning(EDreamUIDiagnosticCode::TranslationGlyphsMissing, Text.Location, FString::Printf(
					TEXT("the font of %s has no glyph for %s in the %s translation (%s)"),
					*Fit.Where, *DreamLocCheck::DescribeGlyphs(Fit.MissingGlyphs), *Translation.Culture, *Where));
			}
		}
	}
}

namespace DreamUITranslationFitLocal
{
	/** `dloc check` laying a .dui text's translations out: in the class compiled from the .dui, as the editor has it. */
	class FProvider final : public IDreamLocContextProvider
	{
	public:
		virtual bool HandlesSource(const FString& InSource) const override
		{
			return InSource.EndsWith(TEXT(".dui"), ESearchCase::IgnoreCase);
		}

		virtual bool MeasureTranslation(const FDreamLocFitRequest& InRequest, FDreamLocFitResult& OutResult) override
		{
			FString Source;
			FString NodePath;
			if (!InRequest.At.Split(TEXT("#"), &Source, &NodePath))
			{
				return false;
			}
			const UDreamWidgetTree* Tree = FindTree(Source, InRequest.OwnerDirectory, InRequest.Namespace);
			DreamUITranslationFit::FFit Fit;
			if (!DreamUITranslationFit::Measure(Tree, NodePath, InRequest.Namespace, InRequest.Key, InRequest.Text, InRequest.Culture, Fit))
			{
				return false;
			}
			OutResult.bMeasured = true;
			OutResult.bOverflows = Fit.bOverflows;
			OutResult.Needed = Fit.Needed;
			OutResult.Box = Fit.Box;
			OutResult.MissingGlyphs = Fit.MissingGlyphs;
			OutResult.Where = Fit.Where;
			return true;
		}

	private:
		/**
		 * The tree compiled from the .dui: a loaded Blueprint whose Source File it is (one compiled this session among them),
		 * else the class the source watcher finds for it, else the class its `class` line names.
		 */
		static const UDreamWidgetTree* FindTree(const FString& InSource, const FString& InOwnerDirectory, const FString& InNamespace)
		{
			TArray<FString> Candidates;
			Candidates.Add(FPaths::ConvertRelativePathToFull(InOwnerDirectory / InSource));
			Candidates.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / InSource));
			for (FString& Candidate : Candidates)
			{
				FPaths::NormalizeFilename(Candidate);
				TArray<UDreamWidgetBlueprint*> Blueprints;
				FDreamUISourceWatcher::FindBlueprintsForSource(Candidate, Blueprints);
				for (const UDreamWidgetBlueprint* Blueprint : Blueprints)
				{
					if (const UDreamWidgetTree* Tree = Blueprint != nullptr ? UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Blueprint->GeneratedClass) : nullptr)
					{
						return Tree;
					}
				}
				if (UClass* Class = FDreamUISourceWatcher::FindClassForSource(Candidate))
				{
					if (const UDreamWidgetTree* Tree = UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Class))
					{
						return Tree;
					}
				}
			}
			if (InNamespace.StartsWith(TEXT("/")))
			{
				const FString Name = FPaths::GetBaseFilename(InNamespace);
				if (UClass* Class = LoadObject<UClass>(nullptr, *FString::Printf(TEXT("%s.%s_C"), *InNamespace, *Name), nullptr, LOAD_NoWarn | LOAD_Quiet))
				{
					return UDreamWidgetGeneratedClass::FindWidgetTreeArchetype(Class);
				}
			}
			return nullptr;
		}
	};

	TUniquePtr<FProvider> Provider;
}

void DreamUITranslationFit::RegisterWithDreamLoc()
{
	using namespace DreamUITranslationFitLocal;
	if (!Provider.IsValid())
	{
		Provider = MakeUnique<FProvider>();
		IModularFeatures::Get().RegisterModularFeature(IDreamLocContextProvider::GetModularFeatureName(), Provider.Get());
	}
}

void DreamUITranslationFit::UnregisterFromDreamLoc()
{
	using namespace DreamUITranslationFitLocal;
	if (Provider.IsValid())
	{
		IModularFeatures::Get().UnregisterModularFeature(IDreamLocContextProvider::GetModularFeatureName(), Provider.Get());
		Provider.Reset();
	}
}

#else // WITH_DREAMLOC

void DreamUITranslationFit::ReportTranslations(const UDreamWidgetTree*, const FDreamUILocalization&, FDreamUIDiagnosticBag&)
{
}

void DreamUITranslationFit::RegisterWithDreamLoc()
{
}

void DreamUITranslationFit::UnregisterFromDreamLoc()
{
}

#endif // WITH_DREAMLOC
