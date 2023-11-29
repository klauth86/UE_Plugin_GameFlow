// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "UObject/NoExportTypes.h"
#include "UObject/Interface.h"
#include "GameFlow.generated.h"

class UEdGraph;

DECLARE_LOG_CATEGORY_EXTERN(LogGameFlow, Log, All);

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

	Failed,
	Cancelled
};

//------------------------------------------------------
// FOperationInfo
//------------------------------------------------------

typedef uint64 OperationId;

const OperationId OPERATION_ID_MAX = 1ull << 33;

const OperationId OPERATION_TYPE_MASK = 0xFFFFFFFF00000000;

namespace EOperationType
{
	const OperationId EnterState				= 1ull << 34;
	const OperationId EnterState_Set			= 1ull << 35;
	const OperationId EnterState_Set_Log		= 1ull << 36;
	const OperationId EnterState_Steps			= 1ull << 37;
	const OperationId EnterState_SubFlow_Set	= 1ull << 38;
	const OperationId EnterState_SubFlow_Set_Log= 1ull << 39;
	const OperationId EnterState_SubFlow		= 1ull << 40;

	const OperationId AutoTransition			= 1ull << 41;

	const OperationId ExitState					= 1ull << 42;
	const OperationId ExitState_SubFlow			= 1ull << 43;
	const OperationId ExitState_SubFlow_Set_Log	= 1ull << 44;
	const OperationId ExitState_SubFlow_Set		= 1ull << 45;
	const OperationId ExitState_Steps			= 1ull << 46;
	const OperationId ExitState_Set_Log			= 1ull << 47;
	const OperationId ExitState_Set				= 1ull << 48;

	const OperationId CatchingOperation			= 1ull << 49;

	const OperationId Reset						= 1ull << 51;
	const OperationId ResetSubFlows				= 1ull << 52;

	const OperationId Cancel_Steps				= 1ull << 53;
	const OperationId Cancel_SubFlow			= 1ull << 54;

	const OperationId TRANSACTION_BEGIN			= 1ull << 55;
	const OperationId TRANSACTION_END			= 1ull << 56;
}

struct FOperationInfo
{
	FOperationInfo(FGuid& activeState, TWeakObjectPtr<UGameFlow> flow, const FGuid state, const OperationId nextOperationId, const bool executeSteps, const bool resetSubFlow, UGameFlowTransitionKey* transitionKey)
		: ActiveState(activeState), Flow(flow), State(state), NextOperationId(nextOperationId), ExecuteSteps(executeSteps), ResetSubFlow(resetSubFlow), TransitionKey(transitionKey)
	{}

	FGuid& ActiveState;
	TWeakObjectPtr<UGameFlow> Flow;
	const FGuid State;
	OperationId NextOperationId;
	const uint8 ExecuteSteps : 1;
	const uint8 ResetSubFlow : 1;
	TWeakObjectPtr<UGameFlowTransitionKey> TransitionKey;

	TSet<int32> StepIndices;
	uint32 ActiveIndex = 0;

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

	/* Gets Value for Key */
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

	/* Executed when owning/parent Flow is reset */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnCancel();

	virtual void OnCancel_Implementation() { OnComplete(EGFSStatus::Cancelled); }

	/* Executed when owning Flow World Context is changed */
	UFUNCTION(BlueprintNativeEvent, BlueprintCosmetic, Category = "Step Base")
	void OnWorldContextChanged(const bool force);

	virtual void OnWorldContextChanged_Implementation(const bool force) {}

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
	
	OperationId ActiveOperationType;

	OperationId CatchingOperationId;
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

	uint8 bSubFlow_Set : 1;

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

	static void CancelOperation(const OperationId operationId, const OperationId nextOperationId);

	static void ExecuteOperation(const OperationId operationId);

	static void LogOperation(const OperationId operationId, const FOperationInfo& operationInfo);

	static void OnEnterState(const OperationId operationId);

	static void OnEnterState_Set(const OperationId operationId);

	static void OnEnterState_Set_Log(const OperationId operationId);

	static void OnEnterState_Steps(const OperationId operationId);

	static void OnEnterState_SubFlow_Set(const OperationId operationId);

	static void OnEnterState_SubFlow_Set_Log(const OperationId operationId);

	static void OnEnterState_SubFlow(const OperationId operationId);

	static void OnExitState(const OperationId operationId);

	static void OnExitState_SubFlow(const OperationId operationId);

	static void OnExitState_SubFlow_Set_Log(const OperationId operationId);

	static void OnExitState_SubFlow_Set(const OperationId operationId);

	static void OnExitState_Steps(const OperationId operationId);

	static void OnExitState_Set_Log(const OperationId operationId);

	static void OnExitState_Set(const OperationId operationId);

	static void OnCatchingOperation(const OperationId operationId);

	static void OnAutoTransition(const OperationId operationId);

	static void OnReset(const OperationId operationId);

	static void OnResetSubFlows(const OperationId operationId);

	static void OnTransactionBegin(const OperationId operationId);

	static void OnTransactionEnd(const OperationId operationId);

	static void OnCancel_State_Steps(const OperationId operationId, const OperationId nextOperationId);

	static void OnCancel_State_SubFlow(const OperationId operationId, const OperationId nextOperationId);

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

	/**
	* Finds States by title
	*
	* @param stateTitle				State Title to search for
	* @param outStateObjects		Collection with search results
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void FindStateByTitle(FName stateTitle, TArray<UGameFlowState*>& outStateObjects) const;

	/**
	* Returns true if Flow is transitioning
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool IsTransitioning() const;

	/**
	* Returns true if Flow can be entered
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool CanEnterFlow() const;

	/**
	* Enters Flow
	*
	* @param executeSteps			If true, this transition will execute steps
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void EnterFlow(const bool executeSteps);

	/**
	* Returns true if Flow can be exited
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	bool CanExitFlow() const;

	/**
	* Exits Flow
	*
	* @param executeSteps			If true, this transition will execute steps
	* @param resetSharedSubFlows	if true, this transition will reset Shared Flow if it is set up in active State
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void ExitFlow(const bool executeSteps, const bool resetSharedSubFlows);

	/**
	* Resets Flow
	*
	* @param resetAnySubFlow		If true, all Sub Flows that are set up on any State of this Flow will also be reset
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void ResetFlow(const bool resetAnySubFlow);

	/**
	* Makes transition by Transition Key
	*
	* @param transitionKey			Transition Key
	* @param executeSteps			If true, this transition will execute steps
	* @param executeAsQueued		if true, this transition will be started just after current if Flow is transitioning; regular call in other case
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void MakeTransition(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool isEnqueued);

	/**
	* Sets World Context for Flow
	*
	* @param worldContextObject		World Context to set
	* @param force					if true, World Context will be set even if Flow is transitioning
	*/
	UFUNCTION(BlueprintCallable, Category = "Flow")
	void SetWorldContext(UObject* worldContextObject, const bool force) { SetWorldPtr(ActiveState, worldContextObject ? worldContextObject->GetWorld() : nullptr, force); }

	virtual UWorld* GetWorld() const override { return WorldPtr.Get(); }

protected:

	OperationId EnterFlow_Internal(FGuid& activeState, const bool executeSteps, const OperationId nextOperationId);

	OperationId ExitFlow_Internal(FGuid& activeState, const bool executeSteps, const bool resetActiveSubFlow, const OperationId nextOperationId);

	OperationId ResetFlow_Internal(FGuid& activeState, const bool resetAnySubFlow, const OperationId nextOperationId);

	OperationId MakeTransition_Internal(UGameFlowTransitionKey* transitionKey, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow);

	void SetWorldPtr(FGuid& activeState, UWorld* world, const bool force);

	void AddEnqueuedOperation(const OperationId operationId) const;

protected:
	
	TWeakObjectPtr<UWorld> WorldPtr;

	UPROPERTY()
	TMap<FGuid, TObjectPtr<UGameFlowState>> States;

	UPROPERTY()
	TMap<FGuid, FGameFlowTransitionCollection> TransitionCollections;

	UPROPERTY()
	FGuid EntryState;

	FGuid ActiveState;

	OperationId ActiveTransactionId;
};