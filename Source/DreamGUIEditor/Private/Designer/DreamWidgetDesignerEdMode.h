// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Tools/LegacyEdModeWidgetHelpers.h"
#include "DreamWidgetDesignerEdMode.generated.h"

/**
 * The editor mode the prefab editor's viewport runs, and the only reason it exists is the engine's
 * transform gizmo.
 *
 * FWidget::Render draws nothing unless some active mode's ShouldDrawWidget() says yes, and the stock
 * answer (FLegacyEdModeWidgetHelper::ShouldDrawWidget) is "yes if actors or scene components are
 * selected". A prefab's contents are DreamWidgets, which the engine's selection sets never hold, so
 * with no mode of our own the gizmo is invisible however the viewport client answers
 * GetWidgetLocation. This mode answers yes unconditionally and lets
 * FDreamWidgetDesignerViewportClient::GetWidgetMode be the one place that decides whether a gizmo
 * appears -- it returns WM_None with nothing selected, and in the designer view, which has its own
 * rect handles.
 *
 * Registration is automatic: UAssetEditorSubsystem::RegisterEditorModes walks every UEdMode CDO and
 * registers it under GetModeInfo().ID.
 */
UCLASS()
class UDreamWidgetDesignerEdMode : public UBaseLegacyWidgetEdMode
{
	GENERATED_BODY()

public:
	static const FEditorModeID EM_DreamUIPrefab;

	UDreamWidgetDesignerEdMode();

	/** No toolkit: this mode has no UI of its own and must not open a panel in the prefab editor. */
	virtual bool UsesToolkits() const override { return false; }
	virtual bool ShouldDrawWidget() const override { return true; }
	virtual bool UsesTransformWidget() const override { return true; }
	virtual bool UsesTransformWidget(UE::Widget::EWidgetMode CheckMode) const override { return true; }
};
