// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LexUIManager.h"

#include "Constraint.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "Event/LexBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectableComponent.h"
#include "Core/LexUISettings.h"
#include "Event/InputModule/LexBaseInputModule.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexVisual.h"
#include "Engine/Engine.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Core/ILexUICultureChangedInterface.h"
#include "Core/LexUIBehaviour.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "PrefabSystem/LGUIPrefabHelperObject.h"
#if WITH_EDITOR
#include "Editor.h"
#include "LevelEditor.h"
#include "SLevelViewport.h"
#include "EditorViewportClient.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "Core/LexUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "LexUIManager"

UE_DISABLE_OPTIMIZATION

ULexUIEditorManagerObject* ULexUIEditorManagerObject::Instance = nullptr;
#if WITH_EDITOR
int ULexUIEditorManagerObject::IndexOfClickSelectUI = INDEX_NONE;
#endif
ULexUIEditorManagerObject::ULexUIEditorManagerObject()
{
	if (this == GetDefault<ULexUIEditorManagerObject>())
	{
#if WITH_EDITOR
		ULGUIPrefabManagerObject::OnSerialize_SortChildrenActors.BindStatic([](TArray<AActor*>& ChildrenActors) {
			//Actually normal UIItem's hierarchyIndex property can do the job, but sub prefab's root actor not, so sort it to make sure.
			Algo::Sort(ChildrenActors, [](const AActor* A, const AActor* B) {
				auto ARoot = A->GetRootComponent();
				auto BRoot = B->GetRootComponent();
				if (ARoot != nullptr && BRoot != nullptr)
				{
					auto AUIRoot = Cast<ULexWidget>(ARoot);
					auto BUIRoot = Cast<ULexWidget>(BRoot);
					if (AUIRoot != nullptr && BUIRoot != nullptr)
					{
						return AUIRoot->GetSiblingIndex() < BUIRoot->GetSiblingIndex();//compare hierarch index for UI actor
					}
				}
				else
				{
					//sort on ActorLabel so the Tick function can be predictable because deserialize order is determinate.
					return A->GetActorLabel().Compare(B->GetActorLabel()) < 0;//compare name for normal actor
				}
				return false;
				});
			});
		ULGUIPrefabManagerObject::OnDeserialize_ProcessComponentsBeforeRerunConstructionScript.BindStatic([](const TArray<UActorComponent*>& Components) {
			for (auto& Comp : Components)
			{
				if (auto Widget = Cast<ULexWidget>(Comp))
				{
					Widget->CalculateTransformFromAnchor();
				}
			}
			});

		ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseClick.BindStatic([](UWorld* World, const FVector& RayOrigin, const FVector& RayDirection, AActor*& ClickHitActor) {
			if (auto LGUIManager = ULexUIManagerWorldSubsystem::GetInstance(World))
			{
				float LineTraceLength = 100000;
				//find hit LexVisualBatchMesh
				auto LineStart = RayOrigin;
				auto LineEnd = RayOrigin + RayDirection * LineTraceLength;
				ULexWidget* ClickHitUI = nullptr;
				static TArray<ULexWidget*> AllWidgetArray;
				AllWidgetArray.Reset();
				{
					for (auto& CanvasItem : LGUIManager->GetCanvasArray(ELexRenderMode::ScreenSpaceOverlay))
					{
						AllWidgetArray.Append(CanvasItem->GetVisualWidgetArray());
					}
					for (auto& CanvasItem : LGUIManager->GetCanvasArray(ELexRenderMode::RenderTarget))
					{
						AllWidgetArray.Append(CanvasItem->GetVisualWidgetArray());
					}
					for (auto& CanvasItem : LGUIManager->GetCanvasArray(ELexRenderMode::WorldSpace))
					{
						AllWidgetArray.Append(CanvasItem->GetVisualWidgetArray());
					}
					for (auto& CanvasItem : LGUIManager->GetCanvasArray(ELexRenderMode::WorldSpace_LexUI))
					{
						AllWidgetArray.Append(CanvasItem->GetVisualWidgetArray());
					}
				}
				if (ULexUIManagerWorldSubsystem::RaycastHitUI(World, AllWidgetArray, LineStart, LineEnd, ClickHitUI, ULexUIEditorManagerObject::IndexOfClickSelectUI))
				{
					ClickHitActor = ClickHitUI->GetOwner();
				}
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseMove.BindStatic([](UWorld* World) {
			ULexUIEditorManagerObject::IndexOfClickSelectUI = INDEX_NONE;
			});

		ULGUIPrefabManagerObject::OnPrefabEditor_CreateRootAgent.BindStatic([](UWorld* World, UClass* RootActorClass, ULGUIPrefab* Prefab, AActor*& OutCreatedRootAgentActor)
			{
				if (RootActorClass->IsChildOf(ALexWidgetActor::StaticClass()))//ui
				{
					auto CanvasSize = Prefab->PrefabDataForPrefabEditor.CanvasSize;
					//create Canvas for UI
					auto RootUICanvasActor = World->SpawnActor<ALexWidgetActor>(ALexWidgetActor::StaticClass(), FTransform::Identity);
					RootUICanvasActor->GetRootComponent()->SetWorldLocationAndRotationNoPhysics(FVector::ZeroVector, FRotator(0, 0, 0));

					if (Prefab->PrefabDataForPrefabEditor.bNeedCanvas)
					{
						auto RenderMode = (ELexRenderMode)Prefab->PrefabDataForPrefabEditor.CanvasRenderMode;
						auto CanvasComp = NewObject<ULexCanvas>(RootUICanvasActor);
						CanvasComp->RegisterComponent();
						RootUICanvasActor->AddInstanceComponent(CanvasComp);
						CanvasComp->SetRenderMode(RenderMode);
						CanvasComp->bFixedSizeInEditMode = true;
					}

					RootUICanvasActor->GetLexWidget()->SetWidth(CanvasSize.X);
					RootUICanvasActor->GetLexWidget()->SetHeight(CanvasSize.Y);
					RootUICanvasActor->GetLexWidget()->SetSiblingIndex(0);

					OutCreatedRootAgentActor = RootUICanvasActor;
				}
				else//not ui
				{
					auto CreatedActor = World->SpawnActor<AActor>(AActor::StaticClass(), FTransform::Identity, FActorSpawnParameters());
					//create SceneComponent
					{
						USceneComponent* RootComponent = NewObject<USceneComponent>(CreatedActor, USceneComponent::GetDefaultSceneRootVariableName(), RF_Transactional);
						RootComponent->Mobility = EComponentMobility::Static;
						RootComponent->bVisualizeComponent = false;

						CreatedActor->SetRootComponent(RootComponent);
						RootComponent->RegisterComponent();
						CreatedActor->AddInstanceComponent(RootComponent);
					}
					OutCreatedRootAgentActor = CreatedActor;
				}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_GetBounds.BindStatic([](USceneComponent* SceneComp, FBox& OutBounds, bool& OutValidBounds)
			{
				if (auto Widget = Cast<ULexWidget>(SceneComp))
				{
					if (Widget->GetWidgetActiveInHierarchy())
					{
						OutBounds = Widget->Bounds.GetBox();
						OutValidBounds = true;
					}
				}
				else if (auto PrimitiveComp = Cast<UPrimitiveComponent>(SceneComp))
				{
					OutBounds = PrimitiveComp->Bounds.GetBox();
					OutValidBounds = true;
				}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_SavePrefab.BindStatic([](AActor* RootAgentActor, ULGUIPrefab* Prefab)
			{
				if (auto Widget = Cast<ULexWidget>(RootAgentActor->GetRootComponent()))
				{
					Prefab->PrefabDataForPrefabEditor.CanvasSize = FIntPoint(Widget->GetWidth(), Widget->GetHeight());
				}
				if (auto Canvas = RootAgentActor->FindComponentByClass<ULexCanvas>())
				{
					Prefab->PrefabDataForPrefabEditor.bNeedCanvas = true;
					Prefab->PrefabDataForPrefabEditor.CanvasRenderMode = (uint8)Canvas->GetRenderMode();
				}
				else
				{
					Prefab->PrefabDataForPrefabEditor.bNeedCanvas = false;
				}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_Refresh.BindStatic([]() {
			ULexUIManagerWorldSubsystem::RefreshAllUI();
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_ReplaceObjectPropertyForApplyOrRevert.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, FName& InPropertyName) {
			if (auto Widget = Cast<ULexWidget>(InObject))
			{
				if (InPropertyName == USceneComponent::GetRelativeLocationPropertyName())
				{
					InPropertyName = ULexWidget::GetPropertyName_AnchorData();
				}
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_AfterObjectPropertyApplyOrRevert.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, FName InPropertyName) {
			if (auto Widget = Cast<ULexWidget>(InObject))
			{
				if (InPropertyName == ULexWidget::GetPropertyName_AnchorData())
				{
					Widget->CalculateTransformFromAnchor();//calculate transform here, because when NotifyPropertyChanged the PostActorConstruction->MoveComponent will call then anchor will calculate from transform value which is wrong
					PrefabHelper->RemoveMemberPropertyFromSubPrefab(Widget->GetOwner(), InObject, USceneComponent::GetRelativeLocationPropertyName());//remove RelativeLocation override because Widget use AnchorData to calculate RelativeLocation
				}
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_AfterMakePrefabAsSubPrefab.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, AActor* InRootActor) {
			//mark HierarchyIndex as default override parameter
			auto RootComp = InRootActor->GetRootComponent();
			if (auto RootUIComp = Cast<ULexWidget>(RootComp))
			{
				PrefabHelper->AddMemberPropertyToSubPrefab(InRootActor, RootUIComp, ULexWidget::GetPropertyName_SiblingIndex());
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_AfterCollectPropertyToOverride.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, FName InPropertyName) {
			if (auto Widget = Cast<ULexWidget>(InObject))
			{
				if (InPropertyName == USceneComponent::GetRelativeLocationPropertyName())//if UI's relative location change, then record anchor data too
				{
					PrefabHelper->AddMemberPropertyToSubPrefab(Widget->GetOwner(), InObject, ULexWidget::GetPropertyName_AnchorData());
				}
				else if (InPropertyName == ULexWidget::GetPropertyName_AnchorData())//if UI's anchor data change, then record relative location too
				{
					PrefabHelper->AddMemberPropertyToSubPrefab(Widget->GetOwner(), InObject, USceneComponent::GetRelativeLocationPropertyName());
				}
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_CopyRootObjectParentAnchorData.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, UObject* OriginObject) {
			auto InObjectWidget = Cast<ULexWidget>(InObject);
			auto OriginObjectWidget = Cast<ULexWidget>(OriginObject);
			if (InObjectWidget != nullptr && OriginObjectWidget != nullptr)//if is Widget, we need to copy parent's property to origin object's parent property, to make anchor & location calculation right
			{
				auto InObjectParent = InObjectWidget->GetUIParent();
				auto OriginObjectParent = OriginObjectWidget->GetUIParent();
				if (InObjectParent != nullptr && OriginObjectParent != nullptr)
				{
					//copy relative location
					auto RelativeLocationProperty = FindFProperty<FProperty>(InObjectParent->GetClass(), USceneComponent::GetRelativeLocationPropertyName());
					RelativeLocationProperty->CopyCompleteValue_InContainer(OriginObjectParent, InObjectParent);
					FLexUIUtils::NotifyPropertyChanged(OriginObjectParent, RelativeLocationProperty);
					//copy anchor data
					auto AnchorDataProperty = FindFProperty<FProperty>(InObjectParent->GetClass(), ULexWidget::GetPropertyName_AnchorData());
					AnchorDataProperty->CopyCompleteValue_InContainer(OriginObjectParent, InObjectParent);
					FLexUIUtils::NotifyPropertyChanged(OriginObjectParent, AnchorDataProperty);
				}
			}
			});
#endif
	}
}
void ULexUIEditorManagerObject::BeginDestroy()
{
#if WITH_EDITORONLY_DATA
	if (OnAssetReimportDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			if (auto ImportSubsystem = GEditor->GetEditorSubsystem<UImportSubsystem>())
			{
				ImportSubsystem->OnAssetReimport.Remove(OnAssetReimportDelegateHandle);
			}
		}
	}
	if (OnActorLabelChangedDelegateHandle.IsValid())
	{
		FCoreDelegates::OnActorLabelChanged.Remove(OnActorLabelChangedDelegateHandle);
	}
	if (OnMapOpenedDelegateHandle.IsValid())
	{
		FEditorDelegates::OnMapOpened.Remove(OnMapOpenedDelegateHandle);
	}
	if (OnPackageReloadedDelegateHandle.IsValid())
	{
		FCoreUObjectDelegates::OnPackageReloaded.Remove(OnPackageReloadedDelegateHandle);
	}
	if (OnBlueprintPreCompileDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			GEditor->OnBlueprintPreCompile().Remove(OnBlueprintPreCompileDelegateHandle);
		}
	}
	if (OnBlueprintCompiledDelegateHandle.IsValid())
	{
		if (GEditor)
		{
			GEditor->OnBlueprintCompiled().Remove(OnBlueprintCompiledDelegateHandle);
		}
	}
#endif
	Instance = nullptr;
	Super::BeginDestroy();
}

void ULexUIEditorManagerObject::Tick(float DeltaTime)
{
#if WITH_EDITOR
	CheckEditorViewportIndexAndKey();
#endif
}
TStatId ULexUIEditorManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIEditorManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR
FDelegateHandle ULexUIEditorManagerObject::RegisterEditorViewportIndexAndKeyChange(const TFunction<void()>& InFunction)
{
	InitCheck();
	return Instance->EditorViewportIndexAndKeyChange.AddLambda(InFunction);
}
void ULexUIEditorManagerObject::UnregisterEditorViewportIndexAndKeyChange(const FDelegateHandle& InDelegateHandle)
{
	if (Instance != nullptr)
	{
		Instance->EditorViewportIndexAndKeyChange.Remove(InDelegateHandle);
	}
}

ULexUIEditorManagerObject* ULexUIEditorManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool ULexUIEditorManagerObject::InitCheck()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<ULexUIEditorManagerObject>();
		Instance->AddToRoot();
		UE_LOG(LGUI, Log, TEXT("[ULGUIManagerObject::InitCheck]No Instance for LGUIManagerObject, create!"));
		Instance->OnActorLabelChangedDelegateHandle = FCoreDelegates::OnActorLabelChanged.AddUObject(Instance, &ULexUIEditorManagerObject::OnActorLabelChanged);
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &ULexUIEditorManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &ULexUIEditorManagerObject::OnPackageReloaded);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &ULexUIEditorManagerObject::OnAssetReimport);
			//blueprint recompile
			Instance->OnBlueprintPreCompileDelegateHandle = GEditor->OnBlueprintPreCompile().AddUObject(Instance, &ULexUIEditorManagerObject::OnBlueprintPreCompile);
			Instance->OnBlueprintCompiledDelegateHandle = GEditor->OnBlueprintCompiled().AddUObject(Instance, &ULexUIEditorManagerObject::OnBlueprintCompiled);
		}
	}
	return true;
}

void ULexUIEditorManagerObject::OnBlueprintPreCompile(UBlueprint* InBlueprint)
{
	
}
void ULexUIEditorManagerObject::OnBlueprintCompiled()
{
	ULGUIPrefabManagerObject::AddOneShotTickFunction([] {
		ULexUIManagerWorldSubsystem::RefreshAllUI();
		});
}

void ULexUIEditorManagerObject::OnAssetReimport(UObject* asset)
{
	if (IsValid(asset))
	{
		auto textureAsset = Cast<UTexture2D>(asset);
		if (IsValid(textureAsset))
		{
			bool needToRebuildUI = false;
			//find sprite data that reference this texture
			for (TObjectIterator<ULexUISpriteData> Itr; Itr; ++Itr)
			{
				ULexUISpriteData* spriteData = *Itr;
				if (IsValid(spriteData))
				{
					if (spriteData->GetSpriteTexture() == textureAsset)
					{
						spriteData->ReloadTexture();
						spriteData->MarkPackageDirty();
						needToRebuildUI = true;
					}
				}
			}
			//Refresh ui
			if (needToRebuildUI)
			{
				ULexUIManagerWorldSubsystem::RefreshAllUI();
			}
		}
	}
}

void ULexUIEditorManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void ULexUIEditorManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
		if (auto PrefabAsset = Cast<ULGUIPrefab>(Asset))
		{
			
		}
	}
}

void ULexUIEditorManagerObject::OnActorLabelChanged(AActor* actor)
{
	if (!IsValid(actor))return;
	auto World = actor->GetWorld();
	if (!IsValid(World))return;
	if (World->IsGameWorld())return;
	if (auto rootComp = actor->GetRootComponent())
	{
		if (auto rootUIComp = Cast<ULexWidget>(rootComp))
		{
			auto actorLabel = actor->GetActorLabel();
			if (actorLabel.StartsWith("//"))
			{
				actorLabel = actorLabel.Right(actorLabel.Len() - 2);
			}
			rootUIComp->SetDisplayName(actorLabel);

			FLexUIUtils::NotifyPropertyChanged(rootUIComp, FName(TEXT("displayName")));
		}
	}
}

void ULexUIEditorManagerObject::CheckEditorViewportIndexAndKey()
{
	if (!IsValid(GEditor))return;
	auto& viewportClients = GEditor->GetAllViewportClients();
	if (PrevEditorViewportCount != viewportClients.Num())
	{
		PrevEditorViewportCount = viewportClients.Num();
		EditorViewportIndexToKeyMap.Reset();
		for (FEditorViewportClient* viewportClient : viewportClients)
		{
			auto viewKey = viewportClient->ViewState.GetReference()->GetViewKey();
			EditorViewportIndexToKeyMap.Add(viewportClient->ViewIndex, viewKey);
		}

		if (EditorViewportIndexAndKeyChange.IsBound())
		{
			EditorViewportIndexAndKeyChange.Broadcast();
		}
	}

	if (auto viewport = GEditor->GetActiveViewport())
	{
		if (auto viewportClient = viewport->GetClient())
		{
			if (ULexUIEditorManagerObject::Instance != nullptr)
			{
				auto editorViewportClient = (FEditorViewportClient*)viewportClient;
				CurrentActiveViewportIndex = editorViewportClient->ViewIndex;
				CurrentActiveViewportKey = ULexUIEditorManagerObject::Instance->GetViewportKeyFromIndex(editorViewportClient->ViewIndex);
			}
		}
	}
}
uint32 ULexUIEditorManagerObject::GetViewportKeyFromIndex(int32 InViewportIndex)
{
	if (auto key = EditorViewportIndexToKeyMap.Find(InViewportIndex))
	{
		return *key;
	}
	return 0;
}
#endif



#if WITH_EDITOR
void ULexUIManagerWorldSubsystem::DrawFrameOnWidget(ULexWidget* Widget, bool ScreenOrWorld)
{
	if (ULGUIPrefabManagerObject::IsSelected(Widget->GetOwner()))//select self
	{
		auto RectDrawColor = FColor(160, 160, 160, 255);//gray means normal object
		auto DrawWidget = [=](ULexWidget* InWidget, const FColor& Color)
		{
			auto WorldTransform = InWidget->GetComponentTransform();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - InWidget->GetPivot().X) * InWidget->GetWidth();
			RelativeOffset.Z = (0.5f - InWidget->GetPivot().Y) * InWidget->GetHeight();
			auto Extends = FVector(1, InWidget->GetWidth(), InWidget->GetHeight()) * 0.5f;
			ULexUIManagerWorldSubsystem::DrawDebugRect(InWidget->GetWorld()
				, RelativeOffset, FMatrix44f(WorldTransform.ToMatrixWithScale())
				, Extends * WorldTransform.GetScale3D(), Color
				, InWidget, InWidget->GetDisplayName(), ScreenOrWorld);
		};
		//parent
		if (auto Parent = Widget->GetUIParent())
		{
			DrawWidget(Parent, RectDrawColor);
		}
		//child
		for (auto& Child : Widget->GetUIChildren())
		{
			if (IsValid(Child) && IsValid(Child->GetOwner()))
			{
				DrawWidget(Child, RectDrawColor);
			}
		}
		//other object of same hierarchy is selected
		if (auto Parent = Widget->GetUIParent())
		{
			for (auto& SiblingWidget : Parent->GetUIChildren())
			{
				if (IsValid(SiblingWidget) && SiblingWidget != Widget)
				{
					DrawWidget(SiblingWidget, RectDrawColor);
				}
			}
		}

		//self
		{
			RectDrawColor = FColor(0, 255, 0, 255);//green means selected object
			auto WorldTransform = Widget->GetComponentTransform();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - Widget->GetPivot().X) * Widget->GetWidth();
			RelativeOffset.Z = (0.5f - Widget->GetPivot().Y) * Widget->GetHeight();
			auto Extends = FVector(1, Widget->GetWidth(), Widget->GetHeight()) * 0.5f;
			ULexUIManagerWorldSubsystem::DrawDebugRect(Widget->GetWorld()
				, RelativeOffset, FMatrix44f(WorldTransform.ToMatrixWithScale())
				, Extends * WorldTransform.GetScale3D(), RectDrawColor
				, Widget, Widget->GetDisplayName(), ScreenOrWorld);

			if (auto Visual = Cast<ULexVisual>(Widget->GetVisual()))
			{
				FVector Min, Max;
				Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
				auto GeometryBoundsDrawColor = FColor(255, 255, 0, 255);//yellow for geometry bounds
				auto GeometryBoundsExtends = (Max - Min) * 0.5f;
				auto GeometryRelativeOffset = (Min + Max) * 0.5f;
				if (Extends != GeometryBoundsExtends || RelativeOffset != GeometryRelativeOffset)
				{
					ULexUIManagerWorldSubsystem::DrawDebugRect(Widget->GetWorld()
						, GeometryRelativeOffset, FMatrix44f(WorldTransform.ToMatrixWithScale())
						, GeometryBoundsExtends, GeometryBoundsDrawColor
						, Widget->GetVisual() , FString::Printf(TEXT("%s.Visual"), *Widget->GetDisplayName()), ScreenOrWorld);
				}
			}
		}
	}
}

void ULexUIManagerWorldSubsystem::DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	if (InControlPoints.Num() != 4)return;
	TArray<FVector> ResultPoints;
	const int segment = FMath::Min(40, FMath::CeilToInt(FVector::Distance(InControlPoints[0], InControlPoints[3]) * 0.5f));

	auto CalculateCubicBezierPoint = [](float t, FVector p0, FVector p1, FVector p2, FVector p3)
	{
		float u = 1 - t;
		float tt = t * t;
		float uu = u * u;
		float uuu = uu * u;
		float ttt = tt * t;

		FVector p = uuu * p0;
		p += 3 * uu * t * p1;
		p += 3 * u * tt * p2;
		p += ttt * p3;

		return p;
	};

	ResultPoints.Add(InControlPoints[0]);
	for (int i = 1; i <= segment; i++)
	{
		float t = i / (float)segment;
		auto pixel = CalculateCubicBezierPoint(t, InControlPoints[0], InControlPoints[1], InControlPoints[2], InControlPoints[3]);
		ResultPoints.Add(pixel);
	}
	
	auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(InWorld, false);
	if (ViewExtension.IsValid())
	{
		TArray<FLexUIHelperLineVertex> Lines;
		//lines
		FVector prevPoint = ResultPoints[0];
		for (int i = 1; i < ResultPoints.Num(); i++)
		{
			new(Lines) FLexUIHelperLineVertex((FVector3f)prevPoint, InColor);
			new(Lines) FLexUIHelperLineVertex((FVector3f)ResultPoints[i], InColor);
			prevPoint = ResultPoints[i];
		}
		//arrow
		new(Lines) FLexUIHelperLineVertex((FVector3f)InControlPoints[3], InColor);
		new(Lines) FLexUIHelperLineVertex((FVector3f)InArrowPointA, InColor);
		new(Lines) FLexUIHelperLineVertex((FVector3f)InControlPoints[3], InColor);
		new(Lines) FLexUIHelperLineVertex((FVector3f)InArrowPointB, InColor);

		if (ScreenOrWorld)
		{
			ViewExtension->AddScreenSpaceLineRender(FLexUIHelperLineKey(Object, DebugName), FLexUIHelperLineRenderParameter(Lines, FMatrix44f::Identity));
		}
		else
		{
			ViewExtension->AddWorldSpaceLineRender(FLexUIHelperLineKey(Object, DebugName), FLexUIHelperLineRenderParameter(Lines, FMatrix44f::Identity));
		}
	}
}

void ULexUIManagerWorldSubsystem::DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectableComponent* InSelectable, bool IsScreenSpace)
{
	auto SourceWidget = InSelectable->GetLexWidget();
	if (!IsValid(SourceWidget))return;
	const FColor Color = ULGUIPrefabManagerObject::IsSelected(SourceWidget->GetOwner()) ? FColor(255, 255, 0, 255) : FColor(140, 140, 0, 255);
	constexpr float Offset = 2;
	constexpr float ArrowSize = 5;
	
	auto GetArrowSizeScaledByDistanceToCamera = [=, this](FVector WorldPoint)
	{
		if (this->GetWorld()->IsGameWorld())
		{
			if (auto PC = this->GetWorld()->GetFirstPlayerController())
			{
				if (auto CameraManager = PC->PlayerCameraManager)
				{
					auto ViewLocation = CameraManager->GetCameraLocation();
					float Distance = FVector::Distance(WorldPoint, ViewLocation);
					return Distance * 0.01f;
				}
			}
		}
		else
		{
			if (auto ViewportClient = GetEditorViewportClient())
			{
				auto ViewLocation = ViewportClient->GetViewLocation();
				float Distance = FVector::Distance(WorldPoint, ViewLocation);
				return Distance * 0.01f;
			}
		}
		return ArrowSize;
	};

	if (auto ToLeftComp = InSelectable->FindSelectableOnLeft())
	{
		if (ToLeftComp != InSelectable)
		{
			auto SourceLeftPoint = FVector(0, SourceWidget->GetLocalSpaceLeft(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) + Offset);
			SourceLeftPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceLeftPoint);
			auto DestWidget = ToLeftComp->GetLexWidget();
			auto LocalDestRightPoint = FVector(0, DestWidget->GetLocalSpaceRight(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) + Offset);
			auto DestRightPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint);
			float Distance = FVector::Distance(SourceLeftPoint, DestRightPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestRightPoint);
			}
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceLeftPoint,
					SourceLeftPoint - SourceWidget->GetRightVector() * Distance,
					DestRightPoint + DestWidget->GetRightVector() * Distance,
					DestRightPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationLeft"), *InSelectable->GetLexWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToRightComp = InSelectable->FindSelectableOnRight())
	{
		if (ToRightComp != InSelectable)
		{
			auto SourceRightPoint = FVector(0, SourceWidget->GetLocalSpaceRight(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) - Offset);
			SourceRightPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceRightPoint);
			auto DestWidget = ToRightComp->GetLexWidget();
			auto LocalDestLeftPoint = FVector(0, DestWidget->GetLocalSpaceLeft(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) - Offset);
			auto DestLeftPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint);
			float Distance = FVector::Distance(SourceRightPoint, DestLeftPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestLeftPoint);
			}
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceRightPoint,
					SourceRightPoint + SourceWidget->GetRightVector() * Distance,
					DestLeftPoint - DestWidget->GetRightVector() * Distance,
					DestLeftPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationRight"), *InSelectable->GetLexWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToDownComp = InSelectable->FindSelectableOnDown())
	{
		if (ToDownComp != InSelectable)
		{
			auto SourceDownPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) - Offset, SourceWidget->GetLocalSpaceBottom());
			SourceDownPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceDownPoint);
			auto DestWidget = ToDownComp->GetLexWidget();
			auto LocalDestUpPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) - Offset, DestWidget->GetLocalSpaceTop());
			auto DestUpPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint);
			float Distance = FVector::Distance(SourceDownPoint, DestUpPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestUpPoint);
			}
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceDownPoint,
					SourceDownPoint - SourceWidget->GetUpVector() * Distance,
					DestUpPoint + DestWidget->GetUpVector() * Distance,
					DestUpPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationDown"), *InSelectable->GetLexWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToUpComp = InSelectable->FindSelectableOnUp())
	{
		if (ToUpComp != InSelectable)
		{
			auto SourceUpPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) + Offset, SourceWidget->GetLocalSpaceTop());
			SourceUpPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceUpPoint);
			auto DestWidget = ToUpComp->GetLexWidget();
			auto LocalDestDownPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) + Offset, DestWidget->GetLocalSpaceBottom());
			auto DestDownPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint);
			float Distance = FVector::Distance(SourceUpPoint, DestDownPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestDownPoint);
			}
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceUpPoint,
					SourceUpPoint + SourceWidget->GetUpVector() * Distance,
					DestDownPoint - DestWidget->GetUpVector() * Distance,
					DestDownPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationUp"), *InSelectable->GetLexWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
}

FEditorViewportClient* ULexUIManagerWorldSubsystem::GetEditorViewportClient()
{
	if (CacheViewportClient == nullptr)
	{
		for (auto& ViewportClient : GEditor->GetAllViewportClients())
		{
			if (ViewportClient->GetWorld() == this->GetWorld())
			{
				if (ViewportClient->IsVisible())
				{
					CacheViewportClient = ViewportClient;
				}
			}
		}
	}
	return CacheViewportClient;
}

void ULexUIManagerWorldSubsystem::OnEndOfFrame()
{
	CacheViewportClient = nullptr;
}

void ULexUIManagerWorldSubsystem::DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix44f& LocalToWorld, FVector const& Box, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	auto ViewExtension = ULexUIManagerWorldSubsystem::GetViewExtension(InWorld, false);
	if (ViewExtension.IsValid())
	{
		TArray<FLexUIHelperLineVertex> Lines;

		FVector Start = FVector(Box.X, Box.Y, Box.Z);
		FVector End = FVector(Box.X, -Box.Y, Box.Z);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = FVector(Box.X, Box.Y, -Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(Box.X, Box.Y, -Box.Z);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = FVector(Box.X, -Box.Y, Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		if (ScreenOrWorld)
		{
			ViewExtension->AddScreenSpaceLineRender(FLexUIHelperLineKey(Object, DebugName), FLexUIHelperLineRenderParameter(Lines, LocalToWorld));
		}
		else
		{
			ViewExtension->AddWorldSpaceLineRender(FLexUIHelperLineKey(Object, DebugName), FLexUIHelperLineRenderParameter(Lines, LocalToWorld));
		}
	}
}

bool ULexUIManagerWorldSubsystem::RaycastHitUI(UWorld* InWorld, const TArray<ULexWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
	, ULexWidget*& ResultSelectTarget, int& InOutTargetIndexInHitArray
)
{
	TArray<FHitResult> HitResultArray;
	for (auto Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		if (Widget->GetWorld() == InWorld)
		{
			if (auto Visual = Widget->GetVisual())
			{
				if (Widget->GetWidgetActiveInHierarchy() && Widget->GetRenderCanvas() != nullptr)
				{
					FHitResult hitInfo;
					auto OriginRaycastType = Visual->GetRaycastType();
					auto OriginVisibility = Widget->GetRaycastable();
					Visual->SetRaycastType(ELexVisualRaycastType::Mesh);//in editor selection, make the ray hit actural triangle
					Widget->SetRaycastable(ELexWidgetRaycastableType::Enabled);
					if (Visual->LineTraceUI(hitInfo, LineStart, LineEnd))
					{
						if (Widget->IsPointVisibleOnClip(hitInfo.Location))
						{
							HitResultArray.Add(hitInfo);
						}
					}
					Visual->SetRaycastType(OriginRaycastType);
					Widget->SetRaycastable(OriginVisibility);
				}
			}
		}
	}
	if (HitResultArray.Num() > 0)//hit something
	{
		HitResultArray.Sort([](const FHitResult& A, const FHitResult& B)
			{
				auto AWidget = (ULexWidget*)(A.Component.Get());
				auto BWidget = (ULexWidget*)(B.Component.Get());
				if (AWidget->GetRenderCanvas()->GetActualSortOrder() == BWidget->GetRenderCanvas()->GetActualSortOrder())//if Canvas's sort order is equal then sort on item's depth
				{
					return AWidget->GetFlattenHierarchyIndex() > BWidget->GetFlattenHierarchyIndex();
				}
				else//if Canvas's depth not equal then sort on Canvas's SortOrder
				{
					return AWidget->GetRenderCanvas()->GetActualSortOrder() > BWidget->GetRenderCanvas()->GetActualSortOrder();
				}
			});
		InOutTargetIndexInHitArray++;
		if (InOutTargetIndexInHitArray >= HitResultArray.Num() || InOutTargetIndexInHitArray < 0)
		{
			InOutTargetIndexInHitArray = 0;
		}
		auto HitWidget = (ULexWidget*)(HitResultArray[InOutTargetIndexInHitArray].Component.Get());//target need to select
		ResultSelectTarget = HitWidget;
		return true;
	}
	return false;
}
#endif

void ULexUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_EDITOR
	InstanceArray.Add(this);
	if (this->GetWorld()->WorldType == EWorldType::EditorPreview//EditorPreview world don't tick, so mannually tick it
		|| this->GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorTickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("LGUIManagerEditorTick"), 0, [WeakThis = MakeWeakObjectPtr(this)](float DeltaTime) {
			if (WeakThis.IsValid())
			{
				WeakThis->Tick(DeltaTime);
				return true;
			}
			return false;
			});
	}
	if (this->GetWorld()->IsGameWorld())
	{
		bIsPlaying = true;
	}
	FCoreDelegates::OnEndFrame.AddUObject(this, &ULexUIManagerWorldSubsystem::OnEndOfFrame);
#endif
	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &ULexUIManagerWorldSubsystem::OnCultureChanged);
}
void ULexUIManagerWorldSubsystem::PostInitialize()
{
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	check(PrefabManager);
	PrefabManager->OnBeginDeserializeSession.AddUObject(this, &ULexUIManagerWorldSubsystem::BeginPrefabSystemProcessingActor);
	PrefabManager->OnEndDeserializeSession.AddUObject(this, &ULexUIManagerWorldSubsystem::EndPrefabSystemProcessingActor);
}
void ULexUIManagerWorldSubsystem::Deinitialize()
{
	Super::Deinitialize();
#if WITH_EDITOR
	InstanceArray.Remove(this);
	if (EditorTickDelegateHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickDelegateHandle);
		EditorTickDelegateHandle.Reset();
	}
	if (this->GetWorld()->IsGameWorld())
	{
		bIsPlaying = false;
	}
#endif
	if (MainViewportViewExtension.IsValid())
	{
		MainViewportViewExtension.Reset();
	}
	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
}
TStatId ULexUIManagerWorldSubsystem::GetStatId() const
{
	//return GetStatID();
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIManagerWorldSubsystem, STATGROUP_Tickables);
}
bool ULexUIManagerWorldSubsystem::IsTickableWhenPaused() const
{
	return true;
}

void ULexUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

ULexUIManagerWorldSubsystem* ULexUIManagerWorldSubsystem::GetInstance(UWorld* InWorld)
{
	if (FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(InWorld))
	{
		return InWorld->GetSubsystem<ULexUIManagerWorldSubsystem>();
	}
	return nullptr;
}

#if WITH_EDITOR
TArray<ULexUIManagerWorldSubsystem*> ULexUIManagerWorldSubsystem::InstanceArray;
bool ULexUIManagerWorldSubsystem::bIsPlaying = false;
#endif

DECLARE_CYCLE_STAT(TEXT("LGUILifeCycleBehaviour Update"), STAT_LGUILifeCycleBehaviourUpdate, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("LGUILifeCycleBehaviour Start"), STAT_LGUILifeCycleBehaviourStart, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas Update"), STAT_UpdateCanvas, STATGROUP_LGUI);
void ULexUIManagerWorldSubsystem::Tick(float DeltaTime)
{
	//editor draw helper frame
#if WITH_EDITOR
	if (IsValid(GEditor))
	{
		auto Settings = GetDefault<ULexUIEditorSettings>();
		if (Settings->bDrawHelperFrame && GEditor->GetSelectedActorCount() > 0)
		{
			if (this->GetWorld()->WorldType == EWorldType::Game
				|| this->GetWorld()->WorldType == EWorldType::PIE
				|| this->GetWorld()->WorldType == EWorldType::Editor
				|| this->GetWorld()->WorldType == EWorldType::EditorPreview
				)
			{
				auto DrawFrame = [this](const TArray<TWeakObjectPtr<ULexCanvas>>& CanvasArray) {
					for (auto& Canvas : CanvasArray)
					{
						auto& WidgetArray = Canvas->GetWidgetArray();
						for (auto& Widget : WidgetArray)
						{
							if (!IsValid(Widget))continue;

							bool bIsScreenSpace = false;
							if (Widget->GetWorld()->IsGameWorld())
							{
								auto RenderCanvas = Widget->GetRenderCanvas();
								bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
							}
							DrawFrameOnWidget(Widget, bIsScreenSpace);
						}
					}
					};
				DrawFrame(ScreenSpaceCanvasArray);
				DrawFrame(WorldSpaceUECanvasArray);
				DrawFrame(WorldSpaceLexCanvasArray);
				DrawFrame(RenderTargetSpaceLexUICanvasArray);
			}
		}

		if (Settings->bDrawSelectableNavigationVisualizer)
		{
			for (auto& Selectable : AllSelectableArray)
			{
				if (!Selectable.IsValid())continue;
				if (!IsValid(Selectable->GetWorld()))continue;
				if (!IsValid(Selectable->GetLexWidget()))continue;
				if (!IsValid(Selectable->GetLexWidget()->GetRenderCanvas()))continue;
				if (!Selectable->GetLexWidget()->GetRaycastableInHierarchy())continue;

				bool bIsScreenSpace = false;
				if (Selectable->GetWorld()->IsGameWorld())
				{
					auto RenderCanvas = Selectable->GetLexWidget()->GetRenderCanvas();
					bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
				}
				DrawNavigationVisualizerOnUISelectable(Selectable->GetWorld(), Selectable.Get()
					, bIsScreenSpace);
			}
		}
	}
#endif

	//Update culture
	{
		if (bShouldUpdateOnCultureChanged)
		{
			bShouldUpdateOnCultureChanged = false;
			for (auto& Culture : AllCultureChangedArray)
			{
				ILexUICultureChangedInterface::Execute_OnCultureChanged(Culture.Get());
			}
		}
	}

	//LGUILifeCycleBehaviour start
	{
		if (LexUIBehavioursForStart.Num() > 0)
		{
			bIsExecutingStart = true;
			SCOPE_CYCLE_COUNTER(STAT_LGUILifeCycleBehaviourStart);
			for (int i = 0; i < LexUIBehavioursForStart.Num(); i++)
			{
				auto item = LexUIBehavioursForStart[i];
				if (item.IsValid())
				{
					item->Call_Start();
					if (item->bCanExecuteUpdate)
					{
						LexUIBehavioursForUpdate.AddUnique(item);
					}
				}
			}
			LexUIBehavioursForStart.Reset();
			bIsExecutingStart = false;
		}
	}

	//LGUILifeCycleBehaviour update
	{
		bIsExecutingUpdate = true;
		auto bIsGamePaused = GetWorld()->IsPaused();
		auto Settings = GetDefault<ULexUISettings>();
		SCOPE_CYCLE_COUNTER(STAT_LGUILifeCycleBehaviourUpdate);
		for (int i = 0; i < LexUIBehavioursForUpdate.Num(); i++)
		{
			CurrentExecutingUpdateIndex = i;
			auto item = LexUIBehavioursForUpdate[i];
			if (item.IsValid())
			{
				if (item->GetRootSceneComponent() && item->GetRootSceneComponent()->IsA(ULexWidget::StaticClass()))
				{
					auto uiItem = (ULexWidget*)item->GetRootSceneComponent();
					bool bAffectByGamePause;
					if (uiItem->IsScreenSpaceOverlayUI())
					{
						bAffectByGamePause = Settings->bScreenSpaceUIAffectByGamePause;
					}
					else
					{
						bAffectByGamePause = Settings->bWorldSpaceUIAffectByGamePause;
					}
					if (!bIsGamePaused || (bIsGamePaused && !bAffectByGamePause))
					{
						item->Update(DeltaTime);
					}
				}
				else
				{
					if (!bIsGamePaused || (bIsGamePaused && item->PrimaryComponentTick.bTickEvenWhenPaused))
					{
						item->Update(DeltaTime);
					}
				}
			}
		}
		bIsExecutingUpdate = false;
		CurrentExecutingUpdateIndex = -1;
		//remove these padding things
		if (LexUIBehavioursNeedToRemoveFromUpdate.Num() > 0)
		{
			for (auto& item : LexUIBehavioursNeedToRemoveFromUpdate)
			{
				LexUIBehavioursForUpdate.Remove(item);
			}
			LexUIBehavioursNeedToRemoveFromUpdate.Reset();
		}
	}

#if WITH_EDITOR
	int ScreenSpaceOverlayCanvasCount = 0;
	for (auto& Canvas : ScreenSpaceCanvasArray)
	{
		if (Canvas.IsValid())
		{
			if (Canvas->IsRootCanvas())
			{
				if (Canvas->GetActualRenderMode() == ELexRenderMode::ScreenSpaceOverlay)
				{
					ScreenSpaceOverlayCanvasCount++;
				}
			}
		}
	}
	if (ScreenSpaceOverlayCanvasCount > 1)
	{
		if (PrevScreenSpaceOverlayCanvasCount != ScreenSpaceOverlayCanvasCount)//only show message when change
		{
			PrevScreenSpaceOverlayCanvasCount = ScreenSpaceOverlayCanvasCount;
			auto errMsg = FText::Format(LOCTEXT("MultipleLexUICanvasRenderScreenSpaceOverlay", "[{0}].{1} Detect multiple LexCanvas rendered with ScreenSpaceOverlay mode, this is not allowed! There should be only one ScreenSpace UI in a world!\
\n	World: {2}, type: {3}")
			, FText::FromString(ANSI_TO_TCHAR(__FUNCTION__)), __LINE__, FText::FromString(this->GetWorld()->GetPathName()), (int)(this->GetWorld()->WorldType));
			UE_LOG(LGUI, Error, TEXT("%s"), *errMsg.ToString());
			FLexUIUtils::EditorNotification(errMsg, 10.0f);
		}
	}
	else
	{
		PrevScreenSpaceOverlayCanvasCount = 0;
	}
#endif

	//update draw-call
	{
		SCOPE_CYCLE_COUNTER(STAT_UpdateCanvas);
		auto UpdateCanvas = [](TArray<TWeakObjectPtr<ULexCanvas>>& InCanvasArray) {
			for (auto& Canvas : InCanvasArray)
			{
				if (Canvas.IsValid())
				{
					Canvas->UpdateRootCanvas();
				}
			}
		};
		UpdateCanvas(ScreenSpaceCanvasArray);
		UpdateCanvas(WorldSpaceUECanvasArray);
		UpdateCanvas(WorldSpaceLexCanvasArray);
		UpdateCanvas(RenderTargetSpaceLexUICanvasArray);
	}
}

void ULexUIManagerWorldSubsystem::AddLexUIBehaviourForLifecycleEvent(ULexUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto SessionId = ULGUIPrefabWorldSubsystem::GetInstance(InComp->GetWorld())->GetPrefabSystemSessionIdForActor(InComp->GetOwner());
			if (SessionId.IsValid())//processing by prefab system, collect for further operation
			{
				if (auto ArrayPtr = Instance->LexUIBehaviours_PrefabSystemProcessing.Find(SessionId))
				{
					auto& CompArray = ArrayPtr->LGUILifeCycleBehaviourArray;
#if !UE_BUILD_SHIPPING
					if (CompArray.Contains(InComp))
					{
						UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
						FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
						return;
					}
#endif
					CompArray.Add(InComp);
				}
			}
			else//not processed by prefab system, just do immediately
			{
				Instance->ProcessLexUILifecycleEvent(InComp);
			}
		}
	}
}

void ULexUIManagerWorldSubsystem::AddLexUIBehavioursForUpdate(ULexUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->LexUIBehavioursForUpdate.Find(InComp, index))
			{
				Instance->LexUIBehavioursForUpdate.Add(InComp);
				return;
			}
			UE_LOG(LGUI, Warning, TEXT("[%s].%d Already exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
		}
	}
}
void ULexUIManagerWorldSubsystem::RemoveLexUIBehavioursFromUpdate(ULexUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& updateArray = Instance->LexUIBehavioursForUpdate;
			int32 index = INDEX_NONE;
			if (updateArray.Find(InComp, index))
			{
				if (Instance->bIsExecutingUpdate)
				{
					if (index > Instance->CurrentExecutingUpdateIndex)//not execute it yet, safe to remove
					{
						updateArray.RemoveAt(index);
					}
					else//already execute or current execute it, not safe to remove. should remove it after execute process complete
					{
						Instance->LexUIBehavioursNeedToRemoveFromUpdate.Add(InComp);
					}
				}
				else//not executing update, safe to remove
				{
					updateArray.RemoveAt(index);
				}
			}
			else
			{
				UE_LOG(LGUI, Warning, TEXT("[ULGUIManagerWorldSubsystem::RemoveLGUILifeCycleBehavioursFromUpdate]Not exist, comp:%s"), *(InComp->GetPathName()));
			}

			//cleanup array
			int inValidCount = 0;
			for (int i = updateArray.Num() - 1; i >= 0; i--)
			{
				if (!updateArray[i].IsValid())
				{
					updateArray.RemoveAt(i);
					inValidCount++;
				}
			}
			if (inValidCount > 0)
			{
				UE_LOG(LGUI, Warning, TEXT("[ULGUIManagerWorldSubsystem::RemoveLGUILifeCycleBehavioursFromUpdate]Cleanup %d invalid LGUILifeCycleBehaviour"), inValidCount);
			}
		}
	}
}

void ULexUIManagerWorldSubsystem::RegisterLexUICultureChangedEvent(TScriptInterface<ILexUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.AddUnique(InItem.GetObject());
	}
}
void ULexUIManagerWorldSubsystem::UnregisterLexUICultureChangedEvent(TScriptInterface<ILexUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.RemoveSingle(InItem.GetObject());
	}
}

#if WITH_EDITOR
void ULexUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
{
	for (auto InstanceItem : InstanceArray)
	{
		if (InstanceItem != nullptr)
		{
			if (InWorld != nullptr && InstanceItem->GetWorld() != InWorld)
			{
				continue;
			}
		}
		auto Instance = InstanceItem;
		for (auto& RootWidget : Instance->AllRootWidgetArray)
		{
			if (RootWidget.IsValid())
			{
				RootWidget->EnsureDataForRebuild();
				RootWidget->EditorForceUpdate();
			}
		}
	}
}

void ULexUIManagerWorldSubsystem::AddRootWidget(ULexWidget* InWidget)
{
	if (auto Instance = GetInstance(InWidget->GetWorld()))
	{
#if !UE_BUILD_SHIPPING
		if (Instance->AllRootWidgetArray.Contains(InWidget))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		Instance->AllRootWidgetArray.AddUnique(InWidget);
	}
}
void ULexUIManagerWorldSubsystem::RemoveRootWidget(ULexWidget* InWidget)
{
	if (auto Instance = GetInstance(InWidget->GetWorld()))
	{
#if !UE_BUILD_SHIPPING
		if (!Instance->AllRootWidgetArray.Contains(InWidget))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		Instance->AllRootWidgetArray.RemoveSingle(InWidget);
	}
}
#endif

void ULexUIManagerWorldSubsystem::AddCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode)
{
	if (auto Instance = GetInstance(InCanvas->GetWorld()))
	{
		if (InCurrentRenderMode != ELexRenderMode::None)//none means canvas's property not ready yet, so no need to collect it, because it will be collected in "CanvasRenderModeChange" function
		{
			switch (InCurrentRenderMode)
			{
			case ELexRenderMode::ScreenSpaceOverlay:
				Instance->ScreenSpaceCanvasArray.Add(InCanvas);
				break;
			case ELexRenderMode::WorldSpace:
				Instance->WorldSpaceUECanvasArray.Add(InCanvas);
				break;
			case ELexRenderMode::WorldSpace_LexUI:
				Instance->WorldSpaceLexCanvasArray.Add(InCanvas);
				break;
			case ELexRenderMode::RenderTarget:
				Instance->RenderTargetSpaceLexUICanvasArray.Add(InCanvas);
				break;
			}
		}
	}
}
void ULexUIManagerWorldSubsystem::RemoveCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode)
{
	if (auto Instance = GetInstance(InCanvas->GetWorld()))
	{
		switch (InCurrentRenderMode)
		{
		case ELexRenderMode::ScreenSpaceOverlay:
			Instance->ScreenSpaceCanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::WorldSpace:
			Instance->WorldSpaceUECanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::WorldSpace_LexUI:
			Instance->WorldSpaceLexCanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLexUICanvasArray.Remove(InCanvas);
			break;
		}
	}
}
void ULexUIManagerWorldSubsystem::CanvasRenderModeChange(ULexCanvas* InCanvas, ELexRenderMode InOldRenderMode, ELexRenderMode InNewRenderMode)
{
	if (auto Instance = GetInstance(InCanvas->GetWorld()))
	{
		//remove from old
		switch (InOldRenderMode)
		{
		case ELexRenderMode::ScreenSpaceOverlay:
			Instance->ScreenSpaceCanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::WorldSpace:
			Instance->WorldSpaceUECanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::WorldSpace_LexUI:
			Instance->WorldSpaceLexCanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLexUICanvasArray.Remove(InCanvas);
			break;
		}
		//add to new
		switch (InNewRenderMode)
		{
		case ELexRenderMode::ScreenSpaceOverlay:
			Instance->ScreenSpaceCanvasArray.Add(InCanvas);
			break;
		case ELexRenderMode::WorldSpace:
			Instance->WorldSpaceUECanvasArray.Add(InCanvas);
			break;
		case ELexRenderMode::WorldSpace_LexUI:
			Instance->WorldSpaceLexCanvasArray.Add(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLexUICanvasArray.Add(InCanvas);
			break;
		}
	}
}
const TArray<TWeakObjectPtr<ULexCanvas>>& ULexUIManagerWorldSubsystem::GetCanvasArray(ELexRenderMode RenderMode)
{
	switch (RenderMode)
	{
	default://this should not happen
#if !UE_BUILD_SHIPPING
		UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
#endif
		return ScreenSpaceCanvasArray;
	case ELexRenderMode::ScreenSpaceOverlay:
		return ScreenSpaceCanvasArray;
	case ELexRenderMode::WorldSpace:
		return WorldSpaceUECanvasArray;
	case ELexRenderMode::WorldSpace_LexUI:
		return WorldSpaceLexCanvasArray;
	case ELexRenderMode::RenderTarget:
		return RenderTargetSpaceLexUICanvasArray;
	}
}

TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> ULexUIManagerWorldSubsystem::GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist)
{
	if (auto Instance = GetInstance(InWorld))
	{
		if (!Instance->MainViewportViewExtension.IsValid())
		{
			if (InCreateIfNotExist)
			{
				Instance->MainViewportViewExtension = FSceneViewExtensions::NewExtension<FLexUIRenderer>(InWorld, ELexUIRendererType::ScreenSpace_and_WorldSpace);
			}
		}
		return Instance->MainViewportViewExtension;
	}
	return nullptr;
}

void ULexUIManagerWorldSubsystem::AddRaycaster(ULexBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		auto& AllRaycasterArray = Instance->AllRaycasterArray;
		if (AllRaycasterArray.Contains(InRaycaster))return;
		//check multiple racaster
		for (auto& item : AllRaycasterArray)
		{
			if (InRaycaster->GetDepth() == item->GetDepth() && InRaycaster->GetTraceChannel() == item->GetTraceChannel())
			{
#if WITH_EDITOR
				auto ErrorNotifyMsg = LOCTEXT("MultipleLGUIBaseRaycasterWithSameDepthAndTraceChannel"
					, "Detect multiple LGUIBaseRaycaster components with same depth and traceChannel, this may cause wrong interaction results! See output log for details.");
				FLexUIUtils::EditorNotification(ErrorNotifyMsg, 10);
#endif
				UE_LOG(LGUI, Warning, TEXT("[%s].%d \
\nDetect multiple LGUIBaseRaycaster components with same depth and traceChannel, this may cause wrong interaction results!\
\neg: Want use mouse to click object A but get object B.\
\nPlease note : \
\n	For LGUIBaseRaycasters with same depth, LGUI will line trace them all and sort result on hit distance.\
\n	For LGUIBaseRaycasters with different depth, LGUI will sort raycasters on depth, and line trace from highest depth to lowest, if hit anything then stop line trace.\
\n	LGUIWorldSpaceInteraction is for all WorldSpaceUI in current level.\
"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);

				break;
			}
		}

		AllRaycasterArray.Add(InRaycaster);
		//sort depth
		AllRaycasterArray.Sort([](const TWeakObjectPtr<ULexBaseRaycaster>& A, const TWeakObjectPtr<ULexBaseRaycaster>& B)
		{
			return A->GetDepth() > B->GetDepth();
		});
	}
}
void ULexUIManagerWorldSubsystem::RemoveRaycaster(ULexBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		int32 index;
		if (Instance->AllRaycasterArray.Find(InRaycaster, index))
		{
			Instance->AllRaycasterArray.RemoveAt(index);
		}
	}
}

void ULexUIManagerWorldSubsystem::SetCurrentInputModule(ULexBaseInputModule* InInputModule)
{
	if (auto Instance = GetInstance(InInputModule->GetWorld()))
	{
		Instance->CurrentInputModule = InInputModule;
	}
}
void ULexUIManagerWorldSubsystem::ClearCurrentInputModule(ULexBaseInputModule* InInputModule)
{
	if (auto Instance = GetInstance(InInputModule->GetWorld()))
	{
		Instance->CurrentInputModule = nullptr;
	}
}

void ULexUIManagerWorldSubsystem::AddSelectable(UUISelectableComponent* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		auto& AllSelectableArray = Instance->AllSelectableArray;
#if !UE_BUILD_SHIPPING
		if (AllSelectableArray.Contains(InSelectable))
		{
			UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		AllSelectableArray.AddUnique(InSelectable);
	}
}
void ULexUIManagerWorldSubsystem::RemoveSelectable(UUISelectableComponent* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		int32 index;
		if (Instance->AllSelectableArray.Find(InSelectable, index))
		{
			Instance->AllSelectableArray.RemoveAt(index);
		}
	}
}

void ULexUIManagerWorldSubsystem::ProcessLexUILifecycleEvent(ULexUIBehaviour* InComp)
{
	if (InComp)
	{
		if (InComp->IsAllowToCallAwake())
		{
			if (!InComp->bIsAwakeCalled)
			{
				InComp->Call_Awake();
#if !UE_BUILD_SHIPPING
				check(!LexUIBehavioursForStart.Contains(InComp));
#endif
				LexUIBehavioursForStart.Add(InComp);
			}
		}
	}
}
void ULexUIManagerWorldSubsystem::BeginPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	FLGUILifeCycleBehaviourArrayContainer Container;
	LexUIBehaviours_PrefabSystemProcessing.Add(InSessionId, Container);
}
void ULexUIManagerWorldSubsystem::EndPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	if (auto ArrayPtr = LexUIBehaviours_PrefabSystemProcessing.Find(InSessionId))
	{
		auto& LateFunctions = ArrayPtr->Functions;
		for (auto& Function : LateFunctions)
		{
			Function();
		}

		auto& LGUILifeCycleBehaviourArray = ArrayPtr->LGUILifeCycleBehaviourArray;
		auto Count = LGUILifeCycleBehaviourArray.Num();
		for (int i = 0; i < Count; i++)
		{
			auto& Item = LGUILifeCycleBehaviourArray[i];
			if (Item.IsValid())
			{
				ProcessLexUILifecycleEvent(Item.Get());
			}
#if !UE_BUILD_SHIPPING
			if (LGUILifeCycleBehaviourArray.Num() != Count)
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
#endif
		}

		LexUIBehaviours_PrefabSystemProcessing.Remove(InSessionId);
	}
}
void ULexUIManagerWorldSubsystem::AddFunctionForPrefabSystemExecutionBeforeAwake(AActor* InPrefabActor, const TFunction<void()>& InFunction)
{
	auto SessionId = ULGUIPrefabWorldSubsystem::GetInstance(InPrefabActor->GetWorld())->GetPrefabSystemSessionIdForActor(InPrefabActor);
	if (SessionId.IsValid())
	{
		auto& Container = LexUIBehaviours_PrefabSystemProcessing[SessionId];
		Container.Functions.Add(InFunction);
	}
}
UE_ENABLE_OPTIMIZATION
#undef LOCTEXT_NAMESPACE