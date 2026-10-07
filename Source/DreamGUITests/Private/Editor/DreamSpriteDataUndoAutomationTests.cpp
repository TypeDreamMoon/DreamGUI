// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS && WITH_EDITOR

#include "Misc/AutomationTest.h"

#include "Core/DreamUISpriteData.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Editor.h"
#include "Engine/Texture2D.h"
#include "ScopedTransaction.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/UnrealType.h"

namespace DreamSpriteDataUndoTestLocal
{
	UTexture2D* MakeTexture(int32 Size)
	{
		UTexture2D* Texture = UTexture2D::CreateTransient(Size, Size, PF_B8G8R8A8);
		if (Texture != nullptr)
		{
			Texture->CompressionSettings = TC_EditorIcon;
			Texture->LODGroup = TEXTUREGROUP_UI;
			Texture->SRGB = true;
		}
		return Texture;
	}

	void EditReference(UObject* Object, FObjectPropertyBase* Property, UObject* Value)
	{
		Object->PreEditChange(Property);
		Property->SetObjectPropertyValue_InContainer(Object, Value);
		FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
		Event.MemberProperty = Property;
		Object->PostEditChangeProperty(Event);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSpriteTextureUndoTest,
	"DreamGUI.Editor.SpriteData.UndoAndRedoReturnTheTextureSelectedByTheRestoredAsset",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpriteTextureUndoTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpriteDataUndoTestLocal;
	if (!TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr))return false;
	TStrongObjectPtr<UTexture2D> TextureA(MakeTexture(8));
	TStrongObjectPtr<UTexture2D> TextureB(MakeTexture(16));
	if (!TestNotNull(TEXT("the first source texture exists"), TextureA.Get())
		|| !TestNotNull(TEXT("the second source texture exists"), TextureB.Get()))return false;
	TStrongObjectPtr<UDreamUISpriteData> Sprite(UDreamUISpriteData::CreateDreamUISpriteData(
		GetTransientPackage(), TextureA.Get(), FMargin(), NAME_None));
	if (!TestNotNull(TEXT("the public sprite factory created an individual sprite"), Sprite.Get()))return false;
	Sprite->SetFlags(RF_Transactional);
	FObjectPropertyBase* TextureProperty = FindFProperty<FObjectPropertyBase>(Sprite->GetClass(), TEXT("SpriteTexture"));
	if (!TestNotNull(TEXT("the editable texture reference exists"), TextureProperty))return false;
	const auto Check = [&](UTexture2D* Expected, int32 Size, const TCHAR* When)
	{
		TestEqual(FString::Printf(TEXT("%s the saved reference names the selected texture"), When), Sprite->GetSpriteTexture(), Expected);
		TestEqual(FString::Printf(TEXT("%s the renderer receives the selected texture"), When), Sprite->GetAtlasTexture(), Expected);
		TestEqual(FString::Printf(TEXT("%s the sprite width matches its source"), When), static_cast<int32>(Sprite->GetSpriteInfo().Width), Size);
		TestEqual(FString::Printf(TEXT("%s the sprite height matches its source"), When), static_cast<int32>(Sprite->GetSpriteInfo().Height), Size);
	};
	Check(TextureA.Get(), 8, TEXT("Initially"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Change a sprite texture")));
		Sprite->Modify();
		EditReference(Sprite.Get(), TextureProperty, TextureB.Get());
	}
	Check(TextureB.Get(), 16, TEXT("After the details edit"));
	if (!TestTrue(TEXT("the editor undoes the texture edit"), GEditor->UndoTransaction()))return false;
	Check(TextureA.Get(), 8, TEXT("After undo"));
	if (!TestTrue(TEXT("the editor redoes the texture edit"), GEditor->RedoTransaction()))return false;
	Check(TextureB.Get(), 16, TEXT("After redo"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FDreamSpritePackingAtlasUndoTest,
	"DreamGUI.Editor.SpriteData.UndoAndRedoKeepBothAtlasMembershipListsConsistentWithTheSprite",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FDreamSpritePackingAtlasUndoTest::RunTest(const FString& Parameters)
{
	using namespace DreamSpriteDataUndoTestLocal;
	if (!TestTrue(TEXT("the editor has a transaction buffer"), GEditor != nullptr && GEditor->Trans != nullptr))return false;
	TStrongObjectPtr<UTexture2D> Texture(MakeTexture(8));
	if (!TestNotNull(TEXT("the source texture exists"), Texture.Get()))return false;
	TStrongObjectPtr<UDreamUISpriteData> Sprite(UDreamUISpriteData::CreateDreamUISpriteData(
		GetTransientPackage(), Texture.Get(), FMargin(), NAME_None));
	TStrongObjectPtr<UDreamUIStaticSpriteAtlasData> AtlasA(NewObject<UDreamUIStaticSpriteAtlasData>(GetTransientPackage(), NAME_None, RF_Transactional));
	TStrongObjectPtr<UDreamUIStaticSpriteAtlasData> AtlasB(NewObject<UDreamUIStaticSpriteAtlasData>(GetTransientPackage(), NAME_None, RF_Transactional));
	if (!TestNotNull(TEXT("the sprite exists"), Sprite.Get()))return false;
	Sprite->SetFlags(RF_Transactional);
	FObjectPropertyBase* AtlasProperty = FindFProperty<FObjectPropertyBase>(Sprite->GetClass(), TEXT("PackingAtlas"));
	FEnumProperty* TypeProperty = FindFProperty<FEnumProperty>(Sprite->GetClass(), TEXT("PackingType"));
	if (!TestNotNull(TEXT("the editable atlas reference exists"), AtlasProperty)
		|| !TestNotNull(TEXT("the packing mode is an enum property"), TypeProperty))return false;
	TypeProperty->GetUnderlyingProperty()->SetIntPropertyValue(TypeProperty->ContainerPtrToValuePtr<void>(Sprite.Get()),
		static_cast<uint64>(EDreamUISpritePackingType::Static));

	const auto Check = [&](UDreamUIStaticSpriteAtlasData* Current, const TCHAR* When)
	{
		TestEqual(FString::Printf(TEXT("%s the sprite names the current atlas"), When), Sprite->GetPackingAtlas(), Current);
		TestEqual(FString::Printf(TEXT("%s atlas A lists exactly its own sprite"), When), AtlasA->ContainsSpriteData(Sprite.Get()), Current == AtlasA.Get());
		TestEqual(FString::Printf(TEXT("%s atlas B lists exactly its own sprite"), When), AtlasB->ContainsSpriteData(Sprite.Get()), Current == AtlasB.Get());
	};
	EditReference(Sprite.Get(), AtlasProperty, AtlasA.Get());
	Check(AtlasA.Get(), TEXT("Initially"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Move a sprite to another static atlas")));
		Sprite->Modify();
		EditReference(Sprite.Get(), AtlasProperty, AtlasB.Get());
	}
	Check(AtlasB.Get(), TEXT("After the details edit"));
	if (!TestTrue(TEXT("the editor undoes the atlas move"), GEditor->UndoTransaction()))return false;
	Check(AtlasA.Get(), TEXT("After undo"));
	if (!TestTrue(TEXT("the editor redoes the atlas move"), GEditor->RedoTransaction()))return false;
	Check(AtlasB.Get(), TEXT("After redo"));
	{
		const FScopedTransaction Transaction(FText::FromString(TEXT("Clear a sprite's static atlas")));
		Sprite->Modify();
		EditReference(Sprite.Get(), AtlasProperty, nullptr);
	}
	Check(nullptr, TEXT("After clearing the atlas"));
	if (!TestTrue(TEXT("the editor undoes clearing the atlas"), GEditor->UndoTransaction()))return false;
	Check(AtlasB.Get(), TEXT("After undoing the clear"));
	if (!TestTrue(TEXT("the editor redoes clearing the atlas"), GEditor->RedoTransaction()))return false;
	Check(nullptr, TEXT("After redoing the clear"));
	return true;
}

#endif
