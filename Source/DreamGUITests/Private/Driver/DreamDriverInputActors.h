// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Event/DreamEnhancedInputEventSystemActor.h"
#include "Event/DreamStandaloneInputEventSystemActor.h"
#include "DreamDriverInputActors.generated.h"

class UDreamDriverInputModule;
class UInputAction;
class UInputMappingContext;

/**
 * The production standalone input actor with one thing changed: its input module is a
 * UDreamDriverInputModule.
 *
 * Everything else is the class a game places. BeginPlay registers the module with the actor's own
 * event system and makes every binding the preset makes -- the three mouse buttons, the wheel axis,
 * Mouse2D, touch, the navigation and confirm keys, Back, paging, the right stick, the AnyKey router
 * -- on the InputComponent AutoReceiveInput gets from player 0. So a key the driver sends through
 * the player controller arrives at the module by exactly the road a player's key does. The one
 * thing that cannot take that road is the pointer POSITION, because a headless rig has no mouse to
 * read and must never read the user's: the driver module turns the module's own override seam on,
 * and the preset's GetPointerPosition -- which asks the module -- follows it.
 *
 * HOW THE MODULE IS SWAPPED. The production constructor creates the module as the default subobject
 * "DreamStandaloneInputModule", and FObjectInitializer::SetDefaultSubobjectClass is the engine's way
 * of changing the class of a subobject a base class creates. It must run before the base constructor
 * does -- the root UObject constructor closes subobject class setup -- which normally means passing
 * it to Super(...). The production constructor takes no FObjectInitializer, so there is nothing to
 * pass it to. A delegating constructor gets the same order: its arguments are evaluated before the
 * constructor it delegates to begins, and that one calls the parameterless Super(). So the override
 * is in place when the base constructor asks for its module, for the class default object and every
 * instance alike, and no production module is ever created beside the driver's.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient, HideDropdown)
class ADreamDriverStandaloneInputActor : public ADreamStandaloneInputEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamDriverStandaloneInputActor(const FObjectInitializer& ObjectInitializer);

	/** The actor's module, which is the driver's. Null only if the subobject override did not take. */
	UDreamDriverInputModule* GetDriverInputModule() const;

private:
	/** The constructor the public one delegates to once the module class has been substituted. See the class comment. */
	ADreamDriverStandaloneInputActor(const FObjectInitializer& ObjectInitializer, bool bInModuleClassSubstituted);
};

/**
 * The production Enhanced Input actor with the same one change, plus the assets it cannot do without.
 *
 * The production class ships with its mapping context and its four actions empty, for a Blueprint to
 * fill (see its constructor); a test may not create assets, so InstallTransientMappings builds the
 * same five objects in memory, named and shaped after the shipped ones under /DreamGUI/EnhancedInput:
 * three Boolean actions and one Axis1D action, and a context mapping LeftMouseButton, RightMouseButton,
 * MiddleMouseButton and MouseWheelAxis to them. The shipped actions carry no triggers of their own, so
 * none are added here either -- an action with no triggers is Started on actuation and Completed on
 * release, which are the two events the preset's button handlers read.
 *
 * The objects are written into the production properties themselves, so BeginPlay -- BindMouseInput
 * binding the actions, AddMappingContextToLocalPlayer pushing the context -- runs exactly the
 * production code over them. That is also why the call has to come BEFORE BeginPlay: after it, the
 * bindings have already been made against nothing.
 */
UCLASS(NotBlueprintable, NotPlaceable, Transient, HideDropdown)
class ADreamDriverEnhancedInputActor : public ADreamEnhancedInputEventSystemActor
{
	GENERATED_BODY()

public:
	ADreamDriverEnhancedInputActor(const FObjectInitializer& ObjectInitializer);

	/** The actor's module, which is the driver's. Null only if the subobject override did not take. */
	UDreamDriverInputModule* GetDriverInputModule() const;

	/**
	 * Build the context and the four actions in memory and put them in the production properties.
	 * Call between spawning (deferred) and BeginPlay. Does nothing when a context is already set.
	 */
	void InstallTransientMappings();

	/** What InstallTransientMappings put in place, for a probe asking whether it reached the player. */
	const UInputMappingContext* GetDriverMappingContext() const;
	/** The action the given button is mapped to, or null for a button the preset has no action for. */
	const UInputAction* GetDriverTriggerAction(EDreamUIMouseButtonType InButton) const;
	const UInputAction* GetDriverWheelAction() const;

private:
	/** The constructor the public one delegates to once the module class has been substituted. */
	ADreamDriverEnhancedInputActor(const FObjectInitializer& ObjectInitializer, bool bInModuleClassSubstituted);
};
