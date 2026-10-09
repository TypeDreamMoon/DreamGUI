// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIFontData_DistanceField.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUIRichTextCustomStyleData.h"
#include "Core/DreamUIRichTextImageData.h"
#include "DreamResourceUndoTestTypes.h"
#include "DreamScopedWorld.h"
#include "Editor.h"
#include "Engine/TextureRenderTarget2D.h"
#include "Engine/World.h"
#include "Misc/Paths.h"
#include "Misc/ScopeExit.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamResourceUndoTestLocal
{
	/** The same pre-change, reflected write and post-change notifications as a details-panel edit. */
	void EditObjectProperty(UObject* InObject, FObjectPropertyBase* InProperty, UObject* InValue)
	{
		InObject->PreEditChange(InProperty);
		InProperty->SetObjectPropertyValue_InContainer(InObject, InValue);
		FPropertyChangedEvent Event(InProperty, EPropertyChangeType::ValueSet);
		Event.MemberProperty = InProperty;
		InObject->PostEditChangeProperty(Event);
	}

	template<typename TAsset>
	bool CheckRichTextResourceUndo(FAutomationTestBase& Test, const TCHAR* PropertyName, TFunctionRef<void(TAsset*)> Notify)
	{
		if (!Test.TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr))return false;
		TStrongObjectPtr<UDreamWidget> Widget(NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transactional));
		UDreamRichTextResourceUndoProbe* Text = Widget->CreateNewVisual<UDreamRichTextResourceUndoProbe>();
		if (!Test.TestNotNull(TEXT("the authored text visual exists"), Text))return false;
		TStrongObjectPtr<TAsset> AssetA(NewObject<TAsset>(GetTransientPackage()));
		TStrongObjectPtr<TAsset> AssetB(NewObject<TAsset>(GetTransientPackage()));
		ON_SCOPE_EXIT
		{
			Widget->DestroyWidget();
			AssetA->OnDataChange.RemoveAll(Text);
			AssetB->OnDataChange.RemoveAll(Text);
		};
		// No glyphs or layout are needed to observe which asset asks this text to repaint.
		Text->SetFont(nullptr);
		Text->SetRichText(true);
		FObjectPropertyBase* Property = FindFProperty<FObjectPropertyBase>(Text->GetClass(), PropertyName);
		if (!Test.TestNotNull(TEXT("the rich-text resource reference is an editable object property"), Property))return false;
		Test.TestTrue(TEXT("an authored text inherits its owner's transaction flag"), Text->HasAnyFlags(RF_Transactional));
		const auto CheckCurrentAsset = [&](TAsset* Current, TAsset* Previous, const TCHAR* When)
		{
			Text->CheckResourceBindingBeforeGeometry();
			Test.TestTrue(FString::Printf(TEXT("%s the property names the current resource"), When), Property->GetObjectPropertyValue_InContainer(Text) == Current);
			Test.TestTrue(FString::Printf(TEXT("%s the text listens to its current resource"), When), Current->OnDataChange.IsBoundToObject(Text));
			Test.TestFalse(FString::Printf(TEXT("%s the text has no listener on the other resource"), When), Previous->OnDataChange.IsBoundToObject(Text));
			const int32 BeforeCurrent = Text->VertexDirtyCalls;
			Notify(Current);
			Test.TestEqual(FString::Printf(TEXT("%s editing the current resource asks the text to repaint once"), When), Text->VertexDirtyCalls - BeforeCurrent, 1);
			const int32 BeforePrevious = Text->VertexDirtyCalls;
			Notify(Previous);
			Test.TestEqual(FString::Printf(TEXT("%s editing the other resource cannot repaint the text"), When), Text->VertexDirtyCalls - BeforePrevious, 0);
		};

		EditObjectProperty(Text, Property, AssetA.Get());
		CheckCurrentAsset(AssetA.Get(), AssetB.Get(), TEXT("Initially"));
		{
			const FScopedTransaction Transaction(FText::FromString(TEXT("Swap a text's rich-text resource")));
			Text->Modify();
			EditObjectProperty(Text, Property, AssetB.Get());
		}
		CheckCurrentAsset(AssetB.Get(), AssetA.Get(), TEXT("After the normal edit"));
		if (!Test.TestTrue(TEXT("the real editor transaction undoes the rich-text resource swap"), GEditor->UndoTransaction()))return false;
		CheckCurrentAsset(AssetA.Get(), AssetB.Get(), TEXT("After undo"));
		if (!Test.TestTrue(TEXT("the real editor transaction redoes the rich-text resource swap"), GEditor->RedoTransaction()))return false;
		CheckCurrentAsset(AssetB.Get(), AssetA.Get(), TEXT("After redo"));
		return true;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamFontEmojiResourceUndoTest,
	"DreamGUI.Text.Font.UndoAndRedoAnEmojiAssetSwapListenOnlyToTheRestoredAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontEmojiResourceUndoTest::RunTest(const FString& Parameters)
{
	using namespace DreamResourceUndoTestLocal;
	if (!TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr))return false;
	DreamTests::FScopedGameWorld TestWorld;
	TStrongObjectPtr<UDreamUIFontEmojiData> EmojiA(NewObject<UDreamUIFontEmojiData>(TestWorld.World));
	TStrongObjectPtr<UDreamUIFontEmojiData> EmojiB(NewObject<UDreamUIFontEmojiData>(TestWorld.World));
	TStrongObjectPtr<UDreamUIFontData_DistanceField> Font(
		NewObject<UDreamUIFontData_DistanceField>(TestWorld.World, NAME_None, RF_Transactional));
	Font->SetFontFilePath(FPaths::Combine(FPaths::EngineContentDir(), TEXT("Slate/Fonts/Roboto-Regular.ttf")), false);
	Font->InitFont();
	FObjectPropertyBase* EmojiProperty = FindFProperty<FObjectPropertyBase>(Font->GetClass(), TEXT("EmojiData"));
	if (!TestNotNull(TEXT("the font's emoji reference is an editable object property"), EmojiProperty))return false;

	int32 Refreshes = 0;
	const FDelegateHandle RefreshHandle = Font->OnEmojiDataChanged.AddLambda([&Refreshes]() { ++Refreshes; });
	ON_SCOPE_EXIT
	{
		Font->OnEmojiDataChanged.Remove(RefreshHandle);
		// A failed undo must not leave the fixture's obsolete listener in the next test.
		EmojiA->OnDataChange.RemoveAll(Font.Get());
		EmojiB->OnDataChange.RemoveAll(Font.Get());
	};
	const auto CheckCurrentAsset = [&](UDreamUIFontEmojiData* Current, UDreamUIFontEmojiData* Previous, const TCHAR* When)
	{
		TestTrue(FString::Printf(TEXT("%s the property names the current emoji asset"), When), Font->GetEmojiData() == Current);
		TestTrue(FString::Printf(TEXT("%s the font listens to its current emoji asset"), When), Current->OnDataChange.IsBoundToObject(Font.Get()));
		TestFalse(FString::Printf(TEXT("%s the font has no listener on the other emoji asset"), When), Previous->OnDataChange.IsBoundToObject(Font.Get()));
		const int32 BeforeCurrent = Refreshes;
		Current->BroadcastOnDataChange();
		TestEqual(FString::Printf(TEXT("%s the current emoji asset refreshes the font once"), When), Refreshes - BeforeCurrent, 1);
		const int32 BeforePrevious = Refreshes;
		Previous->BroadcastOnDataChange();
		TestEqual(FString::Printf(TEXT("%s the other emoji asset cannot refresh the font"), When), Refreshes - BeforePrevious, 0);
	};

	EditObjectProperty(Font.Get(), EmojiProperty, EmojiA.Get());
	CheckCurrentAsset(EmojiA.Get(), EmojiB.Get(), TEXT("Initially"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Swap a font's emoji asset")));
		Font->Modify();
		EditObjectProperty(Font.Get(), EmojiProperty, EmojiB.Get());
	}
	CheckCurrentAsset(EmojiB.Get(), EmojiA.Get(), TEXT("After the normal edit"));
	if (!TestTrue(TEXT("the real editor transaction undoes the emoji asset swap"), GEditor->UndoTransaction()))return false;
	CheckCurrentAsset(EmojiA.Get(), EmojiB.Get(), TEXT("After undo"));
	if (!TestTrue(TEXT("the real editor transaction redoes the emoji asset swap"), GEditor->RedoTransaction()))return false;
	CheckCurrentAsset(EmojiB.Get(), EmojiA.Get(), TEXT("After redo"));
	if (!TestTrue(TEXT("the emoji asset swap can undo a second time"), GEditor->UndoTransaction()))return false;
	CheckCurrentAsset(EmojiA.Get(), EmojiB.Get(), TEXT("After the second undo"));
	if (!TestTrue(TEXT("the emoji asset swap can redo a second time"), GEditor->RedoTransaction()))return false;
	CheckCurrentAsset(EmojiB.Get(), EmojiA.Get(), TEXT("After the second redo"));

	const auto CheckNoAsset = [&](const TCHAR* When)
	{
		TestNull(FString::Printf(TEXT("%s the font's emoji reference is cleared"), When), Font->GetEmojiData());
		TestFalse(FString::Printf(TEXT("%s neither the first asset has a font listener"), When), EmojiA->OnDataChange.IsBoundToObject(Font.Get()));
		TestFalse(FString::Printf(TEXT("%s nor the second asset has a font listener"), When), EmojiB->OnDataChange.IsBoundToObject(Font.Get()));
		const int32 Before = Refreshes;
		EmojiA->BroadcastOnDataChange();
		EmojiB->BroadcastOnDataChange();
		TestEqual(FString::Printf(TEXT("%s neither emoji asset can refresh the cleared font"), When), Refreshes - Before, 0);
	};
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Clear a font's emoji asset")));
		Font->Modify();
		EditObjectProperty(Font.Get(), EmojiProperty, nullptr);
	}
	CheckNoAsset(TEXT("After the normal clear"));
	if (!TestTrue(TEXT("the real editor transaction undoes clearing the emoji asset"), GEditor->UndoTransaction()))return false;
	CheckCurrentAsset(EmojiB.Get(), EmojiA.Get(), TEXT("After undoing the clear"));
	if (!TestTrue(TEXT("the real editor transaction redoes clearing the emoji asset"), GEditor->RedoTransaction()))return false;
	CheckNoAsset(TEXT("After redoing the clear"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamPreviewCanvasResourceUndoTest,
	"DreamGUI.Extensions.CanvasPreviewer.UndoAndRedoACanvasSwapListenOnlyToTheRestoredCanvas",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamPreviewCanvasResourceUndoTest::RunTest(const FString& Parameters)
{
	using namespace DreamResourceUndoTestLocal;
	if (!TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr))return false;
	// These are authored objects with no world or rendering work. SetRenderTarget still sends the real
	// change notification, so its subscription and repaint requests can be checked without an RHI.
	TStrongObjectPtr<UDreamWidget> SourceA(NewObject<UDreamWidget>(GetTransientPackage()));
	TStrongObjectPtr<UDreamWidget> SourceB(NewObject<UDreamWidget>(GetTransientPackage()));
	TStrongObjectPtr<UDreamWidget> Display(NewObject<UDreamWidget>(GetTransientPackage(), NAME_None, RF_Transactional));
	UDreamCanvas* CanvasA = SourceA->AddComponent<UDreamCanvas>();
	UDreamCanvas* CanvasB = SourceB->AddComponent<UDreamCanvas>();
	UDreamRenderTargetPreviewerUndoProbe* Previewer = Display->CreateNewVisual<UDreamRenderTargetPreviewerUndoProbe>();
	if (!TestNotNull(TEXT("the first source canvas exists"), CanvasA)
		|| !TestNotNull(TEXT("the second source canvas exists"), CanvasB)
		|| !TestNotNull(TEXT("the previewer exists"), Previewer))return false;
	ON_SCOPE_EXIT
	{
		Display->DestroyWidget();
		// If the assertion exposed a subscription to the wrong canvas, teardown only knows the restored
		// property. Remove that orphan explicitly after the assertions so later tests see a clean fixture.
		CanvasA->GetRenderTargetChangedEvent().RemoveAll(Previewer);
		CanvasB->GetRenderTargetChangedEvent().RemoveAll(Previewer);
		SourceA->DestroyWidget();
		SourceB->DestroyWidget();
	};
	TestTrue(TEXT("an authored previewer inherits its owner's transaction flag"), Previewer->HasAnyFlags(RF_Transactional));
	FObjectPropertyBase* CanvasProperty = FindFProperty<FObjectPropertyBase>(Previewer->GetClass(), TEXT("Canvas"));
	if (!TestNotNull(TEXT("the previewer's canvas reference is an editable object property"), CanvasProperty))return false;
	const auto CheckCurrentCanvas = [&](UDreamCanvas* Current, UDreamCanvas* Previous, const TCHAR* When)
	{
		// This is the lazy repair opportunity a repaint normally gets, in addition to PostEditUndo.
		Previewer->CheckResourceBindingBeforeGeometry();
		TestTrue(FString::Printf(TEXT("%s the property names the current canvas"), When), Previewer->GetPreviewCanvas() == Current);
		TestTrue(FString::Printf(TEXT("%s the previewer listens to its current canvas"), When), Current->GetRenderTargetChangedEvent().IsBoundToObject(Previewer));
		TestFalse(FString::Printf(TEXT("%s the previewer has no listener on the other canvas"), When), Previous->GetRenderTargetChangedEvent().IsBoundToObject(Previewer));
		const int32 BeforeCurrent = Previewer->TextureDirtyCalls;
		Current->SetRenderTarget(NewObject<UTextureRenderTarget2D>(Current));
		TestEqual(FString::Printf(TEXT("%s a target change on the current canvas requests one repaint"), When), Previewer->TextureDirtyCalls - BeforeCurrent, 1);
		const int32 BeforePrevious = Previewer->TextureDirtyCalls;
		Previous->SetRenderTarget(NewObject<UTextureRenderTarget2D>(Previous));
		TestEqual(FString::Printf(TEXT("%s a target change on the other canvas cannot repaint the previewer"), When), Previewer->TextureDirtyCalls - BeforePrevious, 0);
	};

	EditObjectProperty(Previewer, CanvasProperty, CanvasA);
	CheckCurrentCanvas(CanvasA, CanvasB, TEXT("Initially"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Swap a render-target previewer's canvas")));
		Previewer->Modify();
		EditObjectProperty(Previewer, CanvasProperty, CanvasB);
	}
	CheckCurrentCanvas(CanvasB, CanvasA, TEXT("After the normal edit"));
	if (!TestTrue(TEXT("the real editor transaction undoes the preview canvas swap"), GEditor->UndoTransaction()))return false;
	CheckCurrentCanvas(CanvasA, CanvasB, TEXT("After undo"));
	if (!TestTrue(TEXT("the real editor transaction redoes the preview canvas swap"), GEditor->RedoTransaction()))return false;
	CheckCurrentCanvas(CanvasB, CanvasA, TEXT("After redo"));
	if (!TestTrue(TEXT("the preview canvas swap can undo a second time"), GEditor->UndoTransaction()))return false;
	CheckCurrentCanvas(CanvasA, CanvasB, TEXT("After the second undo"));
	if (!TestTrue(TEXT("the preview canvas swap can redo a second time"), GEditor->RedoTransaction()))return false;
	CheckCurrentCanvas(CanvasB, CanvasA, TEXT("After the second redo"));

	const auto CheckNoCanvas = [&](const TCHAR* When)
	{
		Previewer->CheckResourceBindingBeforeGeometry();
		TestNull(FString::Printf(TEXT("%s the preview canvas reference is cleared"), When), Previewer->GetPreviewCanvas());
		TestFalse(FString::Printf(TEXT("%s neither the first canvas has a previewer listener"), When), CanvasA->GetRenderTargetChangedEvent().IsBoundToObject(Previewer));
		TestFalse(FString::Printf(TEXT("%s nor the second canvas has a previewer listener"), When), CanvasB->GetRenderTargetChangedEvent().IsBoundToObject(Previewer));
		const int32 Before = Previewer->TextureDirtyCalls;
		CanvasA->SetRenderTarget(NewObject<UTextureRenderTarget2D>(CanvasA));
		CanvasB->SetRenderTarget(NewObject<UTextureRenderTarget2D>(CanvasB));
		TestEqual(FString::Printf(TEXT("%s neither canvas can repaint the cleared previewer"), When), Previewer->TextureDirtyCalls - Before, 0);
	};
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Clear a render-target previewer's canvas")));
		Previewer->Modify();
		EditObjectProperty(Previewer, CanvasProperty, nullptr);
	}
	CheckNoCanvas(TEXT("After the normal clear"));
	if (!TestTrue(TEXT("the real editor transaction undoes clearing the preview canvas"), GEditor->UndoTransaction()))return false;
	CheckCurrentCanvas(CanvasB, CanvasA, TEXT("After undoing the clear"));
	if (!TestTrue(TEXT("the real editor transaction redoes clearing the preview canvas"), GEditor->RedoTransaction()))return false;
	CheckNoCanvas(TEXT("After redoing the clear"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRichTextImageResourceUndoTest,
	"DreamGUI.Text.Resources.UndoAndRedoAnImageAssetSwapListenOnlyToTheRestoredAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextImageResourceUndoTest::RunTest(const FString& Parameters)
{
	return DreamResourceUndoTestLocal::CheckRichTextResourceUndo<UDreamUIRichTextImageData>(*this, TEXT("RichTextImageData"),
		[](UDreamUIRichTextImageData* Asset) { Asset->BroadcastOnDataChange(); });
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamRichTextStyleResourceUndoTest,
	"DreamGUI.Text.Resources.UndoAndRedoAStyleAssetSwapListenOnlyToTheRestoredAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamRichTextStyleResourceUndoTest::RunTest(const FString& Parameters)
{
	return DreamResourceUndoTestLocal::CheckRichTextResourceUndo<UDreamUIRichTextCustomStyleData>(*this, TEXT("RichTextCustomStyleData"),
		[](UDreamUIRichTextCustomStyleData* Asset) { Asset->SetDataMap(Asset->GetDataMap()); });
}

#endif // WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR
