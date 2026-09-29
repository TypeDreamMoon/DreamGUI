// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "CoreMinimal.h"
#include "Misc/AutomationTest.h"

#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIDataAsTexture.h"
#include "Core/DreamUIManager.h"
#include "Engine/World.h"
#include "UObject/UnrealType.h"

#include "Lifecycle/DreamLifecycleFixtures.h"

/*
 * A rect block keeps its shape -- corners, border, gradients, shadows -- in a row of a data texture its material
 * reads. The rows used to live in the rect block data asset from the project settings, which every world shares: the
 * editor's world, each play session's copy of it, every preview. A rect block in one world grew the texture the others
 * drew with, and rows a play session never gave back were still taken in the next one. Each world's UI manager keeps
 * its own now, one set for each rect block data asset its rect blocks use.
 */
namespace DreamRectBlockRowsTestLocal
{
	UDreamRectBlock* MakeRectBlock(UWorld* InWorld)
	{
		UDreamWidget* Widget = NewObject<UDreamWidget>(InWorld, NAME_None, RF_Transient);
		Widget->SetWidth(100.0f);
		Widget->SetHeight(100.0f);
		Widget->OnRegister();
		return Widget->CreateNewVisual<UDreamRectBlock>();
	}

	/** One of a rect block's object properties, by name: the two here are not the rect block's to hand out. */
	UObject* PropertyOf(const UDreamRectBlock* InBlock, const TCHAR* InName)
	{
		const FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(UDreamRectBlock::StaticClass(), InName);
		return Property != nullptr && InBlock != nullptr ? Property->GetObjectPropertyValue_InContainer(InBlock) : nullptr;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamRectBlockRowsPerWorldTest,
	"DreamGUI.RectBlock.EachWorldKeepsItsRectBlocksShapesInRowsOfItsOwn",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRectBlockRowsPerWorldTest::RunTest(const FString& Parameters)
{
	using namespace DreamRectBlockRowsTestLocal;
	DreamTests::Lifecycle::FScopedWorld First(EWorldType::Game);
	DreamTests::Lifecycle::FScopedWorld Second(EWorldType::Game);
	if (!TestNotNull(TEXT("A first world"), First.World) || !TestNotNull(TEXT("A second world"), Second.World))
	{
		return false;
	}
	UDreamRectBlock* FirstBlock = MakeRectBlock(First.World);
	UDreamRectBlock* FirstBlockAgain = MakeRectBlock(First.World);
	UDreamRectBlock* SecondBlock = MakeRectBlock(Second.World);
	if (!TestNotNull(TEXT("A rect block in the first world"), FirstBlock)
		|| !TestNotNull(TEXT("...and another"), FirstBlockAgain)
		|| !TestNotNull(TEXT("A rect block in the second world"), SecondBlock))
	{
		return false;
	}
	const UObject* FirstRows = PropertyOf(FirstBlock, TEXT("DataRows"));
	const UObject* FirstRowsAgain = PropertyOf(FirstBlockAgain, TEXT("DataRows"));
	const UObject* SecondRows = PropertyOf(SecondBlock, TEXT("DataRows"));
	if (!TestNotNull(TEXT("The first world's rect block has rows"), FirstRows) || !TestNotNull(TEXT("...and so does the second's"), SecondRows))
	{
		return false;
	}
	TestTrue(TEXT("Two rect blocks of one world share its rows"), FirstRows == FirstRowsAgain);
	TestTrue(TEXT("A rect block of another world has rows of its own"), FirstRows != SecondRows);
	TestTrue(TEXT("Each world's rows are its UI manager's"),
		FirstRows->GetOuter() == UDreamUIManagerWorldSubsystem::GetInstance(First.World)
		&& SecondRows->GetOuter() == UDreamUIManagerWorldSubsystem::GetInstance(Second.World));
	TestTrue(TEXT("...and neither is the rect block data asset the worlds share"),
		FirstRows != PropertyOf(FirstBlock, TEXT("RectBlockData")) && SecondRows != PropertyOf(SecondBlock, TEXT("RectBlockData")));
	const UDreamUIDataAsTexture* Rows = Cast<UDreamUIDataAsTexture>(FirstRows);
	TestTrue(TEXT("The rows have a texture to draw with"), Rows != nullptr && Rows->GetDataTexture() != nullptr);
	return true;
}

#endif
