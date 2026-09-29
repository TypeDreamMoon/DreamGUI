// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Driver/DreamDriverUntil.h"

namespace DreamDriverUntilLocal
{
	/**
	 * One shape for every element condition: ask the predicate, pass if it says yes, give up once the
	 * total wait has reached the timeout, otherwise keep waiting.
	 *
	 * The timespan the delegate is handed is the total waited so far, which is why the timeout can be
	 * enforced here rather than by whoever is doing the waiting -- and why this needs no state of its
	 * own and can be copied freely.
	 */
	FDriverWaitDelegate MakeConditionDelegate(TFunction<bool()> InCondition, FWaitTimeout InTimeout)
	{
		const FTimespan TimeoutSpan = InTimeout.Timespan;
		return FDriverWaitDelegate::CreateLambda(
			[Condition = MoveTemp(InCondition), TimeoutSpan](const FTimespan& InTotalWaitTime) -> FDriverWaitResponse
			{
				if (Condition && Condition())
				{
					return FDriverWaitResponse::Passed();
				}
				if (InTotalWaitTime >= TimeoutSpan)
				{
					return FDriverWaitResponse::Failed();
				}
				return FDriverWaitResponse::Wait();
			});
	}
}

FDriverWaitDelegate FDreamUntil::ElementExists(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->Exists(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsVisible(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->IsVisible(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsGone(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return !InElement->Exists(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsInteractable(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->IsInteractable(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsHovered(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->IsHovered(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsPressed(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->IsPressed(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::ElementIsSelected(const FDreamElementRef& InElement, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(
		[InElement]() { return InElement->IsSelected(); }, InTimeout);
}

FDriverWaitDelegate FDreamUntil::Condition(TFunction<bool()> InCondition, FWaitTimeout InTimeout)
{
	return DreamDriverUntilLocal::MakeConditionDelegate(MoveTemp(InCondition), InTimeout);
}

FDriverWaitDelegate FDreamUntil::Lambda(TFunction<FDriverWaitResponse(const FTimespan&)> InLambda)
{
	return FDriverWaitDelegate::CreateLambda(
		[Answer = MoveTemp(InLambda)](const FTimespan& InTotalWaitTime) -> FDriverWaitResponse
		{
			if (!Answer)
			{
				return FDriverWaitResponse::Failed();
			}
			return Answer(InTotalWaitTime);
		});
}
