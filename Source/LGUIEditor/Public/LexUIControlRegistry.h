// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class ULexWidget;
DECLARE_MULTICAST_DELEGATE(FOnLexUIControlRegistryChanged);

enum class ELexUIControlCreationKind : uint8
{
	Prefab,
	Native,
};

/** A Palette entry with an explicit creation recipe and validation contract. */
struct LGUIEDITOR_API FLexUIControlDescriptor
{
	FName Name;
	FText DisplayName;
	FName Category;
	ELexUIControlCreationKind CreationKind = ELexUIControlCreationKind::Native;
	FString PrefabPath;
	TWeakObjectPtr<UClass> VisualClass;
	TWeakObjectPtr<UClass> LayoutContainerClass;
	TWeakObjectPtr<UClass> LayoutSelfClass;
	TWeakObjectPtr<UClass> BehaviourClass;
	TWeakObjectPtr<UClass> MeshModifierClass;
	FSlateIcon Icon;
	TFunction<void(ULexWidget*)> NativeConfigure;
};

/** Central registration point used by the Palette and available to project/editor extensions. */
class LGUIEDITOR_API FLexUIControlRegistry
{
public:
	static FLexUIControlRegistry& Get();
	bool Register(const FLexUIControlDescriptor& Descriptor);
	bool Unregister(FName Name);
	const TArray<FLexUIControlDescriptor>& GetDescriptors()const { return Descriptors; }
	bool Validate(const FLexUIControlDescriptor& Descriptor, FText& OutError)const;
	void InitializeDynamicDiscovery();
	void ShutdownDynamicDiscovery();
	void RefreshDynamicClasses();
	FOnLexUIControlRegistryChanged& OnChanged() { return RegistryChanged; }

private:
	FLexUIControlRegistry();
	void RegisterDefaults();
	void HandleAssetLoaded(UObject* Asset);
	TArray<FLexUIControlDescriptor> Descriptors;
	TMap<FName, TWeakObjectPtr<UClass>> DynamicPostProcessClasses;
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle AssetLoadedHandle;
	FOnLexUIControlRegistryChanged RegistryChanged;
};
