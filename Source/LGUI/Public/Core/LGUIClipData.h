// Copyright 2019-Present LexLiu. All Rights Reserved.

#pragma once
#include "CoreMinimal.h"

class UUIItem;
class ULGUIDataAsTexture;

class FLGUIClipData
{
public:
	static int InheritClipDepth;//max intersection clip bounds count, this must be the same as LWidgetClipData.ush LWidget_Clip_Depth
	static int BlockSizeInBytes;
	static int SingleBlockSizeInBytes;
	
	FLGUIClipData(const TSharedPtr<FLGUIClipData>& InParent, ULGUIDataAsTexture* InDataTexture, UUIItem* InWidget);
	~FLGUIClipData();
	int GetBufferStartPos() const{return BufferStartPos;}
	UUIItem* GetWidget() const{return Widget.Get();}
	void UpdateData();
	void MarkNeedUpdateData(){bNeedUpdateData = true;}
	bool IsPointVisible(const FVector& Point)const;
private:
	bool bNeedUpdateData = true;
	int BufferStartPos;
	TWeakObjectPtr<UUIItem> Widget;
	TWeakObjectPtr<ULGUIDataAsTexture> DataTexture;
	TWeakPtr<FLGUIClipData> Parent;
};