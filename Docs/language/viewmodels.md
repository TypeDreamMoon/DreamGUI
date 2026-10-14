---
title: "View models"
description: "The viewmodels block declares the view models a class holds and where each comes from; bindings, routes and loops reach them through member paths; how to write one, and when a binding through it updates."
---

## View models

A view model is an object that holds a screen's state and commands, apart from the widgets that show them. The widgets
bind to it; it never refers to them. One view model can serve several screens, and its logic can be tested without any
UI.

```
viewmodels {
    PlayerVM    Player                   // the host gives it
    SettingsVM  Settings = new           // this widget makes one
    InventoryVM Inventory = global       // from the game's registry, by class
    InventoryVM Stash = global "Stash"   // by class and name
    PartyVM     Party = parent           // the nearest enclosing widget's PartyVM
}

VerticalBox Root {
    Text Name { Text <- Player.Name }
    Native.Slider Volume { Value <-> Settings.MasterVolume }
    Native.Button Apply { OnClicked += Settings.Apply() }
}
```

Each entry is a type and a name, and optionally where the object comes from. The type is a class: its reflected name
(`PlayerVM` for a C++ `UPlayerVM`), a full path (`/Script/MyGame.PlayerVM`, `/Game/UI/BP_PlayerVM`), or a name given
by `use /Script/MyGame.PlayerVM as PlayerVM`. A type that names no class, or two, is DUI6015; two entries of one name
are DUI3024; a name the class already uses is DUI6016.

Each entry becomes a variable of the class -- an object reference of that class, FieldNotify, Expose on Spawn, in the
*ViewModels* category -- so everything that can set a variable can hand the widget its view model.

### Where the object comes from

| Written | Means |
|---|---|
| `PlayerVM Player` | The host gives it: a pin of the node that creates the widget, `SetViewModel(Name, Object)` from C++ or Blueprint, a Blueprint Set of the variable, or a host's `.dui` line on this widget used as a component (`Row Audio { Channel <- Settings.Audio }`). |
| `= new` | The widget makes one, outered to itself, when it was given none. Also in the designer's preview, which then shows the view model's defaults; such an instance answers `IsDesignTimeInstance()` true. `new` of an abstract class is DUI6017. |
| `= global`, `= global "Name"` | `UDreamViewModelSubsystem` (one per game instance): `Register(Object, Name)` shares an object, and the entry is filled from it by class, and name when one is written. When nothing fits yet, the entry waits and is filled when a fitting object is registered. Empty in the designer. |
| `= parent`, `= parent "Name"` | The nearest enclosing user widget's entry of a fitting class (and that name, when written), followed as it changes. What a component uses to share its host's view model without being handed it. |

The object is in place before On Initialized runs and before any binding's first value. An entry the host already set
keeps what it was given.

### Writing a view model

Any UObject class works. A class that implements `INotifyFieldValueChanged` -- `UDreamViewModel` is the convenient
base, and UE's own `UMVVMViewModelBase` works the same way -- tells bindings when a field changes, and bindings through
it then update only when it does. A class that does not is read every frame.

```cpp
UCLASS()
class UPlayerVM : public UDreamViewModel
{
    GENERATED_BODY()
public:
    void SetHealth(float InHealth) { DREAM_VM_SET(Health, InHealth); }

    UFUNCTION(BlueprintCallable, Category = "Player")
    void Heal(float Amount) { SetHealth(Health + Amount); }

protected:
    UPROPERTY(BlueprintReadOnly, FieldNotify, Category = "Player")
    float Health = 100.f;
};
```

- Members a binding reads must be visible to Blueprint: `BlueprintReadOnly` / `BlueprintReadWrite` properties,
  `BlueprintCallable` / `BlueprintPure` functions (DUI5023 names the one that is not).
- `DREAM_VM_SET(Member, Value)` assigns when the value changed and announces the field. A Blueprint view model's Set node
  of a FieldNotify variable announces on its own.
- A value computed from others (`GoldText` from `Gold`) is announced with `BroadcastFieldValueChanged` when its sources
  change. Offering such display-ready members is how a view model converts values; there is no separate converter.
- A view model that wants to react when the UI writes a value back through `<->` offers `Set<Member>` (BlueprintCallable,
  one parameter): `<->` calls it instead of writing the property.
