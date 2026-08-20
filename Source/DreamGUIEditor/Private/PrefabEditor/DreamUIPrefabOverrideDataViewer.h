// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FDreamUIPrefabEditor;
struct FDreamUIPrefabOverrideParameterData;
class UDreamUIPrefabHelperObject;
class UDreamWidget;
class UDreamUIPrefab;

DECLARE_DELEGATE_OneParam(FDreamUIPrefabOverrideDataViewer_AfterRevertPrefab, UDreamUIPrefab*);
DECLARE_DELEGATE_OneParam(FDreamUIPrefabOverrideDataViewer_AfterApplyPrefab, UDreamUIPrefab*);

class SDreamUIPrefabOverrideDataViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SDreamUIPrefabOverrideDataViewer) {}
		SLATE_EVENT(FDreamUIPrefabOverrideDataViewer_AfterRevertPrefab, AfterRevertPrefab)
		SLATE_EVENT(FDreamUIPrefabOverrideDataViewer_AfterApplyPrefab, AfterApplyPrefab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<UDreamWidget*()> GetSelectedActorFunction);
	void RefreshDataContent();
private:
	void RefreshDataContent(TArray<FDreamUIPrefabOverrideParameterData> ObjectOverrideParameterArray, UDreamWidget* InReferenceWidget);
	FDreamUIPrefabOverrideDataViewer_AfterRevertPrefab AfterRevertPrefab;
	FDreamUIPrefabOverrideDataViewer_AfterApplyPrefab AfterApplyPrefab;

	TSharedPtr<SVerticalBox> RootContentVerticalBox;
	TWeakObjectPtr<UDreamUIPrefabHelperObject> PrefabHelperObject;
	TFunction<UDreamWidget*()> GetSelectedWidgetFunction = nullptr;
};
