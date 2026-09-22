// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Controls/DreamUIControl.h"
#include "Core/Components/DreamPanelLayouts.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/DreamUIWidgetRegistry.h"

#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/Class.h"
#include "UObject/UObjectIterator.h"
#include "UObject/UnrealType.h"

/*
 * Two guards against the control library drifting away from the one it is measured against.
 *
 * Somebody arriving from UMG reaches for a name -- ScrollWidgetIntoView's destination, a spin box's
 * MinSliderValue -- and what they meet here is one of three things: the same name, a different name,
 * or a decision that the concept has no place in this framework. All three are fine. What is not fine
 * is the fourth: nobody ever looked. A gap of that kind is invisible from inside the codebase, because
 * nothing here references the member that is missing.
 *
 * So the looking is written down. Resources/UMGParity holds one table per UMG class, a row per
 * reflected member, each row saying where the member went or why it went nowhere. The first guard reads
 * UMG's classes through reflection and holds the tables to them: a member the engine gains in an
 * upgrade turns up here as a row that does not exist, and a row pointing at something this plugin has
 * since renamed turns up as a name that does not resolve.
 *
 * The second guard is about the other half of a knob. A property the details panel can edit is only
 * half a feature if a Blueprint cannot change it at runtime -- and "can change it" means through a
 * setter, because a control's fields are read once, when it assembles itself, and a raw write to one
 * changes a number nothing will look at again.
 */
namespace DreamUMGParityGuardLocal
{
	/** Where the tables live. Empty when the plugin cannot be found, which the callers report. */
	FString GetTableDirectory()
	{
		const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("DreamGUI"));
		return Plugin.IsValid() ? FPaths::Combine(Plugin->GetBaseDir(), TEXT("Resources"), TEXT("UMGParity")) : FString();
	}

	/** A file whose name starts with an underscore is data FOR the guards rather than a table. */
	bool IsTableFile(const FString& InFilename)
	{
		return !FPaths::GetCleanFilename(InFilename).StartsWith(TEXT("_"));
	}

	TSharedPtr<FJsonObject> LoadJson(const FString& InPath, FString& OutError)
	{
		FString Text;
		if (!FFileHelper::LoadFileToString(Text, *InPath))
		{
			OutError = FString::Printf(TEXT("could not read %s"), *InPath);
			return nullptr;
		}
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Text);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutError = FString::Printf(TEXT("%s is not valid JSON: %s"), *InPath, *Reader->GetErrorMessage());
			return nullptr;
		}
		return Root;
	}

	/** "UScrollBox" -> "ScrollBox", "FDreamListStyle" -> "DreamListStyle": the name reflection knows. */
	FString StripTypePrefix(const FString& InName)
	{
		if (InName.Len() > 1 && FChar::IsUpper(InName[1])
			&& (InName[0] == TEXT('U') || InName[0] == TEXT('A') || InName[0] == TEXT('F')))
		{
			return InName.RightChop(1);
		}
		return InName;
	}

	/**
	 * A class or a struct, by its C++ name.
	 *
	 * The script packages are tried by path first because a bare-name search is a search among
	 * EVERYTHING: a UFunction is a UStruct too, and "Button" names more than one object in a running
	 * editor.
	 */
	UStruct* FindReflectedType(const FString& InCppName)
	{
		const FString Name = StripTypePrefix(InCppName);
		for (const TCHAR* Package : { TEXT("/Script/DreamGUI"), TEXT("/Script/UMG"), TEXT("/Script/DreamTween"), TEXT("/Script/DreamGUIEditor") })
		{
			const FString Path = FString::Printf(TEXT("%s.%s"), Package, *Name);
			if (UClass* Class = FindObject<UClass>(nullptr, *Path))
			{
				return Class;
			}
			if (UScriptStruct* Struct = FindObject<UScriptStruct>(nullptr, *Path))
			{
				return Struct;
			}
		}
		if (UClass* Class = FindFirstObject<UClass>(*Name, EFindFirstObjectOptions::NativeFirst))
		{
			return Class;
		}
		return FindFirstObject<UScriptStruct>(*Name, EFindFirstObjectOptions::NativeFirst);
	}

	/** A property, a function or a delegate of that name, the owner's supers included. */
	bool HasMember(const UStruct* InOwner, const FString& InMember)
	{
		if (InOwner == nullptr || InMember.IsEmpty())
		{
			return false;
		}
		const FName MemberName(*InMember);
		if (InOwner->FindPropertyByName(MemberName) != nullptr)
		{
			return true;
		}
		const UClass* Class = Cast<UClass>(InOwner);
		return Class != nullptr && Class->FindFunctionByName(MemberName) != nullptr;
	}

	/**
	 * What a UMG class itself offers a Blueprint author: the properties a panel or a graph can see, the
	 * events it can bind, the functions it can call. Inherited members belong to the table of the class
	 * that declares them, which is what keeps UWidget's seventy-odd from being argued about sixty times.
	 */
	void CollectOwnBlueprintSurface(const UClass* InClass, TSet<FString>& OutMembers)
	{
		for (TFieldIterator<FProperty> It(InClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			const FString Name = Property->GetName();
			if (Property->HasAnyPropertyFlags(CPF_Deprecated)
				|| Name.EndsWith(TEXT("_DEPRECATED"))
				|| Name.StartsWith(TEXT("bOverride_")))
			{
				continue;
			}
			if (Property->HasAnyPropertyFlags(CPF_Edit | CPF_BlueprintVisible | CPF_BlueprintAssignable))
			{
				OutMembers.Add(Name);
			}
		}
		for (TFieldIterator<UFunction> It(InClass, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const UFunction* Function = *It;
			if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure)
				&& !Function->HasAnyFunctionFlags(FUNC_Delegate)
				&& !Function->HasMetaData(TEXT("DeprecatedFunction")))
			{
				OutMembers.Add(Function->GetName());
			}
		}
	}

	/** "Member", or "UOwner::Member" / "FOwner::Member" for something that lives elsewhere. */
	bool ResolveDestination(const FString& InDestination, const UStruct* InDefaultOwner, FString& OutWhyNot)
	{
		FString OwnerName;
		FString MemberName;
		const UStruct* Owner = InDefaultOwner;
		if (InDestination.Split(TEXT("::"), &OwnerName, &MemberName))
		{
			Owner = FindReflectedType(OwnerName);
			if (Owner == nullptr)
			{
				OutWhyNot = FString::Printf(TEXT("there is no reflected type called %s"), *OwnerName);
				return false;
			}
		}
		else
		{
			MemberName = InDestination;
		}
		if (!HasMember(Owner, MemberName))
		{
			OutWhyNot = FString::Printf(TEXT("%s has no reflected member called %s"),
				Owner != nullptr ? *Owner->GetName() : TEXT("(no class)"), *MemberName);
			return false;
		}
		return true;
	}

	/** bShowScrollBar is set by SetShowScrollBar; UMG writes it that way and so does everything here. */
	FString GetConventionalSetterName(const FProperty* InProperty)
	{
		FString Name = InProperty->GetName();
		if (InProperty->IsA<FBoolProperty>() && Name.Len() > 1 && Name[0] == TEXT('b') && FChar::IsUpper(Name[1]))
		{
			Name.RightChopInline(1);
		}
		return TEXT("Set") + Name;
	}

	/** The classes whose panels a Blueprint author actually fills in: controls, panels, the slot. */
	void CollectAuthoredClasses(TArray<UClass*>& OutClasses)
	{
		TArray<FDreamUIWidgetRegistry::FEntry> Entries;
		FDreamUIWidgetRegistry::GetAllEntries(Entries);
		for (const FDreamUIWidgetRegistry::FEntry& Entry : Entries)
		{
			// The registry only knows the classes an author can place, and the walk below reads each
			// class's OWN properties -- so the abstract bases in between are added by hand, or the
			// knobs declared on them would be nobody's to check.
			UClass* Class = Entry.ClassGetter != nullptr ? Entry.ClassGetter() : nullptr;
			for (; Class != nullptr && Class->IsChildOf(UDreamUIControl::StaticClass()); Class = Class->GetSuperClass())
			{
				OutClasses.AddUnique(Class);
			}
		}
		for (TObjectIterator<UClass> It; It; ++It)
		{
			UClass* Class = *It;
			if (Class->HasAnyClassFlags(CLASS_Native)
				&& !Class->HasAnyClassFlags(CLASS_Deprecated | CLASS_NewerVersionExists)
				&& (Class->IsChildOf(UDreamPanelLayoutBase::StaticClass()) || Class->IsChildOf(UDreamPanelSlot::StaticClass())))
			{
				OutClasses.AddUnique(Class);
			}
		}
		OutClasses.Sort([](const UClass& A, const UClass& B) { return A.GetName() < B.GetName(); });
	}

	/** Whose property this is, for the question "is it one of ours to hold to the rule". */
	bool IsDeclaredOnAnAuthoredType(const FProperty* InProperty)
	{
		const UClass* Owner = InProperty->GetOwnerClass();
		return Owner != nullptr
			&& (Owner->IsChildOf(UDreamUIControl::StaticClass())
				|| Owner->IsChildOf(UDreamPanelLayoutBase::StaticClass())
				|| Owner->IsChildOf(UDreamPanelSlot::StaticClass()));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamUMGParityTablesArePresentTest,
	"DreamGUI.UMGParity.TheTablesShipWithThePlugin",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamUMGParityTablesArePresentTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGParityGuardLocal;

	// The per-table test enumerates the directory; an empty or misplaced one would make it pass by
	// running nothing, which is the one way a guard like this can fail without anyone noticing.
	const FString Directory = GetTableDirectory();
	if (!TestFalse(TEXT("the DreamGUI plugin can be found"), Directory.IsEmpty()))
	{
		return false;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	Files.RemoveAll([](const FString& File) { return !IsTableFile(File); });
	TestTrue(FString::Printf(TEXT("%s holds the parity tables (found %d)"), *Directory, Files.Num()), Files.Num() >= 50);
	return true;
}

IMPLEMENT_COMPLEX_AUTOMATION_TEST(
	FDreamUMGParityTableTest,
	"DreamGUI.UMGParity.EveryUMGMemberHasSomewhereToGo",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamUMGParityTableTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	using namespace DreamUMGParityGuardLocal;

	const FString Directory = GetTableDirectory();
	if (Directory.IsEmpty())
	{
		return;
	}
	TArray<FString> Files;
	IFileManager::Get().FindFiles(Files, *FPaths::Combine(Directory, TEXT("*.json")), true, false);
	Files.Sort();
	for (const FString& File : Files)
	{
		if (IsTableFile(File))
		{
			OutBeautifiedNames.Add(FPaths::GetBaseFilename(File));
			OutTestCommands.Add(FPaths::Combine(Directory, File));
		}
	}
}

bool FDreamUMGParityTableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGParityGuardLocal;

	FString LoadError;
	const TSharedPtr<FJsonObject> Root = LoadJson(Parameters, LoadError);
	if (!Root.IsValid())
	{
		AddError(LoadError);
		return false;
	}

	const FString UMGClassName = Root->GetStringField(TEXT("umgClass"));
	const FString DreamClassName = Root->GetStringField(TEXT("dreamClass"));
	const UClass* UMGClass = Cast<UClass>(FindReflectedType(UMGClassName));
	const UStruct* DreamType = FindReflectedType(DreamClassName);
	if (!TestNotNull(*FString::Printf(TEXT("the UMG class %s exists"), *UMGClassName), UMGClass)
		|| !TestNotNull(*FString::Printf(TEXT("the DreamGUI type %s exists"), *DreamClassName), DreamType))
	{
		return false;
	}

	const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
	if (!Root->TryGetArrayField(TEXT("members"), Rows) || Rows == nullptr)
	{
		AddError(TEXT("the table has no \"members\" array"));
		return false;
	}

	TSet<FString> Listed;
	TArray<FString> Undecided;
	for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
	{
		const TSharedPtr<FJsonObject>* RowObject = nullptr;
		if (!RowValue.IsValid() || !RowValue->TryGetObject(RowObject) || RowObject == nullptr)
		{
			AddError(TEXT("a row is not an object"));
			continue;
		}
		const FJsonObject& Row = **RowObject;
		const FString Member = Row.GetStringField(TEXT("umg"));
		const FString Status = Row.GetStringField(TEXT("status"));
		FString Destination;
		Row.TryGetStringField(TEXT("dream"), Destination);
		FString Note;
		Row.TryGetStringField(TEXT("note"), Note);
		Listed.Add(Member);

		if (Status == TEXT("adopt") || Status == TEXT("map"))
		{
			if (Status == TEXT("map") && Destination.IsEmpty())
			{
				AddError(FString::Printf(TEXT("%s is mapped, but the row does not say to what"), *Member));
				continue;
			}
			FString WhyNot;
			if (!ResolveDestination(Destination.IsEmpty() ? Member : Destination, DreamType, WhyNot))
			{
				AddError(FString::Printf(TEXT("%s -> %s: %s"), *Member, Destination.IsEmpty() ? *Member : *Destination, *WhyNot));
			}
		}
		else if (Status == TEXT("reject"))
		{
			// A refusal is a design decision, and one that cannot be read is one nobody can disagree with.
			if (Note.TrimStartAndEnd().Len() < 12)
			{
				AddError(FString::Printf(TEXT("%s is rejected without a reason somebody could argue with"), *Member));
			}
		}
		else
		{
			Undecided.Add(Note.IsEmpty() ? Member : FString::Printf(TEXT("%s (%s)"), *Member, *Note));
		}
	}

	if (Undecided.Num() > 0)
	{
		AddError(FString::Printf(TEXT("%d member(s) of %s still have no destination: %s"),
			Undecided.Num(), *UMGClassName, *FString::Join(Undecided, TEXT(", "))));
	}

	// The other direction: what reflection says UMG offers, against what the table thought to list.
	// This is the half that notices an engine upgrade.
	TSet<FString> Surface;
	CollectOwnBlueprintSurface(UMGClass, Surface);
	TArray<FString> Unlisted;
	for (const FString& Member : Surface)
	{
		if (!Listed.Contains(Member))
		{
			Unlisted.Add(Member);
		}
	}
	if (Unlisted.Num() > 0)
	{
		Unlisted.Sort();
		AddError(FString::Printf(TEXT("%s offers %d member(s) the table does not mention: %s"),
			*UMGClassName, Unlisted.Num(), *FString::Join(Unlisted, TEXT(", "))));
	}
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamAuthoredKnobsAreBlueprintWritableTest,
	"DreamGUI.UMGParity.AKnobThePanelCanTurnIsOneABlueprintCanTurn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamAuthoredKnobsAreBlueprintWritableTest::RunTest(const FString& Parameters)
{
	using namespace DreamUMGParityGuardLocal;

	// The knobs that are deliberately construction-time only, each with the reason. Data rather than
	// code so that the list can be argued with -- and shrunk -- without a rebuild.
	TMap<FString, FString> Exceptions;
	{
		const FString Path = FPaths::Combine(GetTableDirectory(), TEXT("_BlueprintWritableExceptions.json"));
		FString LoadError;
		const TSharedPtr<FJsonObject> Root = LoadJson(Path, LoadError);
		if (!Root.IsValid())
		{
			AddError(LoadError);
			return false;
		}
		const TArray<TSharedPtr<FJsonValue>>* Rows = nullptr;
		if (Root->TryGetArrayField(TEXT("exceptions"), Rows) && Rows != nullptr)
		{
			for (const TSharedPtr<FJsonValue>& RowValue : *Rows)
			{
				const TSharedPtr<FJsonObject>* RowObject = nullptr;
				if (RowValue.IsValid() && RowValue->TryGetObject(RowObject) && RowObject != nullptr)
				{
					const FString Key = StripTypePrefix((*RowObject)->GetStringField(TEXT("class")))
						+ TEXT(".") + (*RowObject)->GetStringField(TEXT("property"));
					const FString Reason = (*RowObject)->GetStringField(TEXT("reason"));
					TestTrue(FString::Printf(TEXT("the exception for %s says why"), *Key), Reason.TrimStartAndEnd().Len() >= 12);
					Exceptions.Add(Key, Reason);
				}
			}
		}
	}

	TArray<UClass*> Classes;
	CollectAuthoredClasses(Classes);
	TestTrue(TEXT("there are authored classes to hold to the rule"), Classes.Num() > 20);

	TSet<FString> ExceptionsUsed;
	for (const UClass* Class : Classes)
	{
		TArray<FString> NoSetter;
		TArray<FString> RawWrite;
		for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::ExcludeSuper); It; ++It)
		{
			const FProperty* Property = *It;
			if (!IsDeclaredOnAnAuthoredType(Property)
				|| !Property->HasAnyPropertyFlags(CPF_Edit)
				|| Property->HasAnyPropertyFlags(CPF_EditConst | CPF_DisableEditOnInstance | CPF_EditorOnly | CPF_Deprecated | CPF_Transient)
				|| Property->GetName().StartsWith(TEXT("bOverride_")))
			{
				continue;
			}

			const FString Key = Class->GetName() + TEXT(".") + Property->GetName();
			const FString DeclaredSetter = Property->GetMetaData(TEXT("BlueprintSetter"));
			const bool bHasDeclaredSetter = !DeclaredSetter.IsEmpty() && Class->FindFunctionByName(*DeclaredSetter) != nullptr;
			const UFunction* Conventional = Class->FindFunctionByName(*GetConventionalSetterName(Property));
			const bool bHasConventionalSetter = Conventional != nullptr && Conventional->HasAnyFunctionFlags(FUNC_BlueprintCallable);

			const bool bBroken = !bHasDeclaredSetter && !bHasConventionalSetter;
			// A BlueprintReadWrite property with no BlueprintSetter gives the graph a Set node that
			// writes the field and tells nobody. That is worse than no node: it looks like it worked.
			const bool bRaw = !bHasDeclaredSetter
				&& Property->HasAnyPropertyFlags(CPF_BlueprintVisible)
				&& !Property->HasAnyPropertyFlags(CPF_BlueprintReadOnly);

			if (!bBroken && !bRaw)
			{
				if (Exceptions.Contains(Key))
				{
					AddError(FString::Printf(TEXT("%s is listed as an exception but now has a setter: drop the exception"), *Key));
				}
				continue;
			}
			if (Exceptions.Contains(Key))
			{
				ExceptionsUsed.Add(Key);
				continue;
			}
			(bBroken ? NoSetter : RawWrite).Add(Property->GetName());
		}
		if (NoSetter.Num() > 0)
		{
			AddError(FString::Printf(TEXT("%s: the panel can edit these and a Blueprint cannot set them: %s"),
				*Class->GetName(), *FString::Join(NoSetter, TEXT(", "))));
		}
		if (RawWrite.Num() > 0)
		{
			AddError(FString::Printf(TEXT("%s: a Blueprint Set node writes these without telling the control (BlueprintReadWrite, no BlueprintSetter): %s"),
				*Class->GetName(), *FString::Join(RawWrite, TEXT(", "))));
		}
	}

	for (const TPair<FString, FString>& Exception : Exceptions)
	{
		if (!ExceptionsUsed.Contains(Exception.Key))
		{
			// Either it gained a setter (reported above) or it names nothing that exists any more.
			FString ClassName;
			FString PropertyName;
			Exception.Key.Split(TEXT("."), &ClassName, &PropertyName);
			const UStruct* Owner = FindReflectedType(ClassName);
			if (Owner == nullptr || Owner->FindPropertyByName(*PropertyName) == nullptr)
			{
				AddError(FString::Printf(TEXT("the exception for %s names a property that does not exist"), *Exception.Key));
			}
		}
	}
	return true;
}

#endif
