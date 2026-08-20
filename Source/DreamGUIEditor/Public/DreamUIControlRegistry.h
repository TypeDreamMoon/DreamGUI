// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Textures/SlateIcon.h"

class UDreamWidget;
DECLARE_MULTICAST_DELEGATE(FOnDreamUIControlRegistryChanged);

enum class EDreamUIControlCreationKind : uint8
{
	Prefab,
	Native,
};

/** A Palette entry with an explicit creation recipe and validation contract. */
struct DREAMGUIEDITOR_API FDreamUIControlDescriptor
{
	FName Name;
	FText DisplayName;
	FName Category;
	EDreamUIControlCreationKind CreationKind = EDreamUIControlCreationKind::Native;
	FString PrefabPath;
	TWeakObjectPtr<UClass> VisualClass;
	TWeakObjectPtr<UClass> LayoutContainerClass;
	TWeakObjectPtr<UClass> LayoutSelfClass;
	TWeakObjectPtr<UClass> BehaviourClass;
	TWeakObjectPtr<UClass> MeshModifierClass;
	FSlateIcon Icon;
	TFunction<void(UDreamWidget*)> NativeConfigure;
};

/** Central registration point used by the Palette and available to project/editor extensions. */
class DREAMGUIEDITOR_API FDreamUIControlRegistry
{
public:
	static FDreamUIControlRegistry& Get();
	/**
	 * Add a descriptor. The return value is meaningful and worth checking: false means the entry was
	 * refused -- Name is None, or another descriptor already holds it -- and will never appear in the
	 * Palette. Both refusals are logged with the descriptor they collided with.
	 * A descriptor that merely fails Validate is still registered; the Palette shows it disabled with
	 * the reason, which is how a mistyped prefab path stays visible instead of silently going missing.
	 */
	bool Register(const FDreamUIControlDescriptor& Descriptor);
	bool Unregister(FName Name);
	const TArray<FDreamUIControlDescriptor>& GetDescriptors()const { return Descriptors; }
	bool Validate(const FDreamUIControlDescriptor& Descriptor, FText& OutError)const;
	void InitializeDynamicDiscovery();
	void ShutdownDynamicDiscovery();
	void RefreshDynamicClasses();
	FOnDreamUIControlRegistryChanged& OnChanged() { return RegistryChanged; }

private:
	FDreamUIControlRegistry();
	void RegisterDefaults();
	void HandleAssetLoaded(UObject* Asset);
	TArray<FDreamUIControlDescriptor> Descriptors;
	TMap<FName, TWeakObjectPtr<UClass>> DynamicPostProcessClasses;
	FDelegateHandle BlueprintCompiledHandle;
	FDelegateHandle AssetLoadedHandle;
	FOnDreamUIControlRegistryChanged RegistryChanged;
};
