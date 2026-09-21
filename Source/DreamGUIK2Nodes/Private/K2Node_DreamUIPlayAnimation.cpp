// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "K2Node_DreamUIPlayAnimation.h"
#include "Animation/DreamUIAnimationPlayCallbackProxy.h"

UK2Node_DreamUIPlayAnimation::UK2Node_DreamUIPlayAnimation(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UDreamUIAnimationPlayCallbackProxy, NewPlayAnimationProxyObject);
	ProxyFactoryClass = UDreamUIAnimationPlayCallbackProxy::StaticClass();
	ProxyClass = UDreamUIAnimationPlayCallbackProxy::StaticClass();
}

UK2Node_DreamUIPlayAnimationTimeRange::UK2Node_DreamUIPlayAnimationTimeRange(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	ProxyFactoryFunctionName = GET_FUNCTION_NAME_CHECKED(UDreamUIAnimationPlayCallbackProxy, NewPlayAnimationTimeRangeProxyObject);
	ProxyFactoryClass = UDreamUIAnimationPlayCallbackProxy::StaticClass();
	ProxyClass = UDreamUIAnimationPlayCallbackProxy::StaticClass();
}
