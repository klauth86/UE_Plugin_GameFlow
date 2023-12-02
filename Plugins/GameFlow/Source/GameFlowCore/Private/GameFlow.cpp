// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "GameFlow.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogGameFlow);

const uint32 AdditiveDepthStep = 1;

TMap<OperationId, OperationId> TransactionContext;

TMap<OperationId, FOperationInfo> OperationContext;

void RemoveOperations(const OperationId operationId, const OperationId transactionEndId)
{
	if (operationId != transactionEndId)
	{
		OperationId operationIdToRemove = operationId;
		do
		{
			TransactionContext.Remove(operationIdToRemove);
			operationIdToRemove = OperationContext.FindAndRemoveChecked(operationIdToRemove).NextOperationId;
		}
		while (operationIdToRemove != transactionEndId);
	}
}

void RemoveTransaction(const OperationId operationId)
{
	const OperationId transactionEndId = TransactionContext.FindAndRemoveChecked(operationId);

	RemoveOperations(operationId, transactionEndId);

	OperationContext.FindAndRemoveChecked(transactionEndId);
}

const OperationId GetActiveOperation(const OperationId operationId)
{
	OperationId activeOperationId = operationId;

	for (size_t i = 0; i < OperationContext[operationId].ActiveIndex; i++)
	{
		activeOperationId = OperationContext[activeOperationId].NextOperationId;
	}

	return activeOperationId;
}

FString RepeatTab(int32 num)
{
	FString result;
	for (int32 i = 0; i < num * 4; i++) result.AppendChar(*" ");
	return result;
}

OperationId GetOperationId()
{
	static OperationId counter = 0;
	
	counter = (counter + 1) % OPERATION_ID_MAX;

	if (counter == 0)
	{
		counter = (counter + 1) % OPERATION_ID_MAX;
	}

	return counter;
}

FString GetStepPhaseString(const OperationId operationType)
{
	FString result = "";

	if (operationType == EOperationType::EnterState_Steps)
	{
		result = "OnEnter ";
	}
	else if (operationType == EOperationType::ExitState_Steps)
	{
		result = "OnExit  ";
	}
	else if (operationType == EOperationType::Reset)
	{
		result = "OnCancel";
	}

	return result;
}

FString GetOperationTypeString(const OperationId operationType)
{
	FString result = "";

	if (operationType == EOperationType::EnterState) result = "EnterState";
	if (operationType == EOperationType::EnterState_Set) result = "EnterState_Set";
	if (operationType == EOperationType::EnterState_Steps) result = "EnterState_Steps";
	if (operationType == EOperationType::EnterState_SubFlow_Set) result = "EnterState_SubFlow_Set";
	if (operationType == EOperationType::EnterState_SubFlow) result = "EnterState_SubFlow";

	if (operationType == EOperationType::AutoTransition) result = "AutoTransition";

	if (operationType == EOperationType::ExitState) result = "ExitState";
	if (operationType == EOperationType::ExitState_SubFlow) result = "ExitState_SubFlow";
	if (operationType == EOperationType::ExitState_SubFlow_Set) result = "ExitState_SubFlow_Set";
	if (operationType == EOperationType::ExitState_Steps) result = "ExitState_Steps";
	if (operationType == EOperationType::ExitState_Set) result = "ExitState_Set";

	if (operationType == EOperationType::CatchingOperation) result = "CatchingOperation";

	if (operationType == EOperationType::Reset) result = "Reset";
	if (operationType == EOperationType::ResetSubFlows) result = "ResetSubFlows";

	if (operationType == EOperationType::Cancel_Steps) result = "Cancel_Steps";
	if (operationType == EOperationType::Cancel_SubFlow) result = "Cancel_SubFlow";

	if (operationType == EOperationType::TRANSACTION_BEGIN) result = "TRANSACTION_BEGIN";
	if (operationType == EOperationType::TRANSACTION_END) result = "TRANSACTION_END";

	return result;
}

//------------------------------------------------------
// FOperationInfo
//------------------------------------------------------

void FOperationInfo::ReportStepStatus(const UGFS_Base* step, const EGFSStatus status)
{
	UGameFlow* flow = Flow.Get();
	const UGameFlowState* stateObject = flow->GetStateObject(State);

	const int32 stepIndex = stateObject->Steps.IndexOfByKey(step);

	if (stepIndex != INDEX_NONE)
	{
		if (StepIndices.Contains(stepIndex))
		{
			if (status == EGFSStatus::Started)
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s - BEG %s [%s] in state [%s] in flow {%s}"), *FDateTime::Now().ToString(), *RepeatTab(AdditiveDepth + 1), *GetStepPhaseString(step->ActiveOperationType), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else if (status == EGFSStatus::Finished || status == EGFSStatus::Cancelled)
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s - END %s [%s] in state [%s] in flow {%s}"), *FDateTime::Now().ToString(), *RepeatTab(AdditiveDepth + 1), *GetStepPhaseString(step->ActiveOperationType), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());

				StepIndices.Remove(stepIndex);

				if (StepIndices.IsEmpty())
				{
					UGameFlow::ExecuteOperation(step->CatchingOperationId);
				}
			}
			else if (status == EGFSStatus::Failed)
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - ERR [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(AdditiveDepth + 1), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else
			{
				// Nothing to do here
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - Cant find [%s] in catching operation for flow {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(AdditiveDepth + 1), *step->GenerateDescription().ToString(), *flow->GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - Cant find [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(AdditiveDepth + 1), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
	}
}

//------------------------------------------------------
// OperationFactory
//------------------------------------------------------

namespace OperationFactory
{
	OperationId EnterState(FGuid& activeState, UGameFlow* flow, const FGuid state, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, state, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_Set(FGuid& activeState, UGameFlow* flow, const FGuid state, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, state, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_Set_Log(FGuid& activeState, UGameFlow* flow, const FGuid state, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_Set_Log;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, state, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_Steps(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_Steps;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_SubFlow_Set(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_SubFlow_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_SubFlow_Set_Log(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_SubFlow_Set_Log;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId EnterState_SubFlow(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_SubFlow;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId AutoTransition(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::AutoTransition;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_SubFlow(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_SubFlow;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_SubFlow_Set_Log(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_SubFlow_Set_Log;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_SubFlow_Set(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_SubFlow_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_Steps(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_Steps;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_Set_Log(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_Set_Log;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId ExitState_Set(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CatchingOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::CatchingOperation;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId Reset(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::Reset;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId Reset_SubFlows(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const OperationFlags operationFlags, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ResetSubFlows;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, operationFlags, nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId TransactionBegin(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const uint32 additiveDepth, const bool IsInternalTransaction)
	{
		const OperationId operationId = GetOperationId() | EOperationType::TRANSACTION_BEGIN;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, OperationFlags(), nullptr, additiveDepth);
		operationInfo.IsInternalTransaction = IsInternalTransaction;
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId TransactionEnd(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const uint32 additiveDepth)
	{
		const OperationId operationId = GetOperationId() | EOperationType::TRANSACTION_END;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, OperationFlags(), nullptr, additiveDepth);
		////// Debug operationInfo.TypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}
}

//------------------------------------------------------
// UGFS_Base
//------------------------------------------------------

void UGFS_Base::OnComplete(const EGFSStatus status) const { OperationContext[CatchingOperationId].ReportStepStatus(this, status); }

//------------------------------------------------------
// UGameFlowState
//------------------------------------------------------

UGameFlowState::UGameFlowState(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bResetSubFlowOnEnterState = 1;
	bResetSubFlowOnExitState = 1;
}

//------------------------------------------------------
// UGameFlow
//------------------------------------------------------

UGameFlow::UGameFlow(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	ActiveState.Invalidate();
	ActiveTransactionId = OperationId();
}

void UGameFlow::CancelOperation(const OperationId operationId, const OperationId nextOperationId)
{
	const OperationId activeOperationId = GetActiveOperation(operationId);

	const OperationId operationType = activeOperationId & OPERATION_TYPE_MASK;

	if (operationType == EOperationType::EnterState_Steps) return OnCancel_State_Steps(activeOperationId, nextOperationId);
	if (operationType == EOperationType::ExitState_Steps) return OnCancel_State_Steps(activeOperationId, nextOperationId);
	if (operationType == EOperationType::EnterState_SubFlow) return OnCancel_State_SubFlow(activeOperationId, nextOperationId);
	if (operationType == EOperationType::ExitState_SubFlow) return OnCancel_State_SubFlow(activeOperationId, nextOperationId);

	check(0);
}

void UGameFlow::ExecuteOperation(const OperationId operationId)
{
	if (operationId)
	{
		if (OperationContext.Contains(operationId))
		{
			const FOperationInfo& operationInfo = OperationContext[operationId];

			if (UGameFlow* flow = operationInfo.Flow.Get())
			{
				if (flow->ActiveTransactionId)
				{
					OperationContext[flow->ActiveTransactionId].ActiveIndex++;
				}

				const OperationId operationType = operationId & OPERATION_TYPE_MASK;

				if (operationType == EOperationType::EnterState) return OnEnterState(operationId);
				if (operationType == EOperationType::EnterState_Set) return OnEnterState_Set(operationId);
				if (operationType == EOperationType::EnterState_Set_Log) return OnEnterState_Set_Log(operationId);
				if (operationType == EOperationType::EnterState_Steps) return OnEnterState_Steps(operationId);
				if (operationType == EOperationType::EnterState_SubFlow_Set) return OnEnterState_SubFlow_Set(operationId);
				if (operationType == EOperationType::EnterState_SubFlow_Set_Log) return OnEnterState_SubFlow_Set_Log(operationId);
				if (operationType == EOperationType::EnterState_SubFlow) return OnEnterState_SubFlow(operationId);

				if (operationType == EOperationType::AutoTransition) return OnAutoTransition(operationId);

				if (operationType == EOperationType::ExitState) return OnExitState(operationId);
				if (operationType == EOperationType::ExitState_SubFlow) return OnExitState_SubFlow(operationId);
				if (operationType == EOperationType::ExitState_SubFlow_Set_Log) return OnExitState_SubFlow_Set_Log(operationId);
				if (operationType == EOperationType::ExitState_SubFlow_Set) return OnExitState_SubFlow_Set(operationId);
				if (operationType == EOperationType::ExitState_Steps) return OnExitState_Steps(operationId);
				if (operationType == EOperationType::ExitState_Set_Log) return OnExitState_Set_Log(operationId);
				if (operationType == EOperationType::ExitState_Set) return OnExitState_Set(operationId);

				if (operationType == EOperationType::CatchingOperation) return OnCatchingOperation(operationId);

				if (operationType == EOperationType::Reset) return OnReset(operationId);
				if (operationType == EOperationType::ResetSubFlows) return OnResetSubFlows(operationId);

				if (operationType == EOperationType::TRANSACTION_BEGIN) return OnTransactionBegin(operationId);
				if (operationType == EOperationType::TRANSACTION_END) return OnTransactionEnd(operationId);
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant exec operation [%d] without owning flow!"), *RepeatTab(operationInfo.AdditiveDepth), *FDateTime::Now().ToString(), operationId);
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant find operation [%d]!"), *FDateTime::Now().ToString(), *RepeatTab(0), operationId);
		}
	}
}

void UGameFlow::LogOperation(const OperationId operationId, const FOperationInfo& operationInfo)
{
	if (UGameFlow* flow = operationInfo.Flow.Get())
	{
		const OperationId operationType = operationId & OPERATION_TYPE_MASK;

		if (operationType == EOperationType::EnterState_Set_Log)
		{
			UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];
			UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s--> %s {%s}"), *FDateTime::Now().ToString(), *RepeatTab(operationInfo.AdditiveDepth), *stateObject->StateTitle.ToString(), *flow->GetName());
		}
		else if (operationType == EOperationType::EnterState_SubFlow_Set_Log)
		{
			UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];
			UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s==> %s {%s}"), *FDateTime::Now().ToString(), *RepeatTab(operationInfo.AdditiveDepth), *stateObject->SubFlow->GetName(), *flow->GetName());
		}
		else if (operationType == EOperationType::ExitState_SubFlow_Set_Log)
		{
			UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];
			UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<== %s {%s}"), *FDateTime::Now().ToString(), *RepeatTab(operationInfo.AdditiveDepth), *stateObject->SubFlow->GetName(), *flow->GetName());
		}
		else if (operationType == EOperationType::ExitState_Set_Log)
		{
			UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];
			UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<-- %s {%s}"), *FDateTime::Now().ToString(), *RepeatTab(operationInfo.AdditiveDepth), *stateObject->StateTitle.ToString(), *flow->GetName());
		}
		else
		{
			FString operationTypeString = GetOperationTypeString(operationType);
			UE_LOG(LogGameFlow, Log, TEXT("[%s][OPER]%s - [%s] {%s}"), *FDateTime::Now().ToString(), *RepeatTab(operationInfo.AdditiveDepth), *operationTypeString, *flow->GetName());
		}
	}
}

void UGameFlow::OnEnterState(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (stateObject->TransitionKey)
	{
		nextOperationId = OperationFactory::AutoTransition(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	if (stateObject->SubFlow && (stateObject->bInstancedSubFlow || !stateObject->SubFlow->ActiveState.IsValid() || stateObject->bResetSubFlowOnEnterState))
	{
		nextOperationId = OperationFactory::EnterState_SubFlow(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		nextOperationId = OperationFactory::EnterState_SubFlow_Set_Log(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		nextOperationId = OperationFactory::EnterState_SubFlow_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	if (operationInfo.OperationFlags & EOperationFlags::ExecuteSteps && stateObject->Steps.FindByPredicate([](const TObjectPtr<UGFS_Base>& step) { return step; }))
	{
		nextOperationId = OperationFactory::EnterState_Steps(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	nextOperationId = OperationFactory::EnterState_Set_Log(operationInfo.ActiveState, flow, operationInfo.ActiveState, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);

	nextOperationId = OperationFactory::EnterState_Set(operationInfo.ActiveState, flow, operationInfo.State, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);

	ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
}

void UGameFlow::OnEnterState_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	stateObject->bSubFlow_Set = 0;
	operationInfo.ActiveState = operationInfo.State;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_Set_Log(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_Steps(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	const OperationId catchingOperationId = OperationFactory::CatchingOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	FOperationInfo& catchingOperationInfo = OperationContext[catchingOperationId];

	OperationContext[operationId].NextOperationId = catchingOperationId;

	for (int32 i = 0; i < stateObject->Steps.Num(); i++)
	{
		if (stateObject->Steps[i])
		{
			catchingOperationInfo.StepIndices.Add(i);
		}
	}

	for (int32 i = 0; i < stateObject->Steps.Num(); i++)
	{
		if (stateObject->Steps[i])
		{
			stateObject->Steps[i]->ActiveOperationType = EOperationType::EnterState_Steps;
			stateObject->Steps[i]->CatchingOperationId = catchingOperationId;

			catchingOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

			stateObject->Steps[i]->OnEnter();
		}
	}
}

void UGameFlow::OnEnterState_SubFlow_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	stateObject->bSubFlow_Set = 1;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_SubFlow_Set_Log(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_SubFlow(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow and then enter Sub Flow

			if (stateObject->SubFlow->CanEnterFlow())
			{
				nextOperationId = stateObject->SubFlow->EnterFlow_Internal(stateObject->SubFlowActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);
				nextOperationId = stateObject->SubFlow->ExitFlow_Internal(stateObject->SubFlowActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

				ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
		else
		{
			// Enter Sub Flow

			if (stateObject->SubFlow->CanEnterFlow())
			{
				nextOperationId = stateObject->SubFlow->EnterFlow_Internal(stateObject->SubFlowActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

				ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
	}
	else // Shared
	{
		if (stateObject->SubFlow->ActiveState.IsValid())
		{
			if (stateObject->bResetSubFlowOnEnterState)
			{
				// Exit Sub Flow and then Enter Sub Flow

				if (stateObject->SubFlow->CanEnterFlow())
				{
					nextOperationId = stateObject->SubFlow->EnterFlow_Internal(stateObject->SubFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);
					nextOperationId = stateObject->SubFlow->ExitFlow_Internal(stateObject->SubFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

					ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
				}
				else
				{
					ExecuteOperation(nextOperationId);
				}
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
		else
		{
			// Enter Sub Flow

			if (stateObject->SubFlow->CanEnterFlow())
			{
				nextOperationId = stateObject->SubFlow->EnterFlow_Internal(stateObject->SubFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

				ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
	}
}

void UGameFlow::OnExitState(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	OperationId nextOperationId = operationInfo.NextOperationId;

	nextOperationId = OperationFactory::ExitState_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);	
	nextOperationId = OperationFactory::ExitState_Set_Log(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);

	if (operationInfo.OperationFlags & EOperationFlags::ExecuteSteps && stateObject->Steps.FindByPredicate([](const TObjectPtr<UGFS_Base>& step) { return step; }))
	{
		nextOperationId = OperationFactory::ExitState_Steps(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	if (stateObject->SubFlow && (stateObject->bInstancedSubFlow || stateObject->SubFlow->ActiveState.IsValid() && (stateObject->bResetSubFlowOnExitState || operationInfo.OperationFlags & EOperationFlags::ResetActiveSubFlow)))
	{
		nextOperationId = OperationFactory::ExitState_SubFlow_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		nextOperationId = OperationFactory::ExitState_SubFlow_Set_Log(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		nextOperationId = OperationFactory::ExitState_SubFlow(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
}

void UGameFlow::OnExitState_SubFlow(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow
			
			if (stateObject->SubFlow->CanExitFlow())
			{
				nextOperationId = stateObject->SubFlow->ExitFlow_Internal(stateObject->SubFlowActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

				ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
		else
		{
			ExecuteOperation(nextOperationId);
		}
	}
	else // Shared
	{
		if (stateObject->SubFlow->ActiveState.IsValid() && (stateObject->bResetSubFlowOnExitState || operationInfo.OperationFlags & EOperationFlags::ResetActiveSubFlow))
		{
			// Exit Sub Flow

			if (stateObject->SubFlow->CanExitFlow())
			{
				nextOperationId = stateObject->SubFlow->ExitFlow_Internal(stateObject->SubFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);

				ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
			}
			else
			{
				ExecuteOperation(nextOperationId);
			}
		}
		else
		{
			ExecuteOperation(nextOperationId);
		}
	}
}

void UGameFlow::OnExitState_SubFlow_Set_Log(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnExitState_SubFlow_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	stateObject->bSubFlow_Set = 0;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnExitState_Steps(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	const OperationId catchingOperationId = OperationFactory::CatchingOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	FOperationInfo& catchingOperationInfo = OperationContext[catchingOperationId];

	OperationContext[operationId].NextOperationId = catchingOperationId;

	for (int32 i = stateObject->Steps.Num() - 1; i >= 0; i--)
	{
		if (stateObject->Steps[i])
		{
			catchingOperationInfo.StepIndices.Add(i);
		}
	}

	for (int32 i = stateObject->Steps.Num() - 1; i >= 0; i--)
	{
		if (stateObject->Steps[i])
		{
			stateObject->Steps[i]->ActiveOperationType = EOperationType::ExitState_Steps;
			stateObject->Steps[i]->CatchingOperationId = catchingOperationId;

			catchingOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

			stateObject->Steps[i]->OnExit();
		}
	}
}

void UGameFlow::OnExitState_Set_Log(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnExitState_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	operationInfo.ActiveState.Invalidate();
	stateObject->bSubFlow_Set = 0;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnCatchingOperation(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnAutoTransition(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	OperationId nextOperationId = flow->MakeTransition_Internal(stateObject->TransitionKey, operationInfo.OperationFlags, operationInfo.NextOperationId, operationInfo.AdditiveDepth);

	ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
}

void UGameFlow::OnReset(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (operationInfo.OperationFlags & EOperationFlags::ResetAnySubFlows)
	{
		nextOperationId = OperationFactory::Reset_SubFlows(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
	}

	if (flow->ActiveState.IsValid()) // Special exit
	{
		nextOperationId = OperationFactory::ExitState_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		nextOperationId = OperationFactory::ExitState_Set_Log(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);

		if (operationInfo.OperationFlags & EOperationFlags::ExecuteSteps && stateObject->Steps.FindByPredicate([](const TObjectPtr<UGFS_Base>& step) { return step; }))
		{
			nextOperationId = OperationFactory::ExitState_Steps(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		}

		if (stateObject->SubFlow && (stateObject->bInstancedSubFlow || stateObject->SubFlow->ActiveState.IsValid()))
		{
			nextOperationId = OperationFactory::ExitState_SubFlow_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
			nextOperationId = OperationFactory::ExitState_SubFlow_Set_Log(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
			nextOperationId = stateObject->SubFlow->ResetFlow_Internal(stateObject->bInstancedSubFlow ? stateObject->SubFlowActiveState : stateObject->SubFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);
		}
	}

	ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
}

void UGameFlow::OnResetSubFlows(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();

	TSet<UGameFlow*> subFlows;

	for (const TPair<FGuid, TObjectPtr<UGameFlowState>>& stateEntry : flow->States)
	{
		if (stateEntry.Value && stateEntry.Value->SubFlow && stateEntry.Value->SubFlow->ActiveState.IsValid())
		{
			subFlows.FindOrAdd(stateEntry.Value->SubFlow);
		}
	}

	OperationId nextOperationId = operationInfo.NextOperationId;

	for (UGameFlow* subFlow : subFlows)
	{
		nextOperationId = subFlow->ResetFlow_Internal(subFlow->ActiveState, operationInfo.OperationFlags, nextOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);
	}

	ExecuteOperation(OperationContext[operationId].NextOperationId = nextOperationId);
}

void UGameFlow::OnTransactionBegin(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	flow->ActiveTransactionId = operationId;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnTransactionEnd(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	const OperationId transactionBeginId = flow->ActiveTransactionId;
	flow->ActiveTransactionId = OperationId();

	const FOperationInfo& transactionBeginInfo = OperationContext[transactionBeginId];
	if (!transactionBeginInfo.IsInternalTransaction) RemoveTransaction(transactionBeginId);

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnCancel_State_Steps(const OperationId operationId, const OperationId nextOperationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(EOperationType::Cancel_Steps, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	RemoveOperations(OperationContext[stateObject->Steps[0]->CatchingOperationId].NextOperationId, TransactionContext[flow->ActiveTransactionId]);

	OperationContext[TransactionContext[flow->ActiveTransactionId]].NextOperationId = nextOperationId;
	OperationContext[stateObject->Steps[0]->CatchingOperationId].NextOperationId = TransactionContext[flow->ActiveTransactionId];

	TSet<int32> stepIndices = OperationContext[stateObject->Steps[0]->CatchingOperationId].StepIndices;
	
	for (int32 i : stepIndices)
	{
		if (stateObject->Steps[i])
		{
			stateObject->Steps[i]->ActiveOperationType = EOperationType::Reset;
			stateObject->Steps[i]->OnCancel();
		}
	}
}

void UGameFlow::OnCancel_State_SubFlow(const OperationId operationId, const OperationId nextOperationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(EOperationType::Cancel_SubFlow, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.ActiveState];

	if (!stateObject->bInstancedSubFlow && stateObject->SubFlow->ActiveState.IsValid())
	{
		OperationId tmpOperationId = OperationFactory::ExitState_SubFlow_Set(operationInfo.ActiveState, flow, nextOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		tmpOperationId = OperationFactory::ExitState_SubFlow_Set_Log(operationInfo.ActiveState, flow, tmpOperationId, operationInfo.OperationFlags, operationInfo.AdditiveDepth);
		stateObject->SubFlow->ResetFlow_Params(stateObject->SubFlow->ActiveState, OperationContext[nextOperationId].OperationFlags, tmpOperationId, operationInfo.AdditiveDepth + AdditiveDepthStep, true);
	}
	else
	{
		ExecuteOperation(nextOperationId);
	}
}

#if WITH_EDITORONLY_DATA

UGameFlowState* UGameFlow::AddState(const FGuid& stateToAdd, const FName stateTitle)
{
	States.Add(stateToAdd, NewObject<UGameFlowState>(this));
	States[stateToAdd]->StateGuid = stateToAdd;
	States[stateToAdd]->StateTitle = stateTitle;
	return States[stateToAdd];
}

void UGameFlow::DestroyState(const FGuid& stateToDestroy)
{
	if (ActiveState == stateToDestroy) ActiveState.Invalidate();

	if (EntryState == stateToDestroy) EntryState.Invalidate();

	DestroyStateTransition(stateToDestroy);

	if (TObjectPtr<UGameFlowState> stateObj = States.FindAndRemoveChecked(stateToDestroy))
	{
		stateObj->Rename(nullptr, GetTransientPackage());
	}
}

void UGameFlow::DestroyStateTransition(const FGuid& stateToDestroy)
{
	if (TransitionCollections.Contains(stateToDestroy))
	{
		TSet<FGuid> toTransitionStates;

		for (TPair<FGuid, TObjectPtr<UGameFlowTransition>> transitionEntry : TransitionCollections[stateToDestroy].Transitions)
		{
			toTransitionStates.FindOrAdd(transitionEntry.Key);
		}

		for (const FGuid& toTransitionState : toTransitionStates)
		{
			DestroyTransition(stateToDestroy, toTransitionState);
		}
	}

	TSet<FGuid> fromTransitionStates;

	for (TPair<FGuid, FGameFlowTransitionCollection>& transitionCollectionEntry : TransitionCollections)
	{
		if (transitionCollectionEntry.Value.Transitions.Contains(stateToDestroy))
		{
			fromTransitionStates.FindOrAdd(transitionCollectionEntry.Key);
		}
	}

	for (const FGuid& fromTransitionState : fromTransitionStates)
	{
		DestroyTransition(fromTransitionState, stateToDestroy);
	}
}

UGameFlowTransition* UGameFlow::AddTransition(const FGuid& fromState, const FGuid& toState)
{
	FGameFlowTransitionCollection& transitionCollection = TransitionCollections.FindOrAdd(fromState);

	transitionCollection.Transitions.FindOrAdd(toState) = NewObject<UGameFlowTransition>(this);

	return transitionCollection.Transitions[toState];
}

void UGameFlow::DestroyTransition(const FGuid& fromState, const FGuid& toState)
{
	if (TransitionCollections.Contains(fromState))
	{
		FGameFlowTransitionCollection& transitionCollection = TransitionCollections[fromState];

		if (transitionCollection.Transitions.Contains(toState))
		{
			if (UGameFlowTransition* transitionObj = transitionCollection.Transitions.FindAndRemoveChecked(toState))
			{
				transitionObj->Rename(nullptr, GetTransientPackage());

				if (transitionCollection.Transitions.IsEmpty())
				{
					TransitionCollections.Remove(fromState);
				}
			}
		}
	}
}

#endif

void UGameFlow::FindStateByTitle(FName stateTitle, TArray<UGameFlowState*>& outStateObjects) const
{
	for (const TPair<FGuid, TObjectPtr<UGameFlowState>>& stateEntry : States)
	{
		if (stateEntry.Value && stateEntry.Value->StateTitle == stateTitle)
		{
			outStateObjects.Add(stateEntry.Value.Get());
		}
	}
}

bool UGameFlow::IsTransitioning() const { return ActiveTransactionId != OperationId(); }

bool UGameFlow::CanEnterFlow() const
{
	bool result = false;

	if (!ActiveTransactionId)
	{
		if (!ActiveState.IsValid())
		{
			if (EntryState.IsValid())
			{
				if (States.Contains(EntryState))
				{
					result = true;
				}
				else
				{
					UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *EntryState.ToString(), *GetName());
				}
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find Entry state in flow {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant enter flow that is active {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant enter flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
	}

	return result;
}

void UGameFlow::EnterFlow(const bool executeSteps)
{
	if (CanEnterFlow())
	{
		OperationFlags operationFlags;
		if (executeSteps) operationFlags |= EOperationFlags::ExecuteSteps;

		ExecuteOperation(EnterFlow_Internal(ActiveState, operationFlags, OperationId(), 0, false));
	};
}

bool UGameFlow::CanExitFlow() const
{
	bool result = false;

	if (!ActiveTransactionId)
	{
		if (ActiveState.IsValid())
		{
			result = true;
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is not active {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
	}

	return result;
}

void UGameFlow::ExitFlow(const bool executeSteps, const bool resetActiveSubFlow)
{
	if (CanExitFlow())
	{
		OperationFlags operationFlags;
		if (executeSteps) operationFlags |= EOperationFlags::ExecuteSteps;
		if (resetActiveSubFlow) operationFlags |= EOperationFlags::ResetActiveSubFlow;

		ExecuteOperation(ExitFlow_Internal(ActiveState, operationFlags, OperationId(), 0, false));
	}
}

void UGameFlow::ResetFlow(const bool resetAnySubFlow)
{
	OperationFlags operationFlags;
	if (resetAnySubFlow) operationFlags |= EOperationFlags::ResetAnySubFlows;

	ResetFlow_Params(ActiveState, operationFlags, OperationId(), 0, false);
}

void UGameFlow::MakeTransition(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool resetActiveSubFlow, const bool isEnqueued)
{
	if (transitionKey)
	{
		if (ActiveState.IsValid())
		{
			OperationFlags operationFlags;
			if (executeSteps) operationFlags |= EOperationFlags::ExecuteSteps;
			if (resetActiveSubFlow) operationFlags |= EOperationFlags::ResetActiveSubFlow;

			if (!ActiveTransactionId || isEnqueued)
			{
				const OperationId transactionEndId = OperationFactory::TransactionEnd(ActiveState, this, OperationId(), 0);
				const OperationId transitionOperationId = MakeTransition_Internal(transitionKey, operationFlags, transactionEndId, 0);
				const OperationId transactionBeginId = OperationFactory::TransactionBegin(ActiveState, this, transitionOperationId, 0, false);

				if (transitionOperationId)
				{
					TransactionContext.Add(transactionBeginId, transactionEndId);

					return isEnqueued ? EnqueueOperation(transactionBeginId) : ExecuteOperation(transactionBeginId);
				}
				else
				{
					UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find any transition in flow for Transition Key %s {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *transitionKey->GetName(), *GetName());
				}
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow that is not active {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow without Transition Key {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
	}
}

OperationId UGameFlow::EnterFlow_Internal(FGuid& activeState, const OperationFlags operationFlags, const OperationId nextOperationId, const uint32 additiveDepth, const bool isInternalTransaction)
{
	const OperationId transactionEndId = OperationFactory::TransactionEnd(ActiveState, this, nextOperationId, additiveDepth);
	const OperationId enterStateOperationId = OperationFactory::EnterState(ActiveState, this, EntryState, transactionEndId, operationFlags, additiveDepth);
	const OperationId transactionBeginId = OperationFactory::TransactionBegin(ActiveState, this, enterStateOperationId, additiveDepth, isInternalTransaction);

	TransactionContext.Add(transactionBeginId, transactionEndId);

	return transactionBeginId;
}

OperationId UGameFlow::ExitFlow_Internal(FGuid& activeState, const OperationFlags operationFlags, const OperationId nextOperationId, const uint32 additiveDepth, const bool isInternalTransaction)
{
	const OperationId transactionEndId = OperationFactory::TransactionEnd(ActiveState, this, nextOperationId, additiveDepth);
	const OperationId exitStateOperationId = OperationFactory::ExitState(ActiveState, this, transactionEndId, operationFlags, additiveDepth);
	const OperationId transactionBeginId = OperationFactory::TransactionBegin(ActiveState, this, exitStateOperationId, additiveDepth, isInternalTransaction);

	TransactionContext.Add(transactionBeginId, transactionEndId);

	return transactionBeginId;
}

void UGameFlow::ResetFlow_Params(FGuid& activeState, const OperationFlags operationFlags, const OperationId nextOperationId, const uint32 additiveDepth, const bool isInternalTransaction)
{
	const OperationId resetTransactionId = ResetFlow_Internal(activeState, operationFlags, nextOperationId, additiveDepth, isInternalTransaction);

	if (!ActiveTransactionId)
	{
		ExecuteOperation(resetTransactionId);
	}
	else
	{
		CancelOperation(ActiveTransactionId, resetTransactionId);
	}
}

OperationId UGameFlow::ResetFlow_Internal(FGuid& activeState, const OperationFlags operationFlags, const OperationId nextOperationId, const uint32 additiveDepth, const bool isInternalTransaction)
{
	const OperationId transactionEndId = OperationFactory::TransactionEnd(ActiveState, this, nextOperationId, additiveDepth);
	const OperationId exitStateOperationId = OperationFactory::Reset(ActiveState, this, transactionEndId, operationFlags, additiveDepth);
	const OperationId transactionBeginId = OperationFactory::TransactionBegin(ActiveState, this, exitStateOperationId, additiveDepth, isInternalTransaction);

	TransactionContext.Add(transactionBeginId, transactionEndId);

	return transactionBeginId;
}

OperationId UGameFlow::MakeTransition_Internal(UGameFlowTransitionKey* transitionKey, const OperationFlags operationFlags, const OperationId nextOperationId, const uint32 additiveDepth)
{
	if (States[ActiveState]->SubFlow)
	{
		// Try to find transition in Sub Flow

		if (const OperationId subFlowTransitionId = States[ActiveState]->SubFlow->MakeTransition_Internal(transitionKey, operationFlags, nextOperationId, additiveDepth + AdditiveDepthStep))
		{
			return subFlowTransitionId;
		}
	}

	// Try to find transition in Active state

	if (TransitionCollections.Contains(ActiveState))
	{
		const FGameFlowTransitionCollection& transitionCollection = TransitionCollections[ActiveState];

		for (const TPair<FGuid, TObjectPtr<UGameFlowTransition>>& transitionEntry : transitionCollection.Transitions)
		{
			if (transitionEntry.Value->TransitionKey == transitionKey)
			{
				check(transitionEntry.Key != ActiveState); // Cant have self transitions

				const OperationId enterStateOperationId = OperationFactory::EnterState(ActiveState, this, transitionEntry.Key, nextOperationId, operationFlags, additiveDepth);
				const OperationId exitStateOperationId = OperationFactory::ExitState(ActiveState, this, enterStateOperationId, operationFlags, additiveDepth);

				return exitStateOperationId;
			}
		}
	}

	return OperationId();
}

void UGameFlow::SetWorldPtr(FGuid& activeState, UWorld* world, const bool force)
{
	if (!ActiveTransactionId || force)
	{
		WorldPtr = world;

		if (activeState.IsValid())
		{
			for (const TObjectPtr<UGFS_Base>& step : States[activeState]->Steps)
			{
				if (step)
				{
					step->OnWorldContextChanged(force);
				}
			}
		}

		for (const TPair<FGuid, TObjectPtr<UGameFlowState>>& stateEntry : States)
		{
			if (UGameFlow* subFlow = stateEntry.Value->SubFlow)
			{
				FGuid& subFlowActiveState = stateEntry.Value->bInstancedSubFlow ? stateEntry.Value->SubFlowActiveState : subFlow->ActiveState;
				subFlow->SetWorldPtr(subFlowActiveState, world, force);
			}
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant set world in flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *RepeatTab(0), *GetName());
	}
}

void UGameFlow::EnqueueOperation(const OperationId operationId) const
{
	OperationContext[TransactionContext[ActiveTransactionId]].NextOperationId = operationId;

	if (OperationContext[TransactionContext[ActiveTransactionId]].NextOperationId != OperationId())
	{
		RemoveTransaction(OperationContext[TransactionContext[ActiveTransactionId]].NextOperationId);
	}
}