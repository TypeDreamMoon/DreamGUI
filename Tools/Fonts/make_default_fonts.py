# Copyright 2026-Present TypeDreamMoon. All Rights Reserved.
#
# Builds DreamGUI's default distance-field font as Slate's default font is built: Roboto in four real faces
# (regular, bold, italic, bold italic) with DroidSansFallback behind it for CJK, every face an outline
# (multi-channel) field. Runs inside the editor (it imports `unreal`); see README.md next to it.
#
# Idempotent: assets that exist are kept and set again, nothing is duplicated twice, and every run ends with
# the same five assets saved.
#
# Only what the core editor exposes to Python is used -- unreal.load_asset, the asset registry, AssetTools and
# EditorLoadingAndSavingUtils -- not the Editor Scripting Utilities plugin, which is off by default.

import unreal

PACKAGE_PATH = "/DreamGUI"
DEFAULT_FONT = "DefaultFont_DistanceField"
CJK_FONT = DEFAULT_FONT + "_CJK"
STYLE_FONTS = (
    # (asset name, engine font face, property on the default font that names it)
    (DEFAULT_FONT + "_Bold", "/Engine/EngineFonts/Faces/RobotoBold", "bold_font"),
    (DEFAULT_FONT + "_Italic", "/Engine/EngineFonts/Faces/RobotoItalic", "italic_font"),
    (DEFAULT_FONT + "_BoldItalic", "/Engine/EngineFonts/Faces/RobotoBoldItalic", "bold_italic_font"),
)
REGULAR_FACE = "/Engine/EngineFonts/Faces/RobotoRegular"
CJK_FACE = "/Engine/EngineFonts/Faces/DroidSansFallback"


def log(message):
    unreal.log("[make_default_fonts] " + message)
    print("[make_default_fonts] " + message)


def package_name(asset_name):
    return PACKAGE_PATH + "/" + asset_name


def asset_exists(registry, asset_name):
    # The UFUNCTION returns a bool and an out array; Python gets the array, or None when the bool was false.
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
        # The registry can lag behind a package that is already on disk; then it simply loads.
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


def make_single_face(asset, face):
    """A font that is one engine face and nothing else: no fallbacks, no style faces of its own."""
    set_property(asset, "font_type", unreal.DreamUIDynamicFontDataType.ENGINE_FONT)
    set_property(asset, "engine_font", face)
    set_property(asset, "sdf_source", unreal.DreamUISdfSource.OUTLINE_MULTI_CHANNEL)
    set_property(asset, "fallback_font_array", [])
    for _, _, style_property in STYLE_FONTS:
        set_property(asset, style_property, None)


def main():
    registry = unreal.AssetRegistryHelpers.get_asset_registry()
    # A freshly started editor may still be scanning; whether an asset exists must not depend on that.
    registry.wait_for_completion()
    asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

    default_font = load_required(package_name(DEFAULT_FONT))
    regular_face = load_required(REGULAR_FACE)
    cjk_face = load_required(CJK_FACE)

    # Copies first, while the default is still what they are copies of on the first run.
    cjk_font = get_or_duplicate(registry, asset_tools, CJK_FONT, default_font)
    style_fonts = []
    for asset_name, face_path, style_property in STYLE_FONTS:
        style_fonts.append((get_or_duplicate(registry, asset_tools, asset_name, default_font), load_required(face_path), style_property))

    log("setting " + package_name(CJK_FONT))
    make_single_face(cjk_font, cjk_face)
    for style_font, face, _ in style_fonts:
        log("setting " + style_font.get_path_name())
        make_single_face(style_font, face)

    # The default itself: Roboto, the CJK face behind it, the three style faces. Nothing else on it changes.
    log("setting " + package_name(DEFAULT_FONT))
    set_property(default_font, "engine_font", regular_face)
    set_property(default_font, "sdf_source", unreal.DreamUISdfSource.OUTLINE_MULTI_CHANNEL)
    set_property(default_font, "fallback_font_array", [cjk_font])
    for style_font, _, style_property in style_fonts:
        set_property(default_font, style_property, style_font)

    fonts = [default_font, cjk_font] + [style_font for style_font, _, _ in style_fonts]
    packages = [font.get_outermost() for font in fonts]
    if not unreal.EditorLoadingAndSavingUtils.save_packages(packages, False):
        raise RuntimeError("saving the font packages failed")
    for font in fonts:
        log("saved " + font.get_outermost().get_name())
    log("done")


main()
