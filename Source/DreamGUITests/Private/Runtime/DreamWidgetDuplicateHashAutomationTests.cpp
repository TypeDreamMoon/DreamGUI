// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetGeneratedClass.h"
#include "Core/DreamWidgetTree.h"
#include "Core/Components/DreamWidget.h"
#include "DreamScopedWorld.h"
#include "DreamWidgetDuplicateHashTestTypes.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_COMPLEX_AUTOMATION_TEST(FDreamWidgetDuplicateNestedHashKeysTest,
	"DreamGUI.WidgetTree.DuplicateSubtree.KeepsNestedHashKeysSearchableWithoutSkippingWritableReferences",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

void FDreamWidgetDuplicateNestedHashKeysTest::GetTests(TArray<FString>& OutBeautifiedNames, TArray<FString>& OutTestCommands) const
{
	OutBeautifiedNames.Add(TEXT("Map struct keys"));
	OutTestCommands.Add(TEXT("Map"));
	OutBeautifiedNames.Add(TEXT("Set struct elements"));
	OutTestCommands.Add(TEXT("Set"));
}

bool FDreamWidgetDuplicateNestedHashKeysTest::RunTest(const FString& Parameters)
{
	const bool bMap = Parameters == TEXT("Map");
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamWidgetTree> SourceTree(NewObject<UDreamWidgetTree>(GetTransientPackage()));
	UDreamWidget* Root = SourceTree->ConstructWidget<UDreamWidget>();
	Root->SetDisplayName(TEXT("Root"));
	SourceTree->RootWidget = Root;
	UDreamWidget* SourceValue = SourceTree->ConstructWidget<UDreamWidget>();
	SourceValue->SetDisplayName(TEXT("Value"));
	SourceValue->SetParentBeforeRegister(Root);
	UDreamWidgetDuplicateHashTestBehaviour* Source = Root->AddComponent<UDreamWidgetDuplicateHashTestBehaviour>();
	if (!TestNotNull(TEXT("the source owns an ordinary behaviour"), Source)) return false;
	TArray<FDreamWidgetDuplicateHashTestKey> SourceKeys;
	for (int32 Index = 0; Index < 16; ++Index)
	{
		UDreamWidget* KeyWidget = SourceTree->ConstructWidget<UDreamWidget>();
		KeyWidget->SetDisplayName(FString::Printf(TEXT("Key%d"), Index));
		KeyWidget->SetParentBeforeRegister(Root);
		FDreamWidgetDuplicateHashTestKey Key;
		Key.Widget = KeyWidget;
		SourceKeys.Add(Key);
		Source->DirectMap.Add(KeyWidget, SourceValue);
		Source->StructMap.Add(Key, SourceValue);
		Source->DirectSet.Add(KeyWidget);
		Source->StructSet.Add(Key);
	}
	Source->OrdinaryStruct.Widget = SourceValue;
	Source->FollowingReference = SourceValue;

	// Public duplication leaves the copy detached and lets callers attach it. The existing copying
	// policy preserves map/set key identities (including direct widget keys), while plain values
	// point into the new subtree. Wrapping that same key in a struct must not change this policy.
	TStrongObjectPtr<UDreamWidgetTree> CopyTree(NewObject<UDreamWidgetTree>(TestWorld.World));
	CopyTree->RootWidget = UDreamWidget::DuplicateSubtree(CopyTree.Get(), Root);
	UDreamWidget* CopyRoot = CopyTree->RootWidget;
	if (!TestNotNull(TEXT("the public subtree duplication produced a root"), CopyRoot)) return false;
	UDreamWidgetDuplicateHashTestBehaviour* Copy = CopyRoot->GetComponent<UDreamWidgetDuplicateHashTestBehaviour>();
	UDreamWidget* CopyValue = CopyTree->FindWidgetByVariableName(TEXT("Value"));
	if (!TestNotNull(TEXT("the subtree owns a copied behaviour"), Copy)
		|| !TestNotNull(TEXT("the referenced value has its own counterpart"), CopyValue)) return false;
	TestNotEqual(TEXT("the value was actually copied"), CopyValue, SourceValue);

	// The generated-class path independently defines the same key policy. This control rules out
	// expecting a different semantic for DuplicateSubtree merely because the key is a struct.
	TStrongObjectPtr<UDreamUserWidget> Instance(NewObject<UDreamUserWidget>(TestWorld.World));
	UDreamWidgetGeneratedClass::InitializeWidgetStatic(Instance.Get(), Instance->GetClass(), SourceTree.Get());
	UDreamWidgetTree* InstanceTree = Instance->GetWidgetTree();
	UDreamWidget* InstanceRoot = InstanceTree != nullptr ? InstanceTree->FindWidgetByVariableName(TEXT("Root")) : nullptr;
	UDreamWidgetDuplicateHashTestBehaviour* Instanced = InstanceRoot != nullptr
		? InstanceRoot->GetComponent<UDreamWidgetDuplicateHashTestBehaviour>() : nullptr;
	if (!TestNotNull(TEXT("normal user-widget instancing produced the control behaviour"), Instanced)) return false;

	for (const FDreamWidgetDuplicateHashTestKey& Key : SourceKeys)
	{
		const FString Name = Key.Widget->GetDisplayName();
		if (bMap)
		{
			TestTrue(Name + TEXT(": an unwrapped copied key retains its authored identity"), Copy->DirectMap.Contains(Key.Widget));
			TestTrue(Name + TEXT(": normal instancing preserves the wrapped key"), Instanced->StructMap.Contains(Key));
			const TObjectPtr<UDreamWidget>* Found = Copy->StructMap.Find(Key);
			TestNotNull(Name + TEXT(": the wrapped copied key remains searchable by its authored identity"), Found);
			if (Found != nullptr) TestEqual(Name + TEXT(": its map value is the copy's widget"), Found->Get(), CopyValue);
		}
		else
		{
			TestTrue(Name + TEXT(": an unwrapped copied element retains its authored identity"), Copy->DirectSet.Contains(Key.Widget));
			TestTrue(Name + TEXT(": normal instancing preserves the wrapped element"), Instanced->StructSet.Contains(Key));
			TestTrue(Name + TEXT(": the wrapped copied element remains searchable by its authored identity"), Copy->StructSet.Contains(Key));
		}
	}
	if (bMap)
	{
		TestEqual(TEXT("the copied map retains every entry"), Copy->StructMap.Num(), SourceKeys.Num());
		for (const TPair<FDreamWidgetDuplicateHashTestKey, TObjectPtr<UDreamWidget>>& Entry : Copy->StructMap)
		{
			TestTrue(TEXT("a copied map entry can be found using its own key"), Copy->StructMap.Contains(Entry.Key));
			TestEqual(TEXT("each copied map value was independently retargeted"), Entry.Value.Get(), CopyValue);
		}
	}
	else
	{
		TestEqual(TEXT("the copied set retains every element"), Copy->StructSet.Num(), SourceKeys.Num());
		for (const FDreamWidgetDuplicateHashTestKey& Key : Copy->StructSet)
		{
			TestTrue(TEXT("an iterated copied set element remains findable"), Copy->StructSet.Contains(Key));
		}
	}
	TestEqual(TEXT("a writable struct next to the hash containers retargets normally"), Copy->OrdinaryStruct.Widget.Get(), CopyValue);
	TestEqual(TEXT("a following plain reference retargets normally"), Copy->FollowingReference.Get(), CopyValue);
	for (const FDreamWidgetDuplicateHashTestKey& Key : SourceKeys)
	{
		TestTrue(TEXT("the source's map remains searchable"), Source->StructMap.Contains(Key));
		TestTrue(TEXT("the source's set remains searchable"), Source->StructSet.Contains(Key));
	}
	TestEqual(TEXT("the source's writable reference is unchanged"), Source->FollowingReference.Get(), SourceValue);
	return true;
}

#endif
