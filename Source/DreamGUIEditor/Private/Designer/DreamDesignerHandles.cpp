// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Designer/DreamDesignerHandles.h"

#include "Core/Components/DreamVisual.h"
#include "Core/Components/DreamWidget.h"
#include "Core/DreamUIBehaviour.h"
#include "DetailCustomization/DreamPropertyMeta.h"
#include "UObject/ObjectKey.h"
#include "UObject/UnrealType.h"

namespace DreamDesignerHandlesLocal
{
	TArray<TSharedRef<IDreamDesignerHandleProvider>>& GetRegistry()
	{
		static TArray<TSharedRef<IDreamDesignerHandleProvider>> Providers;
		return Providers;
	}

	TMap<FName, FDreamDesignerHandleKindFactory>& GetKinds()
	{
		static TMap<FName, FDreamDesignerHandleKindFactory> Kinds;
		return Kinds;
	}

	/** A class's tagged properties, found once: the owner is filled in for each object. */
	const TArray<FDreamDesignerHandleBinding>& GetClassBindings(const UClass* InClass)
	{
		static TMap<TObjectKey<UClass>, TArray<FDreamDesignerHandleBinding>> Cache;
		if (const TArray<FDreamDesignerHandleBinding>* Found = Cache.Find(InClass))
		{
			return *Found;
		}
		TArray<FDreamDesignerHandleBinding>& Bindings = Cache.Add(InClass);
		auto Take = [&Bindings](const FProperty* InHead, const FProperty* InProperty)
		{
			FString Args;
			const FName Kind = DreamPropertyMeta::GetHandle(InProperty, &Args);
			if (!Kind.IsNone())
			{
				FDreamDesignerHandleBinding& Binding = Bindings.AddDefaulted_GetRef();
				Binding.HeadProperty = InHead->GetFName();
				Binding.Property = InProperty->GetFName();
				Binding.Kind = Kind;
				Binding.Args = MoveTemp(Args);
			}
		};
		for (TFieldIterator<FProperty> It(InClass); It; ++It)
		{
			Take(*It, *It);
			if (const FStructProperty* AsStruct = CastField<FStructProperty>(*It))
			{
				for (TFieldIterator<FProperty> Member(AsStruct->Struct); Member; ++Member)
				{
					Take(*It, *Member);
				}
			}
		}
		return Bindings;
	}

	/** The providers made for bindings, by the object and the property they are for; dropped once the object is gone. */
	struct FBoundKey
	{
		FObjectKey Owner;
		FName HeadProperty;
		FName Property;

		bool operator==(const FBoundKey& InOther) const
		{
			return Owner == InOther.Owner && HeadProperty == InOther.HeadProperty && Property == InOther.Property;
		}
		friend uint32 GetTypeHash(const FBoundKey& InKey)
		{
			return HashCombine(GetTypeHash(InKey.Owner), HashCombine(GetTypeHash(InKey.HeadProperty), GetTypeHash(InKey.Property)));
		}
	};

	TMap<FBoundKey, TSharedPtr<IDreamDesignerHandleProvider>>& GetBound()
	{
		static TMap<FBoundKey, TSharedPtr<IDreamDesignerHandleProvider>> Bound;
		return Bound;
	}
	FName GActive = NAME_None;
}

const FName FDreamDesignerHandles::Off(TEXT("Off"));

FDreamDesignerHandle& FDreamDesignerHandleSet::AddHandle(int32 InId, const FVector2D& InLocal)
{
	FDreamDesignerHandle& Handle = Handles.AddDefaulted_GetRef();
	Handle.Id = InId;
	Handle.Local = InLocal;
	return Handle;
}

void FDreamDesignerHandleSet::AddLine(const FVector2D& InFrom, const FVector2D& InTo, const FLinearColor& InColor, float InThickness)
{
	FDreamDesignerHandleLine& Line = Lines.AddDefaulted_GetRef();
	Line.From = InFrom;
	Line.To = InTo;
	Line.Color = InColor;
	Line.Thickness = InThickness;
}

void FDreamDesignerHandleSet::AddPolyline(TConstArrayView<FVector2D> InPoints, bool bInClosed, const FLinearColor& InColor, float InThickness)
{
	for (int32 Index = 0; Index + 1 < InPoints.Num(); ++Index)
	{
		AddLine(InPoints[Index], InPoints[Index + 1], InColor, InThickness);
	}
	if (bInClosed && InPoints.Num() > 2)
	{
		AddLine(InPoints.Last(), InPoints[0], InColor, InThickness);
	}
}

void FDreamDesignerHandleSet::AddEllipse(const FVector2D& InCenter, const FVector2D& InRadii, float InAngleDegrees, const FLinearColor& InColor, int32 InSegments)
{
	const int32 Segments = FMath::Clamp(InSegments, 8, 256);
	// Clockwise on screen is towards -Y here: local space runs up.
	double Sine = 0.0;
	double Cosine = 1.0;
	FMath::SinCos(&Sine, &Cosine, FMath::DegreesToRadians(static_cast<double>(InAngleDegrees)));
	const FVector2D AxisX(Cosine, -Sine);
	const FVector2D AxisY(Sine, Cosine);
	TArray<FVector2D> Points;
	Points.Reserve(Segments);
	for (int32 Index = 0; Index < Segments; ++Index)
	{
		double PointSine = 0.0;
		double PointCosine = 1.0;
		FMath::SinCos(&PointSine, &PointCosine, UE_DOUBLE_TWO_PI * Index / Segments);
		Points.Add(InCenter + AxisX * (PointCosine * InRadii.X) + AxisY * (PointSine * InRadii.Y));
	}
	AddPolyline(Points, /*bInClosed*/ true, InColor);
}

void FDreamDesignerHandleSet::AddRoundedRect(const FVector2D& InCenter, const FVector2D& InHalfSize, const FVector4f& InRadii, const FLinearColor& InColor)
{
	const FVector2D Half(FMath::Abs(InHalfSize.X), FMath::Abs(InHalfSize.Y));
	const double Most = FMath::Min(Half.X, Half.Y);
	// Corner by corner, going round anticlockwise from the right-bottom: each corner's centre, its radius and where it starts.
	struct FCorner { FVector2D Sign; double Radius; double StartDegrees; };
	const FCorner Corners[4] = {
		{ FVector2D(1.0, -1.0), FMath::Clamp(static_cast<double>(InRadii.X), 0.0, Most), -90.0 },
		{ FVector2D(1.0, 1.0), FMath::Clamp(static_cast<double>(InRadii.Y), 0.0, Most), 0.0 },
		{ FVector2D(-1.0, 1.0), FMath::Clamp(static_cast<double>(InRadii.Z), 0.0, Most), 90.0 },
		{ FVector2D(-1.0, -1.0), FMath::Clamp(static_cast<double>(InRadii.W), 0.0, Most), 180.0 },
	};
	TArray<FVector2D> Points;
	for (const FCorner& Corner : Corners)
	{
		const FVector2D Centre = InCenter + Corner.Sign * (Half - FVector2D(Corner.Radius, Corner.Radius));
		const int32 Steps = Corner.Radius > 0.0 ? 8 : 1;
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const double Angle = FMath::DegreesToRadians(Corner.StartDegrees + 90.0 * Step / FMath::Max(Steps - 1, 1));
			Points.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Corner.Radius);
		}
	}
	AddPolyline(Points, /*bInClosed*/ true, InColor);
}

void IDreamDesignerHandleProvider::OnRestored(UDreamWidget* InWidget, TConstArrayView<FDreamDesignerEditedProperty> InProperties)
{
	for (const FDreamDesignerEditedProperty& Edited : InProperties)
	{
		UObject* Object = Edited.Object.Get();
		FProperty* Property = Object != nullptr ? Object->GetClass()->FindPropertyByName(Edited.Property) : nullptr;
		if (Property != nullptr)
		{
			FPropertyChangedEvent Event(Property, EPropertyChangeType::ValueSet);
			Object->PostEditChangeProperty(Event);
		}
	}
}

void FDreamDesignerHandles::Register(const TSharedRef<IDreamDesignerHandleProvider>& InProvider)
{
	DreamDesignerHandlesLocal::GetRegistry().AddUnique(InProvider);
}

void FDreamDesignerHandles::Unregister(const TSharedRef<IDreamDesignerHandleProvider>& InProvider)
{
	DreamDesignerHandlesLocal::GetRegistry().Remove(InProvider);
}

const TArray<TSharedRef<IDreamDesignerHandleProvider>>& FDreamDesignerHandles::GetProviders()
{
	return DreamDesignerHandlesLocal::GetRegistry();
}

TSharedPtr<IDreamDesignerHandleProvider> FDreamDesignerHandles::FindProvider(FName InName)
{
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : DreamDesignerHandlesLocal::GetRegistry())
	{
		if (Provider->GetName() == InName)
		{
			return Provider;
		}
	}
	return nullptr;
}

void FDreamDesignerHandles::RegisterKind(FName InKind, FDreamDesignerHandleKindFactory InFactory)
{
	DreamDesignerHandlesLocal::GetKinds().Add(InKind, MoveTemp(InFactory));
	DreamDesignerHandlesLocal::GetBound().Reset();
}

void FDreamDesignerHandles::UnregisterKind(FName InKind)
{
	DreamDesignerHandlesLocal::GetKinds().Remove(InKind);
	DreamDesignerHandlesLocal::GetBound().Reset();
}

bool FDreamDesignerHandles::IsKindRegistered(FName InKind)
{
	return DreamDesignerHandlesLocal::GetKinds().Contains(InKind);
}

void FDreamDesignerHandles::CollectBindings(const UDreamWidget* InWidget, TArray<FDreamDesignerHandleBinding>& OutBindings)
{
	OutBindings.Reset();
	if (!IsValid(InWidget))
	{
		return;
	}
	auto Collect = [&OutBindings](UObject* InOwner)
	{
		if (!IsValid(InOwner))
		{
			return;
		}
		for (const FDreamDesignerHandleBinding& Binding : DreamDesignerHandlesLocal::GetClassBindings(InOwner->GetClass()))
		{
			FDreamDesignerHandleBinding& Bound = OutBindings.Add_GetRef(Binding);
			Bound.Owner = InOwner;
		}
	};
	Collect(InWidget->GetVisual());
	for (UDreamUIBehaviour* Behaviour : InWidget->GetComponents(UDreamUIBehaviour::StaticClass()))
	{
		Collect(Behaviour);
	}
	Collect(const_cast<UDreamWidget*>(InWidget));
}

TArray<TSharedRef<IDreamDesignerHandleProvider>> FDreamDesignerHandles::GetProvidersFor(const UDreamWidget* InWidget)
{
	using namespace DreamDesignerHandlesLocal;
	TArray<TSharedRef<IDreamDesignerHandleProvider>> Providers = GetRegistry();
	TArray<FDreamDesignerHandleBinding> Bindings;
	CollectBindings(InWidget, Bindings);
	TMap<FBoundKey, TSharedPtr<IDreamDesignerHandleProvider>>& Bound = GetBound();
	for (auto It = Bound.CreateIterator(); It; ++It)
	{
		if (It.Key().Owner.ResolveObjectPtr() == nullptr)
		{
			It.RemoveCurrent();
		}
	}
	for (const FDreamDesignerHandleBinding& Binding : Bindings)
	{
		const FDreamDesignerHandleKindFactory* Factory = GetKinds().Find(Binding.Kind);
		if (Factory == nullptr)
		{
			continue;
		}
		const FBoundKey Key{ FObjectKey(Binding.Owner.Get()), Binding.HeadProperty, Binding.Property };
		TSharedPtr<IDreamDesignerHandleProvider>* Made = Bound.Find(Key);
		if (Made == nullptr)
		{
			Made = &Bound.Add(Key, (*Factory)(Binding));
		}
		if (Made->IsValid())
		{
			Providers.Add(Made->ToSharedRef());
		}
	}
	return Providers;
}

TSharedPtr<IDreamDesignerHandleProvider> FDreamDesignerHandles::FindProviderFor(const UDreamWidget* InWidget, FName InName)
{
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : GetProvidersFor(InWidget))
	{
		if (Provider->GetName() == InName)
		{
			return Provider;
		}
	}
	return nullptr;
}

FName FDreamDesignerHandles::MakeBoundName(FName InKind, FName InProperty)
{
	return FName(*FString::Printf(TEXT("%s.%s"), *InKind.ToString(), *InProperty.ToString()));
}

void FDreamDesignerHandles::SetActive(FName InName)
{
	if (DreamDesignerHandlesLocal::GActive != InName)
	{
		DreamDesignerHandlesLocal::GActive = InName;
		OnActiveChanged().Broadcast();
	}
}

FName FDreamDesignerHandles::GetActive()
{
	return DreamDesignerHandlesLocal::GActive;
}

FSimpleMulticastDelegate& FDreamDesignerHandles::OnActiveChanged()
{
	static FSimpleMulticastDelegate Delegate;
	return Delegate;
}

TSharedPtr<IDreamDesignerHandleProvider> FDreamDesignerHandles::ChooseShown(TConstArrayView<TSharedRef<IDreamDesignerHandleProvider>> InAvailable)
{
	const FName Active = GetActive();
	if (Active == Off || InAvailable.IsEmpty())
	{
		return nullptr;
	}
	for (const TSharedRef<IDreamDesignerHandleProvider>& Provider : InAvailable)
	{
		if (Provider->GetName() == Active)
		{
			return Provider;
		}
	}
	return InAvailable[0];
}

FName FDreamDesignerHandles::Cycle(TConstArrayView<TSharedRef<IDreamDesignerHandleProvider>> InAvailable)
{
	if (InAvailable.IsEmpty())
	{
		return GetActive();
	}
	const TSharedPtr<IDreamDesignerHandleProvider> Shown = ChooseShown(InAvailable);
	FName Next = InAvailable[0]->GetName();
	if (Shown.IsValid())
	{
		const int32 Index = InAvailable.IndexOfByPredicate([&Shown](const TSharedRef<IDreamDesignerHandleProvider>& InProvider) { return InProvider == Shown; });
		Next = InAvailable.IsValidIndex(Index + 1) ? InAvailable[Index + 1]->GetName() : Off;
	}
	SetActive(Next);
	return Next;
}

FVector2D FDreamDesignerHandles::RectFractionToLocal(const UDreamWidget* InWidget, const FVector2D& InFromTopLeft)
{
	if (InWidget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	return FVector2D(
		InWidget->GetLocalSpaceLeft() + InFromTopLeft.X * InWidget->GetWidth(),
		InWidget->GetLocalSpaceTop() - InFromTopLeft.Y * InWidget->GetHeight());
}

FVector2D FDreamDesignerHandles::LocalToRectFraction(const UDreamWidget* InWidget, const FVector2D& InLocal)
{
	if (InWidget == nullptr)
	{
		return FVector2D::ZeroVector;
	}
	const double Width = FMath::Max(static_cast<double>(InWidget->GetWidth()), UE_KINDA_SMALL_NUMBER);
	const double Height = FMath::Max(static_cast<double>(InWidget->GetHeight()), UE_KINDA_SMALL_NUMBER);
	return FVector2D((InLocal.X - InWidget->GetLocalSpaceLeft()) / Width, (InWidget->GetLocalSpaceTop() - InLocal.Y) / Height);
}

FLinearColor FDreamDesignerHandles::GetGuideColor()
{
	return FLinearColor(0.95f, 0.95f, 0.95f, 0.9f);
}

FLinearColor FDreamDesignerHandles::GetActiveColor()
{
	return FLinearColor(1.0f, 0.78f, 0.2f, 1.0f);
}
