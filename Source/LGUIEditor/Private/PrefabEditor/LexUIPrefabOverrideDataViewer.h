// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

class FLGUIPrefabEditor;
struct FLGUIPrefabOverrideParameterData;
class ULGUIPrefabHelperObject;
class AActor;
class ULGUIPrefab;

DECLARE_DELEGATE_OneParam(FLexUIPrefabOverrideDataViewer_AfterRevertPrefab, ULGUIPrefab*);
DECLARE_DELEGATE_OneParam(FLexUIPrefabOverrideDataViewer_AfterApplyPrefab, ULGUIPrefab*);

class SLexUIPrefabOverrideDataViewer : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabOverrideDataViewer) {}
		SLATE_EVENT(FLexUIPrefabOverrideDataViewer_AfterRevertPrefab, AfterRevertPrefab)
		SLATE_EVENT(FLexUIPrefabOverrideDataViewer_AfterApplyPrefab, AfterApplyPrefab)
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, TFunction<AActor*()> GetSelectedActorFunction);
private:
	void RefreshDataContent();
	void RefreshDataContent(TArray<FLGUIPrefabOverrideParameterData> ObjectOverrideParameterArray, AActor* InReferenceActor);
	FLexUIPrefabOverrideDataViewer_AfterRevertPrefab AfterRevertPrefab;
	FLexUIPrefabOverrideDataViewer_AfterApplyPrefab AfterApplyPrefab;

	TSharedPtr<SVerticalBox> RootContentVerticalBox;
	TWeakObjectPtr<ULGUIPrefabHelperObject> PrefabHelperObject;
	TFunction<AActor*()> GetSelectedActorFunction = nullptr;
};
