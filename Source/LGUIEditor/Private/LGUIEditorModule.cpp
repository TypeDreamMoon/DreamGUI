// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIEditorModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

#include "ISettingsModule.h"

#include "AssetToolsModule.h"
#include "DetailLayoutBuilder.h"
#include "SceneView.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Engine/CollisionProfile.h"

#include "LGUIEditorStyle.h"
#include "LexUIEditorCommands.h"
#include "LexUIEditorTools.h"

#include "Thumbnail/LexUIPrefabThumbnailRenderer.h"
#include "Thumbnail/LexUISpriteThumbnailRenderer.h"
#include "Thumbnail/LexUISpriteDataBaseObjectThumbnailRenderer.h"
#include "ContentBrowserExtensions/LexUIContentBrowserExtensions.h"
#include "Window/LexUIDynamicSpriteAtlasViewer.h"

#include "AssetTypeActions/AssetTypeActions_LexUISpriteData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIStaticSpriteAtlasData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIFontData_Bitmap.h"
#include "AssetTypeActions/AssetTypeActions_LexUIPrefab.h"
#include "AssetTypeActions/AssetTypeActions_LexUIStaticMeshCache.h"
#include "AssetTypeActions/AssetTypeActions_LexUIRichTextCustomStyleData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIRichTextImageData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIFontData_DistanceField.h"

#include "DetailCustomization/LexWidgetCustomization.h"
#include "DetailCustomization/LexVisualCustomization.h"
#include "DetailCustomization/LexVisualBatchMeshCustomization.h"
#include "DetailCustomization/LexSpriteBaseCustomization.h"
#include "DetailCustomization/LexSpriteCustomization.h"
#include "DetailCustomization/LexTextureCustomization.h"
#include "DetailCustomization/LexCanvasCustomization.h"
#include "DetailCustomization/LexTextCustomization.h"
#include "DetailCustomization/LexTextureBaseCustomization.h"
#include "DetailCustomization/LexRectBlockCustomization.h"
#include "DetailCustomization/LexUISpriteDataCustomization.h"
#include "DetailCustomization/LexUIStaticSpriteAtlasDataCustomization.h"
#include "DetailCustomization/LexUIFontData_FreeTypeRenderCustomization.h"
#include "DetailCustomization/UISelectableCustomization.h"
#include "DetailCustomization/UIToggleCustomization.h"
#include "DetailCustomization/UITextInputCustomization.h"
#include "DetailCustomization/LexUIPrefabCustomization.h"
#include "DetailCustomization/LexUIEventDelegateCustomization.h"
#include "DetailCustomization/LexUIComponentReferenceCustomization.h"
#include "DetailCustomization/UIScrollViewWithScrollBarCustomization.h"
#include "DetailCustomization/UISpriteSequencePlayerCustomization.h"
#include "DetailCustomization/UISpriteSheetTexturePlayerCustomization.h"
#include "DetailCustomization/LexVisualPostProcessCustomization.h"

#include "PrefabEditor/LexUIPrefabOverrideDataViewer.h"
#include "Engine/Selection.h"

#include "PrefabAnimation/LexUIPrefabSequenceComponentCustomization.h"
#include "PrefabAnimation/MovieSceneSequenceEditor_LexUIPrefabSequence.h"
#include "SequencerSettings.h"
#include "ISequencerModule.h"
#include "LexUIComponentReference.h"
#include "PrefabAnimation/LexUIPrefabSequenceEditor.h"
#include "MovieSceneToolsProjectSettings.h"
#include "UMGStyle.h"
#include "PrefabAnimation/LexUIMaterialTrackEditor.h"
#include "PrefabAnimation/LexUIPrefabSequencerSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "AssetTypeActions/AssetTypeActions_LexUIFontEmojiData.h"
#include "Core/LexUIFontData_FreeTypeRender.h"
#include "Core/LexUIFontEmojiData.h"
#include "Core/LexUIImageBrush.h"
#include "Core/LexUIStaticSpriteAtlasData.h"
#include "Core/Components/LexCanvas.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexLayoutSelfFlexBox.h"
#include "Core/Components/LexLayoutContainerFlexBox.h"
#include "Core/Components/LexRectBlock.h"
#include "Core/Components/LexSprite.h"
#include "Core/Components/LexSpriteBase.h"
#include "Core/Components/LexText.h"
#include "Core/Components/LexTexture.h"
#include "Core/Components/LexTextureBase.h"
#include "Core/Components/LexVisualPostProcess.h"
#include "DetailCustomization/LexImageBrushStructCustomization.h"
#include "DetailCustomization/LexLayoutContainerCustomization.h"
#include "DetailCustomization/LexLayoutSelfFlexBoxCustomization.h"
#include "DetailCustomization/LexLayoutContainerFlexBoxCustomization.h"
#include "DetailCustomization/LexUIEventDelegatePresetParamCustomization.h"
#include "DetailCustomization/LexUIFontEmojiDataCustomization.h"
#include "Event/LexUIEventDelegate_PresetParameter.h"
#include "Event/LexWorldSpaceRaycasterBase.h"
#include "Extensions/LexPolygon.h"
#include "Extensions/LexPolygonLine.h"
#include "Extensions/LexRing.h"
#include "Extensions/LexStaticMesh.h"
#include "Extensions/UISpriteSequencePlayer.h"
#include "Extensions/UISpriteSheetTexturePlayer.h"
#include "Extensions/2DLineRenderer/Lex2DLineChildrenAsPoints.h"
#include "Extensions/2DLineRenderer/Lex2DLineRaw.h"
#include "Interaction/UIButtonComponent.h"
#include "Interaction/UIDropdownComponent.h"
#include "Interaction/UIScrollbarComponent.h"
#include "Interaction/UIScrollViewWithScrollbarComponent.h"
#include "Interaction/UISelectableComponent.h"
#include "Interaction/UISliderComponent.h"
#include "Interaction/UITextInputComponent.h"
#include "Interaction/UIToggleComponent.h"
#include "MeshModifier/LexMeshModifierBase.h"
#include "MeshModifier/LexMeshModifierTextAnimation.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequence.h"
#include "PrefabSystem/PrefabAnimation/LexUIPrefabSequenceComponent.h"
#include "PrefabSystem/LexUIPrefab.h"

const FName FLGUIEditorModule::LexUIDynamicSpriteAtlasViewerTabName(TEXT("LexUIDynamicSpriteAtlasViewerName"));
const FName FLGUIEditorModule::LexUIPrefabSequenceTabName(TEXT("LexUIPrefabSequenceTabName"));

#define LOCTEXT_NAMESPACE "FLGUIEditorModule"
DEFINE_LOG_CATEGORY(LGUIEditor);

void FLGUIEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FLGUIEditorStyle::Initialize();
	FLGUIEditorStyle::ReloadTextures();

	OnInitializeSequenceHandle = ULexUIPrefabSequence::OnInitializeSequence().AddStatic(FLGUIEditorModule::OnInitializeSequence);

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(ULexUIPrefabSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_LexUIPrefabSequence>());
	LexUIMaterialTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FLexUIMaterialTrackEditor::CreateTrackEditor));

	FLexUIEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);

	//register window
	{
		//atlas texture viewer
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LexUIDynamicSpriteAtlasViewerTabName, FOnSpawnTab::CreateRaw(this, &FLGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab))
			.SetDisplayName(LOCTEXT("LexUIDynamicSpriteAtlasTextureViewerName", "LexUI Dynamic-Sprite-Atlas Texture Viewer"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LexUIPrefabSequenceTabName, FOnSpawnTab::CreateRaw(this, &FLGUIEditorModule::HandleSpawnLexUIPrefabSequenceTab))
			.SetDisplayName(LOCTEXT("LexUIPrefabSequenceTabName", "LexUI Prefab Sequence"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
	}
	//register custom editor
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.RegisterCustomClassLayout(ULexWidget::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexWidgetCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexVisual::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexVisualCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexVisualBatchMesh::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexVisualBatchMeshCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexSpriteBase::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexSpriteBaseCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexSprite::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexSpriteCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexCanvas::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexCanvasCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexText::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexTextCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexTextureBase::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexTextureBaseCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexRectBlock::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexRectBlockCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexTexture::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexTextureCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexVisualPostProcess::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexVisualPostProcessCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(ULexUISpriteData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUISpriteDataCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexUIStaticSpriteAtlasData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUIStaticSpriteAtlasDataCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexUIFontData_FreeTypeRender::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUIFontData_FreeTypeRenderCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(UUISelectableComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISelectableCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUIToggleComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUIToggleCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUITextInputComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUITextInputCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUIScrollViewWithScrollbarComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUIScrollViewWithScrollBarCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(ULexUIPrefab::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUIPrefabCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSequencePlayerCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSheetTexturePlayerCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(ULexUIFontEmojiData::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUIFontEmojiDataCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexUIEventDelegateCustomization::MakeInstance));
		//PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegateTwoParam::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexUIEventDelegateTwoParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Empty::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Bool::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Float::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Double::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector2::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector3::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector4::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Color::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_LinearColor::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Quaternion::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_String::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Object::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Actor::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_PointerEvent::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Class::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Rotator::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Text::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIEventDelegate_Name::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LexUIEventDelegatePresetParamCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIComponentReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexUIComponentReferenceCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(ULexUIPrefabSequenceComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexUIPrefabSequenceComponentCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIImageBrush::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexImageBrushStructCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(ULexLayoutContainer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutContainerCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexLayoutContainerFlexBox::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutContainerFlexBoxCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexLayoutSelfFlexBox::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutSelfFlexBoxCustomization::MakeInstance));
	}
	//register asset
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools").Get();
		//register AssetCategory
		EAssetTypeCategories::Type LexUIAssetCategoryBit = AssetTools.FindAdvancedAssetCategory(FName(TEXT("LexUI")));
		if (LexUIAssetCategoryBit == EAssetTypeCategories::Misc)
		{
			LexUIAssetCategoryBit = AssetTools.RegisterAdvancedAssetCategory(FName(TEXT("LexUI")), LOCTEXT("LexUIAssetCategory", "LexUI"));
		}

		TSharedPtr<FAssetTypeActions_Base> SpriteDataAction = MakeShareable(new FAssetTypeActions_LexUISpriteData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> StaticSpriteAtlasDataAction = MakeShareable(new FAssetTypeActions_LexUIStaticSpriteAtlasData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> BitmapFontDataAction = MakeShareable(new FAssetTypeActions_LexUIFontData_Bitmap(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> PrefabDataAction = MakeShareable(new FAssetTypeActions_LexUIPrefab(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> UIStaticMeshCacheDataAction = MakeShareable(new FAssetTypeActions_LexUIStaticMeshCache(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextCustomStyleDataAction = MakeShareable(new FAssetTypeActions_LexUIRichTextCustomStyleData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextImageDataAction = MakeShareable(new FAssetTypeActions_LexUIRichTextImageData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> FontEmojiDataAction = MakeShareable(new FAssetTypeActions_LexUIFontEmojiData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> DistanceFieldFontDataTypeAction = MakeShareable(new FAssetTypeActions_LexUIFontData_DistanceField(LexUIAssetCategoryBit));
		AssetTools.RegisterAssetTypeActions(SpriteDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(StaticSpriteAtlasDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(BitmapFontDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(PrefabDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(UIStaticMeshCacheDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextCustomStyleDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextImageDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(FontEmojiDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(DistanceFieldFontDataTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(SpriteDataAction);
		AssetTypeActionsArray.Add(StaticSpriteAtlasDataAction);
		AssetTypeActionsArray.Add(BitmapFontDataAction);
		AssetTypeActionsArray.Add(PrefabDataAction);
		AssetTypeActionsArray.Add(UIStaticMeshCacheDataAction);
		AssetTypeActionsArray.Add(RichTextCustomStyleDataAction);
		AssetTypeActionsArray.Add(RichTextImageDataAction);
		AssetTypeActionsArray.Add(FontEmojiDataAction);
		AssetTypeActionsArray.Add(DistanceFieldFontDataTypeAction);
	}
	//register Thumbnail
	{
		UThumbnailManager::Get().RegisterCustomRenderer(ULexUIPrefab::StaticClass(), ULexUIPrefabThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(ULexUISpriteData::StaticClass(), ULexUISpriteThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(ULexUISpriteData_BaseObject::StaticClass(), ULexUISpriteDataBaseObjectThumbnailRenderer::StaticClass());
	}
	//register right mouse button in content browser
	{
		if (!IsRunningCommandlet())
		{
			FLexUIContentBrowserExtensions::InstallHooks();
		}
	}
	//register setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "LexUI",
				LOCTEXT("LexUISettingsName", "LexUI"),
				LOCTEXT("LexUISettingsDescription", "LexUI Settings"),
				GetMutableDefault<ULexUISettings>());
			SettingsModule->RegisterSettings("Project", "Plugins", "LexUI Editor",
				LOCTEXT("LexUIEditorSettingsName", "LexUI Editor"),
				LOCTEXT("LexUIEditorSettingsDescription", "LexUI Editor Settings"),
				GetMutableDefault<ULexUIEditorSettings>());

			LexUIPrefabSequencerSettings = USequencerSettingsContainer::GetOrCreate<ULexUIPrefabSequencerSettings>(TEXT("EmbeddedLexUIPrefabSequenceEditor"));
			SettingsModule->RegisterSettings("Editor", "ContentEditors", "EmbeddedLexUIPrefabSequenceEditor",
				LOCTEXT("LexUIPrefabSequencerSettingsName", "LexUI Prefab Sequence Editor"),
				LOCTEXT("LexUIPrefabSequencerSettingsDescription", "Configure the look and feel of the LexUI Prefab Sequence Editor."),
				LexUIPrefabSequencerSettings);
		}
	}
	//blueprint
	{
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveAwake"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveStart"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveUpdate"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnNormal"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnHovered"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnPressed"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnDisabled"));
		
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUIToggleTransition::StaticClass(), TEXT("ReceiveToggleOn"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUIToggleTransition::StaticClass(), TEXT("ReceiveToggleOff"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexVisualCustomRaycast::StaticClass(), TEXT("ReceiveRaycast"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexVisualCustomRaycast::StaticClass(), TEXT("ReceiveInit"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveInit"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveGenerateRay"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexWorldSpaceRaycasterSource::StaticClass(), TEXT("ReceiveShouldStartDrag"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexVisualBatchMesh::StaticClass(), TEXT("ReceiveOnBeforeCreateOrUpdateGeometry"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexVisualBatchMesh::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexSpriteBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexTextureBase::StaticClass(), TEXT("ReceiveOnUpdateGeometry"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexMeshModifierBase::StaticClass(), TEXT("ReceiveModifyUIGeometry"));
	}

	InteractableClassIconMap =
		{
		{UUITextInputComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.EditableTextBox")},
		{UUIButtonComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.Button")},
		{UUIToggleComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.CheckBox")},
		{UUISliderComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.Slider")},
		{UUIScrollbarComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.ProgressBar")},
		{UUIDropdownComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.ComboBox")},
		{UUIScrollViewComponent::StaticClass(), FUMGStyle::Get().GetBrush("ClassIcon.ScrollBox")},
		};
}

void FLGUIEditorModule::OnInitializeSequence(ULexUIPrefabSequence* Sequence)
{
	auto* ProjectSettings = GetDefault<UMovieSceneToolsProjectSettings>();
	UMovieScene* MovieScene = Sequence->GetMovieScene();

	FFrameNumber StartFrame = (ProjectSettings->DefaultStartTime * MovieScene->GetTickResolution()).RoundToFrame();
	int32        Duration = (ProjectSettings->DefaultDuration * MovieScene->GetTickResolution()).RoundToFrame().Value;

	MovieScene->SetPlaybackRange(StartFrame, Duration);
}

void FLGUIEditorModule::ShutdownModule()
{
	// This function may be called during shutdown to clean up your module.  For modules that support dynamic reloading,
	// we call this function before unloading the module.
	FLGUIEditorStyle::Shutdown();

	FLexUIEditorCommands::Unregister();

	ULexUIPrefabSequence::OnInitializeSequence().Remove(OnInitializeSequenceHandle);
	ISequencerModule* SequencerModule = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnregisterSequenceEditor(SequenceEditorHandle);
		SequencerModule->UnRegisterTrackEditor(LexUIMaterialTrackEditorCreateTrackEditorHandle);
	}
	
	//unregister window
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LexUIDynamicSpriteAtlasViewerTabName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LexUIPrefabSequenceTabName);
	}
	//unregister custom editor
	if (UObjectInitialized() && FModuleManager::Get().IsModuleLoaded("PropertyEditor"))
	{
		FPropertyEditorModule& PropertyModule = FModuleManager::LoadModuleChecked<FPropertyEditorModule>("PropertyEditor");
		PropertyModule.UnregisterCustomClassLayout(ULexWidget::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexVisual::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexVisualBatchMesh::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexSpriteBase::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexSprite::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexCanvas::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexText::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexTextureBase::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexRectBlock::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexVisualPostProcess::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(ULexUISpriteData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexUIStaticSpriteAtlasData::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexUIFontData_FreeTypeRender::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISelectableComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIToggleComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUITextInputComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUIScrollViewWithScrollbarComponent::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexUIPrefab::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(ULexMeshModifierTextAnimation_Property::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(ULexUIFontEmojiData::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate::StaticStruct()->GetFName());
		//PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegateTwoParam::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Empty::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Bool::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Float::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Double::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Int64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_UInt64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector2::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector3::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Vector4::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Color::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_LinearColor::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Quaternion::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_String::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Object::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Actor::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_PointerEvent::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Class::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIEventDelegate_Rotator::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIComponentReference::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(ULexUIPrefabSequenceComponent::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIImageBrush::StaticStruct()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutContainer::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutContainerFlexBox::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutSelfFlexBox::StaticClass()->GetFName());
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
		UThumbnailManager::Get().UnregisterCustomRenderer(ULexUIPrefab::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(ULexUISpriteData::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(ULexUISpriteData_BaseObject::StaticClass());
	}
	//unregister right mouse button in content browser
	{
		FLexUIContentBrowserExtensions::RemoveHooks();
	}

	//unregister setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "LexUI");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LexUI Editor");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LexUI Prefab");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LexUIPrefabSequencerSettings");
		}
	}

	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	USelection::SelectionChangedEvent.RemoveAll(this);
}

void FLGUIEditorModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(LexUIPrefabSequencerSettings);
}
FString FLGUIEditorModule::GetReferencerName() const 
{
	return "LGUIEditorModule";
}

FLGUIEditorModule& FLGUIEditorModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FLGUIEditorModule>(TEXT("LGUIEditor"));
}

TSharedRef<SDockTab> FLGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SLexUIDynamicSpriteAtlasViewer, ResultTab);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

TSharedRef<SDockTab> FLGUIEditorModule::HandleSpawnLexUIPrefabSequenceTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SLexUIPrefabSequenceEditor);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

TSharedRef<SWidget> FLGUIEditorModule::MakeEditorToolsMenu(TFunction<AActor*()> GetSelectedActorFunction, TFunction<void(FMenuBuilder&)> ExtendEditMenuFunction)
{
	FMenuBuilder MenuBuilder(true, PluginCommands);

	//prefab
	{
		MenuBuilder.BeginSection("Prefab", LOCTEXT("Prefab", "Prefab"));
		{
#if 0//we can create prefab in content browser, so this seems not necessary
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreatePrefab", "Create Prefab"),
				LOCTEXT("CreatePrefab_Tooltip", "Use selected actor to create a new prefab"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreatePrefabAsset, GetSelectedActorFunction)
					, FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCreatePrefab, GetSelectedActorFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCreatePrefab, GetSelectedActorFunction))
			);
#endif
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UnpackPrefab", "Unpack this Prefab"),
				LOCTEXT("UnpackPrefab_Tooltip", "Unpack the actor from related prefab asset"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::UnpackPrefab, GetSelectedActorFunction)
					, FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanUnpackActorForPrefab, GetSelectedActorFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanUnpackActorForPrefab, GetSelectedActorFunction))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("SelectPrefabAsset", "Browse to Prefab asset"),
				LOCTEXT("SelectPrefabAsset_Tooltip", "Browse to Prefab asset in Content Browser"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::SelectPrefabAsset, GetSelectedActorFunction)
					, FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanBrowsePrefabAsset, GetSelectedActorFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanBrowsePrefabAsset, GetSelectedActorFunction))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenPrefabAsset", "Open Prefab asset"),
				LOCTEXT("OpenPrefabAsset_Tooltip", "Open Prefab asset in PrefabEditor"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::OpenPrefabAsset, GetSelectedActorFunction)
					, FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanBrowsePrefabAsset, GetSelectedActorFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanBrowsePrefabAsset, GetSelectedActorFunction))
			);
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction()
					, FCanExecuteAction::CreateStatic(&FLexUIEditorTools::CanCheckPrefabOverrideParameter, GetSelectedActorFunction)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCheckPrefabOverrideParameter, GetSelectedActorFunction))
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
									SNew(SLexUIPrefabOverrideDataViewer, GetSelectedActorFunction)
									.AfterRevertPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset) {
										})
									.AfterApplyPrefab_Lambda([=, this](ULexUIPrefab* PrefabAsset) {
										FLexUIEditorTools::RefreshLevelLoadedPrefab();
										FLexUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
										FLexUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
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

	MenuBuilder.BeginSection("LexUI Actor", LOCTEXT("LexUI Actor", "LexUI Actor Operations"));
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIElementSubMenu", "Create UI Element"),
			LOCTEXT("CreateUIElementSubMenu_Tooltip", "Create UI Element"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIElementSubMenu, GetSelectedActorFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCreateActor, GetSelectedActorFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIExtensionSubMenu", "Create UI Extension Element"),
			LOCTEXT("CreateUIExtensionSubMenu_Tooltip", "Create UI Extension Element"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIExtensionSubMenu, GetSelectedActorFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCreateActor, GetSelectedActorFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIPostProcessSubMenu", "Create UI Post Process"),
			LOCTEXT("CreateUIPostProcessSubMenu_Tooltip", "Create UI Post Process"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIPostProcessSubMenu, GetSelectedActorFunction),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCreateActor, GetSelectedActorFunction)),
			NAME_None, EUserInterfaceActionType::None
		);
		CreateExtraPrefabsSubMenu(MenuBuilder, GetSelectedActorFunction);
	}
	MenuBuilder.EndSection();

	if (ExtendEditMenuFunction != nullptr)
	{
		ExtendEditMenuFunction(MenuBuilder);
	}

	return MenuBuilder.MakeWidget();
}

void FLGUIEditorModule::CreateUIElementSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateLexWidget, GetSelectedActorFunction, Name, InVisualClass, Callback))
			);
		}
		static void CreateUIControlMenuEntry(FMenuBuilder& InBuilder, TFunction<AActor*()> GetSelectedActorFunction, const FString& InControlName, FText InTooltip = FText())
		{
			if (InTooltip.IsEmpty())
			{
				InTooltip = FText::Format(LOCTEXT("CreateUIElementTitle", "Create {0}"), FText::FromString(InControlName));
			}
			InBuilder.AddMenuEntry(
				FText::FromString(InControlName),
				InTooltip,
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateUIControls, GetSelectedActorFunction, FLexUIEditorTools::LexUIPresetPrefabPath + InControlName))
			);
		}
	};

	MenuBuilder.BeginSection("UIElement");
	{
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, "Widget", nullptr, nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, "Text", ULexText::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, "Image", ULexImage::StaticClass(), [](ULexWidget* InWidget)
		{
			if (auto Image = Cast<ULexImage>(InWidget->GetVisual()))
			{
				Image->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
			}
		});
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, "RectBlock", ULexRectBlock::StaticClass(), nullptr);

		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("Button"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("Toggle"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("ToggleGroup"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("HorizontalSlider"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("VerticalSlider"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("HorizontalScrollbar"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("VerticalScrollbar"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("Dropdown"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("TextInput"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("TextInputMultiline"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("HorizontalScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("VerticalScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("HorizontalRecyclableScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, GetSelectedActorFunction, TEXT("VerticalRecyclableScrollView"));
	}
	MenuBuilder.EndSection();
}

const FSlateBrush* FLGUIEditorModule::GetVisualIconBrush(ULexWidget* Widget)
{
	if (!IsValid(Widget))return nullptr;
					
#define RETURN_BRUSH(Class)\
if (Widget->GetOwner()->FindComponentByClass<Class>())\
{\
return *InteractableClassIconMap.Find(Class::StaticClass());\
}
	RETURN_BRUSH(UUITextInputComponent);
	RETURN_BRUSH(UUIButtonComponent);
	RETURN_BRUSH(UUIToggleComponent);
	RETURN_BRUSH(UUISliderComponent);
	RETURN_BRUSH(UUIScrollbarComponent);
	RETURN_BRUSH(UUIDropdownComponent);
	RETURN_BRUSH(UUIScrollViewComponent);
	return nullptr;
}

bool FLGUIEditorModule::IsValidClassName(const FString& InName)
{
	return 
		!InName.StartsWith(TEXT("SKEL_"))
		&& !InName.StartsWith(TEXT("REINST_"))
		&& !InName.Contains(TEXT("TRASH_"))
		&& !InName.Contains(TEXT("_DEPRECATED"))
		;
}

void FLGUIEditorModule::CreateExtraPrefabsSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction)
{
	struct LOCAL
	{
		static void CreateExtraPrefab_SubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction, TArray<ULexUIPrefab*> InPrefabArray)
		{
			for (auto Prefab : InPrefabArray)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(FPaths::GetBaseFilename(Prefab->GetPathName())),
					FText::FromString(Prefab->GetPathName()),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateUIControls, GetSelectedActorFunction, Prefab->GetPathName()))
				);
			}
		}
	};

	auto PrefabFolders = GetDefault<ULexUIEditorSettings>()->ExtraPrefabFolders;
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
		TArray<ULexUIPrefab*> PrefabAssets;
		auto PrefabClassName = ULexUIPrefab::StaticClass()->GetClassPathName();
		for (auto Asset : ScriptAssetList)
		{
			if (Asset.AssetClassPath == PrefabClassName)
			{
				auto AssetObject = Asset.GetAsset();
				if (auto Prefab = Cast<ULexUIPrefab>(AssetObject))
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
				FNewMenuDelegate::CreateStatic(&LOCAL::CreateExtraPrefab_SubMenu, GetSelectedActorFunction, PrefabAssets),
				FUIAction(FExecuteAction()
					, FCanExecuteAction()
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateStatic(&FLexUIEditorTools::CanCreateActor, GetSelectedActorFunction)),
				NAME_None, EUserInterfaceActionType::None
			);
		}
	}
}

void FLGUIEditorModule::CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, TFunction<AActor*()> GetSelectedActorFunction, FString Name, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateLexWidget, GetSelectedActorFunction, Name, InVisualClass, Callback))
			);
		}
	};

	MenuBuilder.BeginSection("UIPostProcess");
	{
		for (TObjectIterator<UClass> ClassItr; ClassItr; ++ClassItr)
		{
			if (ClassItr->IsChildOf(ULexVisualPostProcess::StaticClass()))
			{
				if (
					   !(ClassItr->HasAnyClassFlags(CLASS_Transient))
					&& !(ClassItr->HasAnyClassFlags(CLASS_Abstract))
					&& !(ClassItr->HasAnyClassFlags(CLASS_Deprecated))
					&& !(ClassItr->HasAnyClassFlags(CLASS_NotPlaceable))
					)
				{
					bool isBlueprint = ClassItr->HasAnyClassFlags(CLASS_CompiledFromBlueprint);
					if (isBlueprint)
					{
						if (!IsValidClassName(ClassItr->GetName()))
						{
							continue;
						}
					}
					FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ClassItr->GetName(), *ClassItr, nullptr);
				}
			}
		}
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder, TFunction<AActor*()> GetSelectedActorFunction)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, TFunction<AActor*()> GetSelectedActorFunction, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateLexWidget, GetSelectedActorFunction, InVisualClass->GetName(), InVisualClass, Callback))
			);
		}
		static void CreateMenuEntryByPrefab(FMenuBuilder& InBuilder, TFunction<AActor*()> GetSelectedActorFunction, const FString& InControlName, const FText& InLabel, const FText& InTooltip = FText::GetEmpty())
		{
			InBuilder.AddMenuEntry(
				InLabel,
				InTooltip,
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&FLexUIEditorTools::CreateUIControls, GetSelectedActorFunction, FLexUIEditorTools::LexUIPresetPrefabPath + InControlName))
			);
		}
	};

	MenuBuilder.BeginSection("UIExtension");
	{
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULexPolygon::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULexPolygonLine::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULexRing::StaticClass(), nullptr);
		//FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULexStaticMesh::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULex2DLineRaw::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, GetSelectedActorFunction, ULex2DLineChildrenAsPoints::StaticClass(), nullptr);
		//FunctionContainer::CreateMenuEntryByPrefab(MenuBuilder, TEXT("UIWidget"), LOCTEXT("UIWidget", "UI Widget"), AUIWidgetActor::StaticClass()->GetToolTipText());
		//FunctionContainer::CreateMenuEntryByPrefab(MenuBuilder, TEXT("UIRenderTarget"), LOCTEXT("UIRenderTarget", "UI Render Target"), AUIRenderTargetActor::StaticClass()->GetToolTipText());
	}
	MenuBuilder.EndSection();
}

IMPLEMENT_MODULE(FLGUIEditorModule, LGUIEditor)

#undef LOCTEXT_NAMESPACE