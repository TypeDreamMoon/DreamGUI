// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class ULexWidget;
class ULexUIDataAsTexture;

class FLexUIClipData
{
public:
	static int InheritClipDepth;//max intersection clip bounds count, this must be the same as LWidgetClipData.ush LWidget_Clip_Depth
	static int BlockSizeInBytes;
	static int SingleBlockSizeInBytes;
	
	FLexUIClipData(const TSharedPtr<FLexUIClipData>& InParent, ULexUIDataAsTexture* InDataTexture, ULexWidget* InWidget);
	~FLexUIClipData();
	int GetBufferStartPos() const{return BufferStartPos;}
	ULexWidget* GetWidget() const{return Widget.Get();}
	void UpdateData();
	void MarkNeedUpdateData(){bNeedUpdateData = true;}
	bool IsPointVisible(const FVector& Point)const;
private:
	static void Add2DTranslationToMatrix(FMatrix44d& Matrix, const FVector2d& Translation);
	bool IsPointVisible_CheckCornerRadius(const FVector2D& InLocalHitPoint, ULexWidget* InWidget)const;
	bool bNeedUpdateData = true;
	int BufferStartPos;
	TWeakObjectPtr<ULexWidget> Widget;
	TWeakObjectPtr<ULexUIDataAsTexture> DataTexture;
	TWeakPtr<FLexUIClipData> Parent;
};
