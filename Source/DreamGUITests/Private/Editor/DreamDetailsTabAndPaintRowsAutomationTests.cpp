// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamText.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUITextData.h"
#include "Engine/World.h"
#include "IDetailTreeNode.h"
#include "IPropertyRowGenerator.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"

/*
 * Rows the details panels gained with Tab navigation and gradient text.
 *
 * The widget panel hides its own categories wholesale and re-adds rows one by one, so a property it does not re-add has
 * no UI anywhere: the three Tab properties are pinned to the Behavior category, under "Is Focusable". The text panel
 * gathers the paint fields -- half of them on the text style, half on the text -- into one group.
 */
namespace DreamDetailsTabAndPaintRowsTestLocal
{
	struct FScopedTestWorld
	{
		UWorld* World = nullptr;
		FScopedTestWorld() { World = UWorld::CreateWorld(EWorldType::Editor, false); }
		~FScopedTestWorld() { if (World) { World->DestroyWorld(false); } }
	};

	TSharedPtr<IPropertyRowGenerator> MakeRows(UObject* InObject)
	{
		FPropertyEditorModule& PropertyEditor = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		TSharedPtr<IPropertyRowGenerator> Generator = PropertyEditor.CreatePropertyRowGenerator(FPropertyRowGeneratorArgs());
		Generator->SetObjects({ InObject });
		return Generator;
	}

	FName GetRowName(const TSharedRef<IDetailTreeNode>& InNode)
	{
		const TSharedPtr<IPropertyHandle> Handle = InNode->CreatePropertyHandle();
		return Handle.IsValid() && Handle->GetProperty() != nullptr ? Handle->GetProperty()->GetFName() : InNode->GetNodeName();
	}

	/** The first node, depth first, named InName: a category, a group, or a property row by its property. */
	TSharedPtr<IDetailTreeNode> FindNode(const TSharedRef<IDetailTreeNode>& InNode, FName InName)
	{
		if (InNode->GetNodeName() == InName || GetRowName(InNode) == InName)
		{
			return InNode;
		}
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			if (TSharedPtr<IDetailTreeNode> Found = FindNode(Child, InName))
			{
				return Found;
			}
		}
		return nullptr;
	}

	TSharedPtr<IDetailTreeNode> FindNode(const TSharedPtr<IPropertyRowGenerator>& InRows, FName InName)
	{
		for (const TSharedRef<IDetailTreeNode>& Root : InRows->GetRootTreeNodes())
		{
			if (TSharedPtr<IDetailTreeNode> Found = FindNode(Root, InName))
			{
				return Found;
			}
		}
		return nullptr;
	}

	/** The names of InNode's own rows, in the order the panel shows them. */
	TArray<FName> GetChildRowNames(const TSharedRef<IDetailTreeNode>& InNode)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
		TArray<FName> Names;
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			Names.Add(GetRowName(Child));
		}
		return Names;
	}

	/** The names of every row under InNode, however deep. */
	void CollectRowNames(const TSharedRef<IDetailTreeNode>& InNode, TArray<FName>& OutNames)
	{
		TArray<TSharedRef<IDetailTreeNode>> Children;
		InNode->GetChildren(Children, /*bInIgnoreVisibility*/ true);
		for (const TSharedRef<IDetailTreeNode>& Child : Children)
		{
			OutNames.Add(GetRowName(Child));
			CollectRowNames(Child, OutNames);
		}
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamWidgetDetailsTabRowsTest,
	"DreamGUI.Editor.WidgetDetails.TheTabRowsSitInBehaviorUnderIsFocusable",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamWidgetDetailsTabRowsTest::RunTest(const FString& Parameters)
{
	using namespace DreamDetailsTabAndPaintRowsTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
	const TSharedPtr<IPropertyRowGenerator> Rows = MakeRows(Widget);

	const TSharedPtr<IDetailTreeNode> Behavior = FindNode(Rows, TEXT("DreamBehavior"));
	if (!TestTrue(TEXT("the widget's details customization made its Behavior category"), Behavior.IsValid()))
	{
		return false;
	}
	// By name rather than GET_MEMBER_NAME_CHECKED: they are protected members of UDreamWidget, which the panel reaches as a
	// friend and a test is not. Asked of the class first, so a rename fails here instead of asserting about nothing.
	for (const TCHAR* Expected : { TEXT("bIsFocusable"), TEXT("bIsTabStop"), TEXT("TabIndex"), TEXT("TabNavigation") })
	{
		TestNotNull(*FString::Printf(TEXT("UDreamWidget declares '%s'"), Expected), FindFProperty<FProperty>(UDreamWidget::StaticClass(), FName(Expected)));
	}
	const TArray<FName> Names = GetChildRowNames(Behavior.ToSharedRef());
	const int32 Focusable = Names.IndexOfByKey(FName(TEXT("bIsFocusable")));
	if (!TestTrue(TEXT("Is Focusable is in Behavior"), Focusable != INDEX_NONE))
	{
		return false;
	}
	TestEqual(TEXT("Is Tab Stop comes right under it"), Names.IndexOfByKey(FName(TEXT("bIsTabStop"))), Focusable + 1);
	TestEqual(TEXT("then the Tab index"), Names.IndexOfByKey(FName(TEXT("TabIndex"))), Focusable + 2);
	TestEqual(TEXT("then how its children take part in Tab order"), Names.IndexOfByKey(FName(TEXT("TabNavigation"))), Focusable + 3);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextDetailsPaintGroupTest,
	"DreamGUI.Editor.TextDetails.ThePaintFieldsOfTheStyleAndTheTextAreOneGroup",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextDetailsPaintGroupTest::RunTest(const FString& Parameters)
{
	using namespace DreamDetailsTabAndPaintRowsTestLocal;
	FScopedTestWorld TestWorld;
	UDreamWidget* Widget = NewObject<UDreamWidget>(TestWorld.World);
	UDreamText* Text = Widget->CreateNewVisual<UDreamText>();
	if (!TestNotNull(TEXT("the widget has a text"), Text))
	{
		return false;
	}
	const TSharedPtr<IPropertyRowGenerator> Rows = MakeRows(Text);

	const TSharedPtr<IDetailTreeNode> PaintGroup = FindNode(Rows, TEXT("TextPaint"));
	if (!TestTrue(TEXT("the text's details have a paint group"), PaintGroup.IsValid()))
	{
		return false;
	}
	const TArray<FName> Names = GetChildRowNames(PaintGroup.ToSharedRef());
	for (const FName Expected : {
		GET_MEMBER_NAME_CHECKED(FDreamTextStyle, FacePaint), GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OutlinePaint),
		GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OverlayPaint), GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OverlayBlend),
		GET_MEMBER_NAME_CHECKED(FDreamTextStyle, PaintBoxHorizontal), GET_MEMBER_NAME_CHECKED(FDreamTextStyle, PaintBoxVertical),
		FName(TEXT("FacePaintPhase")), FName(TEXT("OutlinePaintPhase")), FName(TEXT("OverlayPaintPhase")), FName(TEXT("PaintAngleOffset")) })
	{
		TestTrue(*FString::Printf(TEXT("'%s' is in the paint group"), *Expected.ToString()), Names.Contains(Expected));
	}

	// Moved, not copied: the style's own rows no longer list them.
	const TSharedPtr<IDetailTreeNode> Style = FindNode(Rows, TEXT("TextStyle"));
	if (TestTrue(TEXT("the text style still has its row"), Style.IsValid()))
	{
		TArray<FName> StyleNames;
		CollectRowNames(Style.ToSharedRef(), StyleNames);
		TestFalse(TEXT("the face paint is not shown twice"), StyleNames.Contains(GET_MEMBER_NAME_CHECKED(FDreamTextStyle, FacePaint)));
		TestTrue(TEXT("the style's other fields stay with it"), StyleNames.Contains(GET_MEMBER_NAME_CHECKED(FDreamTextStyle, OutlineColor)));
	}
	return true;
}

#endif
