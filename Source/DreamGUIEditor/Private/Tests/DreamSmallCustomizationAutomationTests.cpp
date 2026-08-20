// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamTexture.h"
#include "Core/DreamUIFontData_Bitmap.h"
#include "IDetailsView.h"
#include "ISinglePropertyView.h"
#include "Modules/ModuleManager.h"
#include "PropertyEditorModule.h"
#include "PropertyHandle.h"
#include "Styling/SlateTypes.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// Three small details panels used to write UPROPERTYs behind the property system's back. The sprite
// and texture panels refresh the transient Fill Origin mirror the radial row displays, but refreshed
// it on the first selected object only, so every other selected object kept a stale mirror -- and
// that mirror is what the row then writes back into FillOrigin. The font panel wrote its
// "relative path" flag straight onto the asset, which leaves the package clean, so the asset never
// asks to be saved and the toggle is gone on reopen.
// Both writes live inside CustomizeDetails, so these tests drive them the way the editor does -- a
// real details view, a real property handle -- rather than pinning a helper the panel could quietly
// stop calling.

namespace DreamUIFontDataCustomization
{
	// Defined by DreamUIFontData_FreeTypeRenderCustomization.cpp; that customization has no header of
	// its own to declare them in.
	ECheckBoxState GetUseRelativeFilePathState(TSharedPtr<IPropertyHandle> InHandle);
	bool SetUseRelativeFilePath(TSharedPtr<IPropertyHandle> InHandle, bool bInUseRelativeFilePath);
}

namespace DreamSmallCustomizationTestLocal
{
	/** A real package, not the transient one: half of what is under test is whether it gets dirtied. */
	UPackage* MakeTestPackage()
	{
		UPackage* Package = CreatePackage(TEXT("/Temp/DreamSmallCustomizationTests"));
		Package->SetDirtyFlag(false);
		return Package;
	}

	/** The properties under test are protected and editor-only, so they are reached by name. */
	uint8* FindBytePropertyValue(UObject* InObject, FName InPropertyName)
	{
		FProperty* Property = FindFProperty<FProperty>(InObject->GetClass(), InPropertyName);
		if (Property == nullptr || Property->GetElementSize() != 1)return nullptr;
		return Property->ContainerPtrToValuePtr<uint8>(InObject);
	}

	bool SetByteProperty(UObject* InObject, FName InPropertyName, uint8 InValue)
	{
		uint8* ValuePtr = FindBytePropertyValue(InObject, InPropertyName);
		if (ValuePtr == nullptr)return false;
		*ValuePtr = InValue;
		return true;
	}

	TSharedRef<IDetailsView> MakeDetailsView()
	{
		FDetailsViewArgs Args;
		Args.bAllowSearch = false;
		Args.bShowOptions = false;
		Args.bHideSelectionTip = true;
		Args.NameAreaSettings = FDetailsViewArgs::HideNameArea;
		return FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor").CreateDetailView(Args);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamSpriteFillOriginMirrorFollowsEverySelectedSpriteTest,
	"DreamGUI.Editor.SpriteDetails.FillOriginMirrorFollowsEverySelectedSprite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpriteFillOriginMirrorFollowsEverySelectedSpriteTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallCustomizationTestLocal;
	UPackage* Package = MakeTestPackage();
	// A bare NewObject is collected mid-test: GC follows UPROPERTY references, not the outer chain.
	TStrongObjectPtr<UDreamSprite> First(NewObject<UDreamSprite>(Package));
	TStrongObjectPtr<UDreamSprite> Second(NewObject<UDreamSprite>(Package));

	// Anything but zero: the mirror comes out of load at zero, so a fill origin of zero would let an
	// untouched mirror read as correct.
	const uint8 ExpectedFillOrigin = (uint8)EDreamUISpriteFillOriginType_Radial360::Left;
	TArray<UDreamSprite*> Sprites = { First.Get(), Second.Get() };
	for (UDreamSprite* Sprite : Sprites)
	{
		if (!TestTrue(TEXT("the fixture is a radial-filled sprite with a stale mirror"),
			SetByteProperty(Sprite, TEXT("DrawType"), (uint8)EDreamUISpriteDrawType::Filled)
			&& SetByteProperty(Sprite, TEXT("FillMethod"), (uint8)EDreamUISpriteFillMethod::Radial360)
			&& SetByteProperty(Sprite, TEXT("FillOrigin"), ExpectedFillOrigin)
			&& SetByteProperty(Sprite, TEXT("FillOriginType_Radial360"), 0)))return true;
	}

	Package->SetDirtyFlag(false);
	TSharedRef<IDetailsView> DetailsView = MakeDetailsView();
	DetailsView->SetObjects(TArray<UObject*>{ First.Get(), Second.Get() });

	const uint8* FirstMirror = FindBytePropertyValue(First.Get(), TEXT("FillOriginType_Radial360"));
	const uint8* SecondMirror = FindBytePropertyValue(Second.Get(), TEXT("FillOriginType_Radial360"));
	if (!TestTrue(TEXT("both mirrors are readable"), FirstMirror != nullptr && SecondMirror != nullptr))return true;
	TestEqual(TEXT("the first selected sprite shows its own fill origin"), (int32)*FirstMirror, (int32)ExpectedFillOrigin);
	TestEqual(TEXT("the second selected sprite shows its own fill origin"), (int32)*SecondMirror, (int32)ExpectedFillOrigin);

	// Displaying a panel must not ask the user to save anything.
	TestFalse(TEXT("showing the panel leaves the package clean"), Package->IsDirty());

	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamTextureFillOriginMirrorFollowsEverySelectedTextureTest,
	"DreamGUI.Editor.TextureDetails.FillOriginMirrorFollowsEverySelectedTexture",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamTextureFillOriginMirrorFollowsEverySelectedTextureTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallCustomizationTestLocal;
	UPackage* Package = MakeTestPackage();
	TStrongObjectPtr<UDreamTexture> First(NewObject<UDreamTexture>(Package));
	TStrongObjectPtr<UDreamTexture> Second(NewObject<UDreamTexture>(Package));

	const uint8 ExpectedFillOrigin = (uint8)EDreamUISpriteFillOriginType_Radial360::Left;
	TArray<UDreamTexture*> Textures = { First.Get(), Second.Get() };
	for (UDreamTexture* Texture : Textures)
	{
		if (!TestTrue(TEXT("the fixture is a radial-filled texture with a stale mirror"),
			SetByteProperty(Texture, TEXT("DrawType"), (uint8)EDreamUISpriteDrawType::Filled)
			&& SetByteProperty(Texture, TEXT("FillMethod"), (uint8)EDreamUISpriteFillMethod::Radial360)
			&& SetByteProperty(Texture, TEXT("FillOrigin"), ExpectedFillOrigin)
			&& SetByteProperty(Texture, TEXT("fillOriginType_Radial360"), 0)))return true;
	}

	Package->SetDirtyFlag(false);
	TSharedRef<IDetailsView> DetailsView = MakeDetailsView();
	DetailsView->SetObjects(TArray<UObject*>{ First.Get(), Second.Get() });

	const uint8* FirstMirror = FindBytePropertyValue(First.Get(), TEXT("fillOriginType_Radial360"));
	const uint8* SecondMirror = FindBytePropertyValue(Second.Get(), TEXT("fillOriginType_Radial360"));
	if (!TestTrue(TEXT("both mirrors are readable"), FirstMirror != nullptr && SecondMirror != nullptr))return true;
	TestEqual(TEXT("the first selected texture shows its own fill origin"), (int32)*FirstMirror, (int32)ExpectedFillOrigin);
	TestEqual(TEXT("the second selected texture shows its own fill origin"), (int32)*SecondMirror, (int32)ExpectedFillOrigin);

	TestFalse(TEXT("showing the panel leaves the package clean"), Package->IsDirty());

	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FDreamFontRelativePathToggleGoesThroughThePropertyTest,
	"DreamGUI.Editor.FontDetails.RelativePathToggleGoesThroughTheProperty",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamFontRelativePathToggleGoesThroughThePropertyTest::RunTest(const FString& Parameters)
{
	using namespace DreamSmallCustomizationTestLocal;
	using namespace DreamUIFontDataCustomization;
	UPackage* Package = MakeTestPackage();
	// Transactional so the property write can record undo the way it does on a real asset.
	TStrongObjectPtr<UDreamUIFontData_Bitmap> Font(NewObject<UDreamUIFontData_Bitmap>(Package, NAME_None, RF_Transactional));

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");
	TSharedPtr<ISinglePropertyView> PropertyView =
		PropertyEditorModule.CreateSingleProperty(Font.Get(), TEXT("bUseRelativeFilePath"), FSinglePropertyParams());
	// Everything below reads or writes through this handle, so there is nothing left to check without it.
	if (!TestTrue(TEXT("the asset exposes the relative-path property"),
		PropertyView.IsValid() && PropertyView->GetPropertyHandle().IsValid()))return true;
	TSharedPtr<IPropertyHandle> Handle = PropertyView->GetPropertyHandle();

	TestEqual(TEXT("the checkbox starts from the asset's own value"),
		(int32)GetUseRelativeFilePathState(Handle), (int32)ECheckBoxState::Checked);

	Package->SetDirtyFlag(false);
	TestTrue(TEXT("the toggle reports its write went through"), SetUseRelativeFilePath(Handle, false));
	TestEqual(TEXT("the checkbox reads back what it wrote"),
		(int32)GetUseRelativeFilePathState(Handle), (int32)ECheckBoxState::Unchecked);
	// The whole point of the property system here: a raw write to the UPROPERTY leaves this false and
	// the asset is never offered for saving.
	TestTrue(TEXT("the asset now asks to be saved"), Package->IsDirty());

	// What a renamed or hidden property leaves the row holding.
	TestFalse(TEXT("a toggle with no property behind it writes nothing"), SetUseRelativeFilePath(nullptr, true));
	TestEqual(TEXT("...and has no state to report"),
		(int32)GetUseRelativeFilePathState(nullptr), (int32)ECheckBoxState::Undetermined);

	Package->SetDirtyFlag(false);
	return true;
}

#endif
