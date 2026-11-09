// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DetailCustomization/DreamDetailsLayout.h"

#include "Core/Components/DreamVisual.h"
#include "DetailCategoryBuilder.h"
#include "DetailLayoutBuilder.h"
#include "IDetailCustomization.h"
#include "IDetailGroup.h"
#include "IDetailPropertyRow.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "UObject/UObjectIterator.h"

namespace DreamDetailsLayoutLocal
{
	/** The class the objects all are. */
	const UClass* CommonClassOf(const TArray<UObject*>& InObjects)
	{
		const UClass* Common = nullptr;
		for (const UObject* Object : InObjects)
		{
			if (!IsValid(Object))
			{
				return nullptr;
			}
			if (Common == nullptr)
			{
				Common = Object->GetClass();
			}
			while (Common != nullptr && !Object->IsA(Common))
			{
				Common = Common->GetSuperClass();
			}
		}
		return Common;
	}

	/** The top category of a property's Category, and what follows it as group names: "DreamGUI|Pointer Light". */
	void SplitCategory(const FProperty* InProperty, FName& OutCategory, TArray<FString>& OutSubcategories)
	{
		FString Category;
#if WITH_EDITORONLY_DATA
		Category = InProperty->GetMetaData(TEXT("Category"));
#endif
		TArray<FString> Parts;
		Category.ParseIntoArray(Parts, TEXT("|"), true);
		for (FString& Part : Parts)
		{
			Part.TrimStartAndEndInline();
		}
		if (Parts.Num() == 0)
		{
			OutCategory = InProperty->GetOwnerStruct() != nullptr ? InProperty->GetOwnerStruct()->GetFName() : NAME_None;
			return;
		}
		OutCategory = FName(*Parts[0]);
		Parts.RemoveAt(0);
		OutSubcategories = MoveTemp(Parts);
	}
}

FDreamDetailsLayout::FDreamDetailsLayout(IDetailLayoutBuilder& InBuilder)
	: Builder(InBuilder)
{
}

IDetailCategoryBuilder& FDreamDetailsLayout::Section(DreamPropertyMeta::ESection InSection)
{
	const DreamPropertyMeta::FSectionInfo& Info = DreamPropertyMeta::GetSectionInfo(InSection);
	IDetailCategoryBuilder& Category = Builder.EditCategory(Info.CategoryName, Info.DisplayName, ECategoryPriority::Default);
	Category.SetSortOrder(Info.SortOrder);
	if (Info.bInitiallyCollapsed)
	{
		Category.InitiallyCollapsed(true);
	}
	return Category;
}

IDetailGroup& FDreamDetailsLayout::Group(DreamPropertyMeta::ESection InSection, const FString& InPath, bool bInAdvanced)
{
	TArray<FString> Parts;
	InPath.ParseIntoArray(Parts, TEXT("/"), true);
	for (FString& Part : Parts)
	{
		Part.TrimStartAndEndInline();
	}
	check(Parts.Num() > 0);
	return GroupIn(Section(InSection), Parts, bInAdvanced);
}

IDetailGroup& FDreamDetailsLayout::GroupIn(IDetailCategoryBuilder& InCategory, const TArray<FString>& InPath, bool bInAdvanced)
{
	IDetailGroup* Parent = nullptr;
	// The builder does not name its category, so groups are told apart by which category they are in; their own names, which
	// the panel keeps expansion state by, are their paths.
	FString Key = FString::Printf(TEXT("%p%s"), &InCategory, bInAdvanced ? TEXT("#advanced") : TEXT(""));
	FString GroupPath = bInAdvanced ? TEXT("Advanced") : TEXT("");
	for (const FString& Part : InPath)
	{
		Key += TEXT("/") + Part;
		GroupPath += (GroupPath.IsEmpty() ? TEXT("") : TEXT("_")) + Part.Replace(TEXT(" "), TEXT(""));
		if (IDetailGroup** Found = Groups.Find(Key))
		{
			Parent = *Found;
			continue;
		}
		const FName Name(*GroupPath);
		IDetailGroup& Made = Parent == nullptr
			? InCategory.AddGroup(Name, FText::FromString(Part), bInAdvanced, /*bStartExpanded*/true)
			: Parent->AddGroup(Name, FText::FromString(Part), /*bStartExpanded*/true);
		Groups.Add(Key, &Made);
		Parent = &Made;
	}
	return *Parent;
}

IDetailCategoryBuilder& FDreamDetailsLayout::CategoryFor(const FProperty* InProperty)
{
	const DreamPropertyMeta::ESection AsSection = DreamPropertyMeta::GetSection(InProperty);
	if (AsSection != DreamPropertyMeta::ESection::None)
	{
		return Section(AsSection);
	}
	FName Category;
	TArray<FString> Subcategories;
	DreamDetailsLayoutLocal::SplitCategory(InProperty, Category, Subcategories);
	return Builder.EditCategory(Category);
}

TArray<const FProperty*> FDreamDetailsLayout::PropertiesInOrder(const UClass* InClass)
{
	TArray<const UClass*> Chain;
	for (const UClass* Class = InClass; Class != nullptr; Class = Class->GetSuperClass())
	{
		Chain.Insert(Class, 0);
	}
	TArray<const FProperty*> Out;
	for (const UClass* Class : Chain)
	{
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (It->HasAnyPropertyFlags(CPF_Edit))
			{
				Out.Add(*It);
			}
		}
	}
	return Out;
}

bool FDreamDetailsLayout::IsDrawnByHost(const UClass* InClass, DreamPropertyMeta::ESection InSection)
{
	return InClass != nullptr && InSection == DreamPropertyMeta::ESection::Layer && InClass->IsChildOf(UDreamVisual::StaticClass());
}

void FDreamDetailsLayout::Place(TArray<FPlacement>& InPlacements, TMap<FName, IDetailPropertyRow*>* OutRows)
{
	// Smaller order first; untagged order counts as 0, and a tie keeps declaration order -- the objects being customized before
	// the objects shown inside them.
	InPlacements.StableSort([](const FPlacement& A, const FPlacement& B)
	{
		return DreamPropertyMeta::GetOrder(A.Property).Get(0) < DreamPropertyMeta::GetOrder(B.Property).Get(0);
	});
	for (const FPlacement& Placement : InPlacements)
	{
		const TSharedPtr<IPropertyHandle> Handle = Placement.Objects.Num() > 0
			? Builder.AddObjectPropertyData(Placement.Objects, Placement.Property->GetFName())
			: TSharedPtr<IPropertyHandle>(Builder.GetProperty(Placement.Property->GetFName(), Placement.Property->GetOwnerClass()));
		if (!Handle.IsValid() || !Handle->IsValidHandle())
		{
			continue;
		}
		IDetailCategoryBuilder& Category = CategoryFor(Placement.Property);
		const bool bAdvanced = DreamPropertyMeta::IsAdvanced(Placement.Property);
		TArray<FString> Path = DreamPropertyMeta::GetGroup(Placement.Property);
		if (DreamPropertyMeta::GetSection(Placement.Property) == DreamPropertyMeta::ESection::None && Path.Num() > 0)
		{
			// A group without a section is a group of the property's own category, under the sub-category it already names.
			FName Top;
			TArray<FString> Subcategories;
			DreamDetailsLayoutLocal::SplitCategory(Placement.Property, Top, Subcategories);
			Subcategories.Append(Path);
			Path = MoveTemp(Subcategories);
		}
		IDetailPropertyRow& Row = Path.Num() > 0
			? GroupIn(Category, Path, bAdvanced).AddPropertyRow(Handle.ToSharedRef())
			: Category.AddProperty(Handle, bAdvanced ? EPropertyLocation::Advanced : EPropertyLocation::Default);
		if (OutRows)
		{
			OutRows->Add(Placement.Property->GetFName(), &Row);
		}
	}
}

TMap<FName, IDetailPropertyRow*> FDreamDetailsLayout::PlaceSection(DreamPropertyMeta::ESection InSection, const TArray<UObject*>& InHosted)
{
	TArray<FPlacement> Placements;
	if (const UClass* Base = Builder.GetBaseClass())
	{
		for (const FProperty* Property : PropertiesInOrder(Base))
		{
			if (DreamPropertyMeta::GetSection(Property) != InSection || DreamPropertyMeta::IsHidden(Property))
			{
				continue;
			}
			const TSharedRef<IPropertyHandle> Handle = Builder.GetProperty(Property->GetFName(), Property->GetOwnerClass());
			if (Handle->IsValidHandle() && !Handle->IsCustomized())
			{
				Placements.Add({ Property, {} });
			}
		}
	}
	if (const UClass* Hosted = DreamDetailsLayoutLocal::CommonClassOf(InHosted); Hosted != nullptr && IsDrawnByHost(Hosted, InSection))
	{
		for (const FProperty* Property : PropertiesInOrder(Hosted))
		{
			if (DreamPropertyMeta::GetSection(Property) == InSection && !DreamPropertyMeta::IsHidden(Property))
			{
				Placements.Add({ Property, InHosted });
			}
		}
	}
	TMap<FName, IDetailPropertyRow*> Rows;
	Place(Placements, &Rows);
	return Rows;
}

IDetailPropertyRow* FDreamDetailsLayout::ShowWhen(FName InProperty, TAttribute<bool> InApplies)
{
	const TSharedRef<IPropertyHandle> Handle = Builder.GetProperty(InProperty);
	const FProperty* Property = Handle->IsValidHandle() ? Handle->GetProperty() : nullptr;
	if (Property == nullptr || Handle->IsCustomized())
	{
		return nullptr;
	}
	IDetailPropertyRow& Row = CategoryFor(Property).AddProperty(Handle, DreamPropertyMeta::IsAdvanced(Property) ? EPropertyLocation::Advanced : EPropertyLocation::Default);
	Row.Visibility(TAttribute<EVisibility>::CreateLambda([InApplies]() { return InApplies.Get() ? EVisibility::Visible : EVisibility::Collapsed; }));
	return &Row;
}

void FDreamDetailsLayout::PlaceTagged()
{
	const UClass* Base = Builder.GetBaseClass();
	if (Base == nullptr)
	{
		return;
	}
	TArray<FPlacement> Placements;
	for (const FProperty* Property : PropertiesInOrder(Base))
	{
		const bool bHidden = DreamPropertyMeta::IsHidden(Property);
		const bool bByHost = IsDrawnByHost(Base, DreamPropertyMeta::GetSection(Property));
		if (!bHidden && !bByHost && !DreamPropertyMeta::HasPlacement(Property))
		{
			continue;
		}
		const TSharedRef<IPropertyHandle> Handle = Builder.GetProperty(Property->GetFName(), Property->GetOwnerClass());
		if (!Handle->IsValidHandle() || Handle->IsCustomized())
		{
			continue;
		}
		if (bHidden || bByHost)
		{
			Builder.HideProperty(Handle);
			continue;
		}
		Placements.Add({ Property, {} });
	}
	Place(Placements, nullptr);
}

namespace DreamDetailsLayoutLocal
{
	/** A class customization and PlaceTagged after it, on the same panel. */
	class FTaggedCustomization : public IDetailCustomization
	{
	public:
		explicit FTaggedCustomization(TSharedPtr<IDetailCustomization> InInner) : Inner(MoveTemp(InInner)) {}

		virtual void CustomizeDetails(IDetailLayoutBuilder& DetailBuilder) override
		{
			if (Inner.IsValid())
			{
				Inner->CustomizeDetails(DetailBuilder);
			}
			FDreamDetailsLayout(DetailBuilder).PlaceTagged();
		}

		virtual void CustomizeDetails(const TSharedPtr<IDetailLayoutBuilder>& DetailBuilder) override
		{
			// The inner one may keep the builder to refresh the panel with, so it is handed the shared one.
			if (Inner.IsValid())
			{
				Inner->CustomizeDetails(DetailBuilder);
			}
			if (DetailBuilder.IsValid())
			{
				FDreamDetailsLayout(*DetailBuilder).PlaceTagged();
			}
		}

		virtual void PendingDelete() override
		{
			if (Inner.IsValid())
			{
				Inner->PendingDelete();
			}
		}

	private:
		TSharedPtr<IDetailCustomization> Inner;
	};

	TArray<FName>& TaggedRegistrations()
	{
		static TArray<FName> Names;
		return Names;
	}

	FDelegateHandle& ModulesChangedHandle()
	{
		static FDelegateHandle Handle;
		return Handle;
	}

	bool DeclaresTags(const UClass* InClass)
	{
		for (TFieldIterator<FProperty> It(InClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			if (DreamPropertyMeta::HasPlacement(*It) || DreamPropertyMeta::IsHidden(*It))
			{
				return true;
			}
		}
		return false;
	}

	void RegisterNewTaggedClasses()
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TArray<FName> ToRegister;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			if (Class->HasAnyClassFlags(CLASS_NewerVersionExists | CLASS_Deprecated) || !DeclaresTags(Class))
			{
				continue;
			}
			bool bCustomized = false;
			for (const UClass* Each = Class; Each != nullptr && !bCustomized; Each = Each->GetSuperClass())
			{
				bCustomized = PropertyEditor.GetClassNameToDetailLayoutNameMap().Contains(Each->GetFName());
			}
			if (!bCustomized)
			{
				ToRegister.AddUnique(Class->GetFName());
			}
		}
		for (const FName& Name : ToRegister)
		{
			PropertyEditor.RegisterCustomClassLayout(Name, FOnGetDetailCustomizationInstance::CreateLambda([]()
			{
				return MakeShared<FTaggedCustomization>(nullptr);
			}));
			TaggedRegistrations().Add(Name);
		}
	}
}

FOnGetDetailCustomizationInstance FDreamDetailsLayout::Wrap(FOnGetDetailCustomizationInstance InCustomization)
{
	return FOnGetDetailCustomizationInstance::CreateLambda([InCustomization]() -> TSharedRef<IDetailCustomization>
	{
		return MakeShared<DreamDetailsLayoutLocal::FTaggedCustomization>(InCustomization.IsBound() ? TSharedPtr<IDetailCustomization>(InCustomization.Execute()) : nullptr);
	});
}

void FDreamDetailsLayout::RegisterForTaggedClasses()
{
	DreamDetailsLayoutLocal::RegisterNewTaggedClasses();
	if (!DreamDetailsLayoutLocal::ModulesChangedHandle().IsValid())
	{
		DreamDetailsLayoutLocal::ModulesChangedHandle() = FModuleManager::Get().OnModulesChanged().AddLambda([](FName, EModuleChangeReason InReason)
		{
			if (InReason == EModuleChangeReason::ModuleLoaded && FModuleManager::Get().IsModuleLoaded(TEXT("PropertyEditor")))
			{
				DreamDetailsLayoutLocal::RegisterNewTaggedClasses();
			}
		});
	}
}

void FDreamDetailsLayout::UnregisterForTaggedClasses()
{
	if (DreamDetailsLayoutLocal::ModulesChangedHandle().IsValid())
	{
		FModuleManager::Get().OnModulesChanged().Remove(DreamDetailsLayoutLocal::ModulesChangedHandle());
		DreamDetailsLayoutLocal::ModulesChangedHandle().Reset();
	}
	if (FPropertyEditorModule* PropertyEditor = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
	{
		for (const FName& Name : DreamDetailsLayoutLocal::TaggedRegistrations())
		{
			PropertyEditor->UnregisterCustomClassLayout(Name);
		}
	}
	DreamDetailsLayoutLocal::TaggedRegistrations().Reset();
}
