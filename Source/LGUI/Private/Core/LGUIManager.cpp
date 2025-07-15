// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Core/LGUIManager.h"
#include "LGUI.h"
#include "Utils/LexUIUtils.h"
#include "Core/Components/LexWidget.h"
#include "Core/Components/LexCanvas.h"
#include "Event/LGUIBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectableComponent.h"
#include "Core/LGUISettings.h"
#include "Event/InputModule/LGUIBaseInputModule.h"
#include "Core/Actor/LexWidgetActor.h"
#include "Core/Components/LexVisual.h"
#include "Engine/Engine.h"
#include "Core/LexUIRender/LexUIRenderer.h"
#include "Core/ILGUICultureChangedInterface.h"
#include "Core/LGUILifeCycleBehaviour.h"
#include "Core/Components/LexLayoutAnchor.h"
#include "PrefabSystem/LGUIPrefabManager.h"
#include "PrefabSystem/LGUIPrefabHelperObject.h"
#if WITH_EDITOR
#include "Editor.h"
#include "DrawDebugHelpers.h"
#include "EditorViewportClient.h"
#include "PrefabSystem/LGUIPrefab.h"
#include "Core/Components/LexCanvasScaler.h"
#include "Core/LexUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "LGUIManagerObject"

#if LGUI_CAN_DISABLE_OPTIMIZATION
PRAGMA_DISABLE_OPTIMIZATION
#endif

ULGUIEditorManagerObject* ULGUIEditorManagerObject::Instance = nullptr;
#if WITH_EDITOR
int ULGUIEditorManagerObject::IndexOfClickSelectUI = INDEX_NONE;
#endif
ULGUIEditorManagerObject::ULGUIEditorManagerObject()
{
	if (this == GetDefault<ULGUIEditorManagerObject>())
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
					if (auto LayoutSlot = Widget->GetLayoutSlot())
					{
						LayoutSlot->CalculateTransformFromLayout();
					}
				}
			}
			});

		ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseClick.BindStatic([](UWorld* World, const FVector& RayOrigin, const FVector& RayDirection, AActor*& ClickHitActor) {
			if (auto LGUIManager = ULGUIManagerWorldSubsystem::GetInstance(World))
			{
				float LineTraceLength = 100000;
				//find hit UIBatchMeshRenderable
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
					for (auto& CanvasItem : LGUIManager->GetCanvasArray(ELexRenderMode::WorldSpace_LGUI))
					{
						AllWidgetArray.Append(CanvasItem->GetVisualWidgetArray());
					}
				}
				if (ULGUIManagerWorldSubsystem::RaycastHitUI(World, AllWidgetArray, LineStart, LineEnd, ClickHitUI, ULGUIEditorManagerObject::IndexOfClickSelectUI))
				{
					ClickHitActor = ClickHitUI->GetOwner();
				}
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditorViewport_MouseMove.BindStatic([](UWorld* World) {
			ULGUIEditorManagerObject::IndexOfClickSelectUI = INDEX_NONE;
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
					}

					RootUICanvasActor->GetLexWidget()->SetSize(FLexWidgetSize2::MakeFixed(CanvasSize));
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
					if (Widget->IsVisibleForRender())
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
					Prefab->PrefabDataForPrefabEditor.CanvasSize = FIntPoint(Widget->GetRenderWidth(), Widget->GetRenderHeight());
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
			ULGUIManagerWorldSubsystem::RefreshAllUI();
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_ReplaceObjectPropertyForApplyOrRevert.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, FName& InPropertyName) {
			if (auto Widget = Cast<ULexWidget>(InObject))
			{
				// if (InPropertyName == USceneComponent::GetRelativeLocationPropertyName())
				// {
				// 	InPropertyName = ULexWidget::GetAnchorDataPropertyName();
				// }
			}
			});
		ULGUIPrefabManagerObject::OnPrefabEditor_AfterObjectPropertyApplyOrRevert.BindStatic([](ULGUIPrefabHelperObject* PrefabHelper, UObject* InObject, FName InPropertyName) {
			if (auto Widget = Cast<ULexWidget>(InObject))
			{
				// if (InPropertyName == ULexWidget::GetAnchorDataPropertyName())
				// {
				// 	Widget->CalculateTransformFromAnchor();//calculate transform here, because when NotifyPropertyChanged the PostActorConstruction->MoveComponent will call then anchor will calculate from transform value which is wrong
				// 	PrefabHelper->RemoveMemberPropertyFromSubPrefab(Widget->GetOwner(), InObject, USceneComponent::GetRelativeLocationPropertyName());//remove RelativeLocation override because Widget use AnchorData to calculate RelativeLocation
				// }
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
				// if (InPropertyName == USceneComponent::GetRelativeLocationPropertyName())//if UI's relative location change, then record anchor data too
				// {
				// 	PrefabHelper->AddMemberPropertyToSubPrefab(Widget->GetOwner(), InObject, ULexWidget::GetAnchorDataPropertyName());
				// }
				// else if (InPropertyName == ULexWidget::GetAnchorDataPropertyName())//if UI's anchor data change, then record relative location too
				// {
				// 	PrefabHelper->AddMemberPropertyToSubPrefab(Widget->GetOwner(), InObject, USceneComponent::GetRelativeLocationPropertyName());
				// }
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
					// auto AnchorDataProperty = FindFProperty<FProperty>(InObjectParent->GetClass(), ULexWidget::GetAnchorDataPropertyName());
					// AnchorDataProperty->CopyCompleteValue_InContainer(OriginObjectParent, InObjectParent);
					// FLexUIUtils::NotifyPropertyChanged(OriginObjectParent, AnchorDataProperty);
				}
			}
			});
#endif
	}
}
void ULGUIEditorManagerObject::BeginDestroy()
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

void ULGUIEditorManagerObject::Tick(float DeltaTime)
{
#if WITH_EDITOR
	CheckEditorViewportIndexAndKey();
#endif
}
TStatId ULGUIEditorManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIEditorManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR
FDelegateHandle ULGUIEditorManagerObject::RegisterEditorViewportIndexAndKeyChange(const TFunction<void()>& InFunction)
{
	InitCheck();
	return Instance->EditorViewportIndexAndKeyChange.AddLambda(InFunction);
}
void ULGUIEditorManagerObject::UnregisterEditorViewportIndexAndKeyChange(const FDelegateHandle& InDelegateHandle)
{
	if (Instance != nullptr)
	{
		Instance->EditorViewportIndexAndKeyChange.Remove(InDelegateHandle);
	}
}

ULGUIEditorManagerObject* ULGUIEditorManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool ULGUIEditorManagerObject::InitCheck()
{
	if (Instance == nullptr)
	{
		Instance = NewObject<ULGUIEditorManagerObject>();
		Instance->AddToRoot();
		UE_LOG(LGUI, Log, TEXT("[ULGUIManagerObject::InitCheck]No Instance for LGUIManagerObject, create!"));
		Instance->OnActorLabelChangedDelegateHandle = FCoreDelegates::OnActorLabelChanged.AddUObject(Instance, &ULGUIEditorManagerObject::OnActorLabelChanged);
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &ULGUIEditorManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &ULGUIEditorManagerObject::OnPackageReloaded);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &ULGUIEditorManagerObject::OnAssetReimport);
			//blueprint recompile
			Instance->OnBlueprintPreCompileDelegateHandle = GEditor->OnBlueprintPreCompile().AddUObject(Instance, &ULGUIEditorManagerObject::OnBlueprintPreCompile);
			Instance->OnBlueprintCompiledDelegateHandle = GEditor->OnBlueprintCompiled().AddUObject(Instance, &ULGUIEditorManagerObject::OnBlueprintCompiled);
		}
	}
	return true;
}

void ULGUIEditorManagerObject::OnBlueprintPreCompile(UBlueprint* InBlueprint)
{
	
}
void ULGUIEditorManagerObject::OnBlueprintCompiled()
{
	ULGUIPrefabManagerObject::AddOneShotTickFunction([] {
		ULGUIManagerWorldSubsystem::RefreshAllUI();
		});
}

void ULGUIEditorManagerObject::OnAssetReimport(UObject* asset)
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
				ULGUIManagerWorldSubsystem::RefreshAllUI();
			}
		}
	}
}

void ULGUIEditorManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void ULGUIEditorManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
		if (auto PrefabAsset = Cast<ULGUIPrefab>(Asset))
		{
			
		}
	}
}

void ULGUIEditorManagerObject::OnActorLabelChanged(AActor* actor)
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

void ULGUIEditorManagerObject::CheckEditorViewportIndexAndKey()
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
			if (ULGUIEditorManagerObject::Instance != nullptr)
			{
				auto editorViewportClient = (FEditorViewportClient*)viewportClient;
				CurrentActiveViewportIndex = editorViewportClient->ViewIndex;
				CurrentActiveViewportKey = ULGUIEditorManagerObject::Instance->GetViewportKeyFromIndex(editorViewportClient->ViewIndex);
			}
		}
	}
}
uint32 ULGUIEditorManagerObject::GetViewportKeyFromIndex(int32 InViewportIndex)
{
	if (auto key = EditorViewportIndexToKeyMap.Find(InViewportIndex))
	{
		return *key;
	}
	return 0;
}
#endif



#if WITH_EDITOR
void ULGUIManagerWorldSubsystem::DrawFrameOnWidget(ULexWidget* Widget, bool IsScreenSpace)
{
	auto RectExtends = FVector(0.1f, Widget->GetRenderWidth(), Widget->GetRenderHeight()) * 0.5f;
	bool bCanDrawRect = false;
	auto RectDrawColor = FColor(128, 128, 128, 128);//gray means normal object
	if (ULGUIPrefabManagerObject::IsSelected(Widget->GetOwner()))//select self
	{
		RectDrawColor = FColor(0, 255, 0, 255);//green means selected object
		RectExtends.X = 1;
		bCanDrawRect = true;

		if (auto Visual = Cast<ULexVisual>(Widget->GetVisual()))
		{
			FVector Min, Max;
			Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
			auto WorldTransform = Widget->GetComponentTransform();
			FVector Center = (Max + Min) * 0.5f;
			auto WorldLocation = WorldTransform.TransformPosition(Center);

			auto GeometryBoundsDrawColor = FColor(255, 255, 0, 255);//yellow for geometry bounds
			auto GeometryBoundsExtends = (Max - Min) * 0.5f;
			if (IsScreenSpace)
			{
				ULGUIManagerWorldSubsystem::DrawDebugRectOnScreenSpace(Widget->GetWorld(), WorldLocation, GeometryBoundsExtends * WorldTransform.GetScale3D(), WorldTransform.GetRotation(), GeometryBoundsDrawColor);
			}
			else
			{
				DrawDebugBox(Widget->GetWorld(), WorldLocation, GeometryBoundsExtends * WorldTransform.GetScale3D(), WorldTransform.GetRotation(), GeometryBoundsDrawColor);
			}
		}
	}
	else
	{
		//parent selected
		if (IsValid(Widget->GetUIParent()))
		{
			if (ULGUIPrefabManagerObject::IsSelected(Widget->GetUIParent()->GetOwner()))
			{
				bCanDrawRect = true;
			}
		}
		//child selected
		auto& childrenCompArray = Widget->GetUIChildren();
		for (auto& uiComp : childrenCompArray)
		{
			if (IsValid(uiComp) && IsValid(uiComp->GetOwner()) && ULGUIPrefabManagerObject::IsSelected(uiComp->GetOwner()))
			{
				bCanDrawRect = true;
				break;
			}
		}
		//other object of same hierarchy is selected
		if (IsValid(Widget->GetUIParent()))
		{
			const auto& sameLevelCompArray = Widget->GetUIParent()->GetUIChildren();
			for (auto& uiComp : sameLevelCompArray)
			{
				if (IsValid(uiComp) && IsValid(uiComp->GetOwner()) && ULGUIPrefabManagerObject::IsSelected(uiComp->GetOwner()))
				{
					bCanDrawRect = true;
					break;
				}
			}
		}
	}
	//canvas scaler
	if (!bCanDrawRect)
	{
		if (Widget->IsCanvasWidget())
		{
			if (auto canvasScaler = Widget->GetOwner()->FindComponentByClass<ULexCanvasScaler>())
			{
				if (ULGUIPrefabManagerObject::AnySelectedIsChildOf(Widget->GetOwner()))
				{
					bCanDrawRect = true;
					RectDrawColor = FColor(255, 227, 124);
				}
			}
		}
	}

	if (bCanDrawRect)
	{
		auto WorldTransform = Widget->GetComponentTransform();
		FVector RelativeOffset(0, 0, 0);
		RelativeOffset.Y = (0.5f - Widget->GetPivot().X) * Widget->GetRenderWidth();
		RelativeOffset.Z = (0.5f - Widget->GetPivot().Y) * Widget->GetRenderHeight();
		auto WorldLocation = WorldTransform.TransformPosition(RelativeOffset);

		if (IsScreenSpace)
		{
			ULGUIManagerWorldSubsystem::DrawDebugRectOnScreenSpace(Widget->GetWorld(), WorldLocation, RectExtends * WorldTransform.GetScale3D(), WorldTransform.GetRotation(), RectDrawColor);
		}
		else
		{
			DrawDebugBox(Widget->GetWorld(), WorldLocation, RectExtends * WorldTransform.GetScale3D(), WorldTransform.GetRotation(), RectDrawColor);
		}
	}
}

void ULGUIManagerWorldSubsystem::DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, bool IsScreenSpace)
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

	if (IsScreenSpace)
	{
		auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(InWorld, false);
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

			ViewExtension->AddLineRender(FLexUIHelperLineRenderParameter(Lines));
		}
	}
	else
	{
		//lines
		FVector prevPoint = ResultPoints[0];
		for (int i = 1; i < ResultPoints.Num(); i++)
		{
			DrawDebugLine(InWorld, prevPoint, ResultPoints[i], InColor);
			prevPoint = ResultPoints[i];
		}
		//arrow
		DrawDebugLine(InWorld, InControlPoints[3], InArrowPointA, InColor);
		DrawDebugLine(InWorld, InControlPoints[3], InArrowPointB, InColor);
	}
}

void ULGUIManagerWorldSubsystem::DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectableComponent* InSelectable, bool IsScreenSpace)
{
	auto SourceWidget = InSelectable->GetRootUIComponent();
	if (!IsValid(SourceWidget))return;
	const FColor Color = ULGUIPrefabManagerObject::IsSelected(SourceWidget->GetOwner()) ? FColor(255, 255, 0, 255) : FColor(140, 140, 0, 255);
	constexpr float Offset = 2;
	constexpr float ArrowSize = 2;

	if (auto ToLeftComp = InSelectable->FindSelectableOnLeft())
	{
		if (ToLeftComp != InSelectable)
		{
			auto SourceLeftPoint = FVector(0, SourceWidget->GetLocalSpaceLeft(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) + Offset);
			SourceLeftPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceLeftPoint);
			auto DestWidget = ToLeftComp->GetRootUIComponent();
			auto LocalDestRightPoint = FVector(0, DestWidget->GetLocalSpaceRight(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) + Offset);
			auto DestRightPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint);
			float Distance = FVector::Distance(SourceLeftPoint, DestRightPoint);
			Distance *= 0.2f;
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint + FVector(0, ArrowSize, ArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestRightPoint + FVector(0, ArrowSize, -ArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceLeftPoint,
					SourceLeftPoint - SourceWidget->GetRightVector() * Distance,
					DestRightPoint + DestWidget->GetRightVector() * Distance,
					DestRightPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, IsScreenSpace);
		}
	}
	if (auto ToRightComp = InSelectable->FindSelectableOnRight())
	{
		if (ToRightComp != InSelectable)
		{
			auto SourceRightPoint = FVector(0, SourceWidget->GetLocalSpaceRight(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) - Offset);
			SourceRightPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceRightPoint);
			auto DestWidget = ToRightComp->GetRootUIComponent();
			auto LocalDestLeftPoint = FVector(0, DestWidget->GetLocalSpaceLeft(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) - Offset);
			auto DestLeftPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint);
			float Distance = FVector::Distance(SourceRightPoint, DestLeftPoint);
			Distance *= 0.2f;
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ArrowSize, ArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ArrowSize, -ArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceRightPoint,
					SourceRightPoint + SourceWidget->GetRightVector() * Distance,
					DestLeftPoint - DestWidget->GetRightVector() * Distance,
					DestLeftPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, IsScreenSpace);
		}
	}
	if (auto ToDownComp = InSelectable->FindSelectableOnDown())
	{
		if (ToDownComp != InSelectable)
		{
			auto SourceDownPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) - Offset, SourceWidget->GetLocalSpaceBottom());
			SourceDownPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceDownPoint);
			auto DestWidget = ToDownComp->GetRootUIComponent();
			auto LocalDestUpPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) - Offset, DestWidget->GetLocalSpaceTop());
			auto DestUpPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint);
			float Distance = FVector::Distance(SourceDownPoint, DestUpPoint);
			Distance *= 0.2f;
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint + FVector(0, ArrowSize, ArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestUpPoint + FVector(0, -ArrowSize, ArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceDownPoint,
					SourceDownPoint - SourceWidget->GetUpVector() * Distance,
					DestUpPoint + DestWidget->GetUpVector() * Distance,
					DestUpPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, IsScreenSpace);
		}
	}
	if (auto ToUpComp = InSelectable->FindSelectableOnUp())
	{
		if (ToUpComp != InSelectable)
		{
			auto SourceUpPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) + Offset, SourceWidget->GetLocalSpaceTop());
			SourceUpPoint = SourceWidget->GetComponentTransform().TransformPosition(SourceUpPoint);
			auto DestWidget = ToUpComp->GetRootUIComponent();
			auto LocalDestDownPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) + Offset, DestWidget->GetLocalSpaceBottom());
			auto DestDownPoint = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint);
			float Distance = FVector::Distance(SourceUpPoint, DestDownPoint);
			Distance *= 0.2f;
			auto ArrowPointA = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint + FVector(0, ArrowSize, -ArrowSize));
			auto ArrowPointB = DestWidget->GetComponentTransform().TransformPosition(LocalDestDownPoint + FVector(0, -ArrowSize, -ArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceUpPoint,
					SourceUpPoint + SourceWidget->GetUpVector() * Distance,
					DestDownPoint - DestWidget->GetUpVector() * Distance,
					DestDownPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, IsScreenSpace);
		}
	}
}

void ULGUIManagerWorldSubsystem::DrawDebugBoxOnScreenSpace(UWorld* InWorld, FVector const& Center, FVector const& Box, const FQuat& Rotation, FColor const& Color)
{
	auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(InWorld, false);
	if (ViewExtension.IsValid())
	{
		TArray<FLexUIHelperLineVertex> Lines;

		FTransform const Transform(Rotation);
		FVector Start = Transform.TransformPosition(FVector(Box.X, Box.Y, Box.Z));
		FVector End = Transform.TransformPosition(FVector(Box.X, -Box.Y, Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, -Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, -Box.Y, Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, -Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, Box.Y, Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, Box.Y, Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, Box.Y, -Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, -Box.Y, -Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, -Box.Y, -Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, Box.Y, -Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, Box.Y, -Box.Z));
		new(Lines)FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines)FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, -Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, -Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(-Box.X, Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(-Box.X, Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		ViewExtension->AddLineRender(FLexUIHelperLineRenderParameter(Lines));
	}
}
void ULGUIManagerWorldSubsystem::DrawDebugRectOnScreenSpace(UWorld* InWorld, FVector const& Center, FVector const& Box, const FQuat& Rotation, FColor const& Color)
{
	auto ViewExtension = ULGUIManagerWorldSubsystem::GetViewExtension(InWorld, false);
	if (ViewExtension.IsValid())
	{
		TArray<FLexUIHelperLineVertex> Lines;

		FTransform const Transform(Rotation);
		FVector Start = Transform.TransformPosition(FVector(Box.X, Box.Y, Box.Z));
		FVector End = Transform.TransformPosition(FVector(Box.X, -Box.Y, Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, Box.Y, -Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		Start = Transform.TransformPosition(FVector(Box.X, -Box.Y, Box.Z));
		End = Transform.TransformPosition(FVector(Box.X, -Box.Y, -Box.Z));
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + Start), Color);
		new(Lines) FLexUIHelperLineVertex(FVector3f(Center + End), Color);

		ViewExtension->AddLineRender(FLexUIHelperLineRenderParameter(Lines));
	}
}

bool ULGUIManagerWorldSubsystem::RaycastHitUI(UWorld* InWorld, const TArray<ULexWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
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
				if (Widget->IsVisibleForRender() && Widget->GetRenderCanvas() != nullptr)
				{
					FHitResult hitInfo;
					auto OriginRaycastType = Visual->GetRaycastType();
					auto OriginVisibility = Widget->GetWidgetVisibility();
					Visual->SetRaycastType(ELexVisualHitTestType::Mesh);//in editor selection, make the ray hit actural triangle
					Widget->SetWidgetVisibility(ESlateVisibility::Visible);
					Widget->SetWidgetVisibility(ESlateVisibility::Visible);
					if (Visual->LineTraceUI(hitInfo, LineStart, LineEnd))
					{
						if (Widget->IsPointVisibleOnClip(hitInfo.Location))
						{
							HitResultArray.Add(hitInfo);
						}
					}
					Visual->SetRaycastType(OriginRaycastType);
					Widget->SetWidgetVisibility(OriginVisibility);
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

void ULGUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
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
#endif
	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &ULGUIManagerWorldSubsystem::OnCultureChanged);
}
void ULGUIManagerWorldSubsystem::PostInitialize()
{
	auto PrefabManager = ULGUIPrefabWorldSubsystem::GetInstance(this->GetWorld());
	check(PrefabManager);
	PrefabManager->OnBeginDeserializeSession.AddUObject(this, &ULGUIManagerWorldSubsystem::BeginPrefabSystemProcessingActor);
	PrefabManager->OnEndDeserializeSession.AddUObject(this, &ULGUIManagerWorldSubsystem::EndPrefabSystemProcessingActor);
}
void ULGUIManagerWorldSubsystem::Deinitialize()
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
TStatId ULGUIManagerWorldSubsystem::GetStatId() const
{
	//return GetStatID();
	RETURN_QUICK_DECLARE_CYCLE_STAT(ULGUIManagerWorldSubsystem, STATGROUP_Tickables);
}
bool ULGUIManagerWorldSubsystem::IsTickableWhenPaused() const
{
	return true;
}

void ULGUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

ULGUIManagerWorldSubsystem* ULGUIManagerWorldSubsystem::GetInstance(UWorld* InWorld)
{
	if (FWorldContext* WorldContext = GEngine->GetWorldContextFromWorld(InWorld))
	{
		return InWorld->GetSubsystem<ULGUIManagerWorldSubsystem>();
	}
	return nullptr;
}

#if WITH_EDITOR
TArray<ULGUIManagerWorldSubsystem*> ULGUIManagerWorldSubsystem::InstanceArray;
bool ULGUIManagerWorldSubsystem::bIsPlaying = false;
#endif

DECLARE_CYCLE_STAT(TEXT("LGUILifeCycleBehaviour Update"), STAT_LGUILifeCycleBehaviourUpdate, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("LGUILifeCycleBehaviour Start"), STAT_LGUILifeCycleBehaviourStart, STATGROUP_LGUI);
DECLARE_CYCLE_STAT(TEXT("Canvas Update"), STAT_UpdateCanvas, STATGROUP_LGUI);
void ULGUIManagerWorldSubsystem::Tick(float DeltaTime)
{
	//editor draw helper frame
#if WITH_EDITOR
	if (IsValid(GEditor))
	{
		auto Settings = GetDefault<ULGUIEditorSettings>();
		if (Settings->bDrawHelperFrame && GEditor->GetSelectedActorCount() > 0)
		{
			if (this->GetWorld()->WorldType == EWorldType::Game
				|| this->GetWorld()->WorldType == EWorldType::PIE
				|| this->GetWorld()->WorldType == EWorldType::Editor
				|| this->GetWorld()->WorldType == EWorldType::EditorPreview
				)
			{
				auto bIsGameWorld = this->GetWorld()->IsGameWorld();
				auto DrawFrame = [bIsGameWorld](const TArray<TWeakObjectPtr<ULexCanvas>>& CanvasArray) {
					for (auto& Canvas : CanvasArray)
					{
						auto& WidgetArray = Canvas->GetWidgetArray();
						for (auto& Widget : WidgetArray)
						{
							if (!IsValid(Widget))continue;

							ULGUIManagerWorldSubsystem::DrawFrameOnWidget(Widget, bIsGameWorld ? Widget->IsScreenSpaceOverlayUI() : false);
						}
					}
					};
				DrawFrame(ScreenSpaceCanvasArray);
				DrawFrame(WorldSpaceUECanvasArray);
				DrawFrame(WorldSpaceLGUICanvasArray);
				DrawFrame(RenderTargetSpaceLGUICanvasArray);
			}
		}

		if (Settings->bDrawSelectableNavigationVisualizer)
		{
			for (auto& Selectable : AllSelectableArray)
			{
				if (!Selectable.IsValid())continue;
				if (!IsValid(Selectable->GetWorld()))continue;
				if (!IsValid(Selectable->GetRootUIComponent()))continue;
				if (!Selectable->GetRootUIComponent()->IsVisibleForHitTest())continue;

				ULGUIManagerWorldSubsystem::DrawNavigationVisualizerOnUISelectable(Selectable->GetWorld(), Selectable.Get(), this->GetWorld()->IsGameWorld() ? Selectable->GetRootUIComponent()->IsScreenSpaceOverlayUI() : false);
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
				ILGUICultureChangedInterface::Execute_OnCultureChanged(Culture.Get());
			}
		}
	}

	//LGUILifeCycleBehaviour start
	{
		if (LGUILifeCycleBehavioursForStart.Num() > 0)
		{
			bIsExecutingStart = true;
			SCOPE_CYCLE_COUNTER(STAT_LGUILifeCycleBehaviourStart);
			for (int i = 0; i < LGUILifeCycleBehavioursForStart.Num(); i++)
			{
				auto item = LGUILifeCycleBehavioursForStart[i];
				if (item.IsValid())
				{
					item->Call_Start();
					if (item->bCanExecuteUpdate)
					{
						LGUILifeCycleBehavioursForUpdate.AddUnique(item);
					}
				}
			}
			LGUILifeCycleBehavioursForStart.Reset();
			bIsExecutingStart = false;
		}
	}

	//LGUILifeCycleBehaviour update
	{
		bIsExecutingUpdate = true;
		auto bIsGamePaused = GetWorld()->IsPaused();
		auto Settings = GetDefault<ULGUISettings>();
		SCOPE_CYCLE_COUNTER(STAT_LGUILifeCycleBehaviourUpdate);
		for (int i = 0; i < LGUILifeCycleBehavioursForUpdate.Num(); i++)
		{
			CurrentExecutingUpdateIndex = i;
			auto item = LGUILifeCycleBehavioursForUpdate[i];
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
		if (LGUILifeCycleBehavioursNeedToRemoveFromUpdate.Num() > 0)
		{
			for (auto& item : LGUILifeCycleBehavioursNeedToRemoveFromUpdate)
			{
				LGUILifeCycleBehavioursForUpdate.Remove(item);
			}
			LGUILifeCycleBehavioursNeedToRemoveFromUpdate.Reset();
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
			auto errMsg = FText::Format(LOCTEXT("MultipleLGUICanvasRenderScreenSpaceOverlay", "[{0}].{1} Detect multiple LGUICanvas renderred with ScreenSpaceOverlay mode, this is not allowed! There should be only one ScreenSpace UI in a world!\
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

	//update drawcall
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
		UpdateCanvas(WorldSpaceLGUICanvasArray);
		UpdateCanvas(RenderTargetSpaceLGUICanvasArray);
	}
}

void ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehaviourForLifecycleEvent(ULGUILifeCycleBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto SessionId = ULGUIPrefabWorldSubsystem::GetInstance(InComp->GetWorld())->GetPrefabSystemSessionIdForActor(InComp->GetOwner());
			if (SessionId.IsValid())//processing by prefab system, collect for further operation
			{
				if (auto ArrayPtr = Instance->LGUILifeCycleBehaviours_PrefabSystemProcessing.Find(SessionId))
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
				Instance->ProcessLGUILifecycleEvent(InComp);
			}
		}
	}
}

void ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehavioursForUpdate(ULGUILifeCycleBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->LGUILifeCycleBehavioursForUpdate.Find(InComp, index))
			{
				Instance->LGUILifeCycleBehavioursForUpdate.Add(InComp);
				return;
			}
			UE_LOG(LGUI, Warning, TEXT("[ULGUIManagerWorldSubsystem::AddLGUILifeCycleBehavioursForUpdate]Already exist, comp:%s"), *(InComp->GetPathName()));
		}
	}
}
void ULGUIManagerWorldSubsystem::RemoveLGUILifeCycleBehavioursFromUpdate(ULGUILifeCycleBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& updateArray = Instance->LGUILifeCycleBehavioursForUpdate;
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
						Instance->LGUILifeCycleBehavioursNeedToRemoveFromUpdate.Add(InComp);
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

void ULGUIManagerWorldSubsystem::RegisterLGUICultureChangedEvent(TScriptInterface<ILGUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.AddUnique(InItem.GetObject());
	}
}
void ULGUIManagerWorldSubsystem::UnregisterLGUICultureChangedEvent(TScriptInterface<ILGUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.RemoveSingle(InItem.GetObject());
	}
}

#if WITH_EDITOR
void ULGUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
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

void ULGUIManagerWorldSubsystem::AddRootWidget(ULexWidget* InWidget)
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
void ULGUIManagerWorldSubsystem::RemoveRootWidget(ULexWidget* InWidget)
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

void ULGUIManagerWorldSubsystem::AddCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode)
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
			case ELexRenderMode::WorldSpace_LGUI:
				Instance->WorldSpaceLGUICanvasArray.Add(InCanvas);
				break;
			case ELexRenderMode::RenderTarget:
				Instance->RenderTargetSpaceLGUICanvasArray.Add(InCanvas);
				break;
			}
		}
	}
}
void ULGUIManagerWorldSubsystem::RemoveCanvas(ULexCanvas* InCanvas, ELexRenderMode InCurrentRenderMode)
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
		case ELexRenderMode::WorldSpace_LGUI:
			Instance->WorldSpaceLGUICanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLGUICanvasArray.Remove(InCanvas);
			break;
		}
	}
}
void ULGUIManagerWorldSubsystem::CanvasRenderModeChange(ULexCanvas* InCanvas, ELexRenderMode InOldRenderMode, ELexRenderMode InNewRenderMode)
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
		case ELexRenderMode::WorldSpace_LGUI:
			Instance->WorldSpaceLGUICanvasArray.Remove(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLGUICanvasArray.Remove(InCanvas);
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
		case ELexRenderMode::WorldSpace_LGUI:
			Instance->WorldSpaceLGUICanvasArray.Add(InCanvas);
			break;
		case ELexRenderMode::RenderTarget:
			Instance->RenderTargetSpaceLGUICanvasArray.Add(InCanvas);
			break;
		}
	}
}
const TArray<TWeakObjectPtr<ULexCanvas>>& ULGUIManagerWorldSubsystem::GetCanvasArray(ELexRenderMode RenderMode)
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
	case ELexRenderMode::WorldSpace_LGUI:
		return WorldSpaceLGUICanvasArray;
	case ELexRenderMode::RenderTarget:
		return RenderTargetSpaceLGUICanvasArray;
	}
}

TSharedPtr<class FLexUIRenderer, ESPMode::ThreadSafe> ULGUIManagerWorldSubsystem::GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist)
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

void ULGUIManagerWorldSubsystem::AddRaycaster(ULGUIBaseRaycaster* InRaycaster)
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
		AllRaycasterArray.Sort([](const TWeakObjectPtr<ULGUIBaseRaycaster>& A, const TWeakObjectPtr<ULGUIBaseRaycaster>& B)
		{
			return A->GetDepth() > B->GetDepth();
		});
	}
}
void ULGUIManagerWorldSubsystem::RemoveRaycaster(ULGUIBaseRaycaster* InRaycaster)
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

void ULGUIManagerWorldSubsystem::SetCurrentInputModule(ULGUIBaseInputModule* InInputModule)
{
	if (auto Instance = GetInstance(InInputModule->GetWorld()))
	{
		Instance->CurrentInputModule = InInputModule;
	}
}
void ULGUIManagerWorldSubsystem::ClearCurrentInputModule(ULGUIBaseInputModule* InInputModule)
{
	if (auto Instance = GetInstance(InInputModule->GetWorld()))
	{
		Instance->CurrentInputModule = nullptr;
	}
}

void ULGUIManagerWorldSubsystem::AddSelectable(UUISelectableComponent* InSelectable)
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
void ULGUIManagerWorldSubsystem::RemoveSelectable(UUISelectableComponent* InSelectable)
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

void ULGUIManagerWorldSubsystem::ProcessLGUILifecycleEvent(ULGUILifeCycleBehaviour* InComp)
{
	if (InComp)
	{
		if (!InComp->bIsAwakeCalled)
		{
			InComp->Call_Awake();
#if !UE_BUILD_SHIPPING
			check(!LGUILifeCycleBehavioursForStart.Contains(InComp));
#endif
			LGUILifeCycleBehavioursForStart.Add(InComp);
		}
	}
}
void ULGUIManagerWorldSubsystem::BeginPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	FLGUILifeCycleBehaviourArrayContainer Container;
	LGUILifeCycleBehaviours_PrefabSystemProcessing.Add(InSessionId, Container);
}
void ULGUIManagerWorldSubsystem::EndPrefabSystemProcessingActor(const FGuid& InSessionId)
{
	if (auto ArrayPtr = LGUILifeCycleBehaviours_PrefabSystemProcessing.Find(InSessionId))
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
				ProcessLGUILifecycleEvent(Item.Get());
			}
#if !UE_BUILD_SHIPPING
			if (LGUILifeCycleBehaviourArray.Num() != Count)
			{
				UE_LOG(LGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			}
#endif
		}

		LGUILifeCycleBehaviours_PrefabSystemProcessing.Remove(InSessionId);
	}
}
void ULGUIManagerWorldSubsystem::AddFunctionForPrefabSystemExecutionBeforeAwake(AActor* InPrefabActor, const TFunction<void()>& InFunction)
{
	auto SessionId = ULGUIPrefabWorldSubsystem::GetInstance(InPrefabActor->GetWorld())->GetPrefabSystemSessionIdForActor(InPrefabActor);
	if (SessionId.IsValid())
	{
		auto& Container = LGUILifeCycleBehaviours_PrefabSystemProcessing[SessionId];
		Container.Functions.Add(InFunction);
	}
}
#if LGUI_CAN_DISABLE_OPTIMIZATION
UE_ENABLE_OPTIMIZATION
#endif
#undef LOCTEXT_NAMESPACE