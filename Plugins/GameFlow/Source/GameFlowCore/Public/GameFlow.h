// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "UObject/NoExportTypes.h"
#include "UObject/Interface.h"
#include "GameFlow.generated.h"

class UEdGraph;

DECLARE_LOG_CATEGORY_EXTERN(LogGameFlow, Log, All);

namespace LogGameFlowUtils
{
	extern int32 Depth;

	extern FString RepeatTab(int32 num);
}

//------------------------------------------------------
// UGameFlowContext
//------------------------------------------------------

UINTERFACE(MinimalAPI)
class UGameFlowContext : public UInterface
{
	GENERATED_BODY()
};

class GAMEFLOWCORE_API IGameFlowContext
{
	GENERATED_BODY()

public:

	/* Sets Value for Key in Context */
	UFUNCTION(BlueprintNativeEvent, Category = "Game Flow Context")
	void SetValue(const FString& key, UObject* value);

	virtual void SetValue_Implementation(const FString& key, UObject* value) {}

	/* Gets Value for Key from Context */
	UFUNCTION(BlueprintNativeEvent, Category = "Game Flow Context")
	UObject* GetValue(const FString& key) const;

	virtual UObject* GetValue_Implementation(const FString& key) const { return nullptr; }
};

//------------------------------------------------------
// UGameFlowContextAsset
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGFC_MapBased : public UObject, public IGameFlowContext
{
	GENERATED_BODY()

public:

	virtual void SetValue_Implementation(const FString& key, UObject* value) override { ContextValues.FindOrAdd(key) = value; }

	virtual UObject* GetValue_Implementation(const FString& key) const override { return ContextValues.Contains(key) ? ContextValues[key] : nullptr; }

protected:

	UPROPERTY()
	TMap<FString, UObject*> ContextValues;
};

//------------------------------------------------------
// UGameFlowTransitionKey
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlowTransitionKey : public UObject
{
	GENERATED_BODY()
};

//------------------------------------------------------
// UGFS_Base
//------------------------------------------------------

UCLASS(Abstract, Blueprintable, EditInlineNew)
class GAMEFLOWCORE_API UGFS_Base : public UObject
{
	GENERATED_BODY()

public:

	/* Executed when owning Game Flow/State is entered */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnEnter(UGameFlow* callingGameFlow);

	virtual void OnEnter_Implementation(UGameFlow* callingGameFlow) {}

	/* Executed when owning Game Flow/State is exited */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnExit(UGameFlow* callingGameFlow);

	virtual void OnExit_Implementation(UGameFlow* callingGameFlow) {}

	/* Executed when owning Game Flow World context is changed */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnWorldContextChanged(UGameFlow* callingGameFlow, bool isOwningObjectActive);

	virtual void OnWorldContextChanged_Implementation(UGameFlow* callingGameFlow, bool isOwningObjectActive) {}

	/* Generates description for step */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	FText GenerateDescription() const;

	virtual FText GenerateDescription_Implementation() const { return FText::FromString(GetClass()->GetName()); }
};

//------------------------------------------------------
// UGameFlowTransition
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlowTransition : public UObject
{
	GENERATED_BODY()

public:

	/* If set, transition will be triggered by its Transition Key */
	UPROPERTY(EditAnywhere, Category = "Game Flow Transition")
	TObjectPtr<UGameFlowTransitionKey> TransitionKey;
};

//------------------------------------------------------
// UGameFlowContext
//------------------------------------------------------

USTRUCT()
struct GAMEFLOWCORE_API FGameFlowTransitionCollection
{
	GENERATED_USTRUCT_BODY()

public:

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UGameFlowTransition>> Transitions;
};

//------------------------------------------------------
// UGameFlowState
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlowState : public UObject
{
	GENERATED_BODY()

public:

	UPROPERTY()
	FName StateTitle;

	/* Sub Flow */
	UPROPERTY(EditAnywhere, Category = "State")
	UGameFlow* SubFlow;

	/* If true, Sub Flow asset will be used as template to create its instance inside this State */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr", EditConditionHides))
	uint8 bInstancedSubFlow : 1;

	/* If true, Sub Flow asset will be reset when this State is entered */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr && !bInstancedSubFlow", EditConditionHides))
	uint8 bResetSubFlowOnEnterState : 1;

	/* If true, Sub Flow asset will be reset when this State is exited */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr && !bInstancedSubFlow", EditConditionHides))
	uint8 bResetSubFlowOnExitState : 1;

	UPROPERTY()
	TArray<FGuid> SubFlowActiveStates;

	/* Steps to execute when State is entered and exited, going from first to last when entering and vice versa on exiting */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditInline))
	TArray<TObjectPtr<UGFS_Base>> Steps;
};

//------------------------------------------------------
// UGameFlow
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlow : public UObject
{
	GENERATED_BODY()

public:

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	UEdGraph* EdGraph;

	UGameFlowState* AddState(const FGuid& guid, const FName stateTitle);

	void DestroyState(const FGuid& guid);

	void DestroyStateTransition(const FGuid& guid);

	bool IsStateActive(const FGuid& guid) const { return ActiveStates.Contains(guid); }

	void SetEntryState(const FGuid& guid) { EntryState = guid; }

	void InvalidateEntryState() { EntryState.Invalidate(); }

	const TMap<FGuid, TObjectPtr<UGameFlowState>>& GetStates() const { return States; }

	UGameFlowTransition* AddTransition(const FGuid& fromGuid, const FGuid& toGuid);

	void DestroyTransition(const FGuid& fromGuid, const FGuid& toGuid);

	const TMap<FGuid, FGameFlowTransitionCollection>& GetTransitionCollections() const { return TransitionCollections; }

#endif

	/* Finds States by title */
	UFUNCTION(BlueprintCallable, Category = "Game Flow")
	void FindStateByTitle(FName stateTitle, TArray<UGameFlowState*>& outStates) const;

	/* Enters Game Flow */
	UFUNCTION(BlueprintCallable, Category = "Game Flow")
	void EnterFlow(bool executeSteps = true) { EnterFlow(ActiveStates, executeSteps); }

	/* Exits Game Flow */
	UFUNCTION(BlueprintCallable, Category = "Game Flow")
	void ExitFlow(bool executeSteps = true) { ExitFlow(ActiveStates, executeSteps); }

	/* Makes transition by Transition Key */
	UFUNCTION(BlueprintCallable, Category = "Game Flow")
	bool MakeTransition(UGameFlowTransitionKey* transitionKey, bool executeSteps = true);

	/* Sets World Context for Game Flow */
	UFUNCTION(BlueprintCallable, Category = "Game Flow")
	void SetWorldContext(UObject* worldContextObject) { SetWorldPtr(ActiveStates, worldContextObject ? worldContextObject->GetWorld() : nullptr); }

	virtual UWorld* GetWorld() const override { return WorldPtr.Get(); }

protected:

	void EnterState(TArray<FGuid>& activeStates, const FGuid& guid, bool executeSteps);

	void ExitState(TArray<FGuid>& activeStates, const FGuid& guid, bool executeSteps);

	void EnterFlow(TArray<FGuid>& activeStates, bool executeSteps);

	void ExitFlow(TArray<FGuid>& activeStates, bool executeSteps);

	void SetWorldPtr(TArray<FGuid>& activeStates, UWorld* world);

protected:

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> WorldPtr;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UGameFlowState>> States;

	UPROPERTY()
	TMap<FGuid, FGameFlowTransitionCollection> TransitionCollections;

	UPROPERTY()
	TArray<FGuid> ActiveStates;

	UPROPERTY()
	FGuid EntryState;

	/* Steps to execute when Game Flow is entered and exited, going from first to last when entering and vice versa on exiting */
	UPROPERTY(EditAnywhere, Category = "Game Flow", meta = (EditInline))
	TArray<TObjectPtr<UGFS_Base>> Steps;
};