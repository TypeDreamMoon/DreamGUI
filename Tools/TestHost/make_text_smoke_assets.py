# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#
# Builds the assets of the packaged text smoke test (Tools/TestHost/README.md, "The packaged text smoke test") in the
# DreamGUI test host, under /Game/DreamGUISmoke:
#
#   Font_Emoji      the engine's Noto Color Emoji, embedded in the asset from its absolute path (a CustomFontFile whose
#                   bytes are saved into the asset, and refreshed from the file when the game is cooked)
#   Font_Text       a copy of DreamGUI's default font with three fallback entries: the CJK font for zh-Hans, the CJK
#                   font again for ja at scale 1.2, and Font_Emoji for the emoji ranges only
#   WBP_TextSmoke   a widget Blueprint whose hierarchy is the host's DUI/TextSmoke.dui (the template carries it)
#   L_TextSmoke     an empty level for the packaged game to start in
#
# Runs inside the editor (it imports `unreal`), headless as the README shows:
#
#   UnrealEditor-Cmd.exe <host>/DreamGUITestHost.uproject -EnablePlugins=PythonScriptPlugin -run=pythonscript
#       -script=<host>/Plugins/DreamGUI/Tools/TestHost/make_text_smoke_assets.py -unattended -nullrhi
#
# Idempotent, as Tools/Fonts/make_default_fonts.py is: assets that exist are kept and set again, and every run ends with
# the same four assets saved. Only what the core editor exposes to Python is used -- the asset registry, AssetTools,
# EditorLoadingAndSavingUtils and BlueprintEditorLibrary (an engine editor module the Blueprint editor loads) -- not the
# Editor Scripting Utilities plugin, which is off by default. Properties are named as C++ names them, which Python
# resolves too.

import os

import unreal

PACKAGE_PATH = "/Game/DreamGUISmoke"
DEFAULT_FONT = "/DreamGUI/DefaultFont_DistanceField"
CJK_FONT = "/DreamGUI/DefaultFont_DistanceField_CJK"
EMOJI_FONT = "Font_Emoji"
TEXT_FONT = "Font_Text"
SCREEN = "WBP_TextSmoke"
LEVEL = "L_TextSmoke"
# Relative to the project's DUI folder (DreamUIPaths), which the host template fills.
SCREEN_SOURCE = "TextSmoke.dui"
# The emoji, and nothing a text font has, as Docs/FontsAndPackaging.md lists them: the keycap bases, (c) and (r), the
# emoji among the symbols, arrows, dingbats and enclosed ideographs, and the emoji blocks.
EMOJI_RANGES = ((0x23, 0x23), (0x2A, 0x2A), (0x30, 0x39), (0xA9, 0xA9), (0xAE, 0xAE), (0x203C, 0x3299), (0x1F000, 0x1FAFF))


def log(message):
    unreal.log("[make_text_smoke_assets] " + message)
    print("[make_text_smoke_assets] " + message)


def package_name(asset_name):
    return PACKAGE_PATH + "/" + asset_name


def asset_exists(registry, asset_name):
    assets = registry.get_assets_by_package_name(package_name(asset_name))
    return assets is not None and len(assets) > 0


def load_required(path):
    asset = unreal.load_asset(path)
    if asset is None:
        raise RuntimeError("cannot load " + path)
    return asset


def get_or_duplicate(registry, asset_tools, asset_name, source):
    """The asset if it exists, else a copy of `source` under that name."""
    if asset_exists(registry, asset_name):
        log("kept existing " + package_name(asset_name))
        return load_required(package_name(asset_name))
    created = asset_tools.duplicate_asset(asset_name, PACKAGE_PATH, source)
    if created is None:
        created = unreal.load_asset(package_name(asset_name))
        if created is None:
            raise RuntimeError("cannot create " + package_name(asset_name))
        log("kept existing " + package_name(asset_name))
    else:
        log("created " + package_name(asset_name) + " as a copy of " + source.get_path_name())
    return created


def set_property(asset, name, value):
    asset.set_editor_property(name, value)
    shown = value.get_path_name() if isinstance(value, unreal.Object) else value
    log("  " + asset.get_name() + "." + name + " = " + str(shown))


def fallback_entry(font, cultures="", scale=1.0, ranges=()):
    entry = unreal.DreamUIFontFallback()
    entry.set_editor_property("Font", font)
    entry.set_editor_property("Cultures", cultures)
    entry.set_editor_property("Scale", scale)
    intervals = []
    for low, high in ranges:
        interval = unreal.Int32Interval()
        interval.set_editor_property("Min", low)
        interval.set_editor_property("Max", high)
        intervals.append(interval)
    entry.set_editor_property("Ranges", intervals)
    return entry


def engine_file(*parts):
    return os.path.normpath(os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.engine_content_dir()), *parts))


def make_emoji_font(registry, asset_tools, default_font):
    path = engine_file("Editor", "Slate", "Fonts", "NotoColorEmoji.ttf")
    if not os.path.isfile(path):
        raise RuntimeError("the engine's Noto Color Emoji is not at " + path)
    font = get_or_duplicate(registry, asset_tools, EMOJI_FONT, default_font)
    log("setting " + package_name(EMOJI_FONT))
    # The path before the type: changing FontType (or the embed switch, or EngineFont) reloads the font, and a reload
    # with the copied default font's empty path would fail it and log that the file does not exist.
    set_property(font, "bUseRelativeFilePath", False)
    set_property(font, "FontFilePath", path.replace("\\", "/"))
    # Embedded: the bytes go into the asset, which is what a packaged game reads. The default, spelled out.
    set_property(font, "bUseExternalFileOrEmbedInToUAsset", False)
    set_property(font, "FontType", unreal.DreamUIDynamicFontDataType.CUSTOM_FONT_FILE)
    set_property(font, "EngineFont", None)
    set_property(font, "Fallbacks", [])
    for style_property in ("BoldFont", "ItalicFont", "BoldItalicFont"):
        set_property(font, style_property, None)
    return font


def make_text_font(registry, asset_tools, default_font, cjk_font, emoji_font):
    font = get_or_duplicate(registry, asset_tools, TEXT_FONT, default_font)
    log("setting " + package_name(TEXT_FONT))
    fallbacks = [
        fallback_entry(cjk_font, cultures="zh-Hans"),
        fallback_entry(cjk_font, cultures="ja", scale=1.2),
        fallback_entry(emoji_font, ranges=EMOJI_RANGES),
    ]
    font.set_editor_property("Fallbacks", fallbacks)
    log("  " + font.get_name() + ".Fallbacks = [" + cjk_font.get_path_name() + " (zh-Hans), " + cjk_font.get_path_name()
        + " (ja, scale 1.2), " + emoji_font.get_path_name() + " (emoji ranges)]")
    return font


def make_screen(registry, asset_tools):
    source = os.path.join(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()), "DUI", SCREEN_SOURCE)
    if not os.path.isfile(source):
        raise RuntimeError(source + " is missing: run Tools/TestHost/New-DreamGUITestHost.ps1, whose template carries it")
    if asset_exists(registry, SCREEN):
        blueprint = load_required(package_name(SCREEN))
        log("kept existing " + package_name(SCREEN))
    else:
        # The factory makes a DreamGUI widget Blueprint (UDreamWidgetBlueprint, compiled by DreamGUI's compiler), which an
        # engine Blueprint factory would not; its parent class is the plain user widget until it is reparented below.
        blueprint = asset_tools.create_asset(SCREEN, PACKAGE_PATH, unreal.DreamWidgetBlueprint, unreal.DreamWidgetBlueprintFactory())
        if blueprint is None:
            raise RuntimeError("cannot create " + package_name(SCREEN))
        log("created " + package_name(SCREEN))
    if not hasattr(unreal, "BlueprintEditorLibrary"):
        # Python wraps the types of the modules loaded so far; in a commandlet nothing may have loaded this one by name.
        unreal.load_module("BlueprintEditorLibrary")
    library = unreal.BlueprintEditorLibrary
    defaults = unreal.get_default_object(library.generated_class(blueprint))
    if not isinstance(defaults, unreal.DreamTextUserWidget):
        # A class is text-authored when it derives from UDreamTextUserWidget; the reparent compiles once with no source
        # yet, which may say so in the log before the source is set and it compiles again.
        library.reparent_blueprint(blueprint, unreal.DreamTextUserWidget.static_class())
        log("  reparented " + SCREEN + " to DreamTextUserWidget")
        defaults = unreal.get_default_object(library.generated_class(blueprint))
    # What the designer's "Set Source File..." sets: the class default the compile reads the hierarchy from.
    defaults.set_editor_property("SourceFile", unreal.FilePath(file_path=SCREEN_SOURCE))
    log("  " + SCREEN + ".SourceFile = " + SCREEN_SOURCE + " (" + source + ")")
    if not library.compile_blueprint(blueprint):
        raise RuntimeError(package_name(SCREEN) + " did not compile from " + source + "; see the log")
    generated = library.generated_class(blueprint)
    if generated is None:
        raise RuntimeError(package_name(SCREEN) + " has no generated class after compiling; see the log")
    log("  compiled " + generated.get_path_name())
    return blueprint


def make_level(registry, asset_tools):
    if asset_exists(registry, LEVEL):
        log("kept existing " + package_name(LEVEL))
        return load_required(package_name(LEVEL))
    level = asset_tools.create_asset(LEVEL, PACKAGE_PATH, unreal.World, unreal.WorldFactory())
    if level is None:
        raise RuntimeError("cannot create " + package_name(LEVEL))
    log("created " + package_name(LEVEL))
    return level


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    registry.wait_for_completion()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    default_font = load_required(DEFAULT_FONT)
    cjk_font = load_required(CJK_FONT)
    emoji_font = make_emoji_font(registry, asset_tools, default_font)
    text_font = make_text_font(registry, asset_tools, default_font, cjk_font, emoji_font)
    # The fonts are saved before the screen compiles against them, so the .dui finds them on disk as well as in memory.
    if not unreal.EditorLoadingAndSavingUtils.save_packages([emoji_font.get_outermost(), text_font.get_outermost()], False):
        raise RuntimeError("saving the font packages failed")
    screen = make_screen(registry, asset_tools)
    level = make_level(registry, asset_tools)

    assets = [emoji_font, text_font, screen, level]
    if not unreal.EditorLoadingAndSavingUtils.save_packages([asset.get_outermost() for asset in assets], False):
        raise RuntimeError("saving the smoke packages failed")
    for asset in assets:
        log("saved " + asset.get_outermost().get_name())
    log("done")


main()
