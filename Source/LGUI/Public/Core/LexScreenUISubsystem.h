// Copyright 2026-Present LexLiu. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "LexScreenUISubsystem.generated.h"

class AActor;
class ULexCanvas;
class ULexUIPrefab;
class ULexWidget;

/** UMG-style viewport page management backed by one LexUI screen-space root canvas. */
UCLASS()
class LGUI_API ULexScreenUISubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static ULexScreenUISubsystem* Get(UWorld* InWorld);

	UFUNCTION(BlueprintPure, meta = (WorldContext = "WorldContextObject", DisplayName = "Get LexUI Screen UI Subsystem"), Category = "LGUI|Screen")
	static ULexScreenUISubsystem* GetLexScreenUISubsystem(UObject* WorldContextObject);

	virtual bool DoesSupportWorldType(EWorldType::Type WorldType) const override;
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* GetOrCreateScreenRoot();

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexWidget* GetScreenRoot() const { return ScreenRoot; }

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexCanvas* GetScreenCanvas() const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* LoadPrefabToScreen(ULexUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void AddToViewport(ULexWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveFromViewport(ULexWidget* InRoot);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	bool IsInViewport(ULexWidget* InRoot) const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RegisterUI(FName InName, ULexWidget* InRoot, int32 InSortOrder = 0);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* ShowPrefab(FName InName, ULexUIPrefab* InPrefab, int32 InSortOrder = 0);

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	ULexWidget* GetUI(FName InName) const;

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	bool IsUIShowing(FName InName) const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void SetUIVisible(FName InName, bool bVisible);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveUI(FName InName);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void RemoveAllUI();

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	TArray<FName> GetAllUINames() const;

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	ULexWidget* PushPrefab(FName InName, ULexUIPrefab* InPrefab);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void PushUI(FName InName, ULexWidget* InRoot);

	UFUNCTION(BlueprintCallable, Category = "LGUI|Screen")
	void PopUI();

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	FName GetTopUI() const;

	UFUNCTION(BlueprintPure, Category = "LGUI|Screen")
	int32 GetStackDepth() const { return Stack.Num(); }

private:
	struct FEntry
	{
		TWeakObjectPtr<ULexWidget> Root;
		int32 SortOrder = 0;
	};

	UPROPERTY(Transient)
	TObjectPtr<ULexWidget> ScreenRoot;

	UPROPERTY(Transient)
	TObjectPtr<AActor> InteractionHost;

	UPROPERTY(Transient)
	TObjectPtr<AActor> CreatedEventSystemActor;

	TMap<FName, FEntry> Entries;
	TArray<FName> Stack;
	int32 AutoNameCounter = 0;
	bool bOwnsScreenRoot = false;

	static constexpr int32 StackBaseSortOrder = 1000;
	static constexpr int32 StackSortOrderStep = 10;

	bool IsUsablePage(const ULexWidget* InRoot) const;
	FName FindNameForWidget(const ULexWidget* InRoot) const;
	void ConfigurePage(ULexWidget* InRoot, int32 InSortOrder);
	void DestroyPage(ULexWidget* InRoot);
	void EnsureInteractionObjects(ULexCanvas* InRootCanvas);
};
