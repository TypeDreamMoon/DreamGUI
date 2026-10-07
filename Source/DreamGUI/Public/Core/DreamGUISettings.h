// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "InputCoreTypes.h"
#include "Core/DreamUIInputServices.h"
#include "Event/DreamPointerEventData.h"
#include "DreamGUISettings.generated.h"

class UDreamUIFontData_BaseObject;
class UDreamUserWidget;
class UDreamUISpriteData;
class UMaterialInterface;
class UTexture2D;

/** What Tab and Shift+Tab walk (UDreamGUISettings::TabOrder). */
UENUM(BlueprintType)
enum class EDreamUITabOrder : uint8
{
	/**
	 * The widget hierarchy, depth first, siblings by their Tab Index (UDreamWidget::TabIndex) and then in order: a sequence
	 * that reverses exactly, as a browser's and Slate's do.
	 */
	Hierarchy,
	/** The nearest control to the right, else the nearest below -- what Tab did before this setting. Kept for one version. */
	LegacyGeometric,
};

/**
 * How small text with effects (outline, glow, underlay) draws from coverage glyphs (UDreamGUISettings::bSmallTextCoverage):
 * the effects always come from the distance field, moved by the coverage face's snap so the two line up; this says what
 * the face is drawn from. Face softness and dilation keep a text on the field whatever this says.
 */
UENUM(BlueprintType)
enum class EDreamSmallTextEffectFace : uint8
{
	/** The field, effects and face: a text with effects never draws from coverage, as before. */
	Field,
	/** A hinted coverage glyph: the crispest face; its edges may stand up to half a pixel off the field's outline on thin outlines. */
	Hinted,
	/** An unhinted coverage glyph, the outline where the field has it: exact against the outline, a little softer than Hinted. */
	Unhinted,
	/** Hinted, but Unhinted for a glyph whose outline is under 2 device pixels, where Hinted's offset shows. */
	Auto,
};

/** One row of the direction key table (UDreamGUISettings::DirectionKeys): a key and the way it moves the focus. */
USTRUCT()
struct DREAMGUI_API FDreamUIDirectionKey
{
	GENERATED_BODY()

	FDreamUIDirectionKey() = default;
	FDreamUIDirectionKey(const FKey& InKey, EDreamUINavigationDirection InDirection) : Key(InKey), Direction(InDirection) {}

	UPROPERTY(EditAnywhere, Category = "Keys")
	FKey Key;
	/** Next is Tab's: with Shift held it is Prev. */
	UPROPERTY(EditAnywhere, Category = "Keys")
	EDreamUINavigationDirection Direction = EDreamUINavigationDirection::None;
};

/** One row of the page key table (UDreamGUISettings::PageKeys): a key and how many screenfuls it scrolls, negative back towards the start. */
USTRUCT()
struct DREAMGUI_API FDreamUIPageKey
{
	GENERATED_BODY()

	FDreamUIPageKey() = default;
	FDreamUIPageKey(const FKey& InKey, float InPages) : Key(InKey), Pages(InPages) {}

	UPROPERTY(EditAnywhere, Category = "Keys")
	FKey Key;
	UPROPERTY(EditAnywhere, Category = "Keys")
	float Pages = 1.0f;
};

/** One row of the extent key table (UDreamGUISettings::ExtentKeys): a key that scrolls all the way to the start, or to the end. */
USTRUCT()
struct DREAMGUI_API FDreamUIExtentKey
{
	GENERATED_BODY()

	FDreamUIExtentKey() = default;
	FDreamUIExtentKey(const FKey& InKey, bool bInToStart) : Key(InKey), bToStart(bInToStart) {}

	UPROPERTY(EditAnywhere, Category = "Keys")
	FKey Key;
	UPROPERTY(EditAnywhere, Category = "Keys")
	bool bToStart = true;
};

/** What DreamGUI's input keeps from the game, with the Slate input source. */
UENUM(BlueprintType)
enum class EDreamUIInputConsumePolicy : uint8
{
	/** Nothing: the game hears everything the UI hears, as it does through the preset actors. */
	Never,
	/** A press, a release or a wheel turn over DreamGUI UI, and any key the UI took. */
	WhenOverUI,
	/** Only what a widget answers: a press on a widget that handles presses, and a key the UI took. */
	WhenHandled,
};

/**
 * Every asset and class DreamGUI reaches for by itself, in one place a project can edit.
 *
 * These used to be path literals passed to LoadObject at the point of use. A path literal is worse
 * than a hard reference: it creates no package dependency at all, so renaming or moving the asset
 * compiles clean and turns into a silent null at runtime. Soft pointers fix the rename half -- the
 * editor fixes them up -- and they are a declared reference a tool can read.
 *
 * They do NOT make the assets cookable. A soft pointer assigned to a native CDO produces no asset
 * registry dependency, because the CDO lives in /Script/DreamGUI rather than in a package the
 * registry scans. These assets survive a normal cook only because UE cooks all mounted content by
 * default. What covers an explicit-package-list cook is the plugin's own Config/Game.ini, which puts
 * /DreamGUI into DirectoriesToAlwaysCook -- it ships with the plugin, so a project gets it by
 * enabling the plugin. See that file for why it is named Game.ini, and the constructor below.
 *
 * Everything is a fallback. A UDreamCanvas with its own DefaultMaterial set never reads this; the
 * settings only answer the question "what should this be when nobody said".
 */
UCLASS(config = DreamGUI, defaultconfig, meta = (DisplayName = "Dream GUI"))
class DREAMGUI_API UDreamGUISettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UDreamGUISettings();

	virtual FName GetCategoryName() const override { return TEXT("Plugins"); }

	static const UDreamGUISettings* Get();

	// ---------------------------------------------------------------- Materials

	/** Default material for UI meshes -- images, and both bitmap and SDF text. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> DefaultUIMaterial;

	/** Default material for UDreamRectBlock. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> DefaultRectBlockMaterial;

	/** Material used when a widget renders through a render target. */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TSoftObjectPtr<UMaterialInterface> RenderTargetMaterial;


	/**
	 * Preset materials offered on a distance-field font, in the order they appear in the picker.
	 *
	 * An array rather than four named entries because the picker shows whatever is here: a project
	 * can drop one, reorder them, or add its own text effect without touching the plugin.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Materials")
	TArray<TSoftObjectPtr<UMaterialInterface>> TextEffectPresetMaterials;

	// ---------------------------------------------------------------- Assets

	/** Font used by a UDreamText that has none set. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUIFontData_BaseObject> DefaultFont;

	/** Sprite for a solid white fill -- the default appearance of a UDreamSprite. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUISpriteData> DefaultWhiteSolidSprite;

	/** Sprite for a nine-sliced frame rectangle. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UDreamUISpriteData> DefaultFrameRectSprite;

	/** Texture behind DefaultWhiteSolidSprite, also used directly where a plain white pixel is needed. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<UTexture2D> DefaultWhiteSolidTexture;

	/** Default data asset for UDreamRectBlock. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftObjectPtr<class UDreamRectBlockData> DefaultRectBlockData;

	/** Widget class spawned to show which widget navigation input has selected. */
	UPROPERTY(config, EditAnywhere, Category = "Assets")
	TSoftClassPtr<UDreamUserWidget> NavigationSelectionClass;

	/**
	 * The project's control style sheet -- one asset where every native control's default look
	 * lives. Unset is a supported state, not a missing one: controls then use their styles' own
	 * C++ defaults, which are the built-in theme.
	 *
	 * Typed as the sheet's base class: the sheet is the control library's, and the core does not
	 * name the control library's types. AllowedClasses keeps the picker to style sheets, and
	 * UDreamUIStyleSheet::GetProjectSheet does the cast. The config value is the asset's path either
	 * way, so a project's setting reads the same as before.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Assets", meta = (AllowedClasses = "/Script/DreamGUIControls.DreamUIStyleSheet"))
	TSoftObjectPtr<class UDataAsset> DefaultStyleSheet;

	// ---------------------------------------------------------------- Text

	/**
	 * Darken small distance-field text, in the spirit of FreeType's stem darkening and Skia's text contrast: at 12 screen
	 * pixels per em and below a glyph's stems grow by 0.4 px and its edges get a little more contrast, fading to nothing
	 * at 24 pixels per em. A field's edge is a linear one-pixel ramp, which reads softer and lighter at those sizes than
	 * the hinted bitmaps Slate and browsers draw. Larger text and the text effects (outline, glow, underlay) are drawn the
	 * same either way. Applies to the built-in UI shader and to MF_DreamUI_Shade alike; a canvas takes a change when it
	 * next rebuilds its draw calls.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	bool bSmallTextCorrection = true;

	/**
	 * Draw small screen text of distance-field fonts from hinted coverage glyphs: each glyph rasterized for its pixel size
	 * with FreeType's light hinting in four subpixel positions, placed on whole device pixels -- the crisp stems Chrome
	 * and Slate draw -- instead of from the field. Decided per glyph item at paint time, so layout, carets and selection
	 * are the same either way. Only where it can be exact: screen-space and render-target canvases, a render layer only
	 * once it has held still for a few frames, a flat unrotated unmirrored uniformly scaled transform, no softness or
	 * dilate (outline, glow and underlay are drawn from the field under a coverage face, see SmallTextEffectFace), a
	 * material that shades through MF_DreamUI_Shade, no modifier that moves vertices, pixel snapping not disabled. A font
	 * can override this (its SmallTextCoverage), a text can opt out (UDreamText::SmallTextRaster).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	bool bSmallTextCoverage = true;

	/** Device pixels per em up to which small-text coverage glyphs are used; larger text draws from the field. A font can override it. */
	UPROPERTY(config, EditAnywhere, Category = "Text", meta = (ClampMin = "1.0", UIMax = "48.0", EditCondition = "bSmallTextCoverage"))
	float SmallTextMaxPixelSize = 20.0f;

	/**
	 * Skia's text contrast for coverage glyphs: how much the coverage of light-on-dark and dark-on-light text is boosted
	 * before it is blended, 1.0 as Chromium ships it. 0 blends the raw coverage, as Slate does. Below 16: it shares a
	 * vertex channel with a flag (DreamTextQuadCode::CoverageLinearTarget).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text", meta = (ClampMin = "0.0", ClampMax = "8.0", UIMax = "2.0", EditCondition = "bSmallTextCoverage"))
	float SmallTextContrast = 1.0f;

	/** How small text with an outline, a glow or an underlay draws from coverage glyphs; see EDreamSmallTextEffectFace. */
	UPROPERTY(config, EditAnywhere, Category = "Text", meta = (EditCondition = "bSmallTextCoverage"))
	EDreamSmallTextEffectFace SmallTextEffectFace = EDreamSmallTextEffectFace::Auto;

	/**
	 * How many texts a world may repaint onto coverage glyphs in one frame once their device scale or their render layer has
	 * settled: the rest wait for the next frames. Spreads the repaints of a screen whose labels all stop moving at once.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text", meta = (ClampMin = "1", UIMax = "4096", EditCondition = "bSmallTextCoverage"))
	int32 SmallTextRepaintBudgetPerFrame = 512;

	/**
	 * Run the face of distance-field text above SmallTextMaxPixelSize through the same coverage correction small text gets
	 * (the contrast and the linear-light blend), so its weight does not jump at the cut-off. Off by default until measured
	 * against Chrome. Drawn by the shaders: the canvases pass it with the font atlas's geometry (FontAtlasInfo.z negated).
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	bool bFieldTextCorrection = false;

	/**
	 * Gradients a rich text's `<gradient=Name>` finds by name, written as CSS (FDreamGradient::ParseCss), e.g. Gold =
	 * "linear-gradient(180deg, #FFF3B0, #E8B64A 55%, #9C6A12)". Looked up after the text's custom style entries and before
	 * the name itself is read as CSS; the editor's gradient picker offers them beside the gradient assets.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Text")
	TMap<FName, FString> GradientPresets;

	/**
	 * Whether small text draws from coverage glyphs: bSmallTextCoverage, unless the console variable
	 * DreamGUI.Text.SmallTextCoverage says 0 (off for every font, those set On included) or 1 (on for the fonts that
	 * inherit the project's choice). What a font asks (UDreamUIFontData_BaseObject::SupportsCoverageGlyphs).
	 */
	static bool IsSmallTextCoverageEnabled();
	/** SmallTextMaxPixelSize, unless the console variable DreamGUI.Text.SmallTextMaxPixelSize is above 0. What a font that has no limit of its own asks. */
	static float GetSmallTextMaxPixelSize();
	/** Broadcast on the game thread when either console variable above changes: every text drawing small sizes repaints. */
	static FSimpleMulticastDelegate& GetOnSmallTextCoverageChanged();


	/**
	 * Content folder the shipped control classes live in.
	 *
	 * A folder rather than a list because three places need it and they need it for different
	 * reasons: one builds a path into it, one is the base for the preset menu, and the palette
	 * excludes it so presets do not show up twice.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Editor")
	FString PresetControlFolder;

	// ---------------------------------------------------------------- Spawned actors

	/**
	 * Spawned when a UI needs an event system and the level has none.
	 *
	 * A class rather than an asset because it is spawned: point it at the C++ preset, at one of the
	 * preset Blueprints, or at a project's own actor.
	 *
	 * The only actor class DreamGUI still takes from a project. Root actors and pointer-source
	 * actors used to be named here too, one Blueprint per render mode; a world-space widget is an
	 * ADreamWorldWidgetActor today and its raycaster is put on a transient per-player host with no
	 * class to choose, so neither is a setting any more.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Actors")
	TSoftClassPtr<AActor> EventSystemActorClass;

	// ---------------------------------------------------------------- Input

	/**
	 * Hear input from Slate itself -- an input pre-processor, ahead of the game viewport -- instead of through the
	 * preset actor's bindings on the player controller. DreamGUI then answers in every input mode, the engine's own
	 * UI-only mode included, where the viewport ignores input and the controller hears nothing; and it can keep what
	 * it handled from the game (SlateInputConsumePolicy). The preset actors stand down while it is on. Off for now;
	 * it becomes the default in a later version.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bUseSlateInputSource = false;

	/** With the Slate input source: what the UI keeps from the game. A key typed into a field being edited is always kept. */
	UPROPERTY(config, EditAnywhere, Category = "Input", meta = (EditCondition = "bUseSlateInputSource"))
	EDreamUIInputConsumePolicy SlateInputConsumePolicy = EDreamUIInputConsumePolicy::Never;

	/** With the Slate input source: how fast the right stick scrolls what has focus, in canvas units a second at full tilt. */
	UPROPERTY(config, EditAnywhere, Category = "Input", meta = (ClampMin = "0.0", UIMax = "5000.0", EditCondition = "bUseSlateInputSource"))
	float SlateInputStickScrollSpeed = 1500.0f;

	/**
	 * What a player's keys and pad do while no navigation scope of theirs is active (see EDreamUIScopeInputMode). With no focus
	 * and no active scope, a navigation press looks for somewhere to land on that player's own screen-space canvases only.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	EDreamUIScopeInputMode InputModeWithoutScope = EDreamUIScopeInputMode::All;

	/** Hide the cursor while a player uses a pad, and show it again on the mouse -- where DreamGUI is what shows it (its UI-only input mode). */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bHideCursorOnGamepad = true;

	/**
	 * Draw the focus -- a control's Focused look and the focus ring -- only when keys or a pad put it there, as CSS's
	 * :focus-visible does: a clicked button is not drawn focused. Off: focus is always drawn.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bFocusVisibleOnlyFromKeys = true;

	/**
	 * The pad's confirm and Back are the platform's own (FPlatformInput::GetGamepadAcceptKey and GetGamepadBackKey, which a
	 * Switch swaps): they are added to ConfirmKeys and BackKeys when the game runs, and a face button listed there that
	 * the platform uses the other way is left out. Off: the tables as they are.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bUsePlatformAcceptBack = true;

	/**
	 * What a press outside an open menu -- a Dream Menu Anchor's, a menu anchor panel's, or a dropdown's list -- does besides
	 * closing it. Off, the default, the press then reaches whatever is under the pointer, as Slate's menu stack lets it: the
	 * menus close on the press and it goes on to the widget it landed on, so a menu left open never costs the player the
	 * click they made elsewhere. A press on the menu's own trigger -- a dropdown's face -- closes the menu without opening it
	 * again (UDreamMenuAnchor::ShouldOpenDueToClick). On: the press only closes the menu and goes no further, as DreamGUI's
	 * menus did before. A single dropdown keeps the press whatever this says with UUIDropdown::bUseInteractionBlock.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Input")
	bool bMenusConsumeOutsideClick = false;

	// ---------------------------------------------------------------- Navigation

	/** Tab and Shift+Tab move the focus from control to control. Off: Tab is a key like any other, the game's. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation")
	bool bTabNavigation = true;

	/** What Tab walks; see EDreamUITabOrder. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation", meta = (EditCondition = "bTabNavigation"))
	EDreamUITabOrder TabOrder = EDreamUITabOrder::Hierarchy;

	/** At the last control of a player's screen Tab goes round to the first, and Shift+Tab the other way. Popups and dialogs always go round. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation", meta = (EditCondition = "bTabNavigation"))
	bool bTabWrapsAtScreenEnd = true;

	/** A text field Tab lands on starts its edit, its text selected as the field says, as a browser's does. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation", meta = (EditCondition = "bTabNavigation"))
	bool bTabStartsTextEdit = true;

	/** Keys that confirm: press what has the focus. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FKey> ConfirmKeys = { EKeys::Enter, EKeys::SpaceBar, EKeys::Gamepad_FaceButton_Bottom };

	/** Keys that mean Back when nothing has bound an action to them. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FKey> BackKeys = { EKeys::Escape, EKeys::Gamepad_FaceButton_Right };

	/** Keys that move the focus, and which way. Tab is Next, and Prev with Shift held -- with neither Ctrl, Alt nor Cmd. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FDreamUIDirectionKey> DirectionKeys = {
		FDreamUIDirectionKey(EKeys::Left, EDreamUINavigationDirection::Left),
		FDreamUIDirectionKey(EKeys::Right, EDreamUINavigationDirection::Right),
		FDreamUIDirectionKey(EKeys::Up, EDreamUINavigationDirection::Up),
		FDreamUIDirectionKey(EKeys::Down, EDreamUINavigationDirection::Down),
		FDreamUIDirectionKey(EKeys::Tab, EDreamUINavigationDirection::Next),
		FDreamUIDirectionKey(EKeys::Gamepad_DPad_Left, EDreamUINavigationDirection::Left),
		FDreamUIDirectionKey(EKeys::Gamepad_DPad_Right, EDreamUINavigationDirection::Right),
		FDreamUIDirectionKey(EKeys::Gamepad_DPad_Up, EDreamUINavigationDirection::Up),
		FDreamUIDirectionKey(EKeys::Gamepad_DPad_Down, EDreamUINavigationDirection::Down),
		FDreamUIDirectionKey(EKeys::Gamepad_LeftStick_Left, EDreamUINavigationDirection::Left),
		FDreamUIDirectionKey(EKeys::Gamepad_LeftStick_Right, EDreamUINavigationDirection::Right),
		FDreamUIDirectionKey(EKeys::Gamepad_LeftStick_Up, EDreamUINavigationDirection::Up),
		FDreamUIDirectionKey(EKeys::Gamepad_LeftStick_Down, EDreamUINavigationDirection::Down),
	};

	/** Keys that page the scrolling container around the focus, by screenfuls. The triggers page as Page Up and Page Down do. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FDreamUIPageKey> PageKeys = {
		FDreamUIPageKey(EKeys::PageUp, -1.0f),
		FDreamUIPageKey(EKeys::PageDown, 1.0f),
		FDreamUIPageKey(EKeys::Gamepad_LeftTrigger, -1.0f),
		FDreamUIPageKey(EKeys::Gamepad_RightTrigger, 1.0f),
	};

	/** Keys that scroll the container around the focus all the way: Home and End. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FDreamUIExtentKey> ExtentKeys = {
		FDreamUIExtentKey(EKeys::Home, true),
		FDreamUIExtentKey(EKeys::End, false),
	};

	/** Keys that switch the player's active tab view to the previous tab, and to the next (IDreamUITabSwitchTarget); prompts show them on the action bar. */
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FKey> PreviousTabKeys = { EKeys::Gamepad_LeftShoulder };
	UPROPERTY(config, EditAnywhere, Category = "Navigation|Keys")
	TArray<FKey> NextTabKeys = { EKeys::Gamepad_RightShoulder };

	// ---------------------------------------------------------------- Tooltip

	/** Seconds the pointer rests on a widget before its ToolTipText shows. */
	UPROPERTY(config, EditAnywhere, Category = "Tooltip", meta = (ClampMin = "0.0", UIMax = "3.0"))
	float TooltipDelaySeconds = 0.5f;

	/**
	 * Bubble offset from the pointer, in canvas units, X right and Y up -- the default puts it
	 * below-right of the cursor. The tooltip flips to the pointer's other side when this side runs
	 * out of canvas.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Tooltip")
	FVector2D TooltipOffset = FVector2D(18.0f, -22.0f);

	/** Widest the built-in text bubble grows before the text wraps. */
	UPROPERTY(config, EditAnywhere, Category = "Tooltip", meta = (ClampMin = "50.0"))
	float TooltipMaxWidth = 420.0f;

	/** Font size of the built-in text bubble. */
	UPROPERTY(config, EditAnywhere, Category = "Tooltip", meta = (ClampMin = "6.0"))
	float TooltipFontSize = 14.0f;

	// ---------------------------------------------------------------- Modal

	/** Tint of the input-eating scrim behind a modal dialog. */
	UPROPERTY(config, EditAnywhere, Category = "Modal")
	FColor ModalScrimColor = FColor(0, 0, 0, 160);

	// ---------------------------------------------------------------- Virtual cursor

	/** Show the virtual cursor whenever the physical device is a gamepad, hide it otherwise. */
	UPROPERTY(config, EditAnywhere, Category = "Virtual Cursor")
	bool bAutoVirtualCursorOnGamepad = false;

	/** Cursor speed at full stick deflection, viewport pixels per second. */
	UPROPERTY(config, EditAnywhere, Category = "Virtual Cursor", meta = (ClampMin = "100.0"))
	float VirtualCursorSpeed = 1200.0f;

	/** Widget class drawn as the cursor. Empty draws the built-in square. */
	UPROPERTY(config, EditAnywhere, Category = "Virtual Cursor")
	TSoftClassPtr<UDreamUserWidget> VirtualCursorClass;

	// ---------------------------------------------------------------- Resolution

	/**
	 * Load a configured soft asset, or log which setting is empty or broken and return null.
	 *
	 * Callers used to get a bare null from LoadObject and had no way to say what was missing. Naming
	 * the property is the whole point: "DefaultUIMaterial is not set" is actionable, "material is
	 * null" is not.
	 */
	static UObject* LoadSetting(const FSoftObjectPath& Path, const TCHAR* PropertyName);

	template<typename T>
	static T* LoadSetting(const TSoftObjectPtr<T>& Soft, const TCHAR* PropertyName)
	{
		return Cast<T>(LoadSetting(Soft.ToSoftObjectPath(), PropertyName));
	}

	/**
	 * Same, for a class-valued setting. Templated over the class the setting is typed to, because
	 * these are no longer all actors: a UI hierarchy is a class now too.
	 */
	template<typename T>
	static UClass* LoadSettingClass(const TSoftClassPtr<T>& Soft, const TCHAR* PropertyName)
	{
		// TryLoad on a class path returns the UClass itself, not an instance, so the cast is the
		// identity when the setting is good and a clean null when it points at something else.
		return Cast<UClass>(LoadSetting(Soft.ToSoftObjectPath(), PropertyName));
	}
};
