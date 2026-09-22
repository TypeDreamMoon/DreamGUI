// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamGUIEditorModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

#include "ISettingsModule.h"

#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "Editor.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "KismetCompiler.h"
#include "DreamWidgetBlueprint.h"
#include "DreamWidgetBlueprintCompiler.h"

#include "DreamGUIEditorStyle.h"
#include "Designer/DreamUITextAuthoringGate.h"
#include "Designer/DreamWidgetDesignerCommands.h"
#include "Text/DreamUITextWriteBack.h"
#include "Text/DreamUIBridgeService.h"
#include "Text/DreamUIMenus.h"
#include "Text/DreamUISourceWatcher.h"
#include "Text/DreamUISymbolExport.h"
#include "DreamUIEditorCommands.h"
#include "DreamUIEditorTools.h"
#include "DreamUIControlRegistry.h"
#include "DreamUIBehaviourEditorBackend.h"

#include "Thumbnail/DreamUISpriteThumbnailRenderer.h"
#include "Thumbnail/DreamWidgetBlueprintThumbnailRenderer.h"
#include "Thumbnail/DreamUISpriteDataBaseObjectThumbnailRenderer.h"
#include "ContentBrowserExtensions/DreamUIContentBrowserExtensions.h"
#include "Window/DreamUIDynamicSpriteAtlasViewer.h"

#include "AssetTypeActions/AssetTypeActions_DreamUISpriteData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIStaticSpriteAtlasData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIFontData_Bitmap.h"
#include "AssetTypeActions/AssetTypeActions_DreamWidgetBlueprint.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIStaticMeshCache.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIRichTextCustomStyleData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIRichTextImageData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIFontData_DistanceField.h"

#include "DetailCustomization/DreamWidgetCustomization.h"
#include "DetailCustomization/DreamVisualCustomization.h"
#include "DetailCustomization/DreamVisualBatchMeshCustomization.h"
#include "DetailCustomization/DreamSpriteBaseCustomization.h"
#include "DetailCustomization/DreamSpriteCustomization.h"
#include "DetailCustomization/DreamTextureCustomization.h"
#include "DetailCustomization/DreamCanvasCustomization.h"
#include "DetailCustomization/DreamTextCustomization.h"
#include "DetailCustomization/DreamTextureBaseCustomization.h"
#include "DetailCustomization/DreamRectBlockCustomization.h"
#include "DetailCustomization/DreamUISpriteDataCustomization.h"
#include "DetailCustomization/DreamUIStaticSpriteAtlasDataCustomization.h"
#include "DetailCustomization/DreamUIFontData_FreeTypeRenderCustomization.h"
#include "Controls/DreamUIControl.h"
#include "DetailCustomization/DreamUIControlCustomization.h"
#include "DetailCustomization/UISelectableCustomization.h"
#include "DetailCustomization/UIToggleCustomization.h"
#include "DetailCustomization/UITextInputCustomization.h"
#include "DetailCustomization/DreamUIEventDelegateCustomization.h"
#include "DetailCustomization/DreamWidgetEventCustomization.h"
#include "DetailCustomization/DreamUIComponentReferenceCustomization.h"
#include "DetailCustomization/UIScrollViewWithScrollBarCustomization.h"
#include "DetailCustomization/UISpriteSequencePlayerCustomization.h"
#include "DetailCustomization/UISpriteSheetTexturePlayerCustomization.h"
#include "DetailCustomization/DreamVisualPostProcessCustomization.h"

#include "Engine/Selection.h"

#include "Animation/DreamWidgetAnimationComponentCustomization.h"
#include "Animation/MovieSceneSequenceEditor_DreamWidgetAnimation.h"
#include "SequencerSettings.h"
#include "ISequencerModule.h"
#include "DreamUIComponentReference.h"
#include "MovieSceneToolsProjectSettings.h"
#include "Animation/DreamUIMaterialTrackEditor.h"
#include "Animation/DreamUIAnimEventTrackEditor.h"
#include "Animation/DreamUISequenceTrackEditor.h"
#include "DataFactory/DreamUISequenceFactory.h"
#include "Animation/DreamWidgetAnimationSequencerSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIFontEmojiData.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/DreamUIFontEmojiData.h"
#include "Core/DreamUIImageBrush.h"
#include "Core/DreamUIStaticSpriteAtlasData.h"
#include "Core/Components/DreamCanvas.h"
#include "Core/Components/DreamImage.h"
#include "Core/Components/DreamPanelSlot.h"
#include "Core/Components/DreamRectBlock.h"
#include "Core/Components/DreamSprite.h"
#include "Core/Components/DreamSpriteBase.h"
#include "Core/Components/DreamText.h"
#include "Core/Components/DreamTexture.h"
#include "Core/Components/DreamTextureBase.h"
#include "Core/Components/DreamVisualPostProcess.h"
#include "Core/DreamUserWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "Core/DreamWorldWidgetComponent.h"
#include "DetailCustomization/DreamImageBrushStructCustomization.h"
#include "DetailCustomization/DreamLayoutContainerCustomization.h"
#include "DetailCustomization/DreamUIEventDelegatePresetParamCustomization.h"
#include "DetailCustomization/DreamUIFontEmojiDataCustomization.h"
#include "DetailCustomization/DreamWidgetPresenterBaseCustomization.h"
#include "Event/DreamUIEventDelegate_PresetParameter.h"
#include "Extensions/DreamPolygon.h"
#include "Extensions/DreamPolygonLine.h"
#include "Extensions/DreamRing.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Extensions/2DLineRenderer/Dream2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Dream2DLineRaw.h"
#include "Interaction/UIButton.h"
#include "Interaction/UIDropdown.h"
#include "Interaction/DreamContentWidget.h"
#include "Interaction/UIListView.h"
#include "Interaction/UIScrollbar.h"
#include "Interaction/UIScrollViewWithScrollbar.h"
#include "Interaction/UISelectable.h"
#include "Interaction/UISlider.h"
#include "Interaction/UIStandardControls.h"
#include "Interaction/UITextInput.h"
#include "Interaction/UIToggle.h"
#include "Interaction/UIToggleGroup.h"
#include "MeshModifier/DreamMeshModifierBase.h"
#include "MeshModifier/DreamMeshModifierTextAnimation.h"
#include "Animation/DreamWidgetAnimation.h"
#include "Animation/DreamWidgetAnimationComponent.h"
#include "Styling/SlateIconFinder.h"
#include "Misc/CoreDelegates.h"
#include "Engine/World.h"
#include "UObject/UObjectIterator.h"
#include "Window/DreamUIWidgetInspector.h"
#include "ToolMenus.h"//ShutdownModule takes the designer viewport toolbar back out of the registry
#include "Designer/SDreamWidgetDesignerViewportToolbar.h"

const FName FDreamGUIEditorModule::DreamUIDynamicSpriteAtlasViewerTabName(TEXT("DreamUIDynamicSpriteAtlasViewerName"));
const FName FDreamGUIEditorModule::DreamUIWidgetInspectorTabName(TEXT("DreamUIWidgetInspectorTabName"));

#define LOCTEXT_NAMESPACE "FDreamGUIEditorModule"
DEFINE_LOG_CATEGORY(DreamGUIEditor);

void FDreamGUIEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FDreamGUIEditorStyle::Initialize();
	FDreamGUIEditorStyle::ReloadTextures();
	FDreamUIControlRegistry::Get().InitializeDynamicDiscovery();

	// Save a .dui, and the classes built from it recompile. Registered here rather than lazily on
	// the first designer open, because the loop it closes does not require one: a .dui saved with
	// no designer open still has to reach whatever classes ARE loaded.
	FDreamUISourceWatcher::Register();
	// The completion data external editors read; regenerated at startup so it never goes stale.
	FDreamUISymbolExport::Register();
	// The drop-folder RPC those editors talk back through: functions for completion, reveal,
	// compile-on-demand. Requests land under Saved/DreamGUI/Bridge/.
	FDreamUIBridgeService::Register();
	// The Tools entries and this plugin's share of the Dream-family toolbar combo.
	FDreamUIMenus::Register();

	// Tell the details panel how to ask "can this value be written into a .dui at all". Without it the
	// gate fails open, and a font or texture reference on a text-authored widget reads as editable --
	// the author changes it, the preview agrees, and the next compile drops it, which is the exact
	// silent failure the text pipeline exists to remove. Installed here rather than defaulted inside
	// the gate so the editor module owns the dependency: the gate is about authoring policy, and
	// what has a literal spelling is the write-back's question to answer.
	DreamUITextAuthoring::SetLiteralSpellingProbe(
		[](const FProperty* InLeaf, const void* InValuePtr)
		{
			return FDreamUITextWriteBack::CanSpellAsLiteral(InLeaf, InValuePtr);
		});
	FDreamUIBehaviourEditorBackendRegistry::Get().RegisterBuiltInBackends();

	// Teaches Kismet how to compile a DreamUI hierarchy into a class. Registering the compiler is
	// independent of having an editor for the asset -- the stock Blueprint editor opens it and the
	// stock compile button drives this -- so a DreamUI designer surface can come later on its own.
	FKismetCompilerContext::RegisterCompilerForBP(UDreamWidgetBlueprint::StaticClass(),
		[](UBlueprint* InBlueprint, FCompilerResultsLog& InMessageLog, const FKismetCompilerOptions& InCompileOptions)
		{
			return TSharedPtr<FKismetCompilerContext>(new FDreamWidgetBlueprintCompilerContext(
				CastChecked<UDreamWidgetBlueprint>(InBlueprint), InMessageLog, InCompileOptions));
		});

	OnInitializeSequenceHandle = UDreamWidgetAnimation::OnInitializeSequence().AddStatic(FDreamGUIEditorModule::OnInitializeSequence);

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(UDreamWidgetAnimation::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_DreamWidgetAnimation>());
	DreamUIMaterialTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDreamUIMaterialTrackEditor::CreateTrackEditor));
	DreamUIAnimEventTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDreamUIAnimEventTrackEditor::CreateTrackEditor));
	DreamUISequenceTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDreamUISequenceTrackEditor::CreateTrackEditor));

	FDreamUIEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	//register window
	{
		//atlas texture viewer
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DreamUIDynamicSpriteAtlasViewerTabName, FOnSpawnTab::CreateRaw(this, &FDreamGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab))
			.SetDisplayName(LOCTEXT("DreamUIDynamicSpriteAtlasTextureViewerName", "DreamUI Dynamic-Sprite-Atlas Texture Viewer"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		//world widget inspector
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(DreamUIWidgetInspectorTabName, FOnSpawnTab::CreateRaw(this, &FDreamGUIEditorModule::HandleSpawnDreamUIInspectorTab))
			.SetDisplayName(LOCTEXT("DreamUIInspectorTabName", "DreamUI Inspector"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
	}
	//register custom editor
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(UDreamWidget::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamWidgetCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamVisual::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamVisualCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamVisualBatchMesh::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamVisualBatchMeshCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamSpriteBase::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamSpriteBaseCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamSprite::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamSpriteCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamCanvas::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamCanvasCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamText::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamTextCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamTextureBase::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamTextureBaseCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamRectBlock::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamRectBlockCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamTexture::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamTextureCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamVisualPostProcess::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamVisualPostProcessCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UDreamUISpriteData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUISpriteDataCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamUIStaticSpriteAtlasData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIStaticSpriteAtlasDataCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamUIFontData_FreeTypeRender::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIFontData_FreeTypeRenderCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(UUISelectable::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISelectableCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUIToggle::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUIToggleCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUITextInput::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUITextInputCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUIScrollViewWithScrollbar::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUIScrollViewWithScrollBarCustomization::MakeInstance));
		// ONE registration for every native control, on the base they all share: a class layout is
		// found by walking up the superclass chain, so UDreamUIControl covers UDreamButton,
		// UDreamRingMenu and the twenty-odd others -- and covers the next one for free. The four
		// registrations above are BEHAVIOUR components, which is a different panel entirely.
		PropertyModule.RegisterCustomClassLayout(UDreamUIControl::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIControlCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSequencePlayerCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSheetTexturePlayerCustomization::MakeInstance));

		// The "green +": event rows in the designer's details panel. Two registrations, one class of
		// rows -- UDreamUserWidget is every native control and every nested widget Blueprint, and
		// UDreamUIBehaviour is where the interaction events (UIButton, UIToggle...) actually live.
		// Class layouts stack along the inheritance chain, so these run alongside the widget and
		// behaviour customizations above rather than replacing them.
		PropertyModule.RegisterCustomClassLayout(UDreamUserWidget::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamWidgetEventCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UDreamUIBehaviour::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamWidgetEventCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UDreamUIFontEmojiData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIFontEmojiDataCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamUIEventDelegateCustomization::MakeInstance));
		//PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegateTwoParam::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamUIEventDelegateTwoParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Empty::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Bool::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Float::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Double::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector2::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector3::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector4::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Color::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_LinearColor::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Quaternion::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_String::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Asset::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_DreamWidget::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_PointerEvent::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Class::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Rotator::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Text::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Name::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&DreamUIEventDelegatePresetParamCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIComponentReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamUIComponentReferenceCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UDreamWidgetAnimationComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamWidgetAnimationComponentCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomPropertyTypeLayout(FDreamUIImageBrush::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FDreamImageBrushStructCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(UDreamLayoutContainer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamLayoutContainerCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(UDreamWidgetPresenterComponentBase::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamWidgetPresenterBaseCustomization::MakeInstance));
	}
	//register asset
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		//register AssetCategory
		EAssetTypeCategories::Type DreamUIAssetCategoryBit = AssetTools.FindAdvancedAssetCategory(FName(TEXT("DreamUI")));
		if (DreamUIAssetCategoryBit == EAssetTypeCategories::Misc)
		{
			DreamUIAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("DreamUI")), LOCTEXT("DreamUIAssetCategory", "DreamUI"));
		}

		TSharedPtr<FAssetTypeActions_Base> SpriteDataAction = MakeShareable(new FAssetTypeActions_DreamUISpriteData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> StaticSpriteAtlasDataAction = MakeShareable(new FAssetTypeActions_DreamUIStaticSpriteAtlasData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> BitmapFontDataAction = MakeShareable(new FAssetTypeActions_DreamUIFontData_Bitmap(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> WidgetBlueprintAction = MakeShareable(new FAssetTypeActions_DreamWidgetBlueprint(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> SequenceAssetAction = MakeShareable(new FAssetTypeActions_DreamUISequence(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> UIStaticMeshCacheDataAction = MakeShareable(new FAssetTypeActions_DreamUIStaticMeshCache(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextCustomStyleDataAction = MakeShareable(new FAssetTypeActions_DreamUIRichTextCustomStyleData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextImageDataAction = MakeShareable(new FAssetTypeActions_DreamUIRichTextImageData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> FontEmojiDataAction = MakeShareable(new FAssetTypeActions_DreamUIFontEmojiData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> DistanceFieldFontDataTypeAction = MakeShareable(new FAssetTypeActions_DreamUIFontData_DistanceField(DreamUIAssetCategoryBit));
		AssetTools.RegisterAssetTypeActions(SpriteDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(StaticSpriteAtlasDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(BitmapFontDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(WidgetBlueprintAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(SequenceAssetAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(UIStaticMeshCacheDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextCustomStyleDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextImageDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(FontEmojiDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(DistanceFieldFontDataTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(SpriteDataAction);
		AssetTypeActionsArray.Add(StaticSpriteAtlasDataAction);
		AssetTypeActionsArray.Add(BitmapFontDataAction);
		AssetTypeActionsArray.Add(WidgetBlueprintAction);
		AssetTypeActionsArray.Add(SequenceAssetAction);
		AssetTypeActionsArray.Add(UIStaticMeshCacheDataAction);
		AssetTypeActionsArray.Add(RichTextCustomStyleDataAction);
		AssetTypeActionsArray.Add(RichTextImageDataAction);
		AssetTypeActionsArray.Add(FontEmojiDataAction);
		AssetTypeActionsArray.Add(DistanceFieldFontDataTypeAction);
	}
	//register Thumbnail
	{
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamUISpriteData::StaticClass(), UDreamUISpriteThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamUISpriteData_BaseObject::StaticClass(), UDreamUISpriteDataBaseObjectThumbnailRenderer::StaticClass());
		// A wireframe of the screen each hierarchy authors. Without it every DreamUI Widget Blueprint
		// in the browser was the same generic Blueprint icon, so a folder of screens was a column of
		// identical tiles with only the names to tell them apart.
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamWidgetBlueprint::StaticClass(), UDreamWidgetBlueprintThumbnailRenderer::StaticClass());
	}
	//register right mouse button in content browser
	{
		if (!IsRunningCommandlet())
		{
			FDreamUIContentBrowserExtensions::InstallHooks();
		}
	}
	//register setting
	{
// "Plugins", not a category of our own: UDreamGUISettings is a UDeveloperSettings and registers
// itself under Project Settings > Plugins > Dream GUI, which is where every error message in the
// runtime module sends the reader. These two used to land in a separate "DreamPlugin" category, so a
// user told to open the plugin's settings found half of them -- MSAA, the atlas knobs and the
// built-in shader switches were one category away with nothing pointing at them. Section ids are
// unchanged, and settings persist by class, so this moves the page without moving anyone's values.
#define DREAM_PLUGIN "Plugins"
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", DREAM_PLUGIN, "DreamGUI",
				LOCTEXT("DreamUISettingsName", "DreamUI"),
				LOCTEXT("DreamUISettingsDescription", "DreamGUI Settings"),
				GetMutableDefault<UDreamUISettings>());
			SettingsModule->RegisterSettings("Project", DREAM_PLUGIN, "DreamGUI Editor",
				LOCTEXT("DreamUIEditorSettingsName", "DreamGUI Editor"),
				LOCTEXT("DreamUIEditorSettingsDescription", "DreamGUI Editor Settings"),
				GetMutableDefault<UDreamUIEditorSettings>());

			DreamWidgetAnimationSequencerSettings = USequencerSettingsContainer::GetOrCreate<UDreamWidgetAnimationSequencerSettings>(TEXT("EmbeddedDreamWidgetAnimationEditor"));
			SettingsModule->RegisterSettings("Editor", "ContentEditors", "EmbeddedDreamWidgetAnimationEditor",
				LOCTEXT("DreamWidgetAnimationSequencerSettingsName", "DreamGUI Animation Editor"),
				LOCTEXT("DreamWidgetAnimationSequencerSettingsDescription", "Configure the look and feel of the DreamGUI Animation Editor."),
				DreamWidgetAnimationSequencerSettings);
		}
	}
	//blueprint
	{
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamUIBehaviour::StaticClass(), TEXT("ReceiveAwake"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamUIBehaviour::StaticClass(), TEXT("ReceiveStart"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamUIBehaviour::StaticClass(), TEXT("ReceiveTick"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamUIBehaviour::StaticClass(), TEXT("ReceiveOnDestroy"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnNormal"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnHovered"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnPressed"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnDisabled"));
		
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUIToggleTransition::StaticClass(), TEXT("ReceiveToggleOn"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUIToggleTransition::StaticClass(), TEXT("ReceiveToggleOff"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualCustomRaycast::StaticClass(), TEXT("ReceiveRaycast"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualCustomRaycast::StaticClass(), TEXT("ReceiveInit"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualBatchMesh::StaticClass(), TEXT("ReceiveOnBeforeCreateOrUpdateGeometry"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualBatchMesh::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamSpriteBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamTextureBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamMeshModifierBase::StaticClass(), TEXT("ReceiveModifyUIGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamLayoutAnimation::StaticClass(), TEXT("ReceiveOnApplyLayoutResults"));
	}
	// Recompiling a widget Blueprint leaves every world widget component holding a tree the
	// reinstancer has already replaced. Nothing about the component changes, so no reregister
	// happens and nothing would rebuild it: what is on screen keeps drawing from objects that were
	// marked garbage, and the properties the host reads come from the class that used to exist.
	// Reloading is the only way to be sure the host ends up with a complete instance of the NEW
	// class -- the reinstancer copies matching properties, it does not re-run construction.
	//
	// The event lives on GEditor, and GEditor does not exist yet: this module's loading phase is
	// Default, which runs inside FEngineLoop::PreInit, and the editor engine is not constructed
	// until FEngineLoop::Init. Reading GEditor here on a normal startup gets null, so a plain
	// "bind if GEditor" would never bind at all and would never say so. The engine-is-up delegate
	// is the second chance; the direct branch is still worth keeping for the paths that load this
	// module late, such as a module reload from an already-running editor.
	if (GEditor != nullptr)
	{
		BindBlueprintReinstancedHook();
	}
	else
	{
		PostEngineInitHandle = FCoreDelegates::GetOnPostEngineInit().AddRaw(
			this, &FDreamGUIEditorModule::BindBlueprintReinstancedHook);
	}
}

void FDreamGUIEditorModule::BindBlueprintReinstancedHook()
{
	if (GEditor == nullptr || BlueprintReinstancedHandle.IsValid())
	{
		return;
	}
	// OnBlueprintReinstanced rather than FCoreUObjectDelegates::OnObjectsReinstanced: it fires once
	// per compile, after every instance has been replaced and before garbage collection, which is
	// exactly the window where dropping the old tree is both necessary and safe. The lower-level
	// delegate fires per replacement batch, including batches that have nothing to do with
	// Blueprints, and fires while the swap is still in progress. UMG's designer view hooks the same
	// event to recreate its preview.
	BlueprintReinstancedHandle = GEditor->OnBlueprintReinstanced().AddRaw(
		this, &FDreamGUIEditorModule::HandleBlueprintReinstanced);
}

void FDreamGUIEditorModule::HandleBlueprintReinstanced()
{
	for (TObjectIterator<UDreamWorldWidgetComponent> It; It; ++It)
	{
		UDreamWorldWidgetComponent* Component = *It;
		if (!IsValid(Component))
		{
			continue;
		}
		// Class-default objects, the skeleton pass's components and whatever the reinstancer has
		// parked are all reachable from the iterator and none of them host anything on screen.
		const FString Name = Component->GetName();
		if (Name.Contains(TEXT("SKEL_")) || Name.Contains(TEXT("REINST_")) || Name.Contains(TEXT("TRASH_")))
		{
			continue;
		}
		// Editor worlds only. A PIE or game world tears its widgets down on EndPlay and builds them
		// again on the next BeginPlay, and reloading underneath a running game would restart
		// animations and lose whatever state the widget holds. Naming the two editor types rather
		// than excluding the game ones also drops Inactive -- a world the editor is holding but not
		// showing, whose components have nothing to reload into.
		const UWorld* World = Component->GetWorld();
		if (World == nullptr || World->IsGameWorld()
			|| (World->WorldType != EWorldType::Editor && World->WorldType != EWorldType::EditorPreview))
		{
			continue;
		}
		Component->ReloadWidget();
	}
}

void FDreamGUIEditorModule::OnInitializeSequence(UDreamWidgetAnimation* Sequence)
{
	auto* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FFrameNumber StartFrame = (ProjectSettings->DefaultStartTime * MovieScene->GetTickResolution()).RoundToFrame();
	int32        Duration = (ProjectSettings->DefaultDuration * MovieScene->GetTickResolution()).RoundToFrame().Value;

	MovieScene->SetPlaybackRange(StartFrame, Duration);
}

/** Defined in DesignerEditor/DreamWidgetDesignerDetails.cpp, next to the clipboard it clears. */
void DreamUIWidgetComponentClipboard_Reset();

void FDreamGUIEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	if (PostEngineInitHandle.IsValid())
	{
		FCoreDelegates::GetOnPostEngineInit().Remove(PostEngineInitHandle);
		PostEngineInitHandle.Reset();
	}
	if (GEditor != nullptr && BlueprintReinstancedHandle.IsValid())
	{
		GEditor->OnBlueprintReinstanced().Remove(BlueprintReinstancedHandle);
		BlueprintReinstancedHandle.Reset();
	}
	FDreamUISymbolExport::Unregister();
	FDreamUIBridgeService::Unregister();
	FDreamUIMenus::Unregister();
	FDreamUISourceWatcher::Unregister();
	DreamUITextAuthoring::SetLiteralSpellingProbe(nullptr);
	FDreamUIControlRegistry::Get().ShutdownDynamicDiscovery();
	FDreamUIBehaviourEditorBackendRegistry::Get().UnregisterBuiltInBackends();
	FDreamGUIEditorStyle::Shutdown();
	// The component clipboard parks a UObject in a static. Releasing it at static-teardown time
	// touches an object system that is already gone, so it is released here instead.
	DreamUIWidgetComponentClipboard_Reset();

	FDreamUIEditorCommands::Unregister();
	// Registered lazily by the designer toolkit (DreamWidgetBlueprintEditor). A module only unloads
	// after the AssetEditorSubsystem has closed every toolkit, so the command context comes off here,
	// alongside the other command set -- the same place the stock editor modules unregister theirs.
	FDreamWidgetDesignerCommands::Unregister();

	UDreamWidgetAnimation::OnInitializeSequence().Remove(OnInitializeSequenceHandle);
	ISequencerModule* SequencerModule = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnregisterSequenceEditor(SequenceEditorHandle);
		SequencerModule->UnRegisterTrackEditor(DreamUIMaterialTrackEditorCreateTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(DreamUIAnimEventTrackEditorCreateTrackEditorHandle);
		SequencerModule->UnRegisterTrackEditor(DreamUISequenceTrackEditorCreateTrackEditorHandle);
	}
	
	//unregister window
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DreamUIDynamicSpriteAtlasViewerTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DreamUIWidgetInspectorTabName);
	}
	//unregister custom editor
	if (UObjectInitialized() && FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(UDreamWidget::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamVisual::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamVisualBatchMesh::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamSpriteBase::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamSprite::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamCanvas::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamText::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamTextureBase::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamRectBlock::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamTexture::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamVisualPostProcess::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamUISpriteData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIStaticSpriteAtlasData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIFontData_FreeTypeRender::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISelectable::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIToggle::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUITextInput::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIScrollViewWithScrollbar::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIControl::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamUserWidget::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIBehaviour::StaticClass()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(UDreamUIFontEmojiData::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate::StaticStruct()->GetFName());
		//PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamGUIEventDelegateTwoParam::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Empty::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Bool::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Float::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Double::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Int64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_UInt64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector2::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector3::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Vector4::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Color::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_LinearColor::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Quaternion::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_String::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Asset::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_DreamWidget::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_PointerEvent::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Class::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Rotator::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Text::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIEventDelegate_Name::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIComponentReference::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamWidgetAnimationComponent::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIImageBrush::StaticStruct()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(UDreamLayoutContainer::StaticClass()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(UDreamWidgetPresenterComponentBase::StaticClass()->GetFName());
	}
	//unregister asset
	{
		if (FModuleManager::Get().IsModuleLoaded(TEXT("AssetTools")))
		{
			IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
			for (TSharedPtr<FAssetTypeActions_Base>& AssetTypeActions : AssetTypeActionsArray)
			{
				AssetTools.UnregisterAssetTypeActions(AssetTypeActions.ToSharedRef());
			}
		}
		AssetTypeActionsArray.Empty();
	}
	//unregister thumbnail
	if (UObjectInitialized())
	{
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamUISpriteData::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamUISpriteData_BaseObject::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamWidgetBlueprint::StaticClass());
	}
	//unregister right mouse button in content browser
	{
		FDreamUIContentBrowserExtensions::RemoveHooks();
	}

	//unregister setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			// Container/category/section have to be spelled exactly as they were registered above --
			// UnregisterSettings looks the section up by that triple and silently does nothing when it
			// misses, which is why every one of these outlived the module.
			SettingsModule->UnregisterSettings("Project", DREAM_PLUGIN, "DreamGUI");
			SettingsModule->UnregisterSettings("Project", DREAM_PLUGIN, "DreamGUI Editor");
			SettingsModule->UnregisterSettings("Editor", "ContentEditors", "EmbeddedDreamWidgetAnimationEditor");
		}
	}

	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	// The designer viewport toolbar registers itself into the process-wide UToolMenus registry the
	// first time a designer opens one, and the registry outlives this module. Left behind, the
	// entries keep delegates bound into code that is being unloaded -- and on the next load the
	// IsMenuRegistered guard sees the stale menu and never rebuilds it, so the toolbar comes back
	// empty. Nothing to do when no designer was ever opened; RemoveMenu on an unregistered name is
	// a no-op.
	if (UObjectInitialized() && UToolMenus::IsToolMenuUIEnabled())
	{
		UToolMenus::Get()->RemoveMenu(SDreamWidgetDesignerViewportToolbar::GetViewportToolbarMenuName());
	}

	// USelection::SelectionChangedEvent.RemoveAll(this) used to be here. StartupModule never
	// subscribed to it, so it unhooked nothing and read as though the module listened to the level
	// editor's selection -- which it does not; the designer's selection is UDreamUISelection's.
	//
	// FKismetCompilerContext::RegisterCompilerForBP (StartupModule) deliberately has no counterpart,
	// and this is not an oversight left standing. UMG does the identical thing -- UMGEditorModule.cpp
	// registers UWidgetBlueprint::GetCompilerForWidgetBP on startup and never takes it out -- because
	// the engine exposes no UnregisterCompilerForBP: CustomCompilerMap is a translation-unit global
	// in KismetCompiler.cpp reachable only through that one Add. What the entry would outlive is its
	// own key: the map is keyed by UDreamWidgetBlueprint::StaticClass(), a UClass this module owns, so
	// an unloaded module leaves an entry nothing can look up. The removable registration UMG does have
	// is the IBlueprintCompiler in IKismetCompilerInterface::GetCompilers(), which this plugin does
	// not use -- its compiler context is reached through the factory above.
}

void FDreamGUIEditorModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DreamWidgetAnimationSequencerSettings);
}
FString FDreamGUIEditorModule::GetReferencerName() const 
{
	return "DreamGUIEditorModule";
}

FDreamGUIEditorModule& FDreamGUIEditorModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FDreamGUIEditorModule>(TEXT("DreamGUIEditor"));
}

TSharedRef<SDockTab> FDreamGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SDreamUIDynamicSpriteAtlasViewer, ResultTab);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

TSharedRef<SDockTab> FDreamGUIEditorModule::HandleSpawnDreamUIInspectorTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SDreamUIWidgetInspector, ResultTab);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

TSharedRef<SWidget> FDreamGUIEditorModule::MakeEditorToolsMenu(TFunction<UDreamWidget*()> GetSelectedWidgetFunction, TFunction<void(FMenuBuilder&)> ExtendEditMenuFunction)
{
	FMenuBuilder MenuBuilder(true, PluginCommands);

	MenuBuilder.BeginSection("DreamUI Widget", LOCTEXT("DreamUI Widget", "DreamUI Widget Operations"));
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIElementSubMenu", "Create UI Element"),
			LOCTEXT("CreateUIElementSubMenu_Tooltip", "Create UI Element"),
			FNewMenuDelegate::CreateRaw(this, &FDreamGUIEditorModule::CreateUIElementSubMenu, GetSelectedWidgetFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCreateWidget, GetSelectedWidgetFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIExtensionSubMenu", "Create UI Extension Element"),
			LOCTEXT("CreateUIExtensionSubMenu_Tooltip", "Create UI Extension Element"),
			FNewMenuDelegate::CreateRaw(this, &FDreamGUIEditorModule::CreateUIExtensionSubMenu, GetSelectedWidgetFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCreateWidget, GetSelectedWidgetFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIPostProcessSubMenu", "Create UI Post Process"),
			LOCTEXT("CreateUIPostProcessSubMenu_Tooltip", "Create UI Post Process"),
			FNewMenuDelegate::CreateRaw(this, &FDreamGUIEditorModule::CreateUIPostProcessSubMenu, GetSelectedWidgetFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCreateWidget, GetSelectedWidgetFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
	}
	MenuBuilder.EndSection();

	if (ExtendEditMenuFunction != nullptr)
	{
		ExtendEditMenuFunction(MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}

void FDreamGUIEditorModule::CreateUIElementSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	// The categorised loop at the bottom of this function reads the registry, so a control class
	// authored as a Blueprint and compiled during this session has to be rescanned for first -- the
	// Post Process submenu already did, and this one did not, which is why the same new class
	// appeared in one menu and not the other.
	FDreamUIControlRegistry::Get().RefreshDynamicClasses();
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction, FString Name, UClass* InVisualClass, TFunction<void(UDreamWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : UDreamWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreateWidget, GetSelectedWidgetFunction, Name, InVisualClass, Callback))
			);
		}
		static void CreateUIControlMenuEntry(FMenuBuilder& InBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction, const FDreamUIControlDescriptor& Descriptor)
		{
			FText ValidationError;
			const bool bValid = FDreamUIControlRegistry::Get().Validate(Descriptor, ValidationError);
			const FText Tooltip = bValid
				? FText::Format(LOCTEXT("CreateUIElementTitle", "Create {0}"), Descriptor.DisplayName)
				: ValidationError;
			InBuilder.AddMenuEntry(
				Descriptor.DisplayName,
				Tooltip,
				FSlateIcon(),
				FUIAction(
					FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreateRegisteredControl, GetSelectedWidgetFunction, Descriptor.Name),
					FCanExecuteAction::CreateLambda([bValid]() { return bValid; }))
			);
		}
	};

	MenuBuilder.BeginSection("UIElement");
	{
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedWidgetFunction, "Widget", nullptr, nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedWidgetFunction, "Text", UDreamText::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedWidgetFunction, "Image", UDreamImage::StaticClass(), [](UDreamWidget* InWidget)
		{
			if (auto Image = Cast<UDreamImage>(InWidget->GetVisual()))
			{
				Image->SetBrush_DreamUISprite(UDreamUISpriteData::GetDefaultFrameRect());
			}
		});
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedWidgetFunction, "RectBlock", UDreamRectBlock::StaticClass(), nullptr);

	}
	MenuBuilder.EndSection();

	TArray<FName> AddedCategories;
	for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
	{
		if (!AddedCategories.Contains(Descriptor.Category))
		{
			AddedCategories.Add(Descriptor.Category);
		}
	}
	for (FName Category : AddedCategories)
	{
		MenuBuilder.BeginSection(Category, FText::FromName(Category));
		for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
		{
			if (Descriptor.Category == Category)
			{
				FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedWidgetFunction, Descriptor);
			}
		}
		MenuBuilder.EndSection();
	}
}

const FSlateBrush* FDreamGUIEditorModule::GetInteractionIconBrush(UDreamWidget* Widget)
{
	if (!IsValid(Widget))return nullptr;
					
#define RETURN_BRUSH(Class)\
if (Widget->GetComponent<Class>())\
{\
return FSlateIconFinder::FindIconBrushForClass(Class::StaticClass());\
}
	RETURN_BRUSH(UUITreeView);
	RETURN_BRUSH(UUITileView);
	RETURN_BRUSH(UUIListView);
	RETURN_BRUSH(UUIProgressBar);
	RETURN_BRUSH(UDreamNamedSlotHost);
	RETURN_BRUSH(UDreamContentWidget);
	RETURN_BRUSH(UUITextInput);
	RETURN_BRUSH(UUIButton);
	RETURN_BRUSH(UUIToggle);
	RETURN_BRUSH(UUIToggleGroup);
	RETURN_BRUSH(UUISlider);
	RETURN_BRUSH(UUIScrollbar);
	RETURN_BRUSH(UUIDropdown);
	RETURN_BRUSH(UUIScrollView);
#undef RETURN_BRUSH
	return nullptr;
}

const FSlateBrush* FDreamGUIEditorModule::GetWidgetIconBrush(UDreamWidget* Widget)
{
	if (!IsValid(Widget))return nullptr;
	if (const FSlateBrush* InteractionIcon = GetInteractionIconBrush(Widget))
	{
		return InteractionIcon;
	}
	if (UDreamLayoutContainer* LayoutContainer = Widget->GetLayoutContainer())
	{
		return FSlateIconFinder::FindIconBrushForClass(LayoutContainer->GetClass());
	}
	if (UDreamVisual* Visual = Widget->GetVisual())
	{
		return FSlateIconFinder::FindIconBrushForClass(Visual->GetClass());
	}
	if (UDreamLayoutSelf* LayoutSelf = Widget->GetLayoutSelf())
	{
		return FSlateIconFinder::FindIconBrushForClass(LayoutSelf->GetClass());
	}
	return FSlateIconFinder::FindIconBrushForClass(UDreamWidget::StaticClass());
}

bool FDreamGUIEditorModule::IsValidClassName(const FString& InName)
{
	return 
		!InName.StartsWith(TEXT("SKEL_"))
		&& !InName.StartsWith(TEXT("REINST_"))
		&& !InName.Contains(TEXT("TRASH_"))
		&& !InName.Contains(TEXT("_DEPRECATED"))
		;
}

void FDreamGUIEditorModule::CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	FDreamUIControlRegistry::Get().RefreshDynamicClasses();
	MenuBuilder.BeginSection("UIPostProcess");
	{
		for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
		{
			if (Descriptor.Category != TEXT("Post Process"))
			{
				continue;
			}
			FText ValidationError;
			const bool bValid = FDreamUIControlRegistry::Get().Validate(Descriptor, ValidationError);
			MenuBuilder.AddMenuEntry(
				Descriptor.DisplayName,
				bValid && Descriptor.VisualClass.IsValid() ? Descriptor.VisualClass->GetToolTipText() : ValidationError,
				Descriptor.Icon,
				FUIAction(
					FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreateRegisteredControl, GetSelectedWidgetFunction, Descriptor.Name),
					FCanExecuteAction::CreateLambda([bValid]() { return bValid; })));
		}
	}
	MenuBuilder.EndSection();
}

void FDreamGUIEditorModule::CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	// Same first line as the Post Process submenu below, for the same reason: a Blueprint subclass
	// that the author compiled since this editor started is only in the registry once the dynamic
	// scan has run, and a menu built without it silently offers the built-ins alone.
	FDreamUIControlRegistry::Get().RefreshDynamicClasses();
	MenuBuilder.BeginSection("UIExtension");
	{
		for (const FDreamUIControlDescriptor& Descriptor : FDreamUIControlRegistry::Get().GetDescriptors())
		{
			if (Descriptor.Category != TEXT("Extensions"))
			{
				continue;
			}
			FText ValidationError;
			const bool bValid = FDreamUIControlRegistry::Get().Validate(Descriptor, ValidationError);
			MenuBuilder.AddMenuEntry(
				Descriptor.DisplayName,
				bValid && Descriptor.VisualClass.IsValid() ? Descriptor.VisualClass->GetToolTipText() : ValidationError,
				Descriptor.Icon,
				FUIAction(
					FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreateRegisteredControl, GetSelectedWidgetFunction, Descriptor.Name),
					FCanExecuteAction::CreateLambda([bValid]() { return bValid; })));
		}
	}
	MenuBuilder.EndSection();
}

IMPLEMENT_MODULE(FDreamGUIEditorModule, DreamGUIEditor)

#undef LOCTEXT_NAMESPACE
