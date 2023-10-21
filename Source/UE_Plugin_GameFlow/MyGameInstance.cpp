// Fill out your copyright notice in the Description page of Project Settings.

#include "MyGameInstance.h"
#include "Engine/LocalPlayer.h"
#include "Blueprint/UserWidget.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/SaveGame.h"
#include "EnhancedInputSubsystems.h"
#include "InputMappingContext.h"

DEFINE_LOG_CATEGORY(LogUE_Plugin_GameFlow);

//------------------------------------------------------
// UGFS_ShowWidget
//------------------------------------------------------

UGFS_ShowWidget::UGFS_ShowWidget(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	UserWidgetClass = nullptr;
	UserWidget = nullptr;
	bSwitchInputModeToUIOnly = 1;
	bFocus = 1;
}

void UGFS_ShowWidget::OnEnter_Implementation(UGameFlow* callingGameFlow)
{
	if (UWorld* world = callingGameFlow->GetWorld())
	{
		FConstPlayerControllerIterator cpcIt = world->GetPlayerControllerIterator();
		if (cpcIt)
		{
			APlayerController* pc = cpcIt->Get();

			if (UUserWidget* userWidget = CreateWidget(pc, UserWidgetClass.LoadSynchronous()))
			{
				UserWidget = userWidget;
				userWidget->AddToPlayerScreen();

				if (bSwitchInputModeToUIOnly)
				{
					FInputModeUIOnly imUIOnly;
					if (bFocus && UserWidget->bIsFocusable)
					{
						imUIOnly.SetWidgetToFocus(UserWidget->TakeWidget());
					}
					pc->SetInputMode(imUIOnly);
				}

				if (bShowMouseCursor)
				{
					pc->SetShowMouseCursor(true);
				}
			}
			else
			{
				UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute Show Widget step when it has unset UserWidgetClass (%s)"), *callingGameFlow->GetName());
			}
		}
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute Show Widget step when calling Game Flow has no World (%s)"), *callingGameFlow->GetName());
	}
}

void UGFS_ShowWidget::OnExit_Implementation(UGameFlow* callingGameFlow)
{
	if (UWorld* world = callingGameFlow->GetWorld())
	{
		FConstPlayerControllerIterator cpcIt = world->GetPlayerControllerIterator();
		if (cpcIt)
		{
			APlayerController* pc = cpcIt->Get();

			if (UserWidget)
			{
				if (bShowMouseCursor)
				{
					pc->SetShowMouseCursor(false);
				}

				if (bSwitchInputModeToUIOnly)
				{
					FInputModeGameOnly imGameOnly;
					pc->SetInputMode(imGameOnly);
				}

				UserWidget->RemoveFromParent();
				UserWidget = nullptr;
			}
		}
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute Show Widget step when calling Game Flow has no World (%s)"), *callingGameFlow->GetName());
	}
}

FText UGFS_ShowWidget::GenerateDescription_Implementation() const
{
	return FText::FromString("Show Widget: " + (UserWidgetClass.IsNull() ? "None" : UserWidgetClass.GetAssetName()));
}

//------------------------------------------------------
// UGFS_SaveGame_Load
//------------------------------------------------------

UGFS_SaveGame_Load::UGFS_SaveGame_Load(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	SlotName = "";
}

void UGFS_SaveGame_Load::OnEnter_Implementation(UGameFlow* callingGameFlow)
{
	USaveGame* saveGame = nullptr;

	if (UGameplayStatics::DoesSaveGameExist(SlotName, 0))
	{
		saveGame = UGameplayStatics::LoadGameFromSlot(SlotName, 0);
	}
	else if (UClass* saveGameClass = SaveGameClass.LoadSynchronous())
	{
		saveGame = UGameplayStatics::CreateSaveGameObject(saveGameClass);
		UGameplayStatics::SaveGameToSlot(saveGame, SlotName, 0);
	}

	if (saveGame)
	{
		if (Context)
		{
			Context->Execute_SetValue(Context->_getUObject(), SlotName, saveGame);
		}
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute SaveGame Load step when it has unset SaveGameClass and there is no existing Slot named %s (%s)"), *SlotName, *callingGameFlow->GetName());
	}

	if (TransitionKey)
	{
		callingGameFlow->MakeTransition(TransitionKey);
	}
}

FText UGFS_SaveGame_Load::GenerateDescription_Implementation() const
{
	return FText::FromString("SaveGame Load: " + SlotName);
}

//------------------------------------------------------
// UGFS_Level_Load
//------------------------------------------------------

UGFS_Level_Load::UGFS_Level_Load(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	MapToLoad.Reset();
}

void UGFS_Level_Load::OnEnter_Implementation(UGameFlow* callingGameFlow)
{
	USaveGame* saveGame = nullptr;

	if (!MapToLoad.IsNull())
	{
		UGameplayStatics::OpenLevel(callingGameFlow, MapToLoad.GetAssetFName());
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute Level Load step when it has unset MapToLoad (%s)"), *callingGameFlow->GetName());
	}
}

void UGFS_Level_Load::OnWorldContextChanged_Implementation(UGameFlow* callingGameFlow, bool isOwningObjectActive)
{
	if (isOwningObjectActive)
	{
		UWorld* world = callingGameFlow->GetWorld();

		if (world && !MapToLoad.IsNull() && world->RemovePIEPrefix(world->GetPathName()) == MapToLoad.GetAssetPathString())
		{
			if (TransitionKey)
			{
				callingGameFlow->MakeTransition(TransitionKey);
			}
		}
	}
}

FText UGFS_Level_Load::GenerateDescription_Implementation() const
{
	return FText::FromString("Level Load: " + MapToLoad.GetAssetName());
}

//------------------------------------------------------
// UGFS_InputMappingContext_Switch
//------------------------------------------------------

UGFS_InputMappingContext_Switch::UGFS_InputMappingContext_Switch(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	InputMappingContext = nullptr;
}

void UGFS_InputMappingContext_Switch::OnEnter_Implementation(UGameFlow* callingGameFlow)
{
	if (InputMappingContext)
	{
		UWorld* world = callingGameFlow->GetWorld();
		if (world)
		{
			FConstPlayerControllerIterator cpcIt = world->GetPlayerControllerIterator();
			if (cpcIt)
			{
				APlayerController* pc = cpcIt->Get();

				if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(pc->GetLocalPlayer()))
				{
					Subsystem->AddMappingContext(InputMappingContext, 0);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute InputMappingContext Switch step when it has unset InputMappingContext (%s)"), *callingGameFlow->GetName());
	}
}

void UGFS_InputMappingContext_Switch::OnExit_Implementation(UGameFlow* callingGameFlow)
{
	if (InputMappingContext)
	{
		UWorld* world = callingGameFlow->GetWorld();
		if (world)
		{
			FConstPlayerControllerIterator cpcIt = world->GetPlayerControllerIterator();
			if (cpcIt)
			{
				APlayerController* pc = cpcIt->Get();

				if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(pc->GetLocalPlayer()))
				{
					Subsystem->RemoveMappingContext(InputMappingContext);
				}
			}
		}
	}
	else
	{
		UE_LOG(LogUE_Plugin_GameFlow, Warning, TEXT("Cant execute InputMappingContext Switch step when it has unset InputMappingContext (%s)"), *callingGameFlow->GetName());
	}
}

FText UGFS_InputMappingContext_Switch::GenerateDescription_Implementation() const
{
	return FText::FromString("InputMappingContext Switch: " + GetNameSafe(InputMappingContext.Get()));
}

//------------------------------------------------------
// UMyGameInstance
//------------------------------------------------------

void UMyGameInstance::OnWorldChanged(UWorld* OldWorld, UWorld* NewWorld)
{
	if (OldWorld)
	{
		MainGameFlow->SetWorldContext(nullptr);

		OldWorld->OnWorldBeginPlay.RemoveAll(this);
	}

	if (NewWorld)
	{
		NewWorld->OnWorldBeginPlay.AddUObject(this, &UMyGameInstance::OnWorldBeginPlay, NewWorld);
	}
}

void UMyGameInstance::Shutdown()
{
	if (MainGameFlow)
	{
		MainGameFlow->ExitFlow();
	}

	Super::Shutdown();
}

void UMyGameInstance::OnWorldBeginPlay(UWorld* world)
{
	if (MainGameFlow)
	{
		MainGameFlow->SetWorldContext(world);

		if (EntryMap.GetAssetPathString() == world->RemovePIEPrefix(world->GetPathName()))
		{
			MainGameFlow->EnterFlow();
		}
	}
}