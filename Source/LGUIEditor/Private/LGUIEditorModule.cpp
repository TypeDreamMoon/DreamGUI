// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "LGUIEditorModule.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"

#include "LGUIHeaders.h"

#include "ISettingsModule.h"

#include "SceneOutliner/LGUISceneOutlinerInfoColumn.h"
#include "SceneOutlinerModule.h"
#include "SceneOutlinerPublicTypes.h"
#include "AssetToolsModule.h"
#include "SceneView.h"
#include "Kismet2/KismetEditorUtilities.h"

#include "Engine/CollisionProfile.h"

#include "LGUIEditorStyle.h"
#include "LGUIEditorCommands.h"
#include "LGUIEditorTools.h"

#include "Thumbnail/LGUIPrefabThumbnailRenderer.h"
#include "Thumbnail/LGUISpriteThumbnailRenderer.h"
#include "Thumbnail/LGUISpriteDataBaseObjectThumbnailRenderer.h"
#include "ContentBrowserExtensions/LexUIContentBrowserExtensions.h"
#include "LevelEditorMenuExtensions/LGUILevelEditorExtensions.h"
#include "Window/LGUIDynamicSpriteAtlasViewer.h"

#include "AssetTypeActions/AssetTypeActions_LexUISpriteData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIStaticSpriteAtlasData.h"
#include "AssetTypeActions/AssetTypeActions_LexUIFontData.h"
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
#include "DetailCustomization/LGUIPrefabCustomization.h"
#include "DetailCustomization/LGUIEventDelegateCustomization.h"
#include "DetailCustomization/LGUIEventDelegatePresetParamCustomization.h"
#include "DetailCustomization/LGUIComponentReferenceCustomization.h"
#include "DetailCustomization/UIScrollViewWithScrollBarCustomization.h"
#include "DetailCustomization/UISpriteSequencePlayerCustomization.h"
#include "DetailCustomization/UISpriteSheetTexturePlayerCustomization.h"
#include "DetailCustomization/LexVisualPostProcessCustomization.h"

#include "PrefabEditor/LGUIPrefabOverrideDataViewer.h"
#include "Engine/Selection.h"

#include "PrefabAnimation/LGUIPrefabSequenceComponentCustomization.h"
#include "PrefabAnimation/MovieSceneSequenceEditor_LGUIPrefabSequence.h"
#include "SequencerSettings.h"
#include "ISequencerModule.h"
#include "PrefabAnimation/LGUIPrefabSequenceEditor.h"
#include "MovieSceneToolsProjectSettings.h"
#include "UMGStyle.h"
#include "PrefabAnimation/LGUIMaterialTrackEditor.h"
#include "PrefabAnimation/LGUIPrefabSequencerSettings.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Core/LexUIImageBrush.h"
#include "Core/Components/LexImage.h"
#include "Core/Components/LexLayoutCommonSlot.h"
#include "Core/Components/LexLayoutFlexBox.h"
#include "Core/Components/LexLayoutHorizontalAndVertical.h"
#include "DetailCustomization/LexImageBrushStructCustomization.h"
#include "DetailCustomization/LexLayoutCustomization.h"
#include "DetailCustomization/LexLayoutHorizontalAndVerticalCustomization.h"
#include "DetailCustomization/LexLayoutCommonSlotCustomization.h"
#include "DetailCustomization/LexLayoutFlexBoxCustomization.h"

const FName FLGUIEditorModule::LGUIDynamicSpriteAtlasViewerName(TEXT("LGUIDynamicSpriteAtlasViewerName"));
const FName FLGUIEditorModule::LGUIPrefabSequenceTabName(TEXT("LGUIPrefabSequenceTabName"));

#define LOCTEXT_NAMESPACE "FLGUIEditorModule"
DEFINE_LOG_CATEGORY(LGUIEditor);

void FLGUIEditorModule::StartupModule()
{
	// This code will execute after your module is loaded into memory; the exact timing is specified in the .uplugin file per-module
	
	FLGUIEditorStyle::Initialize();
	FLGUIEditorStyle::ReloadTextures();

	OnInitializeSequenceHandle = ULGUIPrefabSequence::OnInitializeSequence().AddStatic(FLGUIEditorModule::OnInitializeSequence);

	ISequencerModule& SequencerModule = FModuleManager::Get().LoadModuleChecked<ISequencerModule>("Sequencer");
	SequenceEditorHandle = SequencerModule.RegisterSequenceEditor(ULGUIPrefabSequence::StaticClass(), MakeUnique<FMovieSceneSequenceEditor_LGUIPrefabSequence>());
	LGUIMaterialTrackEditorCreateTrackEditorHandle = SequencerModule.RegisterTrackEditor(FOnCreateTrackEditor::CreateStatic(&FLGUIMaterialTrackEditor::CreateTrackEditor));

	FLGUIEditorCommands::Register();
	
	PluginCommands = MakeShareable(new FUICommandList);
		
	FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

	//Editor tools
	{
		auto EditorCommands = FLGUIEditorCommands::Get();

		//actor action
		PluginCommands->MapAction(
			EditorCommands.CopyActor,
			FExecuteAction::CreateStatic(&LGUIEditorTools::CopySelectedActors_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanCopyActor),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanCopyActor)
		);
		PluginCommands->MapAction(
			EditorCommands.CutActor,
			FExecuteAction::CreateStatic(&LGUIEditorTools::CutSelectedActors_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanCutActor),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanCutActor)
		);
		PluginCommands->MapAction(
			EditorCommands.PasteActor,
			FExecuteAction::CreateStatic(&LGUIEditorTools::PasteSelectedActors_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanPasteActor),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanPasteActor)
		);
		PluginCommands->MapAction(
			EditorCommands.DuplicateActor,
			FExecuteAction::CreateStatic(&LGUIEditorTools::DuplicateSelectedActors_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanDuplicateActor),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanDuplicateActor)
		);
		PluginCommands->MapAction(
			EditorCommands.DestroyActor,
			FExecuteAction::CreateStatic(&LGUIEditorTools::DeleteSelectedActors_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanDeleteActor),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanDeleteActor)
		);
		PluginCommands->MapAction(
			EditorCommands.ToggleSpatiallyLoaded,
			FExecuteAction::CreateStatic(&LGUIEditorTools::ToggleSelectedActorsSpatiallyLoaded_Impl),
			FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanToggleActorSpatiallyLoaded),
			FGetActionCheckState::CreateStatic(&LGUIEditorTools::GetActorSpatiallyLoadedProperty),
			FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanToggleActorSpatiallyLoaded)
		);

		//component action
		PluginCommands->MapAction(
			EditorCommands.CopyComponentValues,
			FExecuteAction::CreateStatic(&LGUIEditorTools::CopyComponentValues_Impl),
			FCanExecuteAction::CreateLambda([] {return GEditor->GetSelectedComponentCount() > 0; }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([] {return GEditor->GetSelectedComponentCount() > 0; })
		);
		PluginCommands->MapAction(
			EditorCommands.PasteComponentValues,
			FExecuteAction::CreateStatic(&LGUIEditorTools::PasteComponentValues_Impl),
			FCanExecuteAction::CreateLambda([] {return LGUIEditorTools::HaveValidCopiedComponent(); }),
			FGetActionCheckState(),
			FIsActionButtonVisible::CreateLambda([] {return LGUIEditorTools::HaveValidCopiedComponent(); })
		);
		//view
		PluginCommands->MapAction(
			EditorCommands.FocusToScreenSpaceUI,
			FExecuteAction::CreateStatic(&LGUIEditorTools::FocusToScreenSpaceUI)
		);
		PluginCommands->MapAction(
			EditorCommands.FocusToSelectedUI,
			FExecuteAction::CreateStatic(&LGUIEditorTools::FocusToSelectedUI)
		);
		//settings
		PluginCommands->MapAction(
			EditorCommands.ToggleLGUIInfoColume,
			FExecuteAction::CreateRaw(this, &FLGUIEditorModule::ToggleLGUIColumnInfo),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FLGUIEditorModule::IsLGUIColumnInfoChecked)
		);
		PluginCommands->MapAction(
			EditorCommands.ToggleDrawHelperFrame,
			FExecuteAction::CreateRaw(this, &FLGUIEditorModule::ToggleDrawHelperFrame),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FLGUIEditorModule::IsDrawHelperFrameChecked)
		);
		PluginCommands->MapAction(
			EditorCommands.ToggleAnchorTool,
			FExecuteAction::CreateRaw(this, &FLGUIEditorModule::ToggleAnchorTool),
			FCanExecuteAction(),
			FIsActionChecked::CreateRaw(this, &FLGUIEditorModule::IsAnchorToolChecked)
		);
		//gc
		PluginCommands->MapAction(
			EditorCommands.ForceGC,
			FExecuteAction::CreateStatic(&LGUIEditorTools::ForceGC)
		);

		TSharedPtr<FExtender> toolbarExtender = MakeShareable(new FExtender);
		toolbarExtender->AddToolBarExtension("Play", EExtensionHook::After, PluginCommands, FToolBarExtensionDelegate::CreateRaw(this, &FLGUIEditorModule::AddEditorToolsToToolbarExtension));
		LevelEditorModule.GetToolBarExtensibilityManager()->AddExtender(toolbarExtender);
		LevelEditorModule.GetGlobalLevelEditorActions()->Append(PluginCommands.ToSharedRef());
	}
	//register SceneOutliner ColumnInfo
	{
		ApplyLGUIColumnInfo(IsLGUIColumnInfoChecked(), false);
	}
	//register window
	{
		//atlas texture viewer
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LGUIDynamicSpriteAtlasViewerName, FOnSpawnTab::CreateRaw(this, &FLGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab))
			.SetDisplayName(LOCTEXT("LexUIDynamicSpriteAtlasTextureViewerName", "LexUI Dynamic-Sprite-Atlas Texture Viewer"))
			.SetMenuType(ETabSpawnerMenuType::Hidden);
		FGlobalTabmanager::Get()->RegisterNomadTabSpawner(LGUIPrefabSequenceTabName, FOnSpawnTab::CreateRaw(this, &FLGUIEditorModule::HandleSpawnLGUIPrefabSequenceTab))
			.SetDisplayName(LOCTEXT("LexUIPrefabSequenceTabName", "LGUI Prefab Sequence"))
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
		
		PropertyModule.RegisterCustomClassLayout(ULGUIPrefab::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLGUIPrefabCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSequencePlayerCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FUISpriteSheetTexturePlayerCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLGUIEventDelegateCustomization::MakeInstance));
		//PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegateTwoParam::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLGUIEventDelegateTwoParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Empty::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Bool::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Float::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Double::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt8::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt16::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt32::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt64::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector2::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector3::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector4::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Color::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_LinearColor::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Quaternion::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_String::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Object::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Actor::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_PointerEvent::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Class::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Rotator::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Text::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));
		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIEventDelegate_Name::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&LGUIEventDelegatePresetParamCustomization::MakeInstance));

		PropertyModule.RegisterCustomPropertyTypeLayout(FLGUIComponentReference::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLGUIComponentReferenceCustomization::MakeInstance));

		PropertyModule.RegisterCustomClassLayout(ULGUIPrefabSequenceComponent::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLGUIPrefabSequenceComponentCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomPropertyTypeLayout(FLexUIImageBrush::StaticStruct()->GetFName(), FOnGetPropertyTypeCustomizationInstance::CreateStatic(&FLexImageBrushStructCustomization::MakeInstance));
		
		PropertyModule.RegisterCustomClassLayout(ULexLayout::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexLayoutHorizontalAndVertical::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutHorizontalAndVerticalCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexLayoutFlexBox::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutFlexBoxCustomization::MakeInstance));
		PropertyModule.RegisterCustomClassLayout(ULexLayoutCommonSlot::StaticClass()->GetFName(), FOnGetDetailCustomizationInstance::CreateStatic(&FLexLayoutCommonSlotCustomization::MakeInstance));
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
		TSharedPtr<FAssetTypeActions_Base> FontDataAction = MakeShareable(new FAssetTypeActions_LexUIFontData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> PrefabDataAction = MakeShareable(new FAssetTypeActions_LexUIPrefab(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> UIStaticMeshCacheDataAction = MakeShareable(new FAssetTypeActions_LexUIStaticMeshCache(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextCustomStyleDataAction = MakeShareable(new FAssetTypeActions_LexUIRichTextCustomStyleData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> RichTextImageDataAction = MakeShareable(new FAssetTypeActions_LexUIRichTextImageData(LexUIAssetCategoryBit));
		TSharedPtr<FAssetTypeActions_Base> SDFFontDataTypeAction = MakeShareable(new FAssetTypeActions_LexUIFontData_DistanceField(LexUIAssetCategoryBit));
		AssetTools.RegisterAssetTypeActions(SpriteDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(StaticSpriteAtlasDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(FontDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(PrefabDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(UIStaticMeshCacheDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextCustomStyleDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(RichTextImageDataAction.ToSharedRef());
		AssetTools.RegisterAssetTypeActions(SDFFontDataTypeAction.ToSharedRef());
		AssetTypeActionsArray.Add(SpriteDataAction);
		AssetTypeActionsArray.Add(StaticSpriteAtlasDataAction);
		AssetTypeActionsArray.Add(FontDataAction);
		AssetTypeActionsArray.Add(PrefabDataAction);
		AssetTypeActionsArray.Add(UIStaticMeshCacheDataAction);
		AssetTypeActionsArray.Add(RichTextCustomStyleDataAction);
		AssetTypeActionsArray.Add(RichTextImageDataAction);
		AssetTypeActionsArray.Add(SDFFontDataTypeAction);
	}
	//register Thumbnail
	{
		UThumbnailManager::Get().RegisterCustomRenderer(ULGUIPrefab::StaticClass(), ULGUIPrefabThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(ULexUISpriteData::StaticClass(), ULGUISpriteThumbnailRenderer::StaticClass());
		UThumbnailManager::Get().RegisterCustomRenderer(ULexUISpriteData_BaseObject::StaticClass(), ULGUISpriteDataBaseObjectThumbnailRenderer::StaticClass());
	}
	//register right mouse button in content browser
	{
		if (!IsRunningCommandlet())
		{
			FLexUIContentBrowserExtensions::InstallHooks();
			FLGUILevelEditorExtensions::InstallHooks();
		}
	}
	//register setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->RegisterSettings("Project", "Plugins", "LGUI",
				LOCTEXT("LGUISettingsName", "LGUI"),
				LOCTEXT("LGUISettingsDescription", "LGUI Settings"),
				GetMutableDefault<ULexUISettings>());
			SettingsModule->RegisterSettings("Project", "Plugins", "LGUI Editor",
				LOCTEXT("LGUIEditorSettingsName", "LGUI Editor"),
				LOCTEXT("LGUIEditorSettingsDescription", "LGUI Editor Settings"),
				GetMutableDefault<ULexUIEditorSettings>());
			SettingsModule->RegisterSettings("Project", "Plugins", "LGUIPrefab",
				LOCTEXT("LGUIPrefabSettingsName", "LGUIPrefab"),
				LOCTEXT("LGUIPrefabSettingsDescription", "LGUIPrefab Settings"),
				GetMutableDefault<ULGUIPrefabSettings>());

			LGUIPrefabSequencerSettings = USequencerSettingsContainer::GetOrCreate<ULGUIPrefabSequencerSettings>(TEXT("EmbeddedLGUIPrefabSequenceEditor"));
			SettingsModule->RegisterSettings("Editor", "ContentEditors", "EmbeddedLGUIPrefabSequenceEditor",
				LOCTEXT("LGUIPrefabSequencerSettingsName", "LGUI Prefab Sequence Editor"),
				LOCTEXT("LGUIPrefabSequencerSettingsDescription", "Configure the look and feel of the LGUI Prefab Sequence Editor."),
				LGUIPrefabSequencerSettings);
		}
	}
	//blueprint
	{
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveAwake"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveStart"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, ULexUIBehaviour::StaticClass(), TEXT("ReceiveUpdate"));

		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnNormal"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnHighlighted"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnPressed"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnDisabled"));
		FKismetEditorUtilities::RegisterAutoGeneratedDefaultEvent(this, UUISelectableTransition::StaticClass(), TEXT("ReceiveOnStartCustomTransition"));

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

	CheckPrefabOverrideDataViewerEntry();

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

void FLGUIEditorModule::OnInitializeSequence(ULGUIPrefabSequence* Sequence)
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

	FLGUIEditorCommands::Unregister();

	ULGUIPrefabSequence::OnInitializeSequence().Remove(OnInitializeSequenceHandle);
	ISequencerModule* SequencerModule = FModuleManager::Get().GetModulePtr<ISequencerModule>("Sequencer");
	if (SequencerModule)
	{
		SequencerModule->UnregisterSequenceEditor(SequenceEditorHandle);
		SequencerModule->UnRegisterTrackEditor(LGUIMaterialTrackEditorCreateTrackEditorHandle);
	}

	//unregister SceneOutliner ColumnInfo
	{
		FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");
		SceneOutlinerModule.UnRegisterColumnType<LGUISceneOutliner::FLGUISceneOutlinerInfoColumn>();
	}
	//unregister window
	{
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LGUIDynamicSpriteAtlasViewerName);
		FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(LGUIPrefabSequenceTabName);
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
		PropertyModule.UnregisterCustomClassLayout(ULGUIPrefab::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(ULexMeshModifierTextAnimation_Property::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(UUISpriteSequencePlayer::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(UUISpriteSheetTexturePlayer::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate::StaticStruct()->GetFName());
		//PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegateTwoParam::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Empty::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Bool::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Float::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Double::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt8::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt16::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt32::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Int64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_UInt64::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector2::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector3::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Vector4::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Color::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_LinearColor::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Quaternion::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_String::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Object::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Actor::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_PointerEvent::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Class::StaticStruct()->GetFName());
		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIEventDelegate_Rotator::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLGUIComponentReference::StaticStruct()->GetFName());

		PropertyModule.UnregisterCustomClassLayout(ULGUIPrefabSequenceComponent::StaticClass()->GetFName());

		PropertyModule.UnregisterCustomPropertyTypeLayout(FLexUIImageBrush::StaticStruct()->GetFName());
		
		PropertyModule.UnregisterCustomClassLayout(ULexLayout::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutHorizontalAndVertical::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutFlexBox::StaticClass()->GetFName());
		PropertyModule.UnregisterCustomClassLayout(ULexLayoutCommonSlot::StaticClass()->GetFName());
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
		UThumbnailManager::Get().UnregisterCustomRenderer(ULGUIPrefab::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(ULexUISpriteData::StaticClass());
		UThumbnailManager::Get().UnregisterCustomRenderer(ULexUISpriteData_BaseObject::StaticClass());
	}
	//unregister right mouse button in content browser
	{
		FLexUIContentBrowserExtensions::RemoveHooks();
		FLGUILevelEditorExtensions::RemoveHooks();
	}

	//unregister setting
	{
		if (ISettingsModule* SettingsModule = FModuleManager::GetModulePtr<ISettingsModule>("Settings"))
		{
			SettingsModule->UnregisterSettings("Project", "Plugins", "LGUI");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LGUI Editor");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LGUI Prefab");
			SettingsModule->UnregisterSettings("Project", "Plugins", "LGUIPrefabSequencerSettings");
		}
	}

	FKismetEditorUtilities::UnregisterAutoBlueprintNodeCreation(this);

	USelection::SelectionChangedEvent.RemoveAll(this);
}

void FLGUIEditorModule::AddReferencedObjects(FReferenceCollector& Collector)
{
	Collector.AddReferencedObject(LGUIPrefabSequencerSettings);
}
FString FLGUIEditorModule::GetReferencerName() const 
{
	return "LGUIEditorModule";
}

FLGUIEditorModule& FLGUIEditorModule::Get()
{
	return FModuleManager::Get().GetModuleChecked<FLGUIEditorModule>(TEXT("LGUIEditor"));
}

void FLGUIEditorModule::CheckPrefabOverrideDataViewerEntry()
{
	if (PrefabOverrideDataViewer != nullptr && PrefabOverrideDataViewer.IsValid())return;
	PrefabOverrideDataViewer = 
	SNew(SLGUIPrefabOverrideDataViewer, nullptr)
	.AfterRevertPrefab_Lambda([=, this](ULGUIPrefab* PrefabAsset) {
		MarkOutlinerSelectionChange();//force refresh
		})
	.AfterApplyPrefab_Lambda([=, this](ULGUIPrefab* PrefabAsset) {
		MarkOutlinerSelectionChange();//force refresh
		LGUIEditorTools::RefreshLevelLoadedPrefab(PrefabAsset);
		LGUIEditorTools::RefreshOnSubPrefabChange(PrefabAsset);
		LGUIEditorTools::RefreshOpenedPrefabEditor(PrefabAsset);
		})
	;
}

TSharedRef<SDockTab> FLGUIEditorModule::HandleSpawnDynamicSpriteAtlasViewerTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SLGUIDynamicSpriteAtlasViewer, ResultTab);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

TSharedRef<SDockTab> FLGUIEditorModule::HandleSpawnLGUIPrefabSequenceTab(const FSpawnTabArgs& SpawnTabArgs)
{
	auto ResultTab = SNew(SDockTab).TabRole(ETabRole::NomadTab);
	auto TabContentWidget = SNew(SLGUIPrefabSequenceEditor);
	ResultTab->SetContent(TabContentWidget);
	return ResultTab;
}

bool FLGUIEditorModule::CanUnpackActorForPrefab()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor))
		{
			return true;
		}
		else if (PrefabHelperObject->MissingPrefab.Contains(SelectedActor))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}
bool FLGUIEditorModule::CanBrowsePrefab()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor))
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}

bool FLGUIEditorModule::CanUpdateLevelPrefab()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->SubPrefabMap.Contains(SelectedActor) && !PrefabHelperObject->IsInsidePrefabEditor())//Can only update prefab in level editor
		{
			return true;
		}
		return false;
	}
	else
	{
		return false;
	}
}

ECheckBoxState FLGUIEditorModule::GetAutoUpdateLevelPrefab()const
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return ECheckBoxState::Undetermined;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (auto SubPrefabDataPtr = PrefabHelperObject->SubPrefabMap.Find(SelectedActor))
		{
			return SubPrefabDataPtr->bAutoUpdate ? ECheckBoxState::Checked : ECheckBoxState::Unchecked;
		}
	}
	return ECheckBoxState::Undetermined;
}

bool FLGUIEditorModule::CanCreateActor()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (!LGUIEditorTools::IsActorCompatibleWithLGUIToolsMenu(SelectedActor))return false;
	return true;
}

bool FLGUIEditorModule::CanCheckPrefabOverrideParameter()const
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		for (auto& KeyValue : PrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Key == SelectedActor || SelectedActor->IsAttachedTo(KeyValue.Key))
			{
				return true;
			}
		}
		return false;
	}
	else
	{
		return false;
	}
}

bool FLGUIEditorModule::CanReplaceActor()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (!LGUIEditorTools::IsActorCompatibleWithLGUIToolsMenu(SelectedActor))return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->IsActorBelongsToSubPrefab(SelectedActor))//sub prefab's actor not allow replace
		{
			return false;
		}
		else if (PrefabHelperObject->IsActorBelongsToMissingSubPrefab(SelectedActor))//missing sub prefab's actor not allowed
		{
			return false;
		}
	}
	return true;
}

bool FLGUIEditorModule::CanCreatePrefab()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return false;
	if (!LGUIEditorTools::IsActorCompatibleWithLGUIToolsMenu(SelectedActor))return false;
	if (SelectedActor->HasAnyFlags(EObjectFlags::RF_Transient))return false;
	if (auto PrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor))
	{
		if (PrefabHelperObject->LoadedRootActor == SelectedActor)
		{
			return false;
		}
		if (PrefabHelperObject->IsActorBelongsToSubPrefab(SelectedActor))
		{
			return false;
		}
		else if (PrefabHelperObject->IsActorBelongsToMissingSubPrefab(SelectedActor))
		{
			return false;
		}
	}
	return true;
}

void FLGUIEditorModule::MarkOutlinerSelectionChange()
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	if (SelectedActor == nullptr)return;
	auto NewPrefabHelperObject = LGUIEditorTools::GetPrefabHelperObject_WhichManageThisActor(SelectedActor);
	if (CurrentPrefabHelperObject != NewPrefabHelperObject)
	{
		CurrentPrefabHelperObject = NewPrefabHelperObject;
		if (CurrentPrefabHelperObject != nullptr)
		{
			PrefabOverrideDataViewer->SetPrefabHelperObject(CurrentPrefabHelperObject.Get());
		}
	}
	if (CurrentPrefabHelperObject != nullptr)
	{
		bool bIsSubPrefabRoot = false;
		for (auto& KeyValue : CurrentPrefabHelperObject->SubPrefabMap)
		{
			if (KeyValue.Key == SelectedActor)
			{
				bIsSubPrefabRoot = true;
				break;
			}
		}
		PrefabOverrideDataViewer->RefreshDataContent(CurrentPrefabHelperObject->GetSubPrefabData(SelectedActor).ObjectOverrideParameterArray, bIsSubPrefabRoot ? nullptr : SelectedActor);
	}
}

void FLGUIEditorModule::AddEditorToolsToToolbarExtension(FToolBarBuilder& Builder)
{
	Builder.BeginSection("LGUI");
	{
		Builder.AddComboButton(
			FUIAction(),
			FOnGetContent::CreateRaw(this, &FLGUIEditorModule::MakeEditorToolsMenu, true, true, true, true, true, true),
			LOCTEXT("LGUITools", "LGUI Tools"),
			LOCTEXT("LGUIEditorTools", "LGUI Editor Tools"),
			FSlateIcon(FLGUIEditorStyle::GetStyleSetName(), "LGUIEditor.EditorTools")
		);
	}
	Builder.EndSection();
}

TSharedRef<SWidget> FLGUIEditorModule::MakeEditorToolsMenu(bool InitialSetup, bool ComponentAction, bool OpenWindow, bool PreviewInViewport, bool EditorCameraControl, bool Others)
{
	FMenuBuilder MenuBuilder(true, PluginCommands);
	auto commandList = FLGUIEditorCommands::Get();

	//prefab
	{
		MenuBuilder.BeginSection("Prefab", LOCTEXT("Prefab", "Prefab"));
		{
			MenuBuilder.AddMenuEntry(
				LOCTEXT("CreatePrefab", "Create Prefab"),
				LOCTEXT("CreatePrefab_Tooltip", "Use selected actor to create a new prefab"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreatePrefabAsset)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanCreatePrefab)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCreatePrefab))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UnpackPrefab", "Unpack this Prefab"),
				LOCTEXT("UnpackPrefab_Tooltip", "Unpack the actor from related prefab asset"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::UnpackPrefab)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanUnpackActorForPrefab)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanUnpackActorForPrefab))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("SelectPrefabAsset", "Browse to Prefab asset"),
				LOCTEXT("SelectPrefabAsset_Tooltip", "Browse to Prefab asset in Content Browser"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::SelectPrefabAsset)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanBrowsePrefab)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanBrowsePrefab))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("OpenPrefabAsset", "Open Prefab asset"),
				LOCTEXT("OpenPrefabAsset_Tooltip", "Open Prefab asset in PrefabEditor"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::OpenPrefabAsset)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanBrowsePrefab)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanBrowsePrefab))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("UpdateLevelPrefab", "Update Prefab"),
				LOCTEXT("UpdateLevelPrefab_Tooltip", "Update this prefab to latest version"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::UpdateLevelPrefab)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanUpdateLevelPrefab)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanUpdateLevelPrefab))
			);
			MenuBuilder.AddMenuEntry(
				LOCTEXT("AutoUpdateLevelPrefab", "Auto Update Prefab"),
				LOCTEXT("AutoUpdateLevelPrefab_Tooltip", "Auto update this prefab when detect newer version"),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::ToggleLevelPrefabAutoUpdate)
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanUpdateLevelPrefab)
					, FGetActionCheckState::CreateRaw(this, &FLGUIEditorModule::GetAutoUpdateLevelPrefab)
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanUpdateLevelPrefab)),
				NAME_None,
				EUserInterfaceActionType::ToggleButton
			);
			CheckPrefabOverrideDataViewerEntry();
			MenuBuilder.AddMenuEntry(
				FUIAction(FExecuteAction()
					, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanCheckPrefabOverrideParameter)
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCheckPrefabOverrideParameter))
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
									PrefabOverrideDataViewer.ToSharedRef()
								]
							]
						]
					]
				]
			);
		}
		MenuBuilder.EndSection();
	}

	MenuBuilder.BeginSection("LGUI Actor", LOCTEXT("LGUI Actor", "LGUI Actor Operations"));
	{
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIElementSubMenu", "Create UI Element"),
			LOCTEXT("CreateUIElementSubMenu_Tooltip", "Create UI Element"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIElementSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCreateActor)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIExtensionSubMenu", "Create UI Extension Element"),
			LOCTEXT("CreateUIExtensionSubMenu_Tooltip", "Create UI Extension Element"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIExtensionSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCreateActor)),
			NAME_None, EUserInterfaceActionType::None
		);
		MenuBuilder.AddSubMenu(
			LOCTEXT("CreateUIPostProcessSubMenu", "Create UI Post Process"),
			LOCTEXT("CreateUIPostProcessSubMenu_Tooltip", "Create UI Post Process"),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CreateUIPostProcessSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction()
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCreateActor)),
			NAME_None, EUserInterfaceActionType::None
		);
		CreateExtraPrefabsSubMenu(MenuBuilder);
		if (InitialSetup)
		{
			MenuBuilder.AddSubMenu(
				LOCTEXT("BasicSetup", "Basic Setup"),
				LOCTEXT("BasicSetup", "Basic Setup"),
				FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::BasicSetupSubMenu)
			);
		}
		MenuBuilder.AddSubMenu(
			LOCTEXT("ReplaceActorMenu", "Replace this by..."),
			LOCTEXT("ReplaceActorMenu_Tooltip", "Replace this actor with..."),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::ReplaceActorSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction::CreateRaw(this, &FLGUIEditorModule::CanReplaceActor)
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanReplaceActor)),
			NAME_None,
			EUserInterfaceActionType::None
		);
	}
	MenuBuilder.EndSection();

	// MenuBuilder.BeginSection("CommonActor", LOCTEXT("CommonActor", "Create Common Actors"));
	// {
	// 	this->CreateCommonActorSubMenu(MenuBuilder);
	// }
	// MenuBuilder.EndSection();

	MenuBuilder.BeginSection("ActorAction", LOCTEXT("ActorAction", "Edit Actor With Hierarchy"));
	{
		MenuBuilder.AddMenuEntry(commandList.CopyActor);
		MenuBuilder.AddMenuEntry(commandList.PasteActor);
		MenuBuilder.AddMenuEntry(commandList.CutActor);
		MenuBuilder.AddMenuEntry(commandList.DuplicateActor);
		MenuBuilder.AddMenuEntry(commandList.DestroyActor);
		MenuBuilder.AddMenuEntry(commandList.ToggleSpatiallyLoaded);
	}
	MenuBuilder.EndSection();

	MenuBuilder.BeginSection("CopyReference", LOCTEXT("CopyReference", "Copy Reference"));
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyActorReference", "Copy Actor as Reference"),
			LOCTEXT("CopyActorReference_Tooltip", "Copy Actor as Reference"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CopyReference_Actor)
				, FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanCopyActorReference)
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanCopyActorReference)
				),
				NAME_None, EUserInterfaceActionType::Button);
		MenuBuilder.AddSubMenu(LOCTEXT("CopyWidgetReference", "Copy LexWidget as Reference"),
			LOCTEXT("CopyWidgetReference_Tooltip", "Copy LexWidget as Reference, then we can paste to widget reference property."),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CopyWidgetReferenceSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanCopyWidgetReference)
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanCopyWidgetReference)
				),
				NAME_None, EUserInterfaceActionType::Button);
		MenuBuilder.AddSubMenu(LOCTEXT("CopyComponentReference", "Copy Component as Reference"),
			LOCTEXT("CopyComponentReference_Tooltip", "Copy ActorComponent as Reference, then we can paste to ActorComponent reference property."),
			FNewMenuDelegate::CreateRaw(this, &FLGUIEditorModule::CopyComponentReferenceSubMenu),
			FUIAction(FExecuteAction()
				, FCanExecuteAction::CreateStatic(&LGUIEditorTools::CanCopyComponentReference)
				, FGetActionCheckState()
				, FIsActionButtonVisible::CreateStatic(&LGUIEditorTools::CanCopyComponentReference)
				),
				NAME_None, EUserInterfaceActionType::Button);
	}
	MenuBuilder.EndSection();

	if (ComponentAction)
	{
		MenuBuilder.BeginSection("ComponentAction", LOCTEXT("ComponentAction", "Edit Component"));
		{
			MenuBuilder.AddMenuEntry(commandList.CopyComponentValues);
			MenuBuilder.AddMenuEntry(commandList.PasteComponentValues);
		}
		MenuBuilder.EndSection();
	}

	if (OpenWindow)
	{

	}

	if (EditorCameraControl)
	{
		MenuBuilder.BeginSection("EditorCamera", LOCTEXT("EditorCameraControl", "EditorCameraControl"));
		{
			MenuBuilder.AddMenuEntry(commandList.FocusToScreenSpaceUI);
			MenuBuilder.AddMenuEntry(commandList.FocusToSelectedUI);
		}
		MenuBuilder.EndSection();
	}

	if (Others)
	{
		MenuBuilder.BeginSection("Others", LOCTEXT("Others", "Others"));
		{
			MenuBuilder.AddMenuEntry(commandList.ToggleLGUIInfoColume);
			MenuBuilder.AddMenuEntry(commandList.ToggleDrawHelperFrame);
			MenuBuilder.AddMenuEntry(commandList.ToggleAnchorTool);
			MenuBuilder.AddMenuEntry(commandList.ForceGC);
		}
		MenuBuilder.EndSection();
	}

	return MenuBuilder.MakeWidget();
}

void FLGUIEditorModule::CreateUIElementSubMenu(FMenuBuilder& MenuBuilder)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, FString Name, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateLexWidget, Name, InVisualClass, Callback))
			);
		}
		static void CreateUIControlMenuEntry(FMenuBuilder& InBuilder, const FString& InControlName, FText InTooltip = FText())
		{
			if (InTooltip.IsEmpty())
			{
				InTooltip = FText::Format(LOCTEXT("CreateUIElementTitle", "Create {0}"), FText::FromString(InControlName));
			}
			InBuilder.AddMenuEntry(
				FText::FromString(InControlName),
				InTooltip,
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateUIControls, LGUIEditorTools::LGUIPresetPrefabPath + InControlName))
			);
		}
		static void CreateEmptyActorMenuEntry(FMenuBuilder& InBuilder)
		{
			InBuilder.AddMenuEntry(
				LOCTEXT("CreateEmptyActor", "Create empty actor"),
				LOCTEXT("CreateEmptyActor_Tooltip", "Create empty actor with a SceneComponent as RootComponent, and can be replaced by other SceneComponent type."),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateEmptyActor))
			);
		}
	};

	MenuBuilder.BeginSection("UIElement");
	{
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, "Widget", nullptr, nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, "Text", ULexText::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, "Image", ULexImage::StaticClass(), [](ULexWidget* InWidget)
		{
			if (auto Image = Cast<ULexImage>(InWidget->GetVisual()))
			{
				Image->SetBrush_LexUISprite(ULexUISpriteData::GetDefaultFrameRect());
			}
		});
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, "RectBlock", ULexRectBlock::StaticClass(), nullptr);

		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("Button"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("Toggle"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("ToggleGroup"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("HorizontalSlider"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("VerticalSlider"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("HorizontalScrollbar"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("VerticalScrollbar"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("Dropdown"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("TextInput"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("TextInputMultiline"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("HorizontalScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("VerticalScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("HorizontalRecyclableScrollView"));
		FunctionContainer::CreateUIControlMenuEntry(MenuBuilder, TEXT("VerticalRecyclableScrollView"));
	}
	MenuBuilder.EndSection();
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

#include "IPlacementModeModule.h"
#include "AssetSelection.h"
#include "LevelEditorViewport.h"
void FLGUIEditorModule::CreateCommonActorSubMenu(FMenuBuilder& MenuBuilder)
{
	struct LOCAL
	{
		struct TempGWorldCurrentLevel
		{
			ULevel* OriginLevel = nullptr;
			TempGWorldCurrentLevel(ULevel* NewLevel)
			{
				OriginLevel = GWorld->GetCurrentLevel();
				GWorld->SetCurrentLevel(NewLevel);
			}
			~TempGWorldCurrentLevel()
			{
				GWorld->SetCurrentLevel(OriginLevel);
			}
		};
		//reference from GEditor->UseActorFactory
		static AActor* UseActorFactory(UActorFactory* Factory, const FAssetData& AssetData)
		{
			AActor* NewActor = nullptr;

			if (auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor())
			{
				LGUIEditorTools::MakeCurrentLevel(SelectedActor);
				if (ULevel* DesiredLevel = SelectedActor->GetLevel())
				{
					TempGWorldCurrentLevel Temp(DesiredLevel);//temporary change level, because when create actor form asset, the function (PrivateAddActor) use level by GWorld->GetCurrentLevel
					if (UObject* LoadedAsset = AssetData.GetAsset())
					{
						auto Actors = FLevelEditorViewportClient::TryPlacingActorFromObject(DesiredLevel, LoadedAsset, true, RF_Transactional, Factory);
						if (Actors.Num() && (Actors[0] != nullptr))
						{
							NewActor = Actors[0];
							NewActor->SetActorRelativeTransform(FTransform::Identity);

							auto SelectedRootComp = SelectedActor->GetRootComponent();
							auto NewRootComp = NewActor->GetRootComponent();
							if (SelectedRootComp && NewRootComp)
							{
								NewRootComp->SetMobility(SelectedRootComp->Mobility);
								NewActor->AttachToActor(SelectedActor, FAttachmentTransformRules::KeepRelativeTransform);
							}
						}
					}
				}
			}

			return NewActor;
		}
		//reference from SPlacementAssetMenuEntry::OnMouseButtonUp
		static void CreateActor(TSharedPtr<FPlaceableItem> Item)
		{
			UActorFactory* Factory = Item->Factory;
			if (!Item->Factory)
			{
				// If no actor factory was found or failed, add the actor from the uclass
				UClass* AssetClass = Item->AssetData.GetClass();
				if (AssetClass)
				{
					UObject* ClassObject = AssetClass->GetDefaultObject();
					FActorFactoryAssetProxy::GetFactoryForAssetObject(ClassObject);
				}
			}
			//reference from FLevelEditorActionCallbacks::AddActor
			auto NewActor = UseActorFactory(Factory, Item->AssetData);
			if (NewActor != NULL && IPlacementModeModule::IsAvailable())
			{
				IPlacementModeModule::Get().AddToRecentlyPlaced(Item->AssetData.GetAsset(), Factory);
			}
		}
		static void CreateCommonActorMenuEntry(FMenuBuilder& InBuilder, TSharedPtr<FPlaceableItem> Item)
		{
			InBuilder.AddMenuEntry(
				Item->DisplayName,
				FText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LOCAL::CreateActor, Item))
			);
		}
		static void MakeMenu(FMenuBuilder& MenuBuilder, const TArray<FPlacementCategoryInfo>& Categories, FLGUIEditorModule* EditorModulePtr, IPlacementModeModule& PlacementMode)
		{
			for (auto GroupDataItem : Categories)
			{
				if (GroupDataItem.UniqueHandle == FBuiltInPlacementCategories::RecentlyPlaced())
					GroupDataItem.DisplayName = LOCTEXT("RecentlyPlaced", "Recently Created");

				PlacementMode.RegenerateItemsForCategory(GroupDataItem.UniqueHandle);
				TArray<TSharedPtr<FPlaceableItem>> Items;
				PlacementMode.GetItemsForCategory(GroupDataItem.UniqueHandle, Items);
				if (Items.Num() <= 0)
					continue;

				MenuBuilder.AddSubMenu(
					GroupDataItem.DisplayName,
					FText(),
					FNewMenuDelegate::CreateLambda([Items, UniqueHandle = GroupDataItem.UniqueHandle](FMenuBuilder& MenuBuilder) {
						MenuBuilder.BeginSection(UniqueHandle);
						{
							MenuBuilder.AddSearchWidget();
							for (auto& Item : Items)
							{
								CreateCommonActorMenuEntry(MenuBuilder, Item);
							}
						}
						MenuBuilder.EndSection();
						}),
					FUIAction(FExecuteAction()
						, FCanExecuteAction()
						, FGetActionCheckState()
						, FIsActionButtonVisible::CreateRaw(EditorModulePtr, &FLGUIEditorModule::CanCreateActor)),
					NAME_None, EUserInterfaceActionType::None
				);
			}
		}
	};

	auto& PlacementMode = IPlacementModeModule::Get();
	TArray<FPlacementCategoryInfo> Categories;
	PlacementMode.GetSortedCategories(Categories);
	LOCAL::MakeMenu(MenuBuilder, Categories, this, PlacementMode);
}

void FLGUIEditorModule::CreateExtraPrefabsSubMenu(FMenuBuilder& MenuBuilder)
{
	struct LOCAL
	{
		static void CreateExtraPrefab_SubMenu(FMenuBuilder& MenuBuilder, TArray<ULGUIPrefab*> InPrefabArray)
		{
			for (auto Prefab : InPrefabArray)
			{
				MenuBuilder.AddMenuEntry(
					FText::FromString(FPaths::GetBaseFilename(Prefab->GetPathName())),
					FText::FromString(Prefab->GetPathName()),
					FSlateIcon(),
					FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateUIControls, Prefab->GetPathName()))
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
		TArray<ULGUIPrefab*> PrefabAssets;
		auto PrefabClassName = ULGUIPrefab::StaticClass()->GetClassPathName();
		for (auto Asset : ScriptAssetList)
		{
			if (Asset.AssetClassPath == PrefabClassName)
			{
				auto AssetObject = Asset.GetAsset();
				if (auto Prefab = Cast<ULGUIPrefab>(AssetObject))
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
				FNewMenuDelegate::CreateStatic(&LOCAL::CreateExtraPrefab_SubMenu, PrefabAssets),
				FUIAction(FExecuteAction()
					, FCanExecuteAction()
					, FGetActionCheckState()
					, FIsActionButtonVisible::CreateRaw(this, &FLGUIEditorModule::CanCreateActor)),
				NAME_None, EUserInterfaceActionType::None
			);
		}
	}
}

void FLGUIEditorModule::ToggleLGUIColumnInfo()
{
	auto LGUIEditorSettings = GetMutableDefault<ULexUIEditorSettings>();
	LGUIEditorSettings->ShowLexUIColumnInSceneOutliner = !LGUIEditorSettings->ShowLexUIColumnInSceneOutliner;
	LGUIEditorSettings->SaveConfig();

	ApplyLGUIColumnInfo(LGUIEditorSettings->ShowLexUIColumnInSceneOutliner, true);
}
bool FLGUIEditorModule::IsLGUIColumnInfoChecked()
{
	return GetDefault<ULexUIEditorSettings>()->ShowLexUIColumnInSceneOutliner;
}

void FLGUIEditorModule::ToggleAnchorTool()
{
	auto LGUIEditorSettings = GetMutableDefault<ULexUIEditorSettings>();
	LGUIEditorSettings->bShowAnchorTool = !LGUIEditorSettings->bShowAnchorTool;
	LGUIEditorSettings->SaveConfig();
}
bool FLGUIEditorModule::IsAnchorToolChecked()
{
	return GetDefault<ULexUIEditorSettings>()->bShowAnchorTool;
}

void FLGUIEditorModule::ToggleDrawHelperFrame()
{
	auto LGUIEditorSettings = GetMutableDefault<ULexUIEditorSettings>();
	LGUIEditorSettings->bDrawHelperFrame = !LGUIEditorSettings->bDrawHelperFrame;
	LGUIEditorSettings->SaveConfig();
}
bool FLGUIEditorModule::IsDrawHelperFrameChecked()
{
	return GetDefault<ULexUIEditorSettings>()->bDrawHelperFrame;
}

void FLGUIEditorModule::ApplyLGUIColumnInfo(bool value, bool refreshSceneOutliner)
{
	FSceneOutlinerModule& SceneOutlinerModule = FModuleManager::LoadModuleChecked< FSceneOutlinerModule >("SceneOutliner");
	if (value)
	{
		FSceneOutlinerColumnInfo ColumnInfo(ESceneOutlinerColumnVisibility::Visible, 15, FCreateSceneOutlinerColumn::CreateStatic(&LGUISceneOutliner::FLGUISceneOutlinerInfoColumn::MakeInstance));
		SceneOutlinerModule.RegisterDefaultColumnType<LGUISceneOutliner::FLGUISceneOutlinerInfoColumn>(ColumnInfo);
	}
	else
	{
		SceneOutlinerModule.UnRegisterColumnType<LGUISceneOutliner::FLGUISceneOutlinerInfoColumn>();
	}

	//refresh scene outliner
	if (refreshSceneOutliner)
	{
		FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>("LevelEditor");

		TSharedPtr<FTabManager> LevelEditorTabManager = LevelEditorModule.GetLevelEditorTabManager();
		if (LevelEditorTabManager->FindExistingLiveTab(FName("LevelEditorSceneOutliner")).IsValid())
		{
			if (LevelEditorTabManager.IsValid() && LevelEditorTabManager.Get())
			{
				if (LevelEditorTabManager->GetOwnerTab().IsValid())
				{
					LevelEditorTabManager->TryInvokeTab(FName("LevelEditorSceneOutliner"))->RequestCloseTab();
				}
			}

			if (LevelEditorTabManager.IsValid() && LevelEditorTabManager.Get())
			{
				if (LevelEditorTabManager->GetOwnerTab().IsValid())
				{
					LevelEditorTabManager->TryInvokeTab(FName("LevelEditorSceneOutliner"));
				}
			}
		}
	}
}

void FLGUIEditorModule::CreateUIPostProcessSubMenu(FMenuBuilder& MenuBuilder)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, FString Name, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateLexWidget, Name, InVisualClass, Callback))
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
					FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, ClassItr->GetName(), *ClassItr, nullptr);
				}
			}
		}
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::CreateUIExtensionSubMenu(FMenuBuilder& MenuBuilder)
{
	struct FunctionContainer
	{
		static void CreateWidgetVisualElementMenuEntry(FMenuBuilder& InBuilder, UClass* InVisualClass, TFunction<void(ULexWidget*)> Callback)
		{
			UClass* NameClass = InVisualClass ? InVisualClass : ULexWidget::StaticClass();
			InBuilder.AddMenuEntry(
				FText::FromString(NameClass->GetName()),
				NameClass->GetToolTipText(),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateLexWidget, InVisualClass->GetName(), InVisualClass, Callback))
			);
		}
		static void CreateMenuEntryByPrefab(FMenuBuilder& InBuilder, const FString& InControlName, const FText& InLabel, const FText& InTooltip = FText::GetEmpty())
		{
			InBuilder.AddMenuEntry(
				InLabel,
				InTooltip,
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateUIControls, LGUIEditorTools::LGUIPresetPrefabPath + InControlName))
			);
		}
	};

	MenuBuilder.BeginSection("UIExtension");
	{
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, UUIPolygon::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, UUIPolygonLine::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, UUIRing::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, ULexStaticMesh::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, UUI2DLineRaw::StaticClass(), nullptr);
		FunctionContainer::CreateWidgetVisualElementMenuEntry(MenuBuilder, UUI2DLineChildrenAsPoints::StaticClass(), nullptr);
		//FunctionContainer::CreateMenuEntryByPrefab(MenuBuilder, TEXT("UIWidget"), LOCTEXT("UIWidget", "UI Widget"), AUIWidgetActor::StaticClass()->GetToolTipText());
		//FunctionContainer::CreateMenuEntryByPrefab(MenuBuilder, TEXT("UIRenderTarget"), LOCTEXT("UIRenderTarget", "UI Render Target"), AUIRenderTargetActor::StaticClass()->GetToolTipText());
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::BasicSetupSubMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("UIBasicSetup");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("BasicSetup_ScreenSpaceUI", "Screen Space UI"),
			LOCTEXT("BasicSetup_ScreenSpaceUI_Tooltip", "Create Screen Space UI"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateScreenSpaceUI_BasicSetup))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("BasicSetup_WorldSpaceUERenderer", "World Space UI - UE Renderer"),
			LOCTEXT("BasicSetup_WorldSpaceUERenderer_Tooltip", "Render in world space by UE default render pipeline.\n This mode use engine's default render pipeline, so post process will affect ui."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateWorldSpaceUIBuiltinRenderer_BasicSetup))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("BasicSetup_WorldSpaceLexUIRenderer", "World Space UI - LexUI Renderer"),
			LOCTEXT("BasicSetup_WorldSpaceLexUIRenderer_Tooltip", "Render in world space by LexUI's custom render pipeline.\n This mode use LexUI's custom render pipeline, will not be affected by post process."),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CreateWorldSpaceUILexUIRenderer_BasicSetup))
		);
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::CopyWidgetReferenceSubMenu(FMenuBuilder& MenuBuilder)
{
	MenuBuilder.BeginSection("CopyWidgetReference");
	{
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyWidgetReference_Widget", "Copy LexWidget as Reference"),
			LOCTEXT("CopyWidgetReference_Widget_Tooltip", "Copy LexWidget as Reference"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CopyReference_Widget))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyWidgetReference_Visual", "Copy Visual of the LexWidget as Reference"),
			LOCTEXT("CopyWidgetReference_Visual_Tooltip", "Copy Visual of the LexWidget as Reference"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CopyReference_Visual))
		);
		MenuBuilder.AddMenuEntry(
			LOCTEXT("CopyWidgetReference_Layout", "Copy Layout of the LexWidget as Reference"),
			LOCTEXT("CopyWidgetReference_Layout_Tooltip", "Copy Layout of the LexWidget as Reference"),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CopyReference_Layout))
		);
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::CopyComponentReferenceSubMenu(FMenuBuilder& MenuBuilder)
{
	auto SelectedActor = LGUIEditorTools::GetFirstSelectedActor();
	TArray<UActorComponent*> ValidComponents;
	auto& AllComps = SelectedActor->GetComponents();
	for (auto& Comp : AllComps)
	{
		if (IsValid(Comp))
		{
			if (!Comp->IsVisualizationComponent())
			{
				ValidComponents.Add(Comp);
			}
		}
	}
	MenuBuilder.BeginSection("CopyActorComponentReference");
	{
		for (auto& Comp : ValidComponents)
		{
			auto LabelText = FText::FromString(FString::Printf(TEXT("%s (%s)"), *Comp->GetName(), *Comp->GetClass()->GetName()));
			MenuBuilder.AddMenuEntry(
			LabelText,
			FText::GetEmpty(),
			FSlateIcon(),
			FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::CopyReference_Component, Comp))
		);
		}
	}
	MenuBuilder.EndSection();
}

void FLGUIEditorModule::ReplaceActorSubMenu(FMenuBuilder& MenuBuilder)
{
	struct FPlaceableItem
	{
		UClass* Class;
		FText DisplayName;
		FPlaceableItem(UClass* InClass)
		{
			Class = InClass;
			DisplayName = InClass->GetDisplayNameText();
		}
	};

	struct FunctionContainer
	{
		static void ReplaceUIElement(FMenuBuilder& InBuilder, UClass* InClass)
		{
			auto ClassName = InClass->GetDisplayNameText().ToString();
			ClassName.RemoveFromEnd("Actor");
			InBuilder.AddMenuEntry(
				FText::FromString(ClassName),
				FText::Format(LOCTEXT("ReplaceUIElement", "ReplaceWith {0}"), FText::FromString(ClassName)),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::ReplaceActorByClass, InClass))
			);
		}
		static void CreateCommonActorMenuEntry(FMenuBuilder& InBuilder, const FPlaceableItem& Item)
		{
			InBuilder.AddMenuEntry(
				Item.DisplayName,
				FText::Format(LOCTEXT("ReplaceCommonActor", "ReplaceWith {0}"), Item.DisplayName),
				FSlateIcon(),
				FUIAction(FExecuteAction::CreateStatic(&LGUIEditorTools::ReplaceActorByClass, Item.Class))
			);
		}
	};

	MenuBuilder.BeginSection("Replace");
	{
		FunctionContainer::ReplaceUIElement(MenuBuilder, ALexWidgetActor::StaticClass());

		for (TObjectIterator<UClass> ClassItr; ClassItr; ++ClassItr)
		{
			if (ClassItr->IsChildOf(ALexWidgetActor::StaticClass()))
			{
				if (*ClassItr != ALexWidgetActor::StaticClass()
					&& !(ClassItr->HasAnyClassFlags(CLASS_Transient))
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
					FunctionContainer::ReplaceUIElement(MenuBuilder, *ClassItr);
				}
			}
		}

		
		TArray<FPlaceableItem> AllValidActorArray;
		for (TObjectIterator<UClass> ClassIt; ClassIt; ++ClassIt)
		{
			// Don't offer skeleton classes
			bool bIsSkeletonClass = FKismetEditorUtilities::IsClassABlueprintSkeleton(*ClassIt);

			if (!ClassIt->HasAllClassFlags(CLASS_NotPlaceable) &&
				!ClassIt->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists) &&
				ClassIt->IsChildOf(AActor::StaticClass()) &&
				(!ClassIt->IsChildOf(ABrush::StaticClass()) || ClassIt->IsChildOf(AVolume::StaticClass())) &&
				!bIsSkeletonClass)
			{
				if (!IsValidClassName(ClassIt->GetName()))
				{
					continue;
				}
				AllValidActorArray.Add(FPlaceableItem(*ClassIt));
			}
		}
		Algo::Sort(AllValidActorArray, [](const FPlaceableItem A, const FPlaceableItem B) {
			return A.DisplayName.CompareTo(B.DisplayName) < 0;
			});

		const FString AllActorGroupName = TEXT("All Classes");
		MenuBuilder.AddSubMenu(
			FText::FromString(AllActorGroupName),
			FText(),
			FNewMenuDelegate::CreateLambda([=](FMenuBuilder& ItemMenuBuilder) {
				ItemMenuBuilder.BeginSection(FName(*AllActorGroupName));
				{
					ItemMenuBuilder.AddSearchWidget();
					for (auto& PlaceableItem : AllValidActorArray)
					{
						FunctionContainer::CreateCommonActorMenuEntry(ItemMenuBuilder, PlaceableItem);
					}
				}
				ItemMenuBuilder.EndSection();
				}),
			FUIAction(),
					NAME_None, EUserInterfaceActionType::None
					);
	}
	MenuBuilder.EndSection();
}

IMPLEMENT_MODULE(FLGUIEditorModule, LGUIEditor)

#undef LOCTEXT_NAMESPACE