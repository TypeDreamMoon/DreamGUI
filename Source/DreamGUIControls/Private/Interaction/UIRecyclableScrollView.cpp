// Copyright 2019-Present LexLiu. All Rights Reserved.

#include "Interaction/UIRecyclableScrollView.h"
#include "Core/DreamUserWidget.h"
#include "DreamGUI.h"
#include "DreamUIBPLibrary.h"
#include "Core/Components/DreamWidget.h"
#include "Misc/ScopeExit.h"


void UUIRecyclableScrollView::Awake()
{
    Super::Awake();
}

void UUIRecyclableScrollView::Start()
{
    Super::Start();
    InitializeOnDataSource();
}

void UUIRecyclableScrollView::Tick(float DeltaTime)
{
    // Refill a pool invalidated by a row callback even when the user never scrolls again. One
    // attempt per tick also bounds a source whose callbacks keep destroying every new row.
    if (bCellPoolNeedsRebuild || (!CacheCellList.IsEmpty() && !IsCellPassCurrent(CellPoolGeneration)))
    {
        if (bCellPoolNeedsRebuild)InitializeOnDataSource();
    }
    if (IsValid(this))Super::Tick(DeltaTime);
}

void UUIRecyclableScrollView::OnDestroy()
{
    ++CellPoolGeneration;
    bCellPoolNeedsRebuild = false;
    if (OnScrollEventDelegateHandle.IsValid())
    {
        this->GetOnValueChangedEvent().Remove(OnScrollEventDelegateHandle);
    }
    Super::OnDestroy();
}

void UUIRecyclableScrollView::OnDimensionsChanged(bool PivotChanged, bool WidthChanged, bool HeightChanged)
{
    Super::OnDimensionsChanged(PivotChanged, WidthChanged, HeightChanged);
    // A cell's size along the cross axis is this view's size divided by the row or column count, and
    // the position of every cell is derived from it -- so the whole layout was computed against
    // whatever size this view happened to have when the data source arrived. That is not the final
    // size when the data arrives before the layout does: the view is still at its authored default,
    // and the cells keep that width forever while the view goes on to fill its column. Recomputing
    // here is what the plain scroll view already does with its scroll range, one line above.
    if (WidthChanged || HeightChanged)
    {
        InitializeOnDataSource();
    }
}

#if WITH_EDITOR
void UUIRecyclableScrollView::PostEditChangeProperty(FPropertyChangedEvent& PropertyChangedEvent)
{
    Super::PostEditChangeProperty(PropertyChangedEvent);
    if (auto Property = PropertyChangedEvent.MemberProperty)
    {
        auto PropertyName = Property->GetFName();
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, Horizontal))
        {
            Vertical = !Horizontal;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, Vertical))
        {
            Horizontal = !Vertical;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, OnlyOneDirection))
        {
            OnlyOneDirection = true;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, bInfiniteLoop))
        {
            RestrictRectArea = !bInfiniteLoop;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, Rows))
        {
            if (Horizontal)
            {
                if (Rows != 1)
                {
                    bInfiniteLoop = false;
                }
            }
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, Columns))
        {
            if (Vertical)
            {
                if (Columns != 1)
                {
                    bInfiniteLoop = false;
                }
            }
        }
    }
}
bool UUIRecyclableScrollView::CanEditChange(const FProperty* InProperty)const
{
    if (Super::CanEditChange(InProperty))
    {
        auto PropertyName = InProperty->GetFName();
        if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, CellTemplate))
        {
            return CellTemplateType == EUIRecyclableScrollViewCellTemplateType::Actor;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, CellTemplateClass))
        {
            return CellTemplateType == EUIRecyclableScrollViewCellTemplateType::Prefab;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, OnlyOneDirection))
        {
            return false;
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, bInfiniteLoop))
        {
            if (Horizontal)
            {
                return Rows == 1;
            }
            if (Vertical)
            {
                return Columns == 1;
            }
        }
        else if (PropertyName == GET_MEMBER_NAME_CHECKED(UUIRecyclableScrollView, RestrictRectArea))
        {
            return !bInfiniteLoop;
        }
        return true;
    }
    return false;
}
#endif

void UUIRecyclableScrollView::GetUserFriendlyCacheCellList(TArray<FUIRecyclableScrollViewCellContainer>& OutResult)const
{
    OutResult.SetNumUninitialized(CacheCellList.Num());
    int IndexInSource = MinCellIndexInCacheCellList;
    for (int i = 0; i < CacheCellList.Num(); i++)
    {
        if (IndexInSource >= CacheCellList.Num())
        {
            IndexInSource -= CacheCellList.Num();
        }
        OutResult[i] = CacheCellList[IndexInSource];
        IndexInSource++;
    }
}

void UUIRecyclableScrollView::ClearAllCells()
{
    ++CellPoolGeneration;
    bCellPoolNeedsRebuild = false;
    // Forget the old pool before destroying it: OnDisable/OnDestroy can clear or rebuild this
    // view, and those callbacks must only ever operate on the new pool, not this snapshot.
    TArray<FUIRecyclableScrollViewCellContainer> RemovedCells = MoveTemp(CacheCellList);
    CacheCellList.Reset();
    DataItemCount = 0;
    MinCellIndexInCacheCellList = 0;
    MaxCellIndexInCacheCellList = 0;
    MinCellPosition = 0;
    MinCellDataIndex = 0;
    for (const FUIRecyclableScrollViewCellContainer& Item : RemovedCells)
    {
        if (IsValid(Item.Widget))
        {
            Item.Widget->DestroyWidget();
        }
    }
}

bool UUIRecyclableScrollView::IsCellPassCurrent(uint64 InGeneration)
{
    if (InGeneration != CellPoolGeneration || !IsValid(this) || !IsValid(DataSource)
        || !Content.IsValid() || !ContentParent.IsValid())return false;
    if (CacheCellList.ContainsByPredicate([](const FUIRecyclableScrollViewCellContainer& Cell)
        { return !IsValid(Cell.Widget) || !IsValid(Cell.CellComponent); }))
    {
        // A callback can destroy any row without calling a list API. A shorter ring cannot keep
        // its old data and line cursors: discard that layout and refill it through initialization.
        const uint64 ClearedGeneration = CellPoolGeneration + 1;
        ClearAllCells();
        // Destruction callbacks may already have rebuilt or explicitly cleared the list themselves.
        if (IsValid(this) && CellPoolGeneration == ClearedGeneration)bCellPoolNeedsRebuild = true;
        return false;
    }
    return true;
}

void UUIRecyclableScrollView::SetDataSource(TScriptInterface<IUIRecyclableScrollViewDataSource> InDataSource)
{
    auto InDataSourceObject = InDataSource.GetObject();
    if (!IsValid(InDataSourceObject))
    {
        DataSource = nullptr;
        // Detaching the source also detaches its cells and scroll listener. InitializeOnDataSource
        // cannot do that without a source, and leaving the old pool alive let the next scroll call
        // the data-source interface on nullptr.
        if (OnScrollEventDelegateHandle.IsValid())
        {
            this->GetOnValueChangedEvent().Remove(OnScrollEventDelegateHandle);
            OnScrollEventDelegateHandle.Reset();
        }
        ClearAllCells();
        return;
    }
    if (DataSource != InDataSourceObject)
    {
        DataSource = InDataSourceObject;
        InitializeOnDataSource();
    }
}
void UUIRecyclableScrollView::SetRows(int value)
{
    value = FMath::Max(1, value);
    if (Rows != value)
    {
        Rows = value;
        if (Horizontal)
        {
            InitializeOnDataSource();
        }
    }
}
void UUIRecyclableScrollView::SetInfiniteLoop(bool value)
{
    if ((Horizontal && Rows != 1) || (Vertical && Columns != 1))
    {
        UE_LOG(DreamGUI, Error, TEXT("[%s].%d InfiniteLoop only work when Rows and Columns equals 1"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
        return;
    }
    if (value != bInfiniteLoop)
    {
        bInfiniteLoop = value;
        RestrictRectArea = false;
    }
}
void UUIRecyclableScrollView::SetColumns(int value)
{
    value = FMath::Max(1, value);
    if (Columns != value)
    {
        Columns = value;
        if (Vertical)
        {
            InitializeOnDataSource();
        }
    }
}
void UUIRecyclableScrollView::SetPadding(const FMargin& value)
{
    if (Padding != value)
    {
        Padding = value;
        InitializeOnDataSource();
    }
}
void UUIRecyclableScrollView::SetSpace(const FVector2D& value)
{
    if (Space != value)
    {
        Space = value;
        InitializeOnDataSource();
    }
}

bool UUIRecyclableScrollView::GetCellItemByDataIndex(int Index, FUIRecyclableScrollViewCellContainer& OutResult)const
{
    auto MaxCellIndexInData = FMath::Min(Index + CacheCellList.Num() - 1, DataItemCount - 1);
    auto ValidMinCellDataIndex = GetValidCellDataIndex(MinCellDataIndex);
    if (ValidMinCellDataIndex == INDEX_NONE)
    {
        return false;//no data, so no cell holds it
    }
    if (Index < ValidMinCellDataIndex || Index > MaxCellIndexInData)
    {
        return false;
    }
    else
    {
        auto CellIndexOffset = Index - ValidMinCellDataIndex;
        if (CellIndexOffset >= CacheCellList.Num())
        {
            return false;
        }
        auto CellIndex = MinCellIndexInCacheCellList + CellIndexOffset;
        if (CellIndex >= CacheCellList.Num())
        {
            CellIndex -= CacheCellList.Num();
        }
        if (CacheCellList.IsValidIndex(CellIndex))
        {
            OutResult = CacheCellList[CellIndex];
            return IsValid(OutResult.Widget) && IsValid(OutResult.CellComponent);
        }
        else
        {
            UE_LOG(DreamGUI, Error, TEXT("[%s] Wrong cell index. Index:%d, CellIndexOffset:%d, CellIndex:%d"), ANSI_TO_TCHAR(__FUNCTION__), Index, CellIndexOffset, CellIndex);
            ensure(false);
            return false;
        }
    }
}

void UUIRecyclableScrollView::ScrollToByDataIndex(int InDataIndex, bool InEaseAnimation, float InAnimationDuration)
{
    if (Horizontal == Vertical)return;
    if (CacheCellList.Num() == 0)return;
    if (DataItemCount == 0)return;
    if (InDataIndex < 0 || InDataIndex >= DataItemCount)
    {
        UE_LOG(DreamGUI, Warning, TEXT("[%s] Invalid InDataIndex:%d in range [0, %d]"), ANSI_TO_TCHAR(__FUNCTION__), InDataIndex, DataItemCount);
        return;
    }

    auto ValidMinCellDataIndex = GetValidCellDataIndex(MinCellDataIndex);
    if (Horizontal)
    {
        float CellWidth = WorkingCellTemplateSize.X;
        float StartPos = MinCellPosition + CellWidth * 0.5f;//start cell center horizontal position
        float TargetContentPos = StartPos;
        if (InDataIndex == ValidMinCellDataIndex)
        {

        }
        else if (InDataIndex > ValidMinCellDataIndex)//data index bigger than current minimal cell
        {
            for (int StartIndex = ValidMinCellDataIndex + 1; StartIndex <= InDataIndex; StartIndex += Rows)
            {
                TargetContentPos += CellWidth + Space.X;
            }
        }
        else if (InDataIndex < ValidMinCellDataIndex)
        {
            for (int StartIndex = ValidMinCellDataIndex - 1; StartIndex >= InDataIndex; StartIndex -= Rows)
            {
                TargetContentPos -= CellWidth + Space.X;
            }
        }

        TargetContentPos = FMath::Clamp(-TargetContentPos, HorizontalRange.X, HorizontalRange.Y);
        if (InEaseAnimation)
        {
            // The view's own glide, not a tween of its own: the glide is what a drag, a wheel notch or a setter stops
            // when it takes the content over, and what IsScrolling counts. A tween started here and forgotten went on
            // writing the content after the player grabbed it, putting it back on its way the very next frame. With
            // no tween manager -- a world no game instance owns -- the glide lands at once, as the eased branch did.
            FVector2D Target = GetContentPosition();
            Target.X = TargetContentPos;
            GlideContentTo(Target, true, InAnimationDuration);
        }
        else
        {
            SetScrollValue(FVector2D(TargetContentPos, 0));
        }
    }
    else if (Vertical)
    {
        float CellHeight = WorkingCellTemplateSize.Y;
        float StartPos = MinCellPosition - CellHeight * 0.5f;//start cell center vertical position
        float TargetContentPos = StartPos;
        if (InDataIndex == ValidMinCellDataIndex)
        {

        }
        else if (InDataIndex > ValidMinCellDataIndex)
        {
            for (int StartIndex = ValidMinCellDataIndex + 1; StartIndex <= InDataIndex; StartIndex += Columns)
            {
                TargetContentPos -= CellHeight + Space.Y;
            }
        }
        else if (InDataIndex < ValidMinCellDataIndex)
        {
            for (int StartIndex = ValidMinCellDataIndex - 1; StartIndex >= InDataIndex; StartIndex -= Columns)
            {
                TargetContentPos += CellHeight + Space.Y;//Y: this is the vertical branch, as three lines up
            }
        }

        TargetContentPos = FMath::Clamp(-TargetContentPos, VerticalRange.X, VerticalRange.Y);
        if (InEaseAnimation)
        {
            // The horizontal branch's glide, on the other axis.
            FVector2D Target = GetContentPosition();
            Target.Y = TargetContentPos;
            GlideContentTo(Target, true, InAnimationDuration);
        }
        else
        {
            SetScrollValue(FVector2D(0, TargetContentPos));
        }
    }
}

void UUIRecyclableScrollView::SetCellTemplate(UDreamWidget* value)
{
    if (CellTemplate != value)
    {
        CellTemplate = value;
    }
}

void UUIRecyclableScrollView::SetCellTemplateClass(TSubclassOf<UDreamUserWidget> value)
{
    if (CellTemplateClass != value)
    {
        CellTemplateClass = value;
        //IsValid too: WorkingCellTemplate is a weak pointer and the type tag says only how it WAS
        //made, not that it is still there -- calling through it once it had gone was the crash
        if (WorkingCellTemplateType == EUIRecyclableScrollViewCellTemplateType::Prefab && WorkingCellTemplate.IsValid())//if WorkingCellTemplate is created by prefab, then we need to destroy it so a new one will be created from new prefab
        {
            WorkingCellTemplate->DestroyWidget();
            WorkingCellTemplate = nullptr;
        }
    }
}

void UUIRecyclableScrollView::InitializeOnDataSource()
{
    bCellPoolNeedsRebuild = false;
    const uint64 Generation = ++CellPoolGeneration;
    if (!IsValid(DataSource))return;
    if (!CheckParameters())return;
    if (!IsCellPassCurrent(Generation))return;
    if (Horizontal == Vertical)return;
    // Taken into the members only once the layout is known to go ahead (see the refusal below).
    const int32 NewDataItemCount = IUIRecyclableScrollViewDataSource::Execute_GetItemCount(DataSource);
    if (!IsCellPassCurrent(Generation))return;

    switch (CellTemplateType)
    {
    default:
    case EUIRecyclableScrollViewCellTemplateType::Actor:
    {
        if (!IsValid(CellTemplate))return;
        WorkingCellTemplate = CellTemplate;
        if (WorkingCellTemplate.Get()->GetComponentByInterface(UUIRecyclableScrollViewCell::StaticClass()) == nullptr)
        {
            UE_LOG(DreamGUI, Error, TEXT("[%s] CellTemplate's root actor must have a ActorComponent which implement UIRecyclableScrollViewCell interface!"), ANSI_TO_TCHAR(__FUNCTION__));
            return;
        }
        WorkingCellTemplateType = EUIRecyclableScrollViewCellTemplateType::Actor;
    }
        break;
    case EUIRecyclableScrollViewCellTemplateType::Prefab:
    {
        if (!IsValid(CellTemplateClass))return;
        if (WorkingCellTemplateType != EUIRecyclableScrollViewCellTemplateType::Prefab || !WorkingCellTemplate.IsValid())//WorkingCellTemplate is already created by prefab
        {
            auto CellTemplateInstance = CreateDreamWidget(this->GetWorld(), CellTemplateClass, Content.Get());
            if (!IsCellPassCurrent(Generation))
            {
                if (IsValid(CellTemplateInstance))CellTemplateInstance->DestroyWidget();
                return;
            }
            WorkingCellTemplate = CellTemplateInstance;
        }
        if (!WorkingCellTemplate.IsValid())
        {
            // Creation failed, so there is nothing to destroy -- destroying it THROUGH the pointer
            // that just tested invalid was the crash. Only the report is left to do.
            WorkingCellTemplate = nullptr;
            UE_LOG(DreamGUI, Error, TEXT("[%s] CellTemplateClass's root widget must be a UI actor!"), ANSI_TO_TCHAR(__FUNCTION__));
            return;
        }
        if (WorkingCellTemplate.Get()->GetComponentByInterface(UUIRecyclableScrollViewCell::StaticClass()) == nullptr)
        {
            WorkingCellTemplate->DestroyWidget();
            if (!IsCellPassCurrent(Generation))return;
            WorkingCellTemplate = nullptr;
            UE_LOG(DreamGUI, Error, TEXT("[%s] CellTemplateClass's root widget must have a ActorComponent which implement UIRecyclableScrollViewCell interface!"), ANSI_TO_TCHAR(__FUNCTION__));
            return;
        }
        WorkingCellTemplateType = EUIRecyclableScrollViewCellTemplateType::Prefab;
    }
        break;
    }
    const FVector2D NewCellTemplateSize(WorkingCellTemplate->GetWidth(), WorkingCellTemplate->GetHeight());
    // Each line has to move the layout on, or the count of lines that fill the view below never stops
    // growing: a template with no extent along the scroll axis and no space after it spun that loop for
    // good. A layout that cannot advance has nothing to show, so it is refused here, before anything of
    // the previous layout is taken down -- its item count and cell size among it, which the cells still
    // on screen are recycled against.
    const float LinePitch = Horizontal ? NewCellTemplateSize.X + Space.X : NewCellTemplateSize.Y + Space.Y;
    if (LinePitch <= KINDA_SMALL_NUMBER)
    {
        UE_LOG(DreamGUI, Error, TEXT("[%s].%d The cell template plus the space after it is %.3f along the scroll axis; a cell has to take up room for the list to lay any out."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, LinePitch);
        return;
    }
    DataItemCount = NewDataItemCount;
    WorkingCellTemplateSize = NewCellTemplateSize;

    if (OnScrollEventDelegateHandle.IsValid())
    {
        this->GetOnValueChangedEvent().Remove(OnScrollEventDelegateHandle);
    }

    int VisibleColumnOrRowCount = 0;
    int VisibleCellCount = 0;
    if (Horizontal)
    {
        RangeArea.X = ContentParent->GetLocalSpaceLeft();
        RangeArea.Y = ContentParent->GetLocalSpaceRight();
        float RangeSize = RangeArea.Y - RangeArea.X - (Padding.Left + Padding.Right);
        float AllVisibleCellWidth = 0;
        float CellWidth = WorkingCellTemplateSize.X;
        while (true)
        {
            VisibleColumnOrRowCount++;
            AllVisibleCellWidth += CellWidth;
            if (AllVisibleCellWidth >= RangeSize)
            {
                break;
            }
            AllVisibleCellWidth += Space.X;
        }
        VisibleColumnOrRowCount += 1;
        VisibleCellCount = VisibleColumnOrRowCount * Rows;
        VisibleCellCount = FMath::Min(VisibleCellCount, DataItemCount);
        int HorizontalCellCount = FMath::CeilToInt((float)DataItemCount / Rows);
        float ContentSize = HorizontalCellCount * CellWidth + (HorizontalCellCount - 1) * Space.X + Padding.Left + Padding.Right;
        Content->SetWidth(ContentSize);
    }
    else
    {
        RangeArea.X = ContentParent->GetLocalSpaceBottom();
        RangeArea.Y = ContentParent->GetLocalSpaceTop();
        float RangeSize = RangeArea.Y - RangeArea.X - (Padding.Bottom + Padding.Top);
        float AllVisibleCellHeight = 0;
        float CellHeight = WorkingCellTemplateSize.Y;
        while (true)
        {
            VisibleColumnOrRowCount++;
            AllVisibleCellHeight += CellHeight;
            if (AllVisibleCellHeight > RangeSize)
            {
                break;
            }
            AllVisibleCellHeight += Space.Y;
        }
        VisibleColumnOrRowCount += 1;
        VisibleCellCount = VisibleColumnOrRowCount * Columns;
        VisibleCellCount = FMath::Min(VisibleCellCount, DataItemCount);
        int VerticalCellCount = FMath::CeilToInt((float)DataItemCount / Columns);
        float ContentSize = VerticalCellCount * CellHeight + (VerticalCellCount - 1) * Space.Y + Padding.Bottom + Padding.Top;
        Content->SetHeight(ContentSize);
    }
    if (!IsCellPassCurrent(Generation) || !WorkingCellTemplate.IsValid())return;
    WorkingCellTemplate->SetHorizontalAndVerticalAnchorMinMax(FVector2D(0.0f, 1.0f), FVector2D(0.0f, 1.0f), true, true);

    if (!IsCellPassCurrent(Generation) || !WorkingCellTemplate.IsValid())return;
    const TWeakObjectPtr<UDreamWidget> MeasuringTemplate = WorkingCellTemplate;
    ON_SCOPE_EXIT
    {
        // An InitOnCreate/OnEnable callback can stop the pass before its normal deactivation.
        if (MeasuringTemplate.IsValid() && MeasuringTemplate->GetWidgetActive())
        {
            MeasuringTemplate->SetWidgetActive(false);
        }
    };
    WorkingCellTemplate->SetWidgetActive(true);
    if (!IsCellPassCurrent(Generation) || !WorkingCellTemplate.IsValid())return;
    float CellWidth;
    float CellHeight;
    if (Horizontal)
    {
        CellWidth = WorkingCellTemplateSize.X;
        CellHeight = (Content->GetHeight() 
            - (Padding.Top + Padding.Bottom)//padding
            - (Rows - 1) * Space.Y//space
            ) / Rows;
    }
    else
    {
        CellWidth = (Content->GetWidth()
            - (Padding.Left + Padding.Right)//padding
            - (Columns - 1) * Space.X//space
            ) / Columns;
        CellHeight = WorkingCellTemplateSize.Y;
    }

    //create more cells
    FDreamUIDuplicateDataContainer DuplicateData;
    if (CacheCellList.Num() < VisibleCellCount)
    {
        UDreamUIBPLibrary::PrepareDuplicateData(WorkingCellTemplate.Get(), DuplicateData);
    }
    while (CacheCellList.Num() < VisibleCellCount)
    {
        auto CopiedCell = UDreamUIBPLibrary::DuplicateWidgetWithPreparedData(this, DuplicateData, Content.Get());
        if (!IsCellPassCurrent(Generation))
        {
            if (IsValid(CopiedCell))CopiedCell->DestroyWidget();
            return;
        }
        if (!IsValid(CopiedCell))
        {
            //Going round again would spin: the loop is bounded by how many cells are in the list, and
            //nothing here adds one. Stop with a short pool rather than never returning.
            UE_LOG(DreamGUI, Error, TEXT("[%s].%d Failed to duplicate the cell template; the list stops at %d cells."), ANSI_TO_TCHAR(__FUNCTION__), __LINE__, CacheCellList.Num());
            break;
        }
        auto CellInterfaceComponent = CopiedCell->GetComponentByInterface(UUIRecyclableScrollViewCell::StaticClass());
        if (CellInterfaceComponent == nullptr)
        {
            //An authoring mistake -- a CellTemplate with no cell component on it -- and an assertion
            //is the wrong answer to one: check() takes the whole editor down in every configuration
            //but Shipping, on a mistake a details panel makes in one click. The same condition is
            //already an error log everywhere else this class checks it (see InitializeOnDataSource).
            UE_LOG(DreamGUI, Error, TEXT("[%s].%d CellTemplate's root widget must have a component which implements the UIRecyclableScrollViewCell interface!"), ANSI_TO_TCHAR(__FUNCTION__), __LINE__);
            CopiedCell->DestroyWidget();
            if (!IsCellPassCurrent(Generation))return;
            break;
        }
        FUIRecyclableScrollViewCellContainer CellContainer;
        CellContainer.Widget = CopiedCell;
        CellContainer.CellComponent = CellInterfaceComponent;
        CacheCellList.Add(CellContainer);
        IUIRecyclableScrollViewDataSource::Execute_InitOnCreate(DataSource, CellInterfaceComponent);
        if (!IsCellPassCurrent(Generation))return;
    }
    if (!WorkingCellTemplate.IsValid())return;
    WorkingCellTemplate->SetWidgetActive(false);
    if (!IsCellPassCurrent(Generation))return;
    //delete extra cells
    while (CacheCellList.Num() > VisibleCellCount)
    {
        int LastIndex = CacheCellList.Num() - 1;
        const auto Item = CacheCellList[LastIndex];
        CacheCellList.RemoveAt(LastIndex);
        if (IsValid(Item.Widget))Item.Widget->DestroyWidget();
        if (!IsCellPassCurrent(Generation))return;
    }
    // Every recycling cursor starts over with the layout below, which puts data index 0 in the first cell
    // at the content's start -- all of them, before anything reads them. The data index and the first
    // cell's position used to be reset only after the scroll that follows, and that scroll recycled against
    // them: with fewer cells than the last layout it indexed past the end of the cache, and with more it
    // moved a cell to where the old layout had been scrolled to, after which nothing recycled at all.
    MinCellIndexInCacheCellList = 0;
    // The first cell of the LAST line the cache holds. A source shorter than the view has fewer cells than
    // lines that fit, and the line count would point past them.
    const int CellsPerLine = Horizontal ? Rows : Columns;
    MaxCellIndexInCacheCellList = CacheCellList.Num() > 0 ? ((CacheCellList.Num() - 1) / CellsPerLine) * CellsPerLine : 0;
    MinCellDataIndex = 0;
    MinCellPosition = Horizontal ? Padding.Left : -Padding.Top;

    IUIRecyclableScrollViewDataSource::Execute_BeforeSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;
    //set cell position and size and data
    auto PosX = Padding.Left, PosY = -Padding.Top;
    int RowOrColumnIndex = 0;
    for (int i = 0; i < CacheCellList.Num(); i++)
    {
        const auto CellItem = CacheCellList[i];
        // Awake, whatever the last layout left it as: a cell recycled past the end of a list was put to
        // sleep, and this layout gives every cell an item.
        if (!IsValid(CellItem.Widget) || !IsValid(CellItem.CellComponent))return;
        CellItem.Widget->SetWidgetActive(true);
        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
        IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellItem.CellComponent, i);
        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
        if (Horizontal)
        {
            CellItem.Widget->SetHeight(CellHeight);
            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
            auto AnchoredPosition = FVector2D(
                PosX + CellItem.Widget->GetPivot().X * CellWidth
                , PosY - (1.0f - CellItem.Widget->GetPivot().Y) * CellHeight);
            CellItem.Widget->SetAnchoredPosition(AnchoredPosition);
            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
            RowOrColumnIndex++;
            if (RowOrColumnIndex >= Rows)
            {
                PosX += CellWidth + Space.X;
                PosY = -Padding.Top;
                RowOrColumnIndex = 0;
            }
            else
            {
                PosY -= CellHeight + Space.Y;
            }
        }
        else
        {
            CellItem.Widget->SetWidth(CellWidth);
            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
            auto AnchoredPosition = FVector2D(
                PosX + CellItem.Widget->GetPivot().X * CellWidth
                , PosY - (1.0f - CellItem.Widget->GetPivot().Y) * CellHeight);
            CellItem.Widget->SetAnchoredPosition(AnchoredPosition);
            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
            RowOrColumnIndex++;
            if (RowOrColumnIndex >= Columns)
            {
                PosY -= CellHeight + Space.Y;
                PosX = Padding.Left;
                RowOrColumnIndex = 0;
            }
            else
            {
                PosX += CellWidth + Space.X;
            }
        }
    }
    IUIRecyclableScrollViewDataSource::Execute_AfterSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;

    // The content goes to its START, where the layout above put data index 0: progress 0 on the scrolling
    // axis, horizontal included. In the offset model 1 is the far end on both axes (UUIScrollView's
    // ApplyContentPositionWithProgress), and a horizontal list used to be sent there -- it opened at its last
    // column, with its cells at the first, and never recycled its way back. Nothing is recycled on the way
    // (bResettingCells): the cells are already where this position wants them.
    auto PrevProgress = this->Progress;
    {
        TGuardValue<bool> ResetGuard(bResettingCells, true);
        if (Horizontal)
        {
            this->SetScrollProgress(FVector2D(0.0f, PrevProgress.Y));
        }
        else
        {
            this->SetScrollProgress(FVector2D(PrevProgress.X, 0.0f));
        }
    }

    if (!IsCellPassCurrent(Generation))return;
    PrevContentPosition = FVector2D(Content->GetRelativeLocation().Y, Content->GetRelativeLocation().Z);
    OnScrollEventDelegateHandle = this->GetOnValueChangedEvent().AddUObject(this, &UUIRecyclableScrollView::OnScrollCallback);
    //this->SetScrollProgress(PrevProgress);
}
void UUIRecyclableScrollView::OnScrollCallback(FVector2D value)
{
    if (!IsValid(DataSource))return;
    if (bResettingCells)return;
    if (bCellPoolNeedsRebuild)
    {
        InitializeOnDataSource();
        return;
    }
    if (Horizontal == Vertical)return;
    if (CacheCellList.Num() == 0)return;
    if (DataItemCount == 0)return;

    const uint64 Generation = ++CellPoolGeneration;
    if (!IsCellPassCurrent(Generation))
    {
        if (bCellPoolNeedsRebuild)InitializeOnDataSource();
        return;
    }
    IUIRecyclableScrollViewDataSource::Execute_BeforeSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;
    const auto ContentPosition = FVector2D(Content->GetRelativeLocation().Y, Content->GetRelativeLocation().Z);
    if (Horizontal)
    {
        auto CellWidth = WorkingCellTemplateSize.X;
        auto PointToScrollViewSpaceOffset = Content->GetRelativeLocation().Y;
        if (ContentPosition.X > PrevContentPosition.X)//scroll from left to right
        {
            while (MinCellDataIndex > 0 || (bInfiniteLoop && Rows == 1))
            {
                // The first item of the line this recycle fills. Each cell of the line is that plus its place in the
                // line: added onto the previous cell's index instead, a line of three or more took items C, C+1, C+3,
                // C+6 -- the wrong ones once a grid scrolled back.
                const int LineStartDataIndex = MinCellDataIndex - Rows;
                const auto RightTopCellItem = CacheCellList[MaxCellIndexInCacheCellList];
                auto CellLeftPointInScrollViewSpace = RightTopCellItem.Widget->GetLocalSpaceLeft() + RightTopCellItem.Widget->GetRelativeLocation().Y + PointToScrollViewSpaceOffset;
                if (CellLeftPointInScrollViewSpace > RangeArea.Y)//right item out of range
                {
                    for (int i = 0; i < Rows; i++)
                    {
                        const auto CellItem = CacheCellList[MaxCellIndexInCacheCellList + i];
                        auto Pos = CellItem.Widget->GetAnchoredPosition();
                        Pos.X = MinCellPosition - (CellWidth + Space.X);
                        Pos.X = Pos.X + CellItem.Widget->GetPivot().X * CellWidth;
                        CellItem.Widget->SetAnchoredPosition(Pos);
                        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        //data index
                        MinCellDataIndex--;
                        //set data
                        const int CellDataIndex = GetValidCellDataIndex(LineStartDataIndex + i);
                        if (CellDataIndex < DataItemCount)
                        {
                            CellItem.Widget->SetWidgetActive(true);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                            IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellItem.CellComponent, CellDataIndex);
                            if (!IsCellPassCurrent(Generation))return;
                        }
                        else
                        {
                            // Hidden, not shown: this cell has been recycled past the end of the data and
                            // holds whatever the last row it displayed left in it. The other three recycle
                            // loops all deactivate here; only this one activated, leaving stale rows on
                            // screen at the edge of a horizontally scrolled list.
                            CellItem.Widget->SetWidgetActive(false);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        }
                    }
                    //decrease index
                    DecreaseMinMaxCellIndexInCacheCellList(Rows);
                    //left cell position
                    MinCellPosition -= CellWidth + Space.X;
                }
                else
                {
                    break;
                }
            }
        }
        else if (ContentPosition.X < PrevContentPosition.X)//scroll from right to left
        {
            int RightCellIndexInData = GetValidCellDataIndex(MinCellDataIndex) + CacheCellList.Num() - 1;
            //Rows, not Columns: this is the horizontal branch, where a single ROW is what makes the
            //loop one-dimensional and wrappable -- the other three loops each test their own axis
            while (RightCellIndexInData + 1 < DataItemCount || (bInfiniteLoop && Rows == 1))//check if right cell reach end data
            {
                const auto LeftTopCellItem = CacheCellList[MinCellIndexInCacheCellList];
                auto CellRightPointInScrollViewSpace = LeftTopCellItem.Widget->GetLocalSpaceRight() + LeftTopCellItem.Widget->GetRelativeLocation().Y + PointToScrollViewSpaceOffset;
                if (CellRightPointInScrollViewSpace < RangeArea.X)//left item out of range
                {
                    for (int i = 0; i < Rows; i++)
                    {
                        const auto CellItem = CacheCellList[MinCellIndexInCacheCellList + i];
                        auto Pos = CellItem.Widget->GetAnchoredPosition();
                        Pos.X = MinCellPosition + (CellWidth + Space.X) * (CacheCellList.Num() / Rows);
                        Pos.X = Pos.X + CellItem.Widget->GetPivot().X * CellWidth;
                        CellItem.Widget->SetAnchoredPosition(Pos);
                        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        //data index
                        MinCellDataIndex++;
                        RightCellIndexInData = MinCellDataIndex + CacheCellList.Num() - 1;
                        RightCellIndexInData = GetValidCellDataIndex(RightCellIndexInData);
                        //set data
                        if (RightCellIndexInData < DataItemCount)
                        {
                            CellItem.Widget->SetWidgetActive(true);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                            IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellItem.CellComponent, RightCellIndexInData);
                            if (!IsCellPassCurrent(Generation))return;
                        }
                        else
                        {
                            CellItem.Widget->SetWidgetActive(false);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        }
                    }
                    //increase index
                    IncreaseMinMaxCellIndexInCacheCellList(Rows);
                    //left cell position
                    MinCellPosition += CellWidth + Space.X;
                }
                else
                {
                    break;
                }
            }
        }
    }
    else
    {
        auto CellHeight = WorkingCellTemplateSize.Y;
        auto PointToScrollViewSpaceOffset = Content->GetRelativeLocation().Z;
        if (ContentPosition.Y < PrevContentPosition.Y)//scroll from top to bottom
        {
            while (MinCellDataIndex > 0 || (bInfiniteLoop && Columns == 1))
            {
                // See the horizontal case: each cell of the line is the line's first item plus its place in it.
                const int LineStartDataIndex = MinCellDataIndex - Columns;
                const auto BottomLeftCellItem = CacheCellList[MaxCellIndexInCacheCellList];
                auto CellTopPointInScrollViewSpace = BottomLeftCellItem.Widget->GetLocalSpaceTop() + BottomLeftCellItem.Widget->GetRelativeLocation().Z + PointToScrollViewSpaceOffset;
                if (CellTopPointInScrollViewSpace < RangeArea.X)//bottom item out of range
                {
                    //move bottom to top
                    for (int i = 0; i < Columns; i++)
                    {
                        const auto CellItem = CacheCellList[MaxCellIndexInCacheCellList + i];
                        auto Pos = CellItem.Widget->GetAnchoredPosition();
                        Pos.Y = MinCellPosition + (CellHeight + Space.Y);
                        Pos.Y = Pos.Y - (1.0f - CellItem.Widget->GetPivot().Y) * CellHeight;
                        CellItem.Widget->SetAnchoredPosition(Pos);
                        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        //data index
                        MinCellDataIndex--;
                        //set data
                        const int CellDataIndex = GetValidCellDataIndex(LineStartDataIndex + i);
                        if (CellDataIndex < DataItemCount)
                        {
                            CellItem.Widget->SetWidgetActive(true);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                            IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellItem.CellComponent, CellDataIndex);
                            if (!IsCellPassCurrent(Generation))return;
                        }
                        else
                        {
                            CellItem.Widget->SetWidgetActive(false);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        }
                    }
                    //decrease index
                    DecreaseMinMaxCellIndexInCacheCellList(Columns);
                    //top cell position
                    MinCellPosition += CellHeight + Space.Y;
                }
                else//none out of range, no need recycle anything
                {
                    break;
                }
            }
        }
        else if (ContentPosition.Y > PrevContentPosition.Y)//scroll from bottom to top
        {
            int BottomCellIndexInData = GetValidCellDataIndex(MinCellDataIndex) + CacheCellList.Num() - 1;
            while (BottomCellIndexInData + 1 < DataItemCount || (bInfiniteLoop && Columns == 1))//check if bottom cell reach end data
            {
                const auto TopLeftCellItem = CacheCellList[MinCellIndexInCacheCellList];
                auto CellBottomPointInScrollViewSpace = TopLeftCellItem.Widget->GetLocalSpaceBottom() + TopLeftCellItem.Widget->GetRelativeLocation().Z + PointToScrollViewSpaceOffset;
                if (CellBottomPointInScrollViewSpace > RangeArea.Y)//top item out of range
                {
                    for (int i = 0; i < Columns; i++)
                    {
                        const auto CellItem = CacheCellList[MinCellIndexInCacheCellList + i];
                        auto Pos = CellItem.Widget->GetAnchoredPosition();
                        Pos.Y = MinCellPosition - (CellHeight + Space.Y) * (CacheCellList.Num() / Columns);
                        Pos.Y = Pos.Y - (1.0f - CellItem.Widget->GetPivot().Y) * CellHeight;
                        CellItem.Widget->SetAnchoredPosition(Pos);
                        if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        //data index
                        MinCellDataIndex++;
                        BottomCellIndexInData = MinCellDataIndex + CacheCellList.Num() - 1;
                        BottomCellIndexInData = GetValidCellDataIndex(BottomCellIndexInData);
                        //set data
                        if (BottomCellIndexInData < DataItemCount)
                        {
                            CellItem.Widget->SetWidgetActive(true);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                            IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellItem.CellComponent, BottomCellIndexInData);
                            if (!IsCellPassCurrent(Generation))return;
                        }
                        else
                        {
                            CellItem.Widget->SetWidgetActive(false);
                            if (!IsCellPassCurrent(Generation) || !IsValid(CellItem.Widget))return;
                        }
                    }
                    //increase index
                    IncreaseMinMaxCellIndexInCacheCellList(Columns);
                    //top cell position
                    MinCellPosition -= CellHeight + Space.Y;
                }
                else//none out of range, no need recycle anything
                {
                    break;
                }
            }
        }
    }
    IUIRecyclableScrollViewDataSource::Execute_AfterSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;
    PrevContentPosition = ContentPosition;
}

void UUIRecyclableScrollView::ApplyContentPositionWithProgress()
{
    Super::ApplyContentPositionWithProgress();
    OnScrollCallback(FVector2D::ZeroVector);
}

void UUIRecyclableScrollView::UpdateCellData()
{
    if (!IsValid(DataSource))return;
    if (bCellPoolNeedsRebuild)
    {
        InitializeOnDataSource();
        return;
    }

    const uint64 Generation = ++CellPoolGeneration;
    if (!IsCellPassCurrent(Generation))
    {
        if (bCellPoolNeedsRebuild)InitializeOnDataSource();
        return;
    }
    IUIRecyclableScrollViewDataSource::Execute_BeforeSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;
    auto CellDataIndex = GetValidCellDataIndex(MinCellDataIndex);
    FUIRecyclableScrollViewCellContainer CellContainer;
    //INDEX_NONE means the data source is empty; Before/AfterSetCell still bracket the (empty) pass
    for (int i = 0; CellDataIndex != INDEX_NONE && i < CacheCellList.Num(); i++)
    {
        if (!GetCellItemByDataIndex(CellDataIndex, CellContainer))break;
        IUIRecyclableScrollViewDataSource::Execute_SetCell(DataSource, CellContainer.CellComponent, CellDataIndex);
        if (!IsCellPassCurrent(Generation))return;
        CellDataIndex++;
        if (CellDataIndex >= DataItemCount)
        {
            break;
        }
    }
    IUIRecyclableScrollViewDataSource::Execute_AfterSetCell(DataSource);
    if (!IsCellPassCurrent(Generation))return;
}

bool UUIRecyclableScrollView::IsLooping()const
{
    // The same test every recycle loop makes for its own axis: one row across a horizontal list, one
    // column across a vertical one. bInfiniteLoop alone can outlive that -- SetRows and SetColumns leave it
    // set -- and a grid has no single next cell to wrap round to.
    return bInfiniteLoop && (Horizontal ? Rows : Columns) == 1;
}

// Infinite loop could use out-of-range index, so use this to get a valid index
int UUIRecyclableScrollView::GetValidCellDataIndex(int InMinCellDataIndex)const
{
    // An empty data source has no index to wrap into, and both loops below hang on it: adding or
    // subtracting zero never moves the value across the bound, so `while (Temp >= 0)` spins forever.
    // Two of the callers are reachable from Blueprint without ever passing a count check.
    if (DataItemCount <= 0)
    {
        return INDEX_NONE;
    }
    // Only a looping list wraps. A list that ENDS has cells past its end on its last, partly filled line,
    // and wrapping their indices round to the start filled them with the first items again; the recycle
    // loops' hiding branches, which compare against the count, could never run.
    if (!IsLooping())
    {
        return InMinCellDataIndex;
    }
    auto TempMinCellDataIndex = InMinCellDataIndex % DataItemCount;
    if (TempMinCellDataIndex < 0)
    {
        TempMinCellDataIndex += DataItemCount;//C++ modulo keeps the sign of the dividend
    }
    return TempMinCellDataIndex;
}

void UUIRecyclableScrollView::IncreaseMinMaxCellIndexInCacheCellList(int Count)
{
    MinCellIndexInCacheCellList += Count;
    MaxCellIndexInCacheCellList += Count;
    if (MinCellIndexInCacheCellList >= CacheCellList.Num())
    {
        MinCellIndexInCacheCellList = 0;
    }
    if (MaxCellIndexInCacheCellList >= CacheCellList.Num())
    {
        MaxCellIndexInCacheCellList = 0;
    }
}
void UUIRecyclableScrollView::DecreaseMinMaxCellIndexInCacheCellList(int Count)
{
    MinCellIndexInCacheCellList -= Count;
    MaxCellIndexInCacheCellList -= Count;
    if (MinCellIndexInCacheCellList < 0)
    {
        MinCellIndexInCacheCellList = CacheCellList.Num() - Count;
    }
    if (MaxCellIndexInCacheCellList < 0)
    {
        MaxCellIndexInCacheCellList = CacheCellList.Num() - Count;
    }
}


