// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverLocators.h"

#include "Core/Components/DreamWidget.h"

namespace DreamDriverLocatorsLocal
{
	/**
	 * Every widget under InRoot, root excluded, in child order.
	 *
	 * Children can hold nulls between a teardown and the next tidy-up -- the production lookups guard
	 * against it and so does this -- and a locator that crashed on a half-torn-down tree would be
	 * useless for exactly the tests that care about teardown.
	 */
	void ForEachDescendant(UDreamWidget* InRoot, TFunctionRef<void(UDreamWidget*)> InVisitor)
	{
		if (!IsValid(InRoot))
		{
			return;
		}
		for (UDreamWidget* Child : InRoot->GetChildren())
		{
			if (!IsValid(Child))
			{
				continue;
			}
			InVisitor(Child);
			ForEachDescendant(Child, InVisitor);
		}
	}

	class FDreamNameLocator : public IDreamElementLocator
	{
	public:
		explicit FDreamNameLocator(const FString& InDisplayName)
			: DisplayName(InDisplayName)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			if (!IsValid(InRoot))
			{
				return;
			}
			OutWidgets.Append(InRoot->FindChildArrayByDisplayName(DisplayName, true));
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("By::Name(\"%s\")"), *DisplayName);
		}

	private:
		FString DisplayName;
	};

	class FDreamPathLocator : public IDreamElementLocator
	{
	public:
		explicit FDreamPathLocator(const FString& InPath)
			: Path(InPath)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			if (!IsValid(InRoot))
			{
				return;
			}
			if (UDreamWidget* Found = InRoot->FindChildByDisplayName(Path, false))
			{
				OutWidgets.Add(Found);
			}
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("By::Path(\"%s\")"), *Path);
		}

	private:
		FString Path;
	};

	class FDreamClassLocator : public IDreamElementLocator
	{
	public:
		explicit FDreamClassLocator(UClass* InClass)
			: WidgetClass(InClass)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			UClass* ResolvedClass = WidgetClass.Get();
			if (ResolvedClass == nullptr)
			{
				return;
			}
			ForEachDescendant(InRoot, [&OutWidgets, ResolvedClass](UDreamWidget* InCandidate)
			{
				if (InCandidate->IsA(ResolvedClass))
				{
					OutWidgets.Add(InCandidate);
				}
			});
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("By::Class(%s)"), *GetNameSafe(WidgetClass.Get()));
		}

	private:
		//weak, because a locator can outlive a Blueprint-generated class that was recompiled under it
		TWeakObjectPtr<UClass> WidgetClass;
	};

	class FDreamPredicateLocator : public IDreamElementLocator
	{
	public:
		FDreamPredicateLocator(TFunction<bool(UDreamWidget*)> InPredicate, const FString& InDescription)
			: Predicate(MoveTemp(InPredicate))
			, Description(InDescription)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			if (!Predicate)
			{
				return;
			}
			ForEachDescendant(InRoot, [this, &OutWidgets](UDreamWidget* InCandidate)
			{
				if (Predicate(InCandidate))
				{
					OutWidgets.Add(InCandidate);
				}
			});
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("By::Predicate(%s)"), *Description);
		}

	private:
		TFunction<bool(UDreamWidget*)> Predicate;
		FString Description;
	};

	class FDreamWidgetLocator : public IDreamElementLocator
	{
	public:
		explicit FDreamWidgetLocator(UDreamWidget* InWidget)
			: TargetWidget(InWidget)
		{
		}

		virtual void Locate(UDreamWidget* InRoot, TArray<UDreamWidget*>& OutWidgets) const override
		{
			// The root is not consulted: this locator is the answer already, and insisting the widget
			// still be under a particular root would make it lie about a widget that was reparented.
			if (UDreamWidget* Resolved = TargetWidget.Get(); IsValid(Resolved))
			{
				OutWidgets.Add(Resolved);
			}
		}

		virtual FString Describe() const override
		{
			return FString::Printf(TEXT("By::Widget(%s)"), *GetNameSafe(TargetWidget.Get()));
		}

	private:
		TWeakObjectPtr<UDreamWidget> TargetWidget;
	};
}

FDreamLocatorRef FDreamBy::Name(const FString& InDisplayName)
{
	return MakeShared<DreamDriverLocatorsLocal::FDreamNameLocator>(InDisplayName);
}

FDreamLocatorRef FDreamBy::Path(const FString& InPath)
{
	return MakeShared<DreamDriverLocatorsLocal::FDreamPathLocator>(InPath);
}

FDreamLocatorRef FDreamBy::Class(UClass* InClass)
{
	return MakeShared<DreamDriverLocatorsLocal::FDreamClassLocator>(InClass);
}

FDreamLocatorRef FDreamBy::Predicate(TFunction<bool(UDreamWidget*)> InPredicate, const FString& InDescription)
{
	return MakeShared<DreamDriverLocatorsLocal::FDreamPredicateLocator>(MoveTemp(InPredicate), InDescription);
}

FDreamLocatorRef FDreamBy::Widget(UDreamWidget* InWidget)
{
	return MakeShared<DreamDriverLocatorsLocal::FDreamWidgetLocator>(InWidget);
}
