// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "DreamGUIEditorModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

#include "ISettingsModule.h"

#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "DreamGUIEditorStyle.h"
#include "DreamUIEditorCommands.h"
#include "DreamUIEditorTools.h"
#include "DreamUIControlRegistry.h"
#include "DreamUIBehaviourEditorBackend.h"

#include "Thumbnail/DreamUIPrefabThumbnailRenderer.h"
#include "Thumbnail/DreamUISpriteThumbnailRenderer.h"
#include "Thumbnail/DreamUISpriteDataBaseObjectThumbnailRenderer.h"
#include "ContentBrowserExtensions/DreamUIContentBrowserExtensions.h"
#include "Window/DreamUIDynamicSpriteAtlasViewer.h"

#include "AssetTypeActions/AssetTypeActions_DreamUISpriteData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIStaticSpriteAtlasData.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIFontData_Bitmap.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIPrefab.h"
#include "AssetTypeActions/AssetTypeActions_DreamUIMLResource.h"
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
#include "DetailCustomization/UISelectableCustomization.h"
#include "DetailCustomization/UIToggleCustomization.h"
#include "DetailCustomization/UITextInputCustomization.h"
#include "DetailCustomization/DreamUIPrefabCustomization.h"
#include "DetailCustomization/DreamUIEventDelegateCustomization.h"
#include "DetailCustomization/DreamUIComponentReferenceCustomization.h"
#include "DetailCustomization/UIScrollViewWithScrollBarCustomization.h"
#include "DetailCustomization/UISpriteSequencePlayerCustomization.h"
#include "DetailCustomization/UISpriteSheetTexturePlayerCustomization.h"
#include "DetailCustomization/DreamVisualPostProcessCustomization.h"

#include "PrefabEditor/DreamUIPrefabOverrideDataViewer.h"
#include "Engine/Selection.h"

#include "PrefabAnimation/DreamUIPrefabSequenceComponentCustomization.h"
#include "PrefabAnimation/MovieSceneSequenceEditor_DreamUIPrefabSequence.h"
#include "SequencerSettings.h"
#include "ISequencerModule.h"
#include "DreamUIComponentReference.h"
#include "MovieSceneToolsProjectSettings.h"
#include "PrefabAnimation/DreamUIMaterialTrackEditor.h"
#include "PrefabAnimation/DreamUIPrefabSequencerSettings.h"

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
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "DetailCustomization/DreamImageBrushStructCustomization.h"
#include "DetailCustomization/DreamLayoutContainerCustomization.h"
#include "DetailCustomization/DreamUIEventDelegatePresetParamCustomization.h"
#include "DetailCustomization/DreamUIFontEmojiDataCustomization.h"
#include "DetailCustomization/DreamWidgetPresenterBaseCustomization.h"
#include "Event/DreamUIEventDelegate_PresetParameter.h"
#include "Event/DreamWorldSpaceRaycasterBase.h"
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
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/DreamUIPrefabSequenceComponent.h"
#include "PrefabSystem/DreamUIPrefab.h"
#include "Styling/SlateIconFinder.h"
#include "Window/DreamUIWidgetInspector.h"

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
	FDreamUIBehaviourEditorBackendRegistry::Get().RegisterBuiltInBackends();

	OnInitializeSequenceHandle = UDreamUIPrefabSequence::OnInitializeSequence().AddStatic(FDreamGUIEditorModule::OnInitializeSequence);

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(UDreamUIPrefabSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_DreamUIPrefabSequence>());
	DreamUIMaterialTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FDreamUIMaterialTrackEditor::CreateTrackEditor));

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
		
		PropertyModule.RegisterCustomClassLayout(UDreamUIPrefab::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIPrefabCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSequencePlayerCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSheetTexturePlayerCustomization::MakeInstance));

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

		PropertyModule.RegisterCustomClassLayout(UDreamUIPrefabSequenceComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FDreamUIPrefabSequenceComponentCustomization::MakeInstance));
		
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
		TSharedPtr<FAssetTypeActions_Base> PrefabDataAction = MakeShareable(new FAssetTypeActions_DreamUIPrefab(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> UIStaticMeshCacheDataAction = MakeShareable(new FAssetTypeActions_DreamUIStaticMeshCache(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextCustomStyleDataAction = MakeShareable(new FAssetTypeActions_DreamUIRichTextCustomStyleData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextImageDataAction = MakeShareable(new FAssetTypeActions_DreamUIRichTextImageData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> FontEmojiDataAction = MakeShareable(new FAssetTypeActions_DreamUIFontEmojiData(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> DistanceFieldFontDataTypeAction = MakeShareable(new FAssetTypeActions_DreamUIFontData_DistanceField(DreamUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> UIMLResourceAction = MakeShareable(new FAssetTypeActions_DreamUIMLResource(DreamUIAssetCategoryBit));
		AssetTools.RegisterAssetTypeActions(SpriteDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(StaticSpriteAtlasDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(BitmapFontDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(PrefabDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(UIStaticMeshCacheDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextCustomStyleDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextImageDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(FontEmojiDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(DistanceFieldFontDataTypeAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(UIMLResourceAction.ToSharedRef());
		AssetTypeActionsArray.Add(SpriteDataAction);
		AssetTypeActionsArray.Add(StaticSpriteAtlasDataAction);
		AssetTypeActionsArray.Add(BitmapFontDataAction);
		AssetTypeActionsArray.Add(PrefabDataAction);
		AssetTypeActionsArray.Add(UIStaticMeshCacheDataAction);
		AssetTypeActionsArray.Add(RichTextCustomStyleDataAction);
		AssetTypeActionsArray.Add(RichTextImageDataAction);
		AssetTypeActionsArray.Add(FontEmojiDataAction);
		AssetTypeActionsArray.Add(DistanceFieldFontDataTypeAction);
		AssetTypeActionsArray.Add(UIMLResourceAction);
	}
	//register Thumbnail
	{
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamUIPrefab::StaticClass(), UDreamUIPrefabThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamUISpriteData::StaticClass(), UDreamUISpriteThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(UDreamUISpriteData_BaseObject::StaticClass(), UDreamUISpriteDataBaseObjectThumbnailRenderer::StaticClass());
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
#define DREAM_PLUGIN "DreamPlugin"
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

			DreamUIPrefabSequencerSettings = USequencerSettingsContainer::GetOrCreate<UDreamUIPrefabSequencerSettings>(TEXT("EmbeddedDreamUIPrefabSequenceEditor"));
			SettingsModule->RegisterSettings("Editor", "ContentEditors", "EmbeddedDreamUIPrefabSequenceEditor",
				LOCTEXT("DreamUIPrefabSequencerSettingsName", "DreamGUI Prefab Sequence Editor"),
				LOCTEXT("DreamUIPrefabSequencerSettingsDescription", "Configure the look and feel of the DreamGUI Prefab Sequence Editor."),
				DreamUIPrefabSequencerSettings);
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

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveInit"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveGenerateRay"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveShouldStartDrag"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualBatchMesh::StaticClass(), TEXT("ReceiveOnBeforeCreateOrUpdateGeometry"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamVisualBatchMesh::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamSpriteBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamTextureBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamMeshModifierBase::StaticClass(), TEXT("ReceiveModifyUIGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UDreamLayoutAnimation::StaticClass(), TEXT("ReceiveOnApplyLayoutResults"));
	}
}

void FDreamGUIEditorModule::OnInitializeSequence(UDreamUIPrefabSequence* Sequence)
{
	auto* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FFrameNumber StartFrame = (ProjectSettings->DefaultStartTime * MovieScene->GetTickResolution()).RoundToFrame();
	int32        Duration = (ProjectSettings->DefaultDuration * MovieScene->GetTickResolution()).RoundToFrame().Value;

	MovieScene->SetPlaybackRange(StartFrame, Duration);
}

/** Defined in PrefabEditor/DreamUIPrefabEditorDetails.cpp, next to the clipboard it clears. */
void DreamUIWidgetComponentClipboard_Reset();

void FDreamGUIEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FDreamUIControlRegistry::Get().ShutdownDynamicDiscovery();
	FDreamUIBehaviourEditorBackendRegistry::Get().UnregisterBuiltInBackends();
	FDreamGUIEditorStyle::Shutdown();
	// The component clipboard parks a UObject in a static. Releasing it at static-teardown time
	// touches an object system that is already gone, so it is released here instead.
	DreamUIWidgetComponentClipboard_Reset();

	FDreamUIEditorCommands::Unregister();

	UDreamUIPrefabSequence::OnInitializeSequence().Remove(OnInitializeSequenceHandle);
	ISequencerModule* SequencerModule = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnregisterSequenceEditor(SequenceEditorHandle);
		SequencerModule->UnRegisterTrackEditor(DreamUIMaterialTrackEditorCreateTrackEditorHandle);
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
		PropertyModule.UnregisterCustomClassLayout(UDreamVisualPostProcess::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamUISpriteData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIStaticSpriteAtlasData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIFontData_FreeTypeRender::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISelectable::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIToggle::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUITextInput::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIScrollViewWithScrollbar::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UDreamUIPrefab::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamMeshModifierTextAnimation_Property::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName());
		
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

		PropertyModule.UnregisterCustomPropertyTypeLayout(FDreamUIComponentReference::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UDreamUIPrefabSequenceComponent::StaticClass()->GetFName());

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
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamUIPrefab::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamUISpriteData::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(UDreamUISpriteData_BaseObject::StaticClass());
	}
	//unregister right mouse button in content browser
	{
		FDreamUIContentBrowserExtensions::RemoveHooks();
	}

	//unregister setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "DreamUI");
			SettingsModule->UnregisterSettings("Project", "Plugins", "DreamUI Editor");
			SettingsModule->UnregisterSettings("Project", "Plugins", "DreamUI Prefab");
			SettingsModule->UnregisterSettings("Project", "Plugins", "DreamUIPrefabSequencerSettings");
		}
	}

	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	USelection::SelectionChangedEvent.RemoveAll(this);
}

void FDreamGUIEditorModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(DreamUIPrefabSequencerSettings);
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

	//prefab
	{
		MenuBuilder.BeginSection("Prefab", LOCTEXT("Prefab", "Prefab"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreatePrefab", "Create Prefab"),
				LOCTEXT("CreatePrefab_Tooltip", "Use selected Widget to create a new prefab"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreatePrefabAsset, GetSelectedWidgetFunction)
					, FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanCreatePrefab, GetSelectedWidgetFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCreatePrefab, GetSelectedWidgetFunction))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UnpackPrefab", "Unpack this Prefab"),
				LOCTEXT("UnpackPrefab_Tooltip", "Unpack the Widget from related prefab asset"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::UnpackPrefab, GetSelectedWidgetFunction)
					, FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanUnpackWidgetForPrefab, GetSelectedWidgetFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanUnpackWidgetForPrefab, GetSelectedWidgetFunction))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("SelectPrefabAsset", "Browse to Prefab asset"),
				LOCTEXT("SelectPrefabAsset_Tooltip", "Browse to Prefab asset in Content Browser"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::SelectPrefabAsset, GetSelectedWidgetFunction)
					, FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanBrowsePrefabAsset, GetSelectedWidgetFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanBrowsePrefabAsset, GetSelectedWidgetFunction))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenPrefabAsset", "Open Prefab asset"),
				LOCTEXT("OpenPrefabAsset_Tooltip", "Open Prefab asset in PrefabEditor"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::OpenPrefabAsset, GetSelectedWidgetFunction)
					, FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanBrowsePrefabAsset, GetSelectedWidgetFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanBrowsePrefabAsset, GetSelectedWidgetFunction))
			);
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction()
					, FCanExecuteAction::CreateStatic(&FDreamUIEditorTools::CanCheckPrefabOverrideParameter, GetSelectedWidgetFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCheckPrefabOverrideParameter, GetSelectedWidgetFunction))
				, 
				SNew(SComboButton)
				.HasDownArrow(true)
				.ToolTipText(LOCTEXT("PrefabOverride", "Edit override parameters for this prefab"))
				.ButtonContent()
				[
					SNew(STextBlock)
					.Text(LOCTEXT("OverrideButton", "Prefab Override Properties"))
					.Font(IDetailLayoutBuilder::GetDetailFont())
				]
				.MenuContent()
				[
					SNew(SBox)
					.Padding(FMargin(4, 4))
					[
						SNew(SHorizontalBox)
						+SHorizontalBox::Slot()
						.AutoWidth()
						[
							SNew(SVerticalBox)
							+SVerticalBox::Slot()
							.AutoHeight()
							[
								SNew(SHorizontalBox)
								+SHorizontalBox::Slot()
								.AutoWidth()
								[
									SNew(SDreamUIPrefabOverrideDataViewer, GetSelectedWidgetFunction)
									.AfterRevertPrefab_Lambda([=, this](UDreamUIPrefab* PrefabAsset) {
										})
									.AfterApplyPrefab_Lambda([=, this](UDreamUIPrefab* PrefabAsset) {
										FDreamUIEditorTools::RefreshLoadedPrefab();
										FDreamUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
										FDreamUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
										})
								]
							]
						]
					]
				]
			);
		}
		MenuBuilder.EndSection();
	}

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
		CreateExtraPrefabsSubMenu(MenuBuilder, GetSelectedWidgetFunction);
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

void FDreamGUIEditorModule::CreateExtraPrefabsSubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction)
{
	struct LOCAL
	{
		static void CreateExtraPrefab_SubMenu(FMenuBuilder& MenuBuilder, TFunction<UDreamWidget*()> GetSelectedWidgetFunction, TArray<UDreamUIPrefab*> InPrefabArray)
		{
			for (auto Prefab : InPrefabArray)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(FPaths::GetBaseFilename(Prefab->GetPathName())),
					FText::FromString(Prefab->GetPathName()),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FDreamUIEditorTools::CreateUIControls, GetSelectedWidgetFunction, Prefab->GetPathName()))
				);
			}
		}
	};

	auto PrefabFolders = GetDefault<UDreamUIEditorSettings>()->ExtraPrefabFolders;
	for (auto PrefabFolder : PrefabFolders)
	{
		FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(FName("AssetRegistry"));
		IAssetRegistry& AssetRegistry = AssetRegistryModule.Get();
		// Need to do this if running in the editor with -game to make sure that the assets in the following path are available
		TArray<FString> PathsToScan;
		PathsToScan.Add(TEXT("/Game/"));
		AssetRegistry.ScanPathsSynchronous(PathsToScan);

		TArray<FAssetData> ScriptAssetList;
		AssetRegistry.GetAssetsByPath(FName(*PrefabFolder.Path), ScriptAssetList, false);
		TArray<UDreamUIPrefab*> PrefabAssets;
		auto PrefabClassName = UDreamUIPrefab::StaticClass()->GetClassPathName();
		for (auto Asset : ScriptAssetList)
		{
			if (Asset.AssetClassPath == PrefabClassName)
			{
				auto AssetObject = Asset.GetAsset();
				if (auto Prefab = Cast<UDreamUIPrefab>(AssetObject))
				{
					PrefabAssets.Add(Prefab);
				}
			}
		}

		if(PrefabAssets.Num() > 0)
		{
			MenuBuilder.AddSubMenu(
				FText::Format(LOCTEXT("CreateExtra", "CreateExtra {0}"), FText::FromString(PrefabFolder.Path)),
				FText::Format(LOCTEXT("CreateExtra_Tooltip", "CreateExtra prefab from folder {0}"), FText::FromString(PrefabFolder.Path)),
				FNewMenuDelegate::CreateStatic(&LOCAL::CreateExtraPrefab_SubMenu, GetSelectedWidgetFunction, PrefabAssets),
				FUIAction(FExecuteAction()
					, FCanExecuteAction()
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FDreamUIEditorTools::CanCreateWidget, GetSelectedWidgetFunction)),
				NAME_None, EUserInterfaceActionType::None
			);
		}
	}
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
