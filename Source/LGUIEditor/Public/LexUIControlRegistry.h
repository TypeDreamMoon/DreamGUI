// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

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
	FOnLexUIControlRegistryChanged& OnChanged() { return RegistryChanged; }

private:
	FLexUIControlRegistry();
	void RegisterDefaults();
	TArray<FLexUIControlDescriptor> Descriptors;
	FOnLexUIControlRegistryChanged RegistryChanged;
};
