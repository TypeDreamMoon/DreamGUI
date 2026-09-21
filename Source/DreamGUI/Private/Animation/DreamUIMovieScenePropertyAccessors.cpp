// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Animation/DreamUIMovieScenePropertyAccessors.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamWidgetPresenterComponentBase.h"
#include "MovieSceneTracksComponentTypes.h"
#include "MovieSceneTracksPropertyTypes.h"

namespace DreamUI
{
namespace
{
	using UE::MovieScene::FDoubleIntermediateVector;

	FDoubleIntermediateVector FromVector(const FVector& In)
	{
		return FDoubleIntermediateVector(In.X, In.Y, In.Z);
	}

	FVector ToVector(const FDoubleIntermediateVector& In)
	{
		return FVector(In.X, In.Y, In.Z);
	}

	// One pair per property. Each is the widget's own setter, so whatever that setter does on a
	// change -- the render transform re-derivation, the layout re-request -- happens exactly as it
	// would for a gameplay call.

	FDoubleIntermediateVector GetRenderTranslation(const UObject* Object)
	{
		return FromVector(CastChecked<UDreamWidget>(Object)->GetRenderTranslation());
	}
	void SetRenderTranslation(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidget>(Object)->SetRenderTranslation(ToVector(In));
	}

	FDoubleIntermediateVector GetRenderScale(const UObject* Object)
	{
		return FromVector(CastChecked<UDreamWidget>(Object)->GetRenderScale());
	}
	void SetRenderScale(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidget>(Object)->SetRenderScale(ToVector(In));
	}

	FDoubleIntermediateVector GetRelativeLocation(const UObject* Object)
	{
		return FromVector(CastChecked<UDreamWidget>(Object)->GetRelativeLocation());
	}
	void SetRelativeLocation(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidget>(Object)->SetRelativeLocation(ToVector(In));
	}

	FDoubleIntermediateVector GetRelativeScale(const UObject* Object)
	{
		return FromVector(CastChecked<UDreamWidget>(Object)->GetRelativeScale());
	}
	void SetRelativeScale(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidget>(Object)->SetRelativeScale(ToVector(In));
	}

	FDoubleIntermediateVector GetPerspectiveOrigin(const UObject* Object)
	{
		const FVector2D Origin = CastChecked<UDreamWidget>(Object)->GetPerspectiveOrigin();
		return FDoubleIntermediateVector(Origin.X, Origin.Y);
	}
	void SetPerspectiveOrigin(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidget>(Object)->SetPerspectiveOrigin(FVector2D(In.X, In.Y));
	}

	FDoubleIntermediateVector GetWidgetOffset(const UObject* Object)
	{
		return FromVector(CastChecked<UDreamWidgetPresenterComponentBase>(Object)->GetWidgetOffset());
	}
	void SetWidgetOffset(UObject* Object, const FDoubleIntermediateVector& In)
	{
		CastChecked<UDreamWidgetPresenterComponentBase>(Object)->SetWidgetOffset(ToVector(In));
	}
}

void EnsureMovieScenePropertyAccessorsRegistered()
{
	static bool bRegistered = false;
	if (bRegistered)
	{
		return;
	}
	bRegistered = true;

	// A bisecting switch: with it, the vector tracks fall back to the engine's reflective path,
	// which is how to tell an engine-side regression from one of these accessors. The playback
	// tests pass either way; only the path the value travels changes.
	if (FParse::Param(FCommandLine::Get(), TEXT("DreamGUINoVectorAccessors")))
	{
		UE_LOG(LogTemp, Warning, TEXT("DreamGUI: -DreamGUINoVectorAccessors is set; Sequencer vector tracks on widgets use the reflective property path."));
		return;
	}

	using namespace UE::MovieScene;
	FMovieSceneTracksComponentTypes* TrackComponents = FMovieSceneTracksComponentTypes::Get();

	// Registered against UDreamWidget: the lookup walks the bound object's class hierarchy, so
	// every widget subclass gets them.
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidget::StaticClass(), "RenderTranslation", &GetRenderTranslation, &SetRenderTranslation);
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidget::StaticClass(), "RenderScale", &GetRenderScale, &SetRenderScale);
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidget::StaticClass(), "RelativeLocation", &GetRelativeLocation, &SetRelativeLocation);
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidget::StaticClass(), "RelativeScale", &GetRelativeScale, &SetRelativeScale);
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidget::StaticClass(), "PerspectiveOrigin", &GetPerspectiveOrigin, &SetPerspectiveOrigin);
	TrackComponents->Accessors.DoubleVector.Add(UDreamWidgetPresenterComponentBase::StaticClass(), "WidgetOffset", &GetWidgetOffset, &SetWidgetOffset);
}
}
