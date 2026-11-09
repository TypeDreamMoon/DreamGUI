// Copyright 2026-Present TypeDreamMoon. All Rights Reserved.

#include "Import/DreamPsdDuiModel.h"

void FDreamPsdDuiNode::Set(const FString& InName, const FString& InValue, bool bInSeedOnly)
{
	ensureMsgf(bInSeedOnly || DreamPsdDui::IsOwnedProperty(Type, InName),
		TEXT("The importer writes %s on a %s, which the ownership table does not list: a reimport would never update it."), *InName, *Type);
	for (FDreamPsdDuiProperty& P : Properties)
	{
		if (P.Name == InName)
		{
			P.Value = InValue;
			P.bSeedOnly = bInSeedOnly;
			return;
		}
	}
	Properties.Add({ InName, InValue, bInSeedOnly });
}

const FDreamPsdDuiProperty* FDreamPsdDuiNode::Find(const FString& InName) const
{
	return Properties.FindByPredicate([&InName](const FDreamPsdDuiProperty& P) { return P.Name == InName; });
}

FDreamPsdDuiComponent& FDreamPsdDuiNode::AddComponent(const FString& InClassName)
{
	ensureMsgf(DreamPsdDui::IsOwnedComponent(InClassName), TEXT("The importer adds + %s, which the ownership table does not list."), *InClassName);
	for (FDreamPsdDuiComponent& C : Components)
	{
		if (C.ClassName == InClassName)
		{
			return C;
		}
	}
	FDreamPsdDuiComponent& C = Components.AddDefaulted_GetRef();
	C.ClassName = InClassName;
	return C;
}

const FDreamPsdDuiComponent* FDreamPsdDuiNode::FindComponent(const FString& InClassName) const
{
	return Components.FindByPredicate([&InClassName](const FDreamPsdDuiComponent& C) { return C.ClassName == InClassName; });
}

void FDreamPsdDuiNode::ForEach(TFunctionRef<void(const FDreamPsdDuiNode&, const FString&)> InVisit, const FString& InParentId) const
{
	InVisit(*this, InParentId);
	for (const FDreamPsdDuiNode& Child : Children)
	{
		Child.ForEach(InVisit, Id);
	}
}

FString DreamPsdDui::Number(double InValue)
{
	if (!FMath::IsFinite(InValue))
	{
		return TEXT("0");
	}
	const double Rounded = FMath::RoundToDouble(InValue * 1000.0) / 1000.0;
	FString Text = FString::Printf(TEXT("%.3f"), Rounded);
	while (Text.Contains(TEXT(".")) && (Text.EndsWith(TEXT("0")) || Text.EndsWith(TEXT("."))))
	{
		const bool bDot = Text.EndsWith(TEXT("."));
		Text.LeftChopInline(1);
		if (bDot)
		{
			break;
		}
	}
	return Text == TEXT("-0") ? FString(TEXT("0")) : Text;
}

FString DreamPsdDui::Vec2(double InX, double InY)
{
	return FString::Printf(TEXT("(%s, %s)"), *Number(InX), *Number(InY));
}

FString DreamPsdDui::Vec4(double InX, double InY, double InZ, double InW)
{
	return FString::Printf(TEXT("(%s, %s, %s, %s)"), *Number(InX), *Number(InY), *Number(InZ), *Number(InW));
}

FString DreamPsdDui::Margin(double InLeft, double InTop, double InRight, double InBottom)
{
	return Vec4(InLeft, InTop, InRight, InBottom);
}

FString DreamPsdDui::Color(const FColor& InColor)
{
	return FString::Printf(TEXT("#%02X%02X%02X%02X"), InColor.R, InColor.G, InColor.B, InColor.A);
}

FString DreamPsdDui::Bool(bool bInValue)
{
	return bInValue ? TEXT("true") : TEXT("false");
}

FString DreamPsdDui::String(const FString& InText)
{
	FString Out;
	Out.Reserve(InText.Len() + 2);
	Out.AppendChar(TEXT('"'));
	for (TCHAR C : InText)
	{
		switch (C)
		{
		case TEXT('"'): Out += TEXT("\\\""); break;
		case TEXT('\\'): Out += TEXT("\\\\"); break;
		case TEXT('\n'): Out += TEXT("\\n"); break;
		case TEXT('\r'): Out += TEXT("\\n"); break;
		case TEXT('\t'): Out += TEXT("\\t"); break;
		default: Out.AppendChar(C); break;
		}
	}
	Out.AppendChar(TEXT('"'));
	return Out;
}

FString DreamPsdDui::Asset(const FString& InObjectPath)
{
	return String(InObjectPath);
}

bool DreamPsdDui::ParseGeneratedId(const FString& InId, int32& OutLayerId)
{
	const int32 At = InId.Find(TEXT("_psd"), ESearchCase::CaseSensitive, ESearchDir::FromEnd);
	if (At == INDEX_NONE)
	{
		return false;
	}
	const FString Digits = InId.Mid(At + 4);
	if (Digits.IsEmpty() || !Digits.IsNumeric() || Digits.Contains(TEXT("-")) || Digits.Contains(TEXT(".")))
	{
		return false;
	}
	OutLayerId = FCString::Atoi(*Digits);
	return true;
}

FString DreamPsdDui::MakeGeneratedId(const FString& InLayerName, int32 InLayerId)
{
	FString Base;
	bool bUnderscore = false;
	for (TCHAR C : InLayerName)
	{
		// Letters (any script), digits and '_' are an id's; everything else becomes one '_'.
		const bool bKeep = FChar::IsAlnum(C) || C == TEXT('_') || C > 127;
		if (bKeep && !FChar::IsWhitespace(C))
		{
			Base.AppendChar(C);
			bUnderscore = false;
		}
		else if (!bUnderscore && Base.Len() > 0)
		{
			Base.AppendChar(TEXT('_'));
			bUnderscore = true;
		}
	}
	while (Base.EndsWith(TEXT("_")))
	{
		Base.LeftChopInline(1);
	}
	// "_psd<digits>" inside the name itself would confuse the suffix.
	Base.ReplaceInline(TEXT("_psd"), TEXT("_Psd"), ESearchCase::CaseSensitive);
	if (Base.IsEmpty())
	{
		Base = TEXT("Layer");
	}
	if (FChar::IsDigit(Base[0]))
	{
		Base = TEXT("L") + Base;
	}
	if (Base.Len() > 48)
	{
		Base.LeftInline(48);
	}
	return FString::Printf(TEXT("%s_psd%d"), *Base, InLayerId);
}

FString DreamPsdDui::AssetSafe(const FString& InName, const FString& InFallback)
{
	FString Out;
	bool bUnderscore = false;
	for (TCHAR C : InName)
	{
		if ((C >= TEXT('a') && C <= TEXT('z')) || (C >= TEXT('A') && C <= TEXT('Z')) || (C >= TEXT('0') && C <= TEXT('9')))
		{
			Out.AppendChar(C);
			bUnderscore = false;
		}
		else if (!bUnderscore && Out.Len() > 0)
		{
			Out.AppendChar(TEXT('_'));
			bUnderscore = true;
		}
	}
	while (Out.EndsWith(TEXT("_")))
	{
		Out.LeftChopInline(1);
	}
	if (Out.Len() > 40)
	{
		Out.LeftInline(40);
	}
	return Out.IsEmpty() ? InFallback : Out;
}

namespace DreamPsdDuiLocal
{
	const TSet<FString>& WidgetProperties()
	{
		static const TSet<FString> Names = {
			TEXT("AnchorData.AnchorMin"), TEXT("AnchorData.AnchorMax"), TEXT("AnchorData.Pivot"), TEXT("AnchorData.AnchoredPosition"),
			TEXT("AnchorData.SizeDelta"), TEXT("RenderOpacity"), TEXT("Visibility"), TEXT("bClipToBelow"), TEXT("Clipping"),
			TEXT("ClippingMargin"), TEXT("ClippingCornerRadius.X"), TEXT("ClippingCornerRadius.Y"), TEXT("ClippingCornerRadius.Z"), TEXT("ClippingCornerRadius.W"), TEXT("ClipMask.Shape"), TEXT("ClipMask.Feather"),
			TEXT("ClipMask.Source"), TEXT("ClipMask.Texture"), TEXT("ClipMask.Channel"), TEXT("ClipMask.Fill"),
			TEXT("ClipMask.Threshold"), TEXT("ClipMask.Softness"), TEXT("ClipMask.bInvert"),
		};
		return Names;
	}

	const TSet<FString>& VisualProperties(const FString& InType)
	{
		static const TSet<FString> Image = {
			TEXT("BlendMode"), TEXT("Brush.ResourceObject"), TEXT("Brush.TintColor"), TEXT("Brush.DrawAs"), TEXT("Brush.Margin"),
			TEXT("Brush.ImageSize"),
		};
		static const TSet<FString> RectBlock = {
			TEXT("BlendMode"), TEXT("Color"), TEXT("CornerRadius.X"), TEXT("CornerRadius.Y"), TEXT("CornerRadius.Z"), TEXT("CornerRadius.W"), TEXT("CornerRadiusUnitMode"), TEXT("bEnableBody"), TEXT("BodyColor"),
			TEXT("BodyTextureMode"), TEXT("BodyTexture"), TEXT("BodyTextureScaleMode"), TEXT("bEnableBorder"), TEXT("BorderWidth"),
			TEXT("BorderColor"), TEXT("bSoftEdge"),
		};
		static const TSet<FString> Text = {
			TEXT("BlendMode"), TEXT("Text"), TEXT("Font"), TEXT("FontSize"), TEXT("Color"), TEXT("HAlign"), TEXT("VAlign"),
			TEXT("OverflowType"), TEXT("FontSpace"), TEXT("LineHeightPercentage"), TEXT("bUnderline"), TEXT("bStrikethrough"),
			TEXT("FontStyle"), TEXT("bRichText"), TEXT("WrapTextAt"),
		};
		static const TSet<FString> None;
		if (InType == TEXT("Image")) { return Image; }
		if (InType == TEXT("RectBlock")) { return RectBlock; }
		if (InType == TEXT("Text")) { return Text; }
		return None;
	}
}

bool DreamPsdDui::IsOwnedProperty(const FString& InType, const FString& InName)
{
	return DreamPsdDuiLocal::WidgetProperties().Contains(InName) || DreamPsdDuiLocal::VisualProperties(InType).Contains(InName);
}

bool DreamPsdDui::IsOwnedComponent(const FString& InClassName)
{
	return InClassName == TEXT("DreamLayerGroup") || InClassName == TEXT("UIButton") || InClassName == TEXT("UIToggle") || InClassName == TEXT("UISlider");
}

bool DreamPsdDui::IsOwnedComponentProperty(const FString& InClassName, const FString& InName)
{
	if (InClassName == TEXT("DreamLayerGroup"))
	{
		static const TSet<FString> Names = {
			TEXT("Isolation"), TEXT("GroupOpacity"), TEXT("GroupBlendMode"), TEXT("GroupMask.Shape"), TEXT("GroupMask.Feather"),
			TEXT("GroupMask.Source"), TEXT("GroupMask.Texture"), TEXT("GroupMask.Channel"), TEXT("GroupMask.Fill"), TEXT("GroupMask.bInvert"),
		};
		return Names.Contains(InName);
	}
	return false;
}

FString DreamPsdDui::PrintNode(const FDreamPsdDuiNode& InNode, int32 InDepth)
{
	const FString Indent = FString::ChrN(InDepth * 4, TEXT(' '));
	const FString Inner = FString::ChrN((InDepth + 1) * 4, TEXT(' '));
	FString Out = FString::Printf(TEXT("%s%s %s {\n"), *Indent, *InNode.Type, *InNode.Id);
	for (const FDreamPsdDuiProperty& P : InNode.Properties)
	{
		Out += FString::Printf(TEXT("%s%s = %s\n"), *Inner, *P.Name, *P.Value);
	}
	for (const FDreamPsdDuiComponent& C : InNode.Components)
	{
		if (C.Properties.Num() == 0)
		{
			Out += FString::Printf(TEXT("%s+ %s {}\n"), *Inner, *C.ClassName);
			continue;
		}
		Out += FString::Printf(TEXT("%s+ %s {\n"), *Inner, *C.ClassName);
		for (const FDreamPsdDuiProperty& P : C.Properties)
		{
			Out += FString::Printf(TEXT("%s    %s = %s\n"), *Inner, *P.Name, *P.Value);
		}
		Out += FString::Printf(TEXT("%s}\n"), *Inner);
	}
	for (const FDreamPsdDuiNode& Child : InNode.Children)
	{
		Out += PrintNode(Child, InDepth + 1);
	}
	Out += FString::Printf(TEXT("%s}\n"), *Indent);
	return Out;
}

FString DreamPsdDui::Print(const FDreamPsdDuiNode& InRoot, const FString& InClassPath, const FString& InSourceName)
{
	FString Out;
	Out += FString::Printf(TEXT("// Imported from %s by the DreamGUI layered-document importer.\n"), *InSourceName);
	Out += TEXT("// A node whose id ends in _psd<n> was made from layer <n>: importing the document again rewrites the lines\n");
	Out += TEXT("// the importer writes on it and keeps everything else -- the nodes, lines and components you add.\n");
	if (!InClassPath.IsEmpty())
	{
		Out += FString::Printf(TEXT("class %s\n"), *InClassPath);
	}
	Out += TEXT("\n");
	Out += PrintNode(InRoot, 0);
	return Out;
}
