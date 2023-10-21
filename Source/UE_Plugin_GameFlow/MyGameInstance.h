// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "Engine/GameInstance.h"
#include "GameFlow.h"
#include "MyGameInstance.generated.h"

DECLARE_LOG_CATEGORY_EXTERN(LogUE_Plugin_GameFlow, Log, All);

class UUserWidget;
class USaveGame;
class UInputMappingContext;

//------------------------------------------------------
// UGFS_ShowWidget
//------------------------------------------------------

UCLASS()
class UE_PLUGIN_GAMEFLOW_API UGFS_ShowWidget : public UGFS_Base
{
	GENERATED_UCLASS_BODY()

public:

	virtual void OnEnter_Implementation(UGameFlow* callingGameFlow) override;

	virtual void OnExit_Implementation(UGameFlow* callingGameFlow) override;

	virtual FText GenerateDescription_Implementation() const override;

protected:

	/* UserWidget Class to show when entering and hide when exiting owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Show Widget")
	TSoftClassPtr<UUserWidget> UserWidgetClass;

	UPROPERTY()
	UUserWidget* UserWidget;

	/* If true, Input Mode will be switched to UI Only when enetering and tthen back when exiting owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Show Widget")
	uint8 bSwitchInputModeToUIOnly : 1;

	/* If true, User Widget will be focused when entering and unfocused when exiting owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Show Widget")
	uint8 bFocus : 1;

	/* If true, Mouse cursor will be showed when entering and hidden when exiting owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Show Widget")
	uint8 bShowMouseCursor : 1;
};

//------------------------------------------------------
// UGFS_SaveGame_Load
//------------------------------------------------------

UCLASS()
class UE_PLUGIN_GAMEFLOW_API UGFS_SaveGame_Load : public UGFS_Base
{
	GENERATED_UCLASS_BODY()

public:

	virtual void OnEnter_Implementation(UGameFlow* callingGameFlow) override;

	virtual FText GenerateDescription_Implementation() const override;

protected:

	/* SaveGame Slot Name */
	UPROPERTY(EditDefaultsOnly, Category = "SaveGame Load")
	FString SlotName;

	/* SaveGame Class */
	UPROPERTY(EditDefaultsOnly, Category = "SaveGame Load")
	TSoftClassPtr<USaveGame> SaveGameClass;

	/* Context to store loaded SaveGame */
	UPROPERTY(EditDefaultsOnly, Category = "SaveGame Load")
	TScriptInterface<IGameFlowContext> Context;

	/* Transition Key for next State to go when SaveGame is loaded */
	UPROPERTY(EditDefaultsOnly, Category = "SaveGame Load")
	TObjectPtr<UGameFlowTransitionKey> TransitionKey;
};

//------------------------------------------------------
// UGFS_Level_Load
//------------------------------------------------------

UCLASS()
class UE_PLUGIN_GAMEFLOW_API UGFS_Level_Load : public UGFS_Base
{
	GENERATED_UCLASS_BODY()

public:

	virtual void OnEnter_Implementation(UGameFlow* callingGameFlow) override;

	virtual void OnWorldContextChanged_Implementation(UGameFlow* callingGameFlow, bool isOwningObjectActive) override;

	virtual FText GenerateDescription_Implementation() const override;

protected:

	/* Map to load when entering owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Level Load", meta = (AllowedClasses = "/Script/Engine.World"))
	FSoftObjectPath MapToLoad;

	/* Transition Key for next State to go when Map is loaded */
	UPROPERTY(EditDefaultsOnly, Category = "Level Load")
	TObjectPtr<UGameFlowTransitionKey> TransitionKey;
};

//------------------------------------------------------
// UGFS_InputMappingContext_Switch
//------------------------------------------------------

UCLASS()
class UE_PLUGIN_GAMEFLOW_API UGFS_InputMappingContext_Switch : public UGFS_Base
{
	GENERATED_UCLASS_BODY()

public:

	virtual void OnEnter_Implementation(UGameFlow* callingGameFlow) override;

	virtual void OnExit_Implementation(UGameFlow* callingGameFlow) override;

	virtual FText GenerateDescription_Implementation() const override;

protected:

	/* InputMappingContext to add when entering and remove when exiting owning Game Flow/State */
	UPROPERTY(EditDefaultsOnly, Category = "Input Mapping Context")
	TObjectPtr<UInputMappingContext> InputMappingContext;
};

//------------------------------------------------------
// UMyGameInstance
//------------------------------------------------------

UCLASS()
class UE_PLUGIN_GAMEFLOW_API UMyGameInstance : public UGameInstance
{
	GENERATED_BODY()

public:

	virtual void OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld) override;

	virtual void Shutdown() override;

protected:

	void OnWorldBeginPlay(UWorld* world);

protected:

	UPROPERTY(EditDefaultsOnly, Category = "My Game Instance")
		TObjectPtr<UGameFlow> MainGameFlow;

	UPROPERTY(EditDefaultsOnly, Category = "My Game Instance", meta = (AllowedClasses = "/Script/Engine.World"))
		FSoftObjectPath EntryMap;
};