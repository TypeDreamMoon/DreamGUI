// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class ULexUIBehaviour;
class ULexWidget;

enum class ELexUIBehaviourHandlerType : uint8
{
	Function,
	Event,
};

/**
 * Optional editor integration for a script system that produces ULexUIBehaviour classes.
 * The prefab editor itself only depends on UClass reflection; Blueprint and future
 * AngelScript integrations register code-authoring support through this interface.
 */
class LGUIEDITOR_API ILexUIBehaviourEditorBackend
{
public:
	virtual ~ILexUIBehaviourEditorBackend() = default;

	virtual FName GetBackendName() const = 0;
	virtual int32 GetPriority() const { return 0; }
	virtual bool SupportsClass(const UClass* InBehaviourClass) const = 0;
	virtual bool OpenClass(UClass* InBehaviourClass) = 0;

	virtual bool CanPromoteToVariable(const UClass* InBehaviourClass) const { return false; }
	virtual bool PromoteToVariable(ULexWidget* InRootWidget, ULexUIBehaviour* InPrimaryBehaviour, UObject* InTarget, FText& OutMessage) { return false; }

	virtual bool CanAddEventHandler(const UClass* InBehaviourClass) const { return false; }
	virtual bool CanAddEventHandler(const UClass* InBehaviourClass, ELexUIBehaviourHandlerType InHandlerType) const
	{
		return InHandlerType == ELexUIBehaviourHandlerType::Function && CanAddEventHandler(InBehaviourClass);
	}
	virtual FName AddEventHandler(ULexWidget* InRootWidget, ULexUIBehaviour* InPrimaryBehaviour,
		ULexUIBehaviour* InSourceComponent, FName InEventPropertyName, FText& OutMessage) { return NAME_None; }
	virtual FName AddEventHandler(ULexWidget* InRootWidget, ULexUIBehaviour* InPrimaryBehaviour,
		ULexUIBehaviour* InSourceComponent, FName InEventPropertyName, ELexUIBehaviourHandlerType InHandlerType, FText& OutMessage)
	{
		return InHandlerType == ELexUIBehaviourHandlerType::Function
			? AddEventHandler(InRootWidget, InPrimaryBehaviour, InSourceComponent, InEventPropertyName, OutMessage)
			: NAME_None;
	}
};

class LGUIEDITOR_API FLexUIBehaviourEditorBackendRegistry
{
public:
	static FLexUIBehaviourEditorBackendRegistry& Get();

	void RegisterBackend(const TSharedRef<ILexUIBehaviourEditorBackend>& InBackend);
	void UnregisterBackend(FName InBackendName);
	TSharedPtr<ILexUIBehaviourEditorBackend> FindBackend(const UClass* InBehaviourClass) const;

	void RegisterBuiltInBackends();
	void UnregisterBuiltInBackends();

private:
	TArray<TSharedRef<ILexUIBehaviourEditorBackend>> Backends;
};
