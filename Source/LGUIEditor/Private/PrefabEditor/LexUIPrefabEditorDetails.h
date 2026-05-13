// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"
#pragma once

class FLexUIPrefabEditor;
class ULexWidget;
class SLexWidgetComponentEditor;

/**
 * 
 */
class SLexUIPrefabEditorDetails : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SLexUIPrefabEditorDetails)
    {
    }

    SLATE_END_ARGS()

    /** Widget constructor */
    void Construct(const FArguments& Args, TSharedPtr<FLexUIPrefabEditor> InPrefabEditor);

	virtual ~SLexUIPrefabEditorDetails();
private:
	ULexWidget* GetSelectedWidgetContext() const;
	void OnEditorSelectionChanged();
	void OnComponentSelectionChanged(const TArray<TWeakObjectPtr<class ULexUIBehaviour>>& SelectedComponents);

	TWeakPtr<FLexUIPrefabEditor> PrefabEditorPtr;

	bool IsPropertyReadOnly(const FPropertyAndParent& InPropertyAndParent);
	bool IsPrefabButtonEnable()const;
	FOptionalSize GetPrefabButtonHeight()const;
	EVisibility GetPrefabButtonVisibility()const;
	bool IsEditorAllowEditing()const;

	TSharedPtr<class IDetailsView> DetailsView;
	TSharedPtr<class SLexWidgetComponentEditor> ComponentEditor;
	TSharedPtr<class SLexUIPrefabOverrideDataViewer> PrefabOverrideDataViewer;
	TWeakObjectPtr<ULexWidget> CachedActor;
	bool bIsSelectFromLexUIEditor = false;
	bool bIsSelectFromDetails = false;
};
