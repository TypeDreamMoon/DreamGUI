// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverInputActors.h"

#include "Event/DreamBaseEventData.h"
#include "InputAction.h"
#include "InputCoreTypes.h"
#include "InputMappingContext.h"

#include "Driver/DreamDriverInputModule.h"

namespace DreamDriverInputActorsLocal
{
	/**
	 * The name ADreamStandaloneInputEventSystemActor's constructor gives its module subobject. Written
	 * out here because the production class does not name it anywhere else; if the two ever disagree
	 * the override matches nothing, the production module is created as before, and
	 * GetDriverInputModule answers null -- which is what the game host checks, and says, first.
	 */
	const TCHAR* const ProductionModuleSubobjectName = TEXT("DreamStandaloneInputModule");

	/**
	 * Register the driver module as the class of the production module subobject, on the initializer
	 * that is constructing this actor right now.
	 *
	 * Returns a value only so it can sit in a delegating constructor's argument list, which is the one
	 * place code runs before a parameterless base constructor does. The value means nothing.
	 */
	bool SubstituteDriverModule(const FObjectInitializer& InObjectInitializer)
	{
		InObjectInitializer.SetDefaultSubobjectClass<UDreamDriverInputModule>(FName(ProductionModuleSubobjectName));
		return true;
	}

	/** One of the four actions, as the shipped asset of that name is: a value type and nothing else. */
	UInputAction* MakeAction(UObject* InOuter, const TCHAR* InName, EInputActionValueType InValueType)
	{
		UInputAction* Action = NewObject<UInputAction>(InOuter, FName(InName), RF_Transient);
		Action->ValueType = InValueType;
		return Action;
	}
}

// ---------------------------------------------------------------------------------------------------- standalone

ADreamDriverStandaloneInputActor::ADreamDriverStandaloneInputActor(const FObjectInitializer& ObjectInitializer)
	: ADreamDriverStandaloneInputActor(ObjectInitializer, DreamDriverInputActorsLocal::SubstituteDriverModule(ObjectInitializer))
{
}

ADreamDriverStandaloneInputActor::ADreamDriverStandaloneInputActor(const FObjectInitializer&, bool)
	: Super()
{
	// Nothing of its own: the production constructor has made the root, the event system, the module
	// (now of the driver's class) and set AutoReceiveInput to player 0, which is the whole preset.
}

UDreamDriverInputModule* ADreamDriverStandaloneInputActor::GetDriverInputModule() const
{
	return Cast<UDreamDriverInputModule>(InputModule);
}

// ---------------------------------------------------------------------------------------------------- enhanced

ADreamDriverEnhancedInputActor::ADreamDriverEnhancedInputActor(const FObjectInitializer& ObjectInitializer)
	: ADreamDriverEnhancedInputActor(ObjectInitializer, DreamDriverInputActorsLocal::SubstituteDriverModule(ObjectInitializer))
{
}

ADreamDriverEnhancedInputActor::ADreamDriverEnhancedInputActor(const FObjectInitializer&, bool)
	: Super()
{
	// The module subobject is created by the standalone base constructor, two levels up, under the
	// same name -- so the one override above covers this class too.
}

UDreamDriverInputModule* ADreamDriverEnhancedInputActor::GetDriverInputModule() const
{
	return Cast<UDreamDriverInputModule>(InputModule);
}

void ADreamDriverEnhancedInputActor::InstallTransientMappings()
{
	using namespace DreamDriverInputActorsLocal;
	if (IsValid(MappingContext))
	{
		return;
	}

	// Named after the shipped assets, so a log line or a debugger shows the same names a game would.
	TriggerLeftAction = MakeAction(this, TEXT("IA_Trigger"), EInputActionValueType::Boolean);
	TriggerRightAction = MakeAction(this, TEXT("IA_TriggerRight"), EInputActionValueType::Boolean);
	TriggerMiddleAction = MakeAction(this, TEXT("IA_TriggerMiddle"), EInputActionValueType::Boolean);
	MouseWheelAction = MakeAction(this, TEXT("IA_MouseWheel"), EInputActionValueType::Axis1D);

	UInputMappingContext* TransientContext = NewObject<UInputMappingContext>(this, FName(TEXT("IMC_DreamUIInputContext")), RF_Transient);
	TransientContext->MapKey(TriggerLeftAction, EKeys::LeftMouseButton);
	TransientContext->MapKey(TriggerRightAction, EKeys::RightMouseButton);
	TransientContext->MapKey(TriggerMiddleAction, EKeys::MiddleMouseButton);
	TransientContext->MapKey(MouseWheelAction, EKeys::MouseWheelAxis);
	MappingContext = TransientContext;
}

const UInputMappingContext* ADreamDriverEnhancedInputActor::GetDriverMappingContext() const
{
	return MappingContext;
}

const UInputAction* ADreamDriverEnhancedInputActor::GetDriverTriggerAction(EDreamUIMouseButtonType InButton) const
{
	switch (InButton)
	{
	case EDreamUIMouseButtonType::Left:
		return TriggerLeftAction;
	case EDreamUIMouseButtonType::Right:
		return TriggerRightAction;
	case EDreamUIMouseButtonType::Middle:
		return TriggerMiddleAction;
	default:
		return nullptr;
	}
}

const UInputAction* ADreamDriverEnhancedInputActor::GetDriverWheelAction() const
{
	return MouseWheelAction;
}
