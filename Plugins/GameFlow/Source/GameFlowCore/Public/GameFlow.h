// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "UObject/NoExportTypes.h"
#include "UObject/Interface.h"
#include "GameFlow.generated.h"

class UEdGraph;

DECLARE_LOG_CATEGORY_EXTERN(LogGameFlow, Log, All);
DECLARE_LOG_CATEGORY_EXTERN(LogGameFlowOperations, Log, All);

namespace LogGameFlowUtils
{
	extern int32 Depth;

	extern FString RepeatTab(int32 num);
}

//------------------------------------------------------
// EGFSStatus
//------------------------------------------------------

UENUM(BlueprintType)
enum class EGFSStatus : uint8
{
	Unset = 0,
	Started,
	InProgress,
	Finished,

	Failed
};

//------------------------------------------------------
// FOperationInfo
//------------------------------------------------------

typedef uint64 OperationId;

UENUM()
enum class EOperationType : uint8
{
	Unset,

	EnterState,
	EnterState_Set,
	EnterState_Steps,
	EnterState_SubFlow,

	AutoTransition,

	ExitState,
	ExitState_SubFlow,
	ExitState_Steps,
	ExitState_Set,

	StepsCatcher,

	EnterTransition,
	ExitTransition,
};

struct FOperationInfo
{
	FOperationInfo(const EOperationType operationType, FGuid& activeState, TWeakObjectPtr<UGameFlow> flow, const FGuid state, const OperationId& nextOperationId, const bool executeSteps, const bool resetSharedSubFlows)
		: OperationType(operationType), ActiveState(activeState), Flow(flow), State(state), NextOperationId(nextOperationId), ExecuteSteps(executeSteps), ResetSharedSubFlows(resetSharedSubFlows)
	{}

	const EOperationType OperationType;
	FGuid& ActiveState;
	TWeakObjectPtr<UGameFlow> Flow;
	const FGuid State;
	const OperationId NextOperationId;
	const uint8 ExecuteSteps : 1;
	const uint8 ResetSharedSubFlows : 1;

	TSet<int32> StepIndices;

	void ReportStepStatus(const UGFS_Base* step, const EGFSStatus status);
};

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

	/* Sets Value for Key */
	UFUNCTION(BlueprintNativeEvent, Category = "Flow Context")
	void SetValue(const FString& key, UObject* value);

	virtual void SetValue_Implementation(const FString& key, UObject* value) {}

	/* Gets Value by Key */
	UFUNCTION(BlueprintNativeEvent, Category = "Flow Context")
	UObject* GetValue(const FString& key) const;

	virtual UObject* GetValue_Implementation(const FString& key) const { return nullptr; }
};

//------------------------------------------------------
// UGFC_MapBased
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

	/* Executed when owning State is entered */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnEnter();

	virtual void OnEnter_Implementation() { OnComplete(EGFSStatus::Finished); }

	/* Executed when owning State is exited */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnExit();

	virtual void OnExit_Implementation() { OnComplete(EGFSStatus::Finished); }

	/* Executed when owning Flow World Context is changed */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnWorldContextChanged(const bool isOwningStateActive);

	virtual void OnWorldContextChanged_Implementation(const bool isOwningStateActive) {}

	/* Generates description for Step */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	FText GenerateDescription() const;

	virtual FText GenerateDescription_Implementation() const { return FText::FromString(GetClass()->GetName()); }

	/* Gets owning State for Step */
	UFUNCTION(BlueprintCallable, Category = "Step Base")
	UGameFlowState* GetOwningState() const { return GetTypedOuter<UGameFlowState>(); }

	/* Notifies owning State when Step enter/exit execution is complete */
	UFUNCTION(BlueprintCallable, Category = "Step Base")
	void OnComplete(const EGFSStatus status) const;

	OperationId StepsCatcherOperationId;
};

//------------------------------------------------------
// UGameFlowTransition
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlowTransition : public UObject
{
	GENERATED_BODY()

public:

	/* Transition Key */
	UPROPERTY(EditAnywhere, Category = "Transition")
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
	GENERATED_UCLASS_BODY()

public:

	UFUNCTION(BlueprintCallable, Category = "State")
	UGameFlow* GetOwningFlow() const { return GetTypedOuter<UGameFlow>(); }

public:

	UPROPERTY()
	FGuid StateGuid;

	UPROPERTY()
	FName StateTitle;

	/* Sub Flow */
	UPROPERTY(EditAnywhere, Category = "State")
	UGameFlow* SubFlow;

	/* If true, Sub Flow will be used as template to create instance inside this State */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr", EditConditionHides))
	uint8 bInstancedSubFlow : 1;

	/* If true, Sub Flow will be reset when this State is entered */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr && !bInstancedSubFlow", EditConditionHides))
	uint8 bResetSubFlowOnEnterState : 1;

	/* If true, Sub Flow will be reset when this State is exited */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow != nullptr && !bInstancedSubFlow", EditConditionHides))
	uint8 bResetSubFlowOnExitState : 1;

	UPROPERTY()
	FGuid SubFlowActiveState;

	/* Steps to execute when State is entered/exited, going from first to last when entering and vice versa when exiting */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditInline))
	TArray<TObjectPtr<UGFS_Base>> Steps;

	/* Transition Key to apply when all Steps of this State will finish Enter execution */
	UPROPERTY(EditAnywhere, Category = "State", meta = (EditCondition = "SubFlow == nullptr", EditConditionHides))
	TObjectPtr<UGameFlowTransitionKey> TransitionKey;
};

//------------------------------------------------------
// UGameFlow
//------------------------------------------------------

UCLASS(BlueprintType)
class GAMEFLOWCORE_API UGameFlow : public UObject
{
	GENERATED_UCLASS_BODY()

public:

	static void ExecuteOperation(const OperationId& operationId);

	static void OnEnterState(const FOperationInfo& operationInfo);

	static void OnEnterState_Set(const FOperationInfo& operationInfo);

	static void OnEnterState_Steps(const FOperationInfo& operationInfo);

	static void OnEnterState_SubFlow(const FOperationInfo& operationInfo);

	static void OnExitState(const FOperationInfo& operationInfo);

	static void OnExitState_SubFlow(const FOperationInfo& operationInfo);

	static void OnExitState_Steps(const FOperationInfo& operationInfo);

	static void OnExitState_Set(const FOperationInfo& operationInfo);

	static void OnStepsCatcher(const FOperationInfo& operationInfo) { ExecuteOperation(operationInfo.NextOperationId); }

	static void OnAutoTransition(const FOperationInfo& operationInfo);

	static void OnEnterTransition(const FOperationInfo& operationInfo);

	static void OnExitTransition(const FOperationInfo& operationInfo);

#if WITH_EDITORONLY_DATA

	UPROPERTY()
	UEdGraph* EdGraph;

	UGameFlowState* AddState(const FGuid& stateToAdd, const FName stateTitle);

	void DestroyState(const FGuid& stateToDestroy);

	void DestroyStateTransition(const FGuid& stateToDestroy);

	bool IsStateActive(const FGuid& state) const { return state == ActiveState; }

	void SetEntryState(const FGuid& state) { EntryState = state; }

	const TMap<FGuid, TObjectPtr<UGameFlowState>>& GetStates() const { return States; }

	UGameFlowTransition* AddTransition(const FGuid& fromState, const FGuid& toState);

	void DestroyTransition(const FGuid& fromState, const FGuid& toState);

	const TMap<FGuid, FGameFlowTransitionCollection>& GetTransitionCollections() const { return TransitionCollections; }

#endif

	const UGameFlowState* GetStateObject(const FGuid& state) const { return States.Contains(state) ? States[state] : nullptr; }

	/* Finds States by title */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void FindStateByTitle(FName stateTitle, TArray<UGameFlowState*>& outStateObjects) const;

	/* Returns true if Flow is transitioning */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool IsTransitioning() const { return bIsTransitioning; }

	/* Enters Flow */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void EnterFlow(const bool executeSteps);

	/* Exits Flow */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void ExitFlow(const bool executeSteps, const bool resetSharedSubFlows);

	/* Makes transition by Transition Key */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void MakeTransition(UGameFlowTransitionKey* transitionKey, const bool executeSteps);

	/* Sets World Context for Flow */
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void SetWorldContext(UObject* worldContextObject, const bool force) { SetWorldPtr(ActiveState, worldContextObject ? worldContextObject->GetWorld() : nullptr, force); }

	virtual UWorld* GetWorld() const override { return WorldPtr.Get(); }

protected:
	
	void EnterFlow(FGuid& activeState, const OperationId& nextOperationId, const bool executeSteps);

	void ExitFlow(FGuid& activeState, const OperationId& nextOperationId, const bool executeSteps, const bool resetSharedSubFlows);

	OperationId CreateTransitionOperation(UGameFlowTransitionKey* transitionKey, const OperationId& nextOperationId, const bool executeSteps, const bool resetSharedSubFlows);

	void SetWorldPtr(FGuid& activeState, UWorld* world, const bool force);

protected:

	UPROPERTY(Transient)
	TWeakObjectPtr<UWorld> WorldPtr;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UGameFlowState>> States;

	UPROPERTY()
	TMap<FGuid, FGameFlowTransitionCollection> TransitionCollections;

	/* Reset Flow with this property */
	UPROPERTY(EditAnywhere, Category = "Flow")
	FGuid ActiveState;

	UPROPERTY()
	FGuid EntryState;

	uint8 bIsTransitioning : 1;
};