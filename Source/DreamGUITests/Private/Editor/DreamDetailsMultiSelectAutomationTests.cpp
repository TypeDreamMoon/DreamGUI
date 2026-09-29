// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "DetailCustomization/DreamDetailsMultiSelect.h"
#include "DreamWidgetBlueprintTestTypes.h"

#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

/*
 * Reading a property across a selection that does not agree.
 *
 * IPropertyHandle::GetValue leaves its out-param UNTOUCHED when the objects hold different values.
 * Twenty-one customization sites declared a bare local and read it anyway, so the stack decided
 * which rows the panel built and which box drew checked -- and in two of them, what got written back
 * into the asset.
 *
 * The reason none of it was ever reported is the reason this file exists: with ONE object selected
 * the read always succeeds, so every assertion anyone would think to make passes. These tests are
 * built on a genuinely multi-object handle, which is the only shape that can fail.
 */
namespace DreamDetailsMultiSelectTestLocal
{
	/** A real handle over several objects -- the row generator is the headless way to get one. */
	struct FScopedHandle
	{
		TSharedPtr<IPropertyRowGenerator> Generator;
		TSharedPtr<IPropertyHandle> Handle;

		FScopedHandle(const TArray<UObject*>& InObjects, FName InPropertyName)
		{
			FPropertyEditorModule& PropertyEditor =
				FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
			FPropertyRowGeneratorArgs Args;
			Generator = PropertyEditor.CreatePropertyRowGenerator(Args);
			Generator->SetObjects(InObjects);
			for (const TSharedRef<IDetailTreeNode>& Category : Generator->GetRootTreeNodes())
			{
				if (Find(Category, InPropertyName))
				{
					return;
				}
			}
		}

		bool Find(const TSharedRef<IDetailTreeNode>& InNode, FName InPropertyName)
		{
			if (TSharedPtr<IPropertyHandle> Candidate = InNode->CreatePropertyHandle())
			{
				if (Candidate->GetProperty() != nullptr && Candidate->GetProperty()->GetFName() == InPropertyName)
				{
					Handle = Candidate;
					return true;
				}
			}
			TArray<TSharedRef<IDetailTreeNode>> Children;
			InNode->GetChildren(Children);
			for (const TSharedRef<IDetailTreeNode>& Child : Children)
			{
				if (Find(Child, InPropertyName))
				{
					return true;
				}
			}
			return false;
		}
	};

	UDreamDetailsMultiSelectTestObject* MakeObject(bool bFlag)
	{
		UDreamDetailsMultiSelectTestObject* Object =
			NewObject<UDreamDetailsMultiSelectTestObject>(GetTransientPackage(), NAME_None, RF_Transient);
		Object->bFlag = bFlag;
		return Object;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDetailsMultiSelectAgreeingTest,
	"DreamGUI.DetailsPanel.AnAgreeingSelectionReadsAsItsSharedValue",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDetailsMultiSelectAgreeingTest::RunTest(const FString&)
{
	using namespace DreamDetailsMultiSelectTestLocal;
	using namespace DreamDetailsMultiSelect;

	UObject* First = MakeObject(true);
	UObject* Second = MakeObject(true);
	FScopedHandle Scoped({ First, Second }, FName(TEXT("bFlag")));
	if (!TestTrue(TEXT("A handle over both objects"), Scoped.Handle.IsValid()))
	{
		return false;
	}

	TestTrue(TEXT("ValueOr gives the shared value, not the fallback"), ValueOr(Scoped.Handle, false));
	TestTrue(TEXT("CheckedIfEqual is Checked"),
		CheckedIfEqual(Scoped.Handle, true) == ECheckBoxState::Checked);
	TestTrue(TEXT("CheckedIfEqual is Unchecked for the other value"),
		CheckedIfEqual(Scoped.Handle, false) == ECheckBoxState::Unchecked);
	TestTrue(TEXT("AllEqual holds"), AllEqual(Scoped.Handle, true));
	TestFalse(TEXT("And not for the other value"), AllEqual(Scoped.Handle, false));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamDetailsMultiSelectDisagreeingTest,
	"DreamGUI.DetailsPanel.ADisagreeingSelectionReadsDeterministically",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamDetailsMultiSelectDisagreeingTest::RunTest(const FString&)
{
	using namespace DreamDetailsMultiSelectTestLocal;
	using namespace DreamDetailsMultiSelect;

	// The shape the whole defect lived in, and the only one that can catch it.
	UObject* First = MakeObject(true);
	UObject* Second = MakeObject(false);
	FScopedHandle Scoped({ First, Second }, FName(TEXT("bFlag")));
	if (!TestTrue(TEXT("A handle over both objects"), Scoped.Handle.IsValid()))
	{
		return false;
	}
	// If this fails the rest proves nothing: the objects agree after all, and every read below would
	// succeed for the ordinary reason.
	bool Ignored = false;
	if (!TestTrue(TEXT("The selection really does disagree"),
		Scoped.Handle->GetValue(Ignored) == FPropertyAccess::MultipleValues))
	{
		return false;
	}

	// The fallback, every time. Before this it was whatever the caller's stack happened to hold, so
	// the SAME panel could come up either way on two consecutive openings.
	TestFalse(TEXT("ValueOr gives the fallback"), ValueOr(Scoped.Handle, false));
	TestTrue(TEXT("And gives the other fallback when asked for it"), ValueOr(Scoped.Handle, true));

	TestTrue(TEXT("A box over a disagreeing selection is Undetermined"),
		CheckedIfEqual(Scoped.Handle, true) == ECheckBoxState::Undetermined);
	TestTrue(TEXT("Whichever value it is asked about"),
		CheckedIfEqual(Scoped.Handle, false) == ECheckBoxState::Undetermined);

	// Both false, and that is the point: "hide this row" and "write this across" may only act on
	// unanimity, so disagreement has to be unanimous with nothing.
	TestFalse(TEXT("AllEqual is false for one value"), AllEqual(Scoped.Handle, true));
	TestFalse(TEXT("And false for the other"), AllEqual(Scoped.Handle, false));

	return true;
}

#endif
