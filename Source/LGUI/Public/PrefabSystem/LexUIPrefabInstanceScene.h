// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"
#include "LexUIPrefabScene.h"

#if WITH_EDITOR

class ULexWidgetPresenterComponent;
class ULexWidget;
class UStaticMeshComponent;
class ULexUIPrefab;
class AActor;

//Encapsulates a simple scene setup for Prefab Editor.
class LGUI_API FLexUIPrefabInstanceScene : public FLexUIPrefabScene
{
public:
	FLexUIPrefabInstanceScene(ConstructionValues CVS);
	~FLexUIPrefabInstanceScene();
	
	static const FString RootAgentActorName;
	ULexWidget* GetParentForLoadPrefab(ULexUIPrefab* InPrefab);
	ULexWidgetPresenterComponent* GetWidgetPresenter()const { return WidgetPresenter; }
	void SetSkyCubeVisibility(bool bVisible);
private:

	ULexWidgetPresenterComponent* WidgetPresenter = nullptr;
	UStaticMeshComponent* SkySphereComponent = nullptr;
};
#endif
