// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UDreamUIBehaviour;
class UDreamWidget;

enum class EDreamUIBehaviourHandlerType : uint8
{
	Function,
	Event,
};

/**
 * Optional editor integration for a script system that produces UDreamUIBehaviour classes.
 * The prefab editor itself only depends on UClass reflection; Blueprint and future
 * AngelScript integrations register code-authoring support through this interface.
 */
class DREAMGUIEDITOR_API IDreamUIBehaviourEditorBackend
{
public:
	virtual ~IDreamUIBehaviourEditorBackend() = default;

	virtual FName GetBackendName() const = 0;
	virtual int32 GetPriority() const { return 0; }
	virtual bool SupportsClass(const UClass* InBehaviourClass) const = 0;
	virtual bool OpenClass(UClass* InBehaviourClass) = 0;

	virtual bool CanPromoteToVariable(const UClass* InBehaviourClass) const { return false; }
	virtual bool PromoteToVariable(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour, UObject* InTarget, FText& OutMessage) { return false; }

	virtual bool CanAddEventHandler(const UClass* InBehaviourClass) const { return false; }
	virtual bool CanAddEventHandler(const UClass* InBehaviourClass, EDreamUIBehaviourHandlerType InHandlerType) const
	{
		return InHandlerType == EDreamUIBehaviourHandlerType::Function && CanAddEventHandler(InBehaviourClass);
	}
	virtual FName AddEventHandler(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour,
		UDreamUIBehaviour* InSourceComponent, FName InEventPropertyName, FText& OutMessage) { return NAME_None; }
	virtual FName AddEventHandler(UDreamWidget* InRootWidget, UDreamUIBehaviour* InPrimaryBehaviour,
		UDreamUIBehaviour* InSourceComponent, FName InEventPropertyName, EDreamUIBehaviourHandlerType InHandlerType, FText& OutMessage)
	{
		return InHandlerType == EDreamUIBehaviourHandlerType::Function
			? AddEventHandler(InRootWidget, InPrimaryBehaviour, InSourceComponent, InEventPropertyName, OutMessage)
			: NAME_None;
	}
};

class DREAMGUIEDITOR_API FDreamUIBehaviourEditorBackendRegistry
{
public:
	static FDreamUIBehaviourEditorBackendRegistry& Get();

	void RegisterBackend(const TSharedRef<IDreamUIBehaviourEditorBackend>& InBackend);
	void UnregisterBackend(FName InBackendName);
	TSharedPtr<IDreamUIBehaviourEditorBackend> FindBackend(const UClass* InBehaviourClass) const;

	void RegisterBuiltInBackends();
	void UnregisterBuiltInBackends();

private:
	TArray<TSharedRef<IDreamUIBehaviourEditorBackend>> Backends;
};
