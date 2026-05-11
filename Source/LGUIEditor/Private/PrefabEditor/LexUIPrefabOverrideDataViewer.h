// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLexUIPrefabEditor;
struct FLexUIPrefabOverrideParameterData;
class ULexUIPrefabHelperObject;
class ULexWidget;
class ULexUIPrefab;

DECLARE_DELEGATE_OneParam(FLexUIPrefabOverrideDataViewer_AfterRevertPrefab, ULexUIPrefab*);
DECLARE_DELEGATE_OneParam(FLexUIPrefabOverrideDataViewer_AfterApplyPrefab, ULexUIPrefab*);

class SLexUIPrefabOverrideDataViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabOverrideDataViewer) {}
		SLATE_EVENT(FLexUIPrefabOverrideDataViewer_AfterRevertPrefab, AfterRevertPrefab)
		SLATE_EVENT(FLexUIPrefabOverrideDataViewer_AfterApplyPrefab, AfterApplyPrefab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<ULexWidget*()> GetSelectedActorFunction);
	void RefreshDataContent();
private:
	void RefreshDataContent(TArray<FLexUIPrefabOverrideParameterData> ObjectOverrideParameterArray, ULexWidget* InReferenceWidget);
	FLexUIPrefabOverrideDataViewer_AfterRevertPrefab AfterRevertPrefab;
	FLexUIPrefabOverrideDataViewer_AfterApplyPrefab AfterApplyPrefab;

	TSharedPtr<SVerticalBox> RootContentVerticalBox;
	TWeakObjectPtr<ULexUIPrefabHelperObject> PrefabHelperObject;
	TFunction<ULexWidget*()> GetSelectedWidgetFunction = nullptr;
};
