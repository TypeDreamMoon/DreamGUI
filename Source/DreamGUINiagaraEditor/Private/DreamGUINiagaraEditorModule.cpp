// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "DreamNiagaraSystem.h"
#include "DreamNiagaraSystemCustomization.h"
#include "DreamNiagaraTrigger.h"
#include "DreamUIControlRegistry.h"
#include "Modules/ModuleManager.h"
#include "NiagaraSystem.h"
#include "PropertyEditorModule.h"
#include "Styling/SlateIconFinder.h"

#define LOCTEXT_NAMESPACE "DreamGUINiagaraEditor"

namespace DreamGUINiagaraEditorLocal
{
	const FName SystemEntry(TEXT("NiagaraSystem"));
	const FName TriggerEntry(TEXT("NiagaraTrigger"));
}

class FDreamGUINiagaraEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		using namespace DreamGUINiagaraEditorLocal;
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>(TEXT("PropertyEditor"));
		PropertyModule.RegisterCustomClassLayout(UDreamNiagaraSystem::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamNiagaraSystemCustomization::MakeInstance));

		// The palette: the visual under Effects with Niagara's own icon, the trigger under Components.
		FDreamUIControlDescriptor System;
		System.Name = SystemEntry;
		System.DisplayName = LOCTEXT("SystemEntry", "Niagara System");
		System.Category = DreamUIPaletteCategory::Effects;
		System.VisualClass = UDreamNiagaraSystem::StaticClass();
		System.Icon = FSlateIconFinder::FindIconForClass(UNiagaraSystem::StaticClass());
		FDreamUIControlRegistry::Get().Register(System);

		FDreamUIControlDescriptor Trigger;
		Trigger.Name = TriggerEntry;
		Trigger.DisplayName = LOCTEXT("TriggerEntry", "Niagara Trigger");
		Trigger.Category = DreamUIPaletteCategory::Components;
		Trigger.BehaviourClass = UDreamNiagaraTrigger::StaticClass();
		Trigger.Icon = FSlateIconFinder::FindIconForClass(UNiagaraSystem::StaticClass());
		FDreamUIControlRegistry::Get().Register(Trigger);
	}

	virtual void ShutdownModule() override
	{
		using namespace DreamGUINiagaraEditorLocal;
		if (FPropertyEditorModule* PropertyModule = FModuleManager::GetModulePtr<FPropertyEditorModule>(TEXT("PropertyEditor")))
		{
			PropertyModule->UnregisterCustomClassLayout(UDreamNiagaraSystem::StaticClass()->GetFName());
		}
		FDreamUIControlRegistry::Get().Unregister(SystemEntry);
		FDreamUIControlRegistry::Get().Unregister(TriggerEntry);
	}
};

IMPLEMENT_MODULE(FDreamGUINiagaraEditorModule, DreamGUINiagaraEditor)

#undef LOCTEXT_NAMESPACE
