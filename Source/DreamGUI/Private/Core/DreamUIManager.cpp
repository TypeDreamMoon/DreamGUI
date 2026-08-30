// Copyright 2019-Present LexLiu. All Rights Reserved.
// Modified by TypeDreamMoon.

#include "Core/DreamUIManager.h"
#include "Core/DreamGUISettings.h"

#include "DreamGUI.h"
#include "Utils/DreamUIUtils.h"
#include "Core/Components/DreamWidget.h"
#include "Core/Components/DreamCanvas.h"
#include "Event/DreamBaseRaycaster.h"
#include "Engine/World.h"
#include "Interaction/UISelectable.h"
#include "Core/DreamUISettings.h"
#include "Core/DreamUIFontData_FreeTypeRender.h"
#include "Core/Components/DreamVisual.h"
#include "Engine/Engine.h"
#include "Core/DreamUIRender/DreamUIRenderer.h"
#include "Core/IDreamUICultureChangedInterface.h"
#include "Core/DreamUIBehaviour.h"
#include "Core/Components/DreamLayout.h"
#include "Core/DreamUIMesh/DreamUIGizmoMesh.h"
#include "CoreGlobals.h"
#include "Event/DreamEventSystem.h"
#include "Core/DreamWidgetPresenterComponent.h"
#if WITH_EDITOR
#include "Editor.h"
#include "EditorViewportClient.h"
#include "Core/DreamUISpriteData.h"
#endif

#define LOCTEXT_NAMESPACE "DreamUIManager"


#define ENABLED_DreamGUI_DEBUG_DUMP				0
#define ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME		0


UDreamUIManagerObject* UDreamUIManagerObject::Instance = nullptr;
#if WITH_EDITOR
bool UDreamUIManagerObject::bIsBlueprintCompiling = false;
#endif
UDreamUIManagerObject::UDreamUIManagerObject()
{

}
void UDreamUIManagerObject::BeginDestroy()
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

void UDreamUIManagerObject::Tick(float DeltaTime)
{
#if WITH_EDITOR
	if (EditorTick.IsBound())
	{
		EditorTick.Broadcast(DeltaTime);
	}
	if (OneShotFunctionsToExecuteInTick.Num() > 0)
	{
		for (int i = 0; i < OneShotFunctionsToExecuteInTick.Num(); i++)
		{
			auto& Item = OneShotFunctionsToExecuteInTick[i];
			if (Item.Key <= 0)
			{
				Item.Value();
				OneShotFunctionsToExecuteInTick.RemoveAt(i);
				i--;
			}
			else
			{
				Item.Key--;
			}
		}
	}
#endif
}
TStatId UDreamUIManagerObject::GetStatId() const
{
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamGUIEditorManagerObject, STATGROUP_Tickables);
}

#if WITH_EDITOR

void UDreamUIManagerObject::AddOneShotTickFunction(const TFunction<void()>& InFunction, int InDelayFrameCount)
{
	InitCheck();
	InDelayFrameCount = FMath::Max(0, InDelayFrameCount);
	TTuple<int, TFunction<void()>> Item;
	Item.Key = InDelayFrameCount;
	Item.Value = InFunction;
	Instance->OneShotFunctionsToExecuteInTick.Add(Item);
}

FDreamUIEditorTickMulticastDelegate& UDreamUIManagerObject::GetEditorTickDelegate()
{
	return EditorTick;
}

UDreamUIManagerObject* UDreamUIManagerObject::GetInstance(bool CreateIfNotValid)
{
	if (CreateIfNotValid)
	{
		InitCheck();
	}
	return Instance;
}
bool UDreamUIManagerObject::InitCheck()
{
	if (Instance == nullptr)
	{
		UE_LOG(DreamGUI, Log, TEXT("[%s].%d No Instance of class %s, create it"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *UDreamUIManagerObject::StaticClass()->GetName());
		Instance = NewObject<UDreamUIManagerObject>();
		Instance->AddToRoot();
		//open map
		Instance->OnMapOpenedDelegateHandle = FEditorDelegates::OnMapOpened.AddUObject(Instance, &UDreamUIManagerObject::OnMapOpened);
		Instance->OnPackageReloadedDelegateHandle = FCoreUObjectDelegates::OnPackageReloaded.AddUObject(Instance, &UDreamUIManagerObject::OnPackageReloaded);
		if (GEditor)
		{
			//reimport asset
			Instance->OnAssetReimportDelegateHandle = GEditor->GetEditorSubsystem<UImportSubsystem>()->OnAssetReimport.AddUObject(Instance, &UDreamUIManagerObject::OnAssetReimport);
			//blueprint recompile
			Instance->OnBlueprintPreCompileDelegateHandle = GEditor->OnBlueprintPreCompile().AddUObject(Instance, &UDreamUIManagerObject::OnBlueprintPreCompile);
			Instance->OnBlueprintCompiledDelegateHandle = GEditor->OnBlueprintCompiled().AddUObject(Instance, &UDreamUIManagerObject::OnBlueprintCompiled);
		}
	}
	return true;
}

void UDreamUIManagerObject::OnBlueprintPreCompile(UBlueprint* InBlueprint)
{
	bIsBlueprintCompiling = true;
}
void UDreamUIManagerObject::OnBlueprintCompiled()
{
	UDreamUIManagerObject::AddOneShotTickFunction([] {
		bIsBlueprintCompiling = false;
		UDreamUIManagerWorldSubsystem::RefreshAllUI();
		});
}

void UDreamUIManagerObject::OnAssetReimport(UObject* Asset)
{
	if (IsValid(Asset))
	{
		if (auto TextureAsset = Cast<UTexture2D>(Asset))
		{
			bool bNeedToRebuildUI = false;
			//find sprite data that reference this texture
			for (TObjectIterator<UDreamUISpriteData> Itr; Itr; ++Itr)
			{
				UDreamUISpriteData* SpriteData = *Itr;
				if (IsValid(SpriteData))
				{
					if (SpriteData->GetSpriteTexture() == TextureAsset)
					{
						SpriteData->ReloadTexture();
						SpriteData->MarkPackageDirty();
						bNeedToRebuildUI = true;
					}
				}
			}
			//Refresh ui
			if (bNeedToRebuildUI)
			{
				UDreamUIManagerWorldSubsystem::RefreshAllUI();
			}
		}
		// A presenter used to be refreshed by hand when its prefab was saved, by comparing a stored
		// MD5. It holds a class now, and recompiling a Blueprint reinstances the objects of that class
		// on its own, so there is nothing left for this branch to do.
	}
}

void UDreamUIManagerObject::OnMapOpened(const FString& FileName, bool AsTemplate)
{

}

void UDreamUIManagerObject::OnPackageReloaded(EPackageReloadPhase Phase, FPackageReloadedEvent* Event)
{
	if (Phase == EPackageReloadPhase::PostBatchPostGC && Event != nullptr && Event->GetNewPackage() != nullptr)
	{
		auto Asset = Event->GetNewPackage()->FindAssetInPackage();
	}
}


UDreamUISelection* UDreamUISelection::GetInstance(UWorld* InWorld)
{
	if (auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(InWorld))
	{
		return DreamUIManager->GetSelection();
	}
	return nullptr;
}

void UDreamUISelection::SelectWidget(UDreamWidget* Widget)
{
	// A widget listed twice takes every per-selection delta twice: Align and Distribute walk the
	// array, so a duplicate entry moves that widget by double the offset the others get.
	SelectedWidgetArray.AddUnique(Widget);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::DeselectWidget(UDreamWidget* Widget)
{
	if (SelectedWidgetArray.Remove(Widget) > 0)
	{
		OnSelectionChanged.Broadcast();
	}
}

void UDreamUISelection::SelectComponent(UDreamUIBehaviour* Component)
{
	SelectedComponentArray.Add(Component);
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::ClearComponentSelection()
{
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

void UDreamUISelection::SelectNone()
{
	SelectedWidgetArray.Empty();
	SelectedComponentArray.Empty();
	OnSelectionChanged.Broadcast();
}

bool UDreamUISelection::IsSelected(UDreamWidget* Widget)const
{
	return SelectedWidgetArray.Contains(Widget);
}

void UDreamUIManagerWorldSubsystem::DrawFrameOnWidget(UDreamWidget* Widget, bool ScreenOrWorld)
{
	if (GetSelection()->IsSelected(Widget))//select self
	{
		auto RectDrawColor = FColor(160, 160, 160, 255);//gray means normal object
		auto DrawWidget = [=](UDreamWidget* InWidget, const FColor& Color)
		{
			// The DRAWN matrix, not the layout one. These frames exist to say "this is the widget",
			// so inside a Perspective scope they have to follow the widget's foreshortened geometry
			// rather than sit where layout would have put it. Nothing is dragged by them, which is
			// what makes this safe here and not safe for the designer handles.
			auto WorldTransform = InWidget->GetWorldMatrix();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - InWidget->GetPivot().X) * InWidget->GetWidth();
			RelativeOffset.Z = (0.5f - InWidget->GetPivot().Y) * InWidget->GetHeight();
			auto Extends = FVector2D(InWidget->GetWidth(), InWidget->GetHeight()) * 0.5f;
			UDreamUIManagerWorldSubsystem::DrawDebugRect(InWidget->GetWorld()
				, RelativeOffset, WorldTransform
				, Extends, Color
				, InWidget, InWidget->GetDisplayName(), ScreenOrWorld);
		};
		//parent
		if (auto Parent = Widget->GetParent())
		{
			DrawWidget(Parent, RectDrawColor);
		}
		//child
		for (auto& Child : Widget->GetChildren())
		{
			if (IsValid(Child)
				&& Child->GetRenderVisibleInHierarchy())
			{
				DrawWidget(Child, RectDrawColor);
			}
		}
		//other object of same hierarchy is selected
		if (auto Parent = Widget->GetParent())
		{
			for (auto& SiblingWidget : Parent->GetChildren())
			{
				if (IsValid(SiblingWidget)
					&& SiblingWidget->GetRenderVisibleInHierarchy()
					&& SiblingWidget != Widget)
				{
					DrawWidget(SiblingWidget, RectDrawColor);
				}
			}
		}

		//self
		{
			RectDrawColor = FColor(0, 255, 0, 255);//green means selected object
			auto WorldTransform = Widget->GetWorldMatrix();
			FVector RelativeOffset(0, 0, 0);
			RelativeOffset.Y = (0.5f - Widget->GetPivot().X) * Widget->GetWidth();
			RelativeOffset.Z = (0.5f - Widget->GetPivot().Y) * Widget->GetHeight();
			auto Extends = FVector2D(Widget->GetWidth(), Widget->GetHeight()) * 0.5f;
			UDreamUIManagerWorldSubsystem::DrawDebugRect(Widget->GetWorld()
				, RelativeOffset, WorldTransform
				, Extends, RectDrawColor
				, Widget, Widget->GetDisplayName(), ScreenOrWorld);

			// The pivot, at the widget's origin. Sized off the widget so it stays readable on a
			// 20-pixel icon and does not swallow a full-screen panel, and clamped so it does neither.
			const float PivotSize = FMath::Clamp(FMath::Min(Widget->GetWidth(), Widget->GetHeight()) * 0.12f, 3.0f, 12.0f);
			UDreamUIManagerWorldSubsystem::DrawDebugPivot(Widget->GetWorld()
				, WorldTransform, PivotSize, RectDrawColor
				, Widget, Widget->GetDisplayName(), ScreenOrWorld);

			if (auto Visual = Cast<UDreamVisual>(Widget->GetVisual()))
			{
				FVector Min, Max;
				Visual->GetGeometryBounds3DInLocalSpace(Min, Max);
				auto GeometryBoundsDrawColor = FColor(255, 255, 0, 255);//yellow for geometry bounds
				auto GeometryBoundsExtends = (Max - Min) * 0.5f;
				auto GeometryRelativeOffset = (Min + Max) * 0.5f;
				auto WidgetExtends3D = FVector(0, Extends.X, Extends.Y); 
				if (WidgetExtends3D != GeometryBoundsExtends || RelativeOffset != GeometryRelativeOffset)
				{
					UDreamUIManagerWorldSubsystem::DrawDebugBox(Widget->GetWorld()
						, GeometryRelativeOffset, WorldTransform
						, GeometryBoundsExtends, GeometryBoundsDrawColor
						, Widget->GetVisual() , FString::Printf(TEXT("%s.Visual"), *Widget->GetDisplayName()), ScreenOrWorld);
				}
			}
		}
	}
}

void UDreamUIManagerWorldSubsystem::DrawNavigationArrow(UWorld* InWorld, const TArray<FVector>& InControlPoints, const FVector& InArrowPointA, const FVector& InArrowPointB, FColor const& InColor, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	if (InControlPoints.Num() != 4)return;
	TArray<FVector3f> ResultPoints;
	TArray<FDreamUIMeshVertex> VertexArray;
	TArray<FDreamUIMeshIndex> IndexArray;
	const int Segment = FMath::Min(40, FMath::CeilToInt(FVector::Distance(InControlPoints[0], InControlPoints[3]) * 0.5f));

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

	IndexArray.Add(InControlPoints.Num());
	new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[0]), InColor);
	for (int i = 1; i <= Segment; i++)
	{
		float t = i / (float)Segment;
		auto InterPoint = CalculateCubicBezierPoint(t, InControlPoints[0], InControlPoints[1], InControlPoints[2], InControlPoints[3]);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InterPoint), InColor);
	}
	
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		//arrow
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[3]), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InArrowPointA), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InControlPoints[3]), InColor);
		IndexArray.Add(InControlPoints.Num());
		new(VertexArray) FDreamUIMeshVertex(FVector3f(InArrowPointB), InColor);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = FMatrix::Identity;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawNavigationVisualizerOnUISelectable(UWorld* InWorld, UUISelectable* InSelectable, bool IsScreenSpace)
{
	auto SourceWidget = InSelectable->GetWidget();
	if (!IsValid(SourceWidget))return;
	const FColor Color = GetSelection()->IsSelected(SourceWidget) ? FColor(255, 255, 0, 255) : FColor(140, 140, 0, 255);
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
				if (ViewportClient->IsOrtho())
				{
					return ViewportClient->GetOrthoZoom() * 0.001f; 
				}
				else
				{
					auto ViewLocation = ViewportClient->GetViewLocation();
					float Distance = FVector::Distance(WorldPoint, ViewLocation);
					return Distance * 0.01f;
				}
			}
		}
		return ArrowSize;
	};

	if (auto ToLeftComp = InSelectable->FindSelectableOnLeft())
	{
		if (ToLeftComp != InSelectable)
		{
			auto SourceLeftPoint = FVector(0, SourceWidget->GetLocalSpaceLeft(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) + Offset);
			SourceLeftPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceLeftPoint);
			auto DestWidget = ToLeftComp->GetWidget();
			auto LocalDestRightPoint = FVector(0, DestWidget->GetLocalSpaceRight(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) + Offset);
			auto DestRightPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint);
			float Distance = FVector::Distance(SourceLeftPoint, DestRightPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestRightPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestRightPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceLeftPoint,
					SourceLeftPoint - SourceWidget->GetRightVector() * Distance,
					DestRightPoint + DestWidget->GetRightVector() * Distance,
					DestRightPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationLeft"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToRightComp = InSelectable->FindSelectableOnRight())
	{
		if (ToRightComp != InSelectable)
		{
			auto SourceRightPoint = FVector(0, SourceWidget->GetLocalSpaceRight(), 0.5f * (SourceWidget->GetLocalSpaceTop() + SourceWidget->GetLocalSpaceBottom()) - Offset);
			SourceRightPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceRightPoint);
			auto DestWidget = ToRightComp->GetWidget();
			auto LocalDestLeftPoint = FVector(0, DestWidget->GetLocalSpaceLeft(), 0.5f * (DestWidget->GetLocalSpaceTop() + DestWidget->GetLocalSpaceBottom()) - Offset);
			auto DestLeftPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint);
			float Distance = FVector::Distance(SourceRightPoint, DestLeftPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestLeftPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestLeftPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceRightPoint,
					SourceRightPoint + SourceWidget->GetRightVector() * Distance,
					DestLeftPoint - DestWidget->GetRightVector() * Distance,
					DestLeftPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationRight"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToDownComp = InSelectable->FindSelectableOnDown())
	{
		if (ToDownComp != InSelectable)
		{
			auto SourceDownPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) - Offset, SourceWidget->GetLocalSpaceBottom());
			SourceDownPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceDownPoint);
			auto DestWidget = ToDownComp->GetWidget();
			auto LocalDestUpPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) - Offset, DestWidget->GetLocalSpaceTop());
			auto DestUpPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint);
			float Distance = FVector::Distance(SourceDownPoint, DestUpPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestUpPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, ScaledArrowSize, ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestUpPoint + FVector(0, -ScaledArrowSize, ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceDownPoint,
					SourceDownPoint - SourceWidget->GetUpVector() * Distance,
					DestUpPoint + DestWidget->GetUpVector() * Distance,
					DestUpPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationDown"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
	if (auto ToUpComp = InSelectable->FindSelectableOnUp())
	{
		if (ToUpComp != InSelectable)
		{
			auto SourceUpPoint = FVector(0, 0.5f * (SourceWidget->GetLocalSpaceLeft() + SourceWidget->GetLocalSpaceRight()) + Offset, SourceWidget->GetLocalSpaceTop());
			SourceUpPoint = SourceWidget->GetWorldTransform().TransformPosition(SourceUpPoint);
			auto DestWidget = ToUpComp->GetWidget();
			auto LocalDestDownPoint = FVector(0, 0.5f * (DestWidget->GetLocalSpaceLeft() + DestWidget->GetLocalSpaceRight()) + Offset, DestWidget->GetLocalSpaceBottom());
			auto DestDownPoint = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint);
			float Distance = FVector::Distance(SourceUpPoint, DestDownPoint);
			Distance *= 0.2f;
			auto ScaledArrowSize = ArrowSize;
			if (!IsScreenSpace)
			{
				ScaledArrowSize = GetArrowSizeScaledByDistanceToCamera(DestDownPoint);
			}
			auto ArrowPointA = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, ScaledArrowSize, -ScaledArrowSize));
			auto ArrowPointB = DestWidget->GetWorldTransform().TransformPosition(LocalDestDownPoint + FVector(0, -ScaledArrowSize, -ScaledArrowSize));
			DrawNavigationArrow(InWorld
				, {
					SourceUpPoint,
					SourceUpPoint + SourceWidget->GetUpVector() * Distance,
					DestDownPoint - DestWidget->GetUpVector() * Distance,
					DestDownPoint,
				}
				, ArrowPointA, ArrowPointB
				, Color, InSelectable, FString::Printf(TEXT("%s.NavigationUp"), *InSelectable->GetWidget()->GetDisplayName()), IsScreenSpace);
		}
	}
}

FEditorViewportClient* UDreamUIManagerWorldSubsystem::GetEditorViewportClient()
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


void UDreamUIManagerWorldSubsystem::OnEndOfFrame()
{
	CacheViewportClient = nullptr;
}

void UDreamUIManagerWorldSubsystem::OnEnginePreExit()
{
	// for (TObjectIterator<UDreamUIPrefab> Itr; Itr; ++Itr)
	// {
	// 	auto Prefab = *Itr;
	// 	Prefab->ClearDesignerScene();
	// }
}

void UDreamUIManagerWorldSubsystem::DrawDebugPivot(UWorld* InWorld, const FMatrix& LocalToWorld, float Size, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	// The widget's origin is its pivot: the rect is drawn offset from here by the pivot fractions, so
	// this marker is what tells you which corner of that rect the numbers are measured from.
	TArray<FVector3f> LinePoints;
	const float Arm = Size;
	const float Diamond = Size * 0.45f;
	// A cross...
	LinePoints.Add(FVector3f(0, -Arm, 0));
	LinePoints.Add(FVector3f(0, Arm, 0));
	LinePoints.Add(FVector3f(0, 0, -Arm));
	LinePoints.Add(FVector3f(0, 0, Arm));
	// ...inside a diamond, so it reads as a point rather than as two more frame edges.
	LinePoints.Add(FVector3f(0, -Diamond, 0));
	LinePoints.Add(FVector3f(0, 0, Diamond));
	LinePoints.Add(FVector3f(0, 0, Diamond));
	LinePoints.Add(FVector3f(0, Diamond, 0));
	LinePoints.Add(FVector3f(0, Diamond, 0));
	LinePoints.Add(FVector3f(0, 0, -Diamond));
	LinePoints.Add(FVector3f(0, 0, -Diamond));
	LinePoints.Add(FVector3f(0, -Diamond, 0));
	UDreamUIManagerWorldSubsystem::DrawDebugLine(InWorld, LocalToWorld, LinePoints, Color, Object, DebugName, ScreenOrWorld);
}

void UDreamUIManagerWorldSubsystem::DrawDebugRect(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld, FVector2D const& Rect, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		auto PushNewLine = [&](FVector Start, FVector End)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + Start), Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + End), Color);
		};

		auto Start = FVector(0, Rect.X, Rect.Y);
		auto End = FVector(0, -Rect.X, Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, Rect.X, -Rect.Y);
		End = FVector(0, -Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, Rect.X, Rect.Y);
		End = FVector(0, Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		Start = FVector(0, -Rect.X, Rect.Y);
		End = FVector(0, -Rect.X, -Rect.Y);
		PushNewLine(Start, End);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawDebugBox(UWorld* InWorld, const FVector& Center, const FMatrix& LocalToWorld,
	FVector const& Box, FColor const& Color, void* Object, const FString& DebugName, bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		auto PushNewLine = [&](const FVector& Start, const FVector& End)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + Start), Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(FVector3f(Center + End), Color);
		};

		FVector Start, End;
		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, -Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, Box.Z);
		End = FVector(Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, -Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(-Box.X, -Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, Box.Z);
		End = FVector(-Box.X, Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, Box.Z);
		End = FVector(-Box.X, -Box.Y, Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, Box.Y, -Box.Z);
		End = FVector(-Box.X, Box.Y, -Box.Z);
		PushNewLine(Start, End);

		Start = FVector(Box.X, -Box.Y, -Box.Z);
		End = FVector(-Box.X, -Box.Y, -Box.Z);
		PushNewLine(Start, End);

		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

void UDreamUIManagerWorldSubsystem::DrawDebugLine(UWorld* InWorld, const FMatrix& LocalToWorld,
	const TArray<FVector3f>& LinePoints, FColor const& Color, void* Object, const FString& DebugName,
	bool ScreenOrWorld)
{
	auto ViewExtension = UDreamUIManagerWorldSubsystem::GetViewExtension(InWorld, true);
	if (ViewExtension.IsValid())
	{
		TArray<FDreamUIMeshVertex> VertexArray;
		TArray<FDreamUIMeshIndex> IndexArray;
		//lines
		for (int i = 0; i < LinePoints.Num(); i+=2)
		{
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(LinePoints[i], Color);
			IndexArray.Add(VertexArray.Num());
			new(VertexArray) FDreamUIMeshVertex(LinePoints[i + 1], Color);
		}
		auto LineMesh = MakeShared<FDreamUIGizmoMesh>(VertexArray, IndexArray, EDreamUIGizmoMeshPrimitiveType::Line);
		LineMesh->LocalToWorldMatrix = LocalToWorld;
		LineMesh->UpdateLocalBounds();
		LineMesh->Render(ViewExtension, ScreenOrWorld);
	}
}

bool UDreamUIManagerWorldSubsystem::RaycastHitUI(UWorld* InWorld, const TArray<UDreamWidget*>& InWidgets, const FVector& LineStart, const FVector& LineEnd
                                               , UDreamWidget*& ResultSelectTarget, int& InOutTargetIndexInHitArray
)
{
	TArray<FDreamUIHitResult> HitResultArray;
	for (auto Widget : InWidgets)
	{
		if (!IsValid(Widget))continue;
		if (Widget->GetWorld() == InWorld)
		{
			if (auto Visual = Widget->GetVisual())
			{
				if (Widget->GetRenderVisibleInHierarchy() && Widget->GetRenderCanvas() != nullptr)
				{
					FDreamUIHitResult HitInfo;
					auto OriginRaycastType = Visual->GetRaycastType();
					Visual->SetRaycastType(EDreamVisualRaycastType::Mesh);//in editor selection, make the ray hit actural triangle
					if (Visual->LineTraceUI(HitInfo, LineStart, LineEnd))
					{
						if (Widget->IsPointVisibleOnClip(HitInfo.Location))
						{
							HitResultArray.Add(HitInfo);
						}
					}
					Visual->SetRaycastType(OriginRaycastType);
				}
			}
		}
	}
	if (HitResultArray.Num() > 0)//hit something
	{
		HitResultArray.Sort([](const FDreamUIHitResult& A, const FDreamUIHitResult& B)
			{
				auto AWidget = (UDreamWidget*)(A.Widget.Get());
				auto BWidget = (UDreamWidget*)(B.Widget.Get());
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
		auto HitWidget = (UDreamWidget*)(HitResultArray[InOutTargetIndexInHitArray].Widget.Get());//target need to select
		ResultSelectTarget = HitWidget;
		return true;
	}
	return false;
}
#endif

bool UDreamUIManagerWorldSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	return !IsRunningCommandlet() && Super::ShouldCreateSubsystem(Outer);
}

void UDreamUIManagerWorldSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);
#if WITH_EDITOR
	InstanceArray.Add(this);
	if (this->GetWorld()->WorldType == EWorldType::EditorPreview//EditorPreview world don't tick, so manually tick it
		|| this->GetWorld()->WorldType == EWorldType::Editor)
	{
		EditorTickDelegateHandle = FTSTicker::GetCoreTicker().AddTicker(TEXT("DreamUIManagerWorldSubsystemEditorTick"), 0, [WeakThis = MakeWeakObjectPtr(this)](float DeltaTime) {
			if (WeakThis.IsValid())
			{
				WeakThis->Tick(DeltaTime);
				return true;
			}
			return false;
			});
	}
	if (this->GetWorld()->IsGameWorld() || this->GetWorld()->WorldType == EWorldType::Editor)//game world or editor world, skip editor preview world
	{
		bShouldTickInEditor = true;
	}
	else
	{
		bShouldTickInEditor = false;
	}
	FCoreDelegates::OnEndFrame.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEndOfFrame);
	FCoreDelegates::OnEnginePreExit.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnEnginePreExit);
	UDreamUIManagerObject::GetInstance(true);//make sure it is created
#endif
	//localization
	OnCultureChangedDelegateHandle = FInternationalization::Get().OnCultureChanged().AddUObject(this, &UDreamUIManagerWorldSubsystem::OnCultureChanged);
}
void UDreamUIManagerWorldSubsystem::PostInitialize()
{
	Super::PostInitialize();
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.AddUObject(this, &UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates);
}
void UDreamUIManagerWorldSubsystem::Deinitialize()
{
#if WITH_EDITOR
	InstanceArray.Remove(this);
	if (EditorTickDelegateHandle.IsValid())
	{
		FTSTicker::GetCoreTicker().RemoveTicker(EditorTickDelegateHandle);
		EditorTickDelegateHandle.Reset();
	}
	OnDeinitialize.Broadcast();
#endif
	DestroyRegisteredWidgetTrees();
	if (MainViewportViewExtension.IsValid())
	{
		MainViewportViewExtension.Reset();
	}
	if (OnCultureChangedDelegateHandle.IsValid())
	{
		FInternationalization::Get().OnCultureChanged().Remove(OnCultureChangedDelegateHandle);
	}
	FWorldDelegates::OnWorldPreSendAllEndOfFrameUpdates.RemoveAll(this);
	Super::Deinitialize();
}

void UDreamUIManagerWorldSubsystem::BeginDestroy()
{
	check(!IsInitialized());
	DestroyRegisteredWidgetTrees();
	Super::BeginDestroy();
}

TStatId UDreamUIManagerWorldSubsystem::GetStatId() const
{
	//return GetStatID();
	RETURN_QUICK_DECLARE_CYCLE_STAT(UDreamGUIManagerWorldSubsystem, STATGROUP_Tickables);
}
bool UDreamUIManagerWorldSubsystem::IsTickableWhenPaused() const
{
	return true;
}

void UDreamUIManagerWorldSubsystem::OnCultureChanged()
{
	bShouldUpdateOnCultureChanged = true;
}

UDreamUIManagerWorldSubsystem* UDreamUIManagerWorldSubsystem::GetInstance(UWorld* InWorld)
{
	return IsValid(InWorld) ? InWorld->GetSubsystem<UDreamUIManagerWorldSubsystem>() : nullptr;
}

#if WITH_EDITOR
UDreamUISelection* UDreamUIManagerWorldSubsystem::GetSelection() const
{
	if (!IsValid(Selection))
	{
		Selection = NewObject<UDreamUISelection>();
		Selection->SetFlags(RF_Transactional);
	}
	return Selection;
}

void UDreamUIManagerWorldSubsystem::MarkDreamUIWidgetOutlinerChanged()
{
	bDreamUIWidgetOutlinerChanged = true;
}
#endif

void UDreamUIManagerWorldSubsystem::OnWorldBeginPlay(UWorld& InWorld)
{
	Super::OnWorldBeginPlay(InWorld);
	//normally BeginPlay is already called when LoadPrefab, but if World has not BeginPlay then this will work
	for (int i = 0; i < AllWidgetArray.Num(); i++)
	{
		auto& Widget = AllWidgetArray[i];
		if (!Widget->HasBegunPlay())
		{
			Widget->BeginPlay();
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldEndPlay(UWorld& InWorld)
{
#if WITH_EDITOR
	OnEndPlay.Broadcast();
	if (this->GetWorld()->IsGameWorld())//game mode should deinit when EndPlay
#endif
	{
		DestroyRegisteredWidgetTrees();
	}
	Super::OnWorldEndPlay(InWorld);
}

#if WITH_EDITOR
TArray<UDreamUIManagerWorldSubsystem*> UDreamUIManagerWorldSubsystem::InstanceArray;
#endif

DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Tick"), STAT_DreamUIBehaviourTick, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("DreamUIBehaviour Start"), STAT_DreamUIBehaviourStart, STATGROUP_DreamGUI);
DECLARE_CYCLE_STAT(TEXT("UpdateLayout"), STAT_UpdateLayout, STATGROUP_DreamGUI);

void UDreamUIManagerWorldSubsystem::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);
#if WITH_EDITOR
	if (bShouldTickInEditor)
#endif
	{
		this->TickDreamUI(DeltaTime);
	}
}

void UDreamUIManagerWorldSubsystem::AddPropertyBindingUser(UDreamUserWidget* InUserWidget)
{
	if (IsValid(InUserWidget))
	{
		PropertyBindingUsers.AddUnique(InUserWidget);
	}
}

void UDreamUIManagerWorldSubsystem::RemovePropertyBindingUser(UDreamUserWidget* InUserWidget)
{
	PropertyBindingUsers.RemoveSingleSwap(InUserWidget);
}

void UDreamUIManagerWorldSubsystem::TickDreamUI(float DeltaTime)
{
	SweepExpiredParkedWidgets();
	//Update culture
	{
		if (bShouldUpdateOnCultureChanged)
		{
			bShouldUpdateOnCultureChanged = false;
			for (auto& Culture : AllCultureChangedArray)
			{
				IDreamUICultureChangedInterface::Execute_OnCultureChanged(Culture.Get());
			}
		}
	}

	// Property bindings, BEFORE the behaviours: a behaviour that reads a bound property this frame
	// should see this frame's value, not the one from before the function was called.
	{
		for (int32 Index = PropertyBindingUsers.Num() - 1; Index >= 0; --Index)
		{
			UDreamUserWidget* UserWidget = PropertyBindingUsers[Index].Get();
			if (!IsValid(UserWidget))
			{
				PropertyBindingUsers.RemoveAtSwap(Index);
				continue;
			}
			// Only the polled remainder: subscribed bindings re-evaluate from their field's
			// broadcast, and visiting them here would just do the work twice.
			UserWidget->EvaluatePolledPropertyBindings();
		}
	}

	//DreamUIBehaviour start
	{
		if (DreamUIBehavioursForStart.Num() > 0)
		{
			bIsExecutingStart = true;
			SCOPE_CYCLE_COUNTER(STAT_DreamUIBehaviourStart);
			for (int i = 0; i < DreamUIBehavioursForStart.Num(); i++)
			{
				auto item = DreamUIBehavioursForStart[i];
				if (item.IsValid())
				{
					item->Call_Start();
					if (item->bCanExecuteTick)
					{
						DreamUIBehavioursForTick.AddUnique(item);
					}
				}
			}
			DreamUIBehavioursForStart.Reset();
			bIsExecutingStart = false;
		}
	}

	//DreamUIBehaviour tick
	{
		bIsExecutingTick = true;
		auto bIsGamePaused = GetWorld()->IsPaused();
		auto Settings = GetDefault<UDreamUISettings>();
		SCOPE_CYCLE_COUNTER(STAT_DreamUIBehaviourTick);
		for (int i = 0; i < DreamUIBehavioursForTick.Num(); i++)
		{
			CurrentExecutingTickIndex = i;
			auto Behaviour = DreamUIBehavioursForTick[i];
			if (auto Widget = Behaviour->GetWidget())
			{
				bool bAffectByGamePause;
				if (Widget->IsScreenSpaceOverlayUI())
				{
					bAffectByGamePause = Settings->bScreenSpaceUIAffectByGamePause;
				}
				else
				{
					bAffectByGamePause = Settings->bWorldSpaceUIAffectByGamePause;
				}
				if (!bIsGamePaused || (bIsGamePaused && !bAffectByGamePause))
				{
					Behaviour->Tick(DeltaTime);
				}
			}
			else
			{
				if (!bIsGamePaused || (bIsGamePaused && Behaviour->bTickEvenWhenPaused))
				{
					Behaviour->Tick(DeltaTime);
				}
			}
		}
		bIsExecutingTick = false;
		CurrentExecutingTickIndex = -1;
		//remove these padding things
		if (DreamUIBehavioursNeedToRemoveFromTick.Num() > 0)
		{
			for (auto& item : DreamUIBehavioursNeedToRemoveFromTick)
			{
				DreamUIBehavioursForTick.Remove(item);
			}
			DreamUIBehavioursNeedToRemoveFromTick.Reset();
		}
	}

	//update layout
	if (LayoutDirtyWidgetArray.Num() > 0)
	{
		bIsExecutingLayout = true;
		constexpr int32 MaxLayoutPassesPerFrame = 32;
		int32 LayoutPassCount = 0;
		LastLayoutPassCount = 0;
#if WITH_EDITOR && ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME
		auto Time = FDateTime::Now();
		UE_LOG(DreamGUI, Log, TEXT("---Begin layout frame:%d, World:%s---"), GFrameNumber, *GetWorld()->GetPathName());
#endif
		LayoutContainerArrayWhichHasSnapshot.Reset();
		while (LayoutDirtyWidgetArray.Num() > 0 && LayoutPassCount < MaxLayoutPassesPerFrame)
		{
			SCOPE_CYCLE_COUNTER(STAT_UpdateLayout);
			++LayoutPassCount;
			LastLayoutPassCount = LayoutPassCount;

			TArray<TWeakObjectPtr<UDreamWidget>> CopiedLayoutDirtyWidgetArray;
			Swap(CopiedLayoutDirtyWidgetArray, LayoutDirtyWidgetArray);

			// Collect the live roots up front so ancestry can be tested against the whole batch.
			TSet<UDreamWidget*> BatchRoots;
			TArray<UDreamWidget*> OrderedRoots;
			BatchRoots.Reserve(CopiedLayoutDirtyWidgetArray.Num());
			OrderedRoots.Reserve(CopiedLayoutDirtyWidgetArray.Num());
			for (const TWeakObjectPtr<UDreamWidget>& WeakWidget : CopiedLayoutDirtyWidgetArray)
			{
				if (UDreamWidget* Widget = WeakWidget.Get(); IsValid(Widget))
				{
					bool bAlreadyPresent = false;
					BatchRoots.Add(Widget, &bAlreadyPresent);
					if (!bAlreadyPresent)
					{
						OrderedRoots.Add(Widget);
					}
				}
			}

			// CalculateLayoutTree walks an entire subtree, so a root sitting under another root in the same
			// batch is redundant - the ancestor's walk already covers it. It was worse than redundant: this
			// batch used to be iterated back-to-front, so the descendant usually ran FIRST, laying its
			// subtree out against the ancestor's stale size and then being laid out a second time when the
			// ancestor's walk reached it. Both roots are easy to enqueue at once, because
			// UDreamWidget::MarkLayoutForRebuild falls back to the widget itself when no layout exists yet on
			// its ancestor chain - sizing a widget before parenting it is enough.
			// The survivors are pairwise unrelated, so their relative order no longer matters; keep enqueue
			// order for determinism.
			constexpr int32 MaxHierarchyDepthGuard = 1024;
			for (UDreamWidget* Widget : OrderedRoots)
			{
				bool bCoveredByAncestor = false;
				int32 DepthGuard = 0;
				for (UDreamWidget* Ancestor = Widget->GetParent();
					IsValid(Ancestor) && DepthGuard < MaxHierarchyDepthGuard;
					Ancestor = Ancestor->GetParent(), ++DepthGuard)
				{
					if (BatchRoots.Contains(Ancestor))
					{
						bCoveredByAncestor = true;
						break;
					}
				}
				if (!bCoveredByAncestor)
				{
					CalculateLayoutTree(Widget);
				}
			}
		}
		if (LayoutDirtyWidgetArray.Num() > 0)
		{
			UE_LOG(DreamGUI, Error,
				TEXT("Layout did not converge after %d passes in World %s. Deferring %d pending widgets to the next frame."),
				MaxLayoutPassesPerFrame, *GetNameSafe(GetWorld()), LayoutDirtyWidgetArray.Num());
		}
		for (auto& SnapshotLayout : LayoutContainerArrayWhichHasSnapshot)
		{
			if (IsValid(SnapshotLayout))
			{
				SnapshotLayout->ApplyLayoutResult();
			}
		}
#if WITH_EDITOR && ENABLED_DreamGUI_DEBUG_LAYOUT_FRAME
		for (auto& CalcCountKeyValue : LayoutCalculationCounterMap)
		{
			if (CalcCountKeyValue.Value >= 2)
			{
				UE_LOG(DreamGUI, Warning, TEXT("Widget %s has been calculated layout %d times in a frame"), *CalcCountKeyValue.Key, CalcCountKeyValue.Value);
			}
		}
		LayoutCalculationCounterMap.Reset();
		auto TimeSpan = (FDateTime::Now() - Time).GetTotalMilliseconds();
		UE_LOG(DreamGUI, Log, TEXT("---end layout frame:%d, count:%d, time:%f"), GFrameNumber, LayoutPassCount, TimeSpan);
#endif
		bIsExecutingLayout = false;
	}

#if WITH_EDITOR
	const int32 ScreenSpaceOverlayCanvasCount = CountCompetingScreenSpaceOverlayCanvases();
	if (ScreenSpaceOverlayCanvasCount > 1)
	{
		if (PrevScreenSpaceOverlayCanvasCount != ScreenSpaceOverlayCanvasCount)//only show message when change
		{
			PrevScreenSpaceOverlayCanvasCount = ScreenSpaceOverlayCanvasCount;
			auto errMsg = FText::Format(LOCTEXT("MultipleDreamUICanvasRenderScreenSpaceOverlay", "[{0}].{1} Detect multiple DreamCanvas rendered with ScreenSpaceOverlay mode, this is not allowed! There should be only one ScreenSpace UI in a world!\
\n	World: {2}, type: {3}")
			, FText::FromString(ANSI_TO_TCHAR(__FUNCTION__)), __LINE__, FText::FromString(this->GetWorld()->GetPathName()), (int)(this->GetWorld()->WorldType));
			UE_LOG(DreamGUI, Error, TEXT("%s"), *errMsg.ToString());
			FDreamUIUtils::EditorNotification(errMsg, false, 10.0f);
		}
	}
	else
	{
		PrevScreenSpaceOverlayCanvasCount = 0;
	}
	if (bDreamUIWidgetOutlinerChanged)
	{
		bDreamUIWidgetOutlinerChanged = false;
		OnDreamUIWidgetOutlinerChanged.Broadcast();
	}
#endif

	// Refresh clip rectangles after layout, before draw-calls.
	//
	// This is the equivalent of UGUI's ClipperRegistry.Cull(): a clip rectangle is derived from widget world
	// transforms, which the layout pass above has just changed, so it is recomputed here every tick instead of
	// being driven by dirty flags. Flag-driven invalidation was the wrong shape for this — a clip depends on the
	// transform of every ancestor, and whatever moves an ancestor has no idea a descendant owns a clip, so every
	// missed mark left the shader clipping against a stale rectangle and silently culled a whole subtree.
	// FDreamUIClipData::UpdateData diffs against the last uploaded block, so an unchanged clip costs one matrix
	// build and a memcmp, with no GPU write.
	for (auto& Canvas : AllCanvasArray)
	{
		if (Canvas.IsValid())
		{
			Canvas->RefreshAllClipData();
		}
	}

	//update draw-call
	{
		auto UpdateCanvas = [this](EDreamRenderMode RenderMode) {
			for (auto& Canvas : AllCanvasArray)
			{
				if (!Canvas.IsValid())continue;
				if (!Canvas->IsRootCanvas())continue;
				if (Canvas->GetActualRenderMode() != RenderMode)continue;
				Canvas->UpdateRootCanvas();
			}
		};
		UpdateCanvas(EDreamRenderMode::ScreenSpaceOverlay);
		UpdateCanvas(EDreamRenderMode::WorldSpace);
		UpdateCanvas(EDreamRenderMode::WorldSpace_DreamUI);
		UpdateCanvas(EDreamRenderMode::RenderTarget);
	}
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();

	// Consume render-priority sort requests at their owner. A request raised outside the owner's own
	// draw-call rebuild (runtime SetSortOrder, a child canvas rebuilding alone) used to sit in the flag
	// until the owner happened to rebuild for some other reason; this sweep executes it the same frame.
	for (auto& Canvas : AllCanvasArray)
	{
		if (Canvas.IsValid())
		{
			Canvas->ConsumePendingRenderPrioritySort();
		}
	}
}

void UDreamUIManagerWorldSubsystem::OnWorldPreSendAllEndOfFrameUpdates(UWorld* InWorld)
{
	if (InWorld == this->GetWorld())
	{
#if WITH_EDITOR
		this->DrawHelperGizmo();
#endif
		this->SubmitCanvasDrawCall();
	}
}

#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::DrawHelperGizmo()
{
	//editor draw helper frame
	auto Settings = GetDefault<UDreamUIEditorSettings>();
	if (Settings->bDrawHelperFrame)
	{
		if (this->GetWorld()->WorldType == EWorldType::Game
			|| this->GetWorld()->WorldType == EWorldType::PIE
			|| this->GetWorld()->WorldType == EWorldType::Editor
			// || this->GetWorld()->WorldType == EWorldType::EditorPreview
			)
		{
			struct LOCAL
			{
				static void ForEachWidget(UDreamUIManagerWorldSubsystem* DreamUIManager, UDreamWidget* Widget, bool bIsGameWorld)
				{
					if (!IsValid(Widget))return;

					bool bIsScreenSpace = false;
					if (bIsGameWorld)
					{
						if (auto RenderCanvas = Widget->GetRenderCanvas())
						{
							bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
						}
					}
					DreamUIManager->DrawFrameOnWidget(Widget, bIsScreenSpace);

					for (auto& Child : Widget->GetChildren())
					{
						ForEachWidget(DreamUIManager, Child, bIsGameWorld);
					}
				}
			};
			auto bIsGameWorld = this->GetWorld()->IsGameWorld();
			for (auto& Canvas : AllCanvasArray)
			{
				if (!Canvas.IsValid())continue;
				if (!Canvas->IsRootCanvas())continue;
				LOCAL::ForEachWidget(this, Canvas->GetWidget(), bIsGameWorld);
			}
		}
	}

	if (Settings->bDrawSelectableNavigationVisualizer)
	{
		for (auto& Selectable : AllSelectableArray)
		{
			if (!Selectable.IsValid())continue;
			if (!IsValid(Selectable->GetWorld()))continue;
			if (!IsValid(Selectable->GetWidget()))continue;
			if (!IsValid(Selectable->GetWidget()->GetRenderCanvas()))continue;
			if (!Selectable->GetWidget()->GetInteractableInHierarchy())continue;

			bool bIsScreenSpace = false;
			if (Selectable->GetWorld()->IsGameWorld())
			{
				auto RenderCanvas = Selectable->GetWidget()->GetRenderCanvas();
				bIsScreenSpace = RenderCanvas->IsRenderToScreenSpace() || RenderCanvas->IsRenderToRenderTarget();
			}
			DrawNavigationVisualizerOnUISelectable(Selectable->GetWorld(), Selectable.Get()
				, bIsScreenSpace);
		}
	}
}
#endif

void UDreamUIManagerWorldSubsystem::SubmitCanvasDrawCall()
{
	UDreamUIFontData_FreeTypeRender::FlushPendingFontTextures();
	//update draw-call
	{
		auto UpdateCanvas = [this](EDreamRenderMode RenderMode) {
			for (auto& Canvas : AllCanvasArray)
			{
				if (!Canvas.IsValid())continue;
				if (!Canvas->IsRootCanvas())continue;
				if (Canvas->GetRenderMode() != RenderMode)continue;
				Canvas->UpdateDrawCallBatchData();
			}
		};
		UpdateCanvas(EDreamRenderMode::ScreenSpaceOverlay);
		UpdateCanvas(EDreamRenderMode::WorldSpace);
		UpdateCanvas(EDreamRenderMode::WorldSpace_DreamUI);
		UpdateCanvas(EDreamRenderMode::RenderTarget);
	}
}

void UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForTick(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->DreamUIBehavioursForTick.Find(InComp, index))
			{
				Instance->DreamUIBehavioursForTick.Add(InComp);
				return;
			}
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Already exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
		}
	}
}
void UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromTick(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& TickArray = Instance->DreamUIBehavioursForTick;
			int32 Index = INDEX_NONE;
			if (TickArray.Find(InComp, Index))
			{
				if (Instance->bIsExecutingTick)
				{
					if (Index > Instance->CurrentExecutingTickIndex)//not execute it yet, safe to remove
					{
						TickArray.RemoveAt(Index);
					}
					else//already execute or current execute it, not safe to remove. should remove it after execute process complete
					{
						Instance->DreamUIBehavioursNeedToRemoveFromTick.Add(InComp);
					}
				}
				else//not executing tick, safe to remove
				{
					TickArray.RemoveAt(Index);
				}
			}
			else
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Not exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
			}

			//cleanup array
			int InvalidCount = 0;
			for (int i = TickArray.Num() - 1; i >= 0; i--)
			{
				if (!TickArray[i].IsValid())
				{
					TickArray.RemoveAt(i);
					InvalidCount++;
				}
			}
			if (InvalidCount > 0)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Cleanup %d invalid DreamUIBehaviour"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, InvalidCount);
			}
		}
	}
}
void UDreamUIManagerWorldSubsystem::AddDreamUIBehavioursForStart(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			int32 index = INDEX_NONE;
			if (!Instance->DreamUIBehavioursForStart.Find(InComp, index))
			{
				Instance->DreamUIBehavioursForStart.Add(InComp);
				return;
			}
			UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Already exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
		}
	}
}
void UDreamUIManagerWorldSubsystem::RemoveDreamUIBehavioursFromStart(UDreamUIBehaviour* InComp)
{
	if (IsValid(InComp))
	{
		if (auto Instance = GetInstance(InComp->GetWorld()))
		{
			auto& startArray = Instance->DreamUIBehavioursForStart;
			int32 index = INDEX_NONE;
			if (startArray.Find(InComp, index))
			{
				if (Instance->bIsExecutingStart)
				{
					if (!InComp->bIsStartCalled)//if already called start then nothing to do, because start array will be cleared after execute start
					{
						startArray.RemoveAt(index);//not execute start yet, safe to remove
					}
				}
				else
				{
					startArray.RemoveAt(index);//not executing start, safe to remove
				}
			}
			else
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Not exist, comp:%s"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *(InComp->GetPathName()));
			}

			//cleanup array
			int inValidCount = 0;
			for (int i = startArray.Num() - 1; i >= 0; i--)
			{
				if (!startArray[i].IsValid())
				{
					startArray.RemoveAt(i);
					inValidCount++;
				}
			}
			if (inValidCount > 0)
			{
				UE_LOG(DreamGUI, Warning, TEXT("[%s].%d Cleanup %d invalid DreamUIBehaviour"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, inValidCount);
			}
		}
	}
}

void UDreamUIManagerWorldSubsystem::RegisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.AddUnique(InItem.GetObject());
	}
}
void UDreamUIManagerWorldSubsystem::UnregisterDreamUICultureChangedEvent(TScriptInterface<IDreamUICultureChangedInterface> InItem)
{
	if (auto Instance = GetInstance(InItem.GetObject()->GetWorld()))
	{
		Instance->AllCultureChangedArray.RemoveSingle(InItem.GetObject());
	}
}

TArray<UDreamCanvas*> UDreamUIManagerWorldSubsystem::GetCanvasArrayByRenderMode(EDreamRenderMode RenderMode) const
{
	TArray<UDreamCanvas*> CanvasArray;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (Canvas->GetActualRenderMode() == RenderMode)
		{
			CanvasArray.Add(Canvas.Get());
		}
	}
	return CanvasArray;
}

#if WITH_EDITOR
void UDreamUIManagerWorldSubsystem::RefreshAllUI(UWorld* InWorld)
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
		for (auto& Canvas : Instance->AllCanvasArray)
		{
			if (!Canvas.IsValid())continue;
			if (!Canvas->IsRootCanvas())continue;
			if (auto Widget = Canvas->GetWidget())
			{
				Widget->EnsureDataForRebuild();
				Widget->MarkCanvasUpdate(true);
			}
		}
	}
}

#endif

void UDreamUIManagerWorldSubsystem::AddCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.AddUnique(InCanvas);
}

void UDreamUIManagerWorldSubsystem::RemoveCanvas(UDreamCanvas* InCanvas)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!this->AllCanvasArray.Contains(InCanvas))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	this->AllCanvasArray.RemoveSingle(InCanvas);
}

#if WITH_EDITOR
int32 UDreamUIManagerWorldSubsystem::CountCompetingScreenSpaceOverlayCanvases()const
{
	int32 Count = 0;
	for (auto& Canvas : AllCanvasArray)
	{
		if (!Canvas.IsValid())continue;
		if (!Canvas->IsRootCanvas())continue;
		if (Canvas->GetRenderMode() != EDreamRenderMode::ScreenSpaceOverlay)continue;
		// A canvas on an inactive widget is not on screen and is not fighting anyone for it. This
		// is the ordinary state of a widget that has been created but not yet added, so counting it
		// would fire the "only one ScreenSpace UI" error on a page prefab merely being prepared.
		const UDreamWidget* CanvasWidget = Canvas->GetWidget();
		if (CanvasWidget != nullptr && !CanvasWidget->GetWidgetActiveInHierarchy())continue;
		Count++;
	}
	return Count;
}
#endif

void UDreamUIManagerWorldSubsystem::ParkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget) || IsWidgetParked(InWidget))
	{
		return;
	}
	FDreamParkedWidgetEntry& Entry = ParkedWidgets.AddDefaulted_GetRef();
	Entry.Widget = InWidget;
	Entry.ParkedAtSeconds = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	InWidget->SetParked(true);
}

bool UDreamUIManagerWorldSubsystem::UnparkWidget(UDreamWidget* InWidget)
{
	if (!IsValid(InWidget))
	{
		return false;
	}
	const int32 Index = ParkedWidgets.IndexOfByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
	if (Index == INDEX_NONE)
	{
		return false;
	}
	ParkedWidgets.RemoveAt(Index);
	InWidget->SetParked(false);
	return true;
}

namespace DreamParkedWidgetConsole
{
	/**
	 * The other half of "held, not lost". Holding created widgets in a named array is what stops
	 * them being collected; being able to list them is what stops that becoming a place things
	 * quietly accumulate.
	 */
	static FAutoConsoleCommandWithWorldAndArgs ListParkedWidgetsCommand(
		TEXT("dreamgui.ListPendingWidgets"),
		TEXT("List widgets created but not yet added to anything, with how long they have been waiting."),
		FConsoleCommandWithWorldAndArgsDelegate::CreateLambda(
			[](const TArray<FString>& Args, UWorld* World)
			{
				auto DreamUIManager = UDreamUIManagerWorldSubsystem::GetInstance(World);
				if (DreamUIManager == nullptr)
				{
					UE_LOG(DreamGUI, Log, TEXT("No DreamUI manager for this world."));
					return;
				}
				const TArray<FDreamParkedWidgetEntry>& Parked = DreamUIManager->GetParkedWidgets();
				if (Parked.IsEmpty())
				{
					UE_LOG(DreamGUI, Log, TEXT("No pending widgets."));
					return;
				}
				const double Now = World ? World->GetTimeSeconds() : 0.0;
				UE_LOG(DreamGUI, Log, TEXT("%d pending widget(s):"), Parked.Num());
				for (const FDreamParkedWidgetEntry& Entry : Parked)
				{
					UE_LOG(DreamGUI, Log, TEXT("  %s   waiting %.1fs")
						, Entry.Widget != nullptr ? *Entry.Widget->GetPathDisplayName() : TEXT("<stale>")
						, Now - Entry.ParkedAtSeconds);
				}
			}));
}

int32 UDreamUIManagerWorldSubsystem::SweepExpiredParkedWidgets()
{
	const float LifetimeSeconds = UDreamUISettings::GetParkedWidgetLifetimeSeconds();
	if (LifetimeSeconds <= 0.0f || ParkedWidgets.IsEmpty())
	{
		return 0;//off by default: a slow-but-legitimate caller must not have its widget taken away
	}
	UWorld* World = GetWorld();
	if (World == nullptr)
	{
		return 0;
	}
	const double Now = World->GetTimeSeconds();

	TArray<TObjectPtr<UDreamWidget>> Expired;
	for (const FDreamParkedWidgetEntry& Entry : ParkedWidgets)
	{
		if (Entry.Widget != nullptr && (Now - Entry.ParkedAtSeconds) >= (double)LifetimeSeconds)
		{
			Expired.Add(Entry.Widget);
		}
	}
	for (const TObjectPtr<UDreamWidget>& Widget : Expired)
	{
		if (!IsValid(Widget))
		{
			continue;
		}
		UE_LOG(DreamGUI, Warning, TEXT("Widget %s was created %.1fs ago and never added to anything; destroying it. Add it with AddChild or AddToViewport, or destroy it yourself. (DreamUI setting: Parked Widget Lifetime Seconds)")
			, *Widget->GetPathDisplayName(), LifetimeSeconds);
		// DestroyWidget rather than letting go: it unregisters and ends play in the right order, so
		// the widget never reaches BeginDestroy still registered, which is the state that logs an
		// error and an on-screen banner from a stack that says nothing about where it came from.
		Widget->DestroyWidget();
	}
	return Expired.Num();
}

bool UDreamUIManagerWorldSubsystem::IsWidgetParked(const UDreamWidget* InWidget)const
{
	return ParkedWidgets.ContainsByPredicate(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == InWidget; });
}

void UDreamUIManagerWorldSubsystem::AddWidget(UDreamWidget* InWidget)
{
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.AddUnique(InWidget);
}

void UDreamUIManagerWorldSubsystem::RemoveWidget(UDreamWidget* InWidget)
{
	ParkedWidgets.RemoveAll(
		[InWidget](const FDreamParkedWidgetEntry& Entry) { return Entry.Widget == nullptr || Entry.Widget == InWidget; });
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
	if (!AllWidgetArray.Contains(InWidget))
	{
		UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
		FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
	}
#endif
	AllWidgetArray.RemoveSingle(InWidget);
}

void UDreamUIManagerWorldSubsystem::DestroyRegisteredWidgetTrees()
{
	if (AllWidgetArray.IsEmpty())
	{
		return;
	}

	const TArray<TObjectPtr<UDreamWidget>> RegisteredWidgets = AllWidgetArray;
	TSet<UDreamWidget*> Roots;
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget == nullptr || Widget->HasAnyFlags(RF_FinishDestroyed))
		{
			continue;
		}
		UDreamWidget* Root = Widget->GetRootWidgetInHierarchyEvenIfUnreachable();
		Roots.Add(Root ? Root : Widget);
	}

	for (UDreamWidget* Root : Roots)
	{
		if (Root != nullptr && !Root->HasAnyFlags(RF_FinishDestroyed))
		{
			Root->DestroyWidget();
		}
	}

	// Corrupt or partially collected hierarchies may not have a usable cached root.
	for (UDreamWidget* Widget : RegisteredWidgets)
	{
		if (Widget != nullptr && !Widget->HasAnyFlags(RF_FinishDestroyed)
			&& (Widget->HasRegistered() || Widget->HasBegunPlay()))
		{
			Widget->DestroyWidget();
		}
	}
}

void UDreamUIManagerWorldSubsystem::AddLayoutDirtyWidget(UDreamWidget* InWidget)
{
	if (IsValid(InWidget))
	{
		LayoutDirtyWidgetArray.AddUnique(InWidget);
	}
}

void UDreamUIManagerWorldSubsystem::MarkRebuildLayoutTree(UDreamWidget* InWidget)
{
	if (!bIsExecutingLayout)
	{
		MapWidgetToLayoutTree.Remove(InWidget);
	}
}

void UDreamUIManagerWorldSubsystem::MarkRebuildAllLayoutTree()
{
	if (!bIsExecutingLayout)
	{
		MapWidgetToLayoutTree.Empty();
	}
}

void UDreamUIManagerWorldSubsystem::CalculateLayoutTree(UDreamWidget* RootLayoutWidget)
{
	if (!IsValid(RootLayoutWidget))
	{
		return;
	}

	struct LOCAL
	{
		static void CollectLayoutTree(UDreamWidget* Widget, TArray<TObjectPtr<UDreamWidget>>& LayoutTreeArray,
			TSet<const UDreamWidget*>& VisitedWidgets)
		{
			if (!IsValid(Widget))return;
			if (VisitedWidgets.Contains(Widget))return;
			VisitedWidgets.Add(Widget);
			//Collect the full subtree, including layout-invisible and not-yet-registered widgets. Both flags flip
			//without a usable chance to invalidate this cache: a collapsed subtree that becomes visible from
			//inside a layout pass hits the bIsExecutingLayout guard in MarkRebuildAllLayoutTree, and OnRegister
			//never invalidates the cache at all. Pruning here would bake such a subtree out of the cached tree
			//permanently, so it would only lay out again after something re-dirties the whole tree top-down
			//(a viewport resize). Filter per-widget at update time instead.
			LayoutTreeArray.Add(Widget);
			for (UDreamWidget* Child : Widget->GetChildren())
			{
				CollectLayoutTree(Child, LayoutTreeArray, VisitedWidgets);
			}
		}
	};
	auto& LayoutTree = MapWidgetToLayoutTree.FindOrAdd(RootLayoutWidget);
	if (LayoutTree.WidgetArray.IsEmpty())
	{
		TSet<const UDreamWidget*> VisitedWidgets;
		LOCAL::CollectLayoutTree(RootLayoutWidget, LayoutTree.WidgetArray, VisitedWidgets);
	}
	//Iterate a copy: UpdateLayout can re-enter CalculateLayoutTree through RebuildLayoutImmediately, and the
	//FindOrAdd there may rehash the map out from under a reference into it.
	const TArray<TObjectPtr<UDreamWidget>> LayoutTreeArray = LayoutTree.WidgetArray;
	for (int i = 0; i < LayoutTreeArray.Num(); i++)
	{
		auto Widget = LayoutTreeArray[i];
		if (!IsValid(Widget))
		{
			continue;
		}
		if (!Widget->GetLayoutVisibleInHierarchy())
		{
			continue;//collapsed for layout, but stays in the tree so it lays out as soon as it becomes visible
		}
		if (!Widget->HasRegistered())
		{
			continue;//if not registered, means it could about to remove
		}
		if (auto LayoutContainer = Widget->GetLayoutContainer())
		{
			if (!LayoutContainerArrayWhichHasSnapshot.Contains(LayoutContainer))
			{
				LayoutContainer->SnapshotLayout();
				LayoutContainerArrayWhichHasSnapshot.Add(LayoutContainer);
			}
		}
		Widget->UpdateLayout();
	}
}

void UDreamUIManagerWorldSubsystem::RebuildLayoutImmediately(UDreamWidget* InWidget)
{
	auto RootLayoutWidget = InWidget;
	//move up, find if parent widget affect by layout then mark dirty
	while (RootLayoutWidget)
	{
		if (auto ParentWidget = RootLayoutWidget->GetParent())
		{
			if (ParentWidget->GetLayoutContainer())//parent contains LayoutContainer, need calculate layout
			{
				RootLayoutWidget = ParentWidget;
				continue;
			}
		}
		break;
	}

	bool bCanCalculateLayoutTree = true;
	if (RootLayoutWidget == InWidget)//no valid layout parent
	{
		if (InWidget->GetLayoutContainer())//self contains layout container
		{
			bCanCalculateLayoutTree = true;
		}
		else
		{
			bCanCalculateLayoutTree = false;
		}
	}
	if (bCanCalculateLayoutTree)
	{
		CalculateLayoutTree(RootLayoutWidget);
	}
}

#if WITH_EDITOR
int UDreamUIManagerWorldSubsystem::IncreateLayoutCalculationCounter(const FString& InPathName)
{
	if (auto CounterPtr = LayoutCalculationCounterMap.Find(InPathName))
	{
		(*CounterPtr)++;
		if (*CounterPtr >= 2)
		{
			// UE_LOG(DreamGUI, Warning, TEXT("Widget %s has been calculated layout %d times in a frame"), *InPathName, *CounterPtr);
		}
		return *CounterPtr;
	}
	else
	{
		LayoutCalculationCounterMap.Add(InPathName, 1);
		return 1;
	}
}
#endif

TSharedPtr<class FDreamUIRenderer, ESPMode::ThreadSafe> UDreamUIManagerWorldSubsystem::GetViewExtension(UWorld* InWorld, bool InCreateIfNotExist)
{
	if (auto Instance = GetInstance(InWorld))
	{
		if (!Instance->MainViewportViewExtension.IsValid())
		{
			if (InCreateIfNotExist)
			{
				Instance->MainViewportViewExtension = FSceneViewExtensions::NewExtension<FDreamUIRenderer>(InWorld, EDreamUIRendererType::ScreenSpace_and_WorldSpace);
			}
		}
		return Instance->MainViewportViewExtension;
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddRaycaster(UDreamBaseRaycaster* InRaycaster)
{
	if (auto Instance = GetInstance(InRaycaster->GetWorld()))
	{
		auto& AllRaycasterArray = Instance->AllRaycasterArray;
		if (AllRaycasterArray.Contains(InRaycaster))return;
		AllRaycasterArray.Add(InRaycaster);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveRaycaster(UDreamBaseRaycaster* InRaycaster)
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

void UDreamUIManagerWorldSubsystem::AddSelectable(UUISelectable* InSelectable)
{
	if (auto Instance = GetInstance(InSelectable->GetWorld()))
	{
		auto& AllSelectableArray = Instance->AllSelectableArray;
#if !UE_BUILD_SHIPPING && ENABLED_DreamGUI_DEBUG_DUMP
		if (AllSelectableArray.Contains(InSelectable))
		{
			UE_LOG(DreamGUI, Error, TEXT("[%s].%d break here for debug"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
			FDebug::DumpStackTraceToLog(ELogVerbosity::Warning);
		}
#endif
		AllSelectableArray.AddUnique(InSelectable);
	}
}
void UDreamUIManagerWorldSubsystem::RemoveSelectable(UUISelectable* InSelectable)
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

UDreamEventSystem* UDreamUIManagerWorldSubsystem::GetEventSystemByUserIndex(int UserIndex)
{
	if (auto ResultPtr = MapUserIndexToEventSystem.Find(UserIndex))
	{
		return ResultPtr->Get();
	}
	return nullptr;
}

void UDreamUIManagerWorldSubsystem::AddEventSystem(UDreamEventSystem* InEventSystem)
{
	if (auto InstancePtr = MapUserIndexToEventSystem.Find(InEventSystem->GetUserIndex()))
	{
		auto Instance = *InstancePtr;
		FString ActorName =
#if WITH_EDITOR
			Instance->GetOwner()->GetActorLabel();
#else
			Instance->GetOwner()->GetName();
#endif
		FString ErrorMsg = FString::Printf(TEXT("[%s].%d DreamEventSystem component is already exist in actor:%s, pathName:%s, world:%s, multiple DreamEventSystem with same UserIndex in same world is not allowed!")
			, ANSI_TO_TCHAR(__FUNCTION__), __LINE__, *ActorName, *Instance->GetPathName(), *GetWorld()->GetPathName());
		UE_LOG(DreamGUI, Error, TEXT("%s"), *ErrorMsg);
		GEngine->AddOnScreenDebugMessage(-1, -1, FColor::Red, ErrorMsg);
#if WITH_EDITOR
		FDreamUIUtils::EditorNotification(FText::FromString(ErrorMsg), false, 10);
#endif
	}
	else
	{
		MapUserIndexToEventSystem.Add(InEventSystem->GetUserIndex(), InEventSystem);
	}
}

void UDreamUIManagerWorldSubsystem::RemoveEventSystem(UDreamEventSystem* InEventSystem)
{
	MapUserIndexToEventSystem.Remove(InEventSystem->GetUserIndex());
}

#undef LOCTEXT_NAMESPACE
