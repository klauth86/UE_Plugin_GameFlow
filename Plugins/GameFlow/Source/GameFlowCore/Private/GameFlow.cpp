// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "GameFlow.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogGameFlow);

int32 LogGameFlowUtils::Depth = 0;

TMap<OperationId, FOperationInfo> OperationContext;

FString LogGameFlowUtils::RepeatTab(int32 num)
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

	if (operationType == EOperationType::MakeTransition_Enqueued) result = "MakeTransition_Enqueued";

	if (operationType == EOperationType::Reset) result = "Reset";
	if (operationType == EOperationType::ResetSubFlows) result = "ResetSubFlows";

	if (operationType == EOperationType::Cancel_Steps) result = "Cancel_Steps";
	if (operationType == EOperationType::Cancel_SubFlow) result = "Cancel_SubFlow";

	if (operationType == EOperationType::TRANSACTION_BEGIN) result = "TRANSACTION_BEGIN";
	if (operationType == EOperationType::TRANSACTION_END) result = "TRANSACTION_END";

	return result;
}

void LogOperation(const OperationId operationId, const FOperationInfo& operationInfo)
{
	if (UGameFlow* flow = operationInfo.Flow.Get())
	{
		FString operationTypeString = GetOperationTypeString(operationId & OPERATION_TYPE_MASK);
		UE_LOG(LogGameFlow, Log, TEXT("[%s][OPER]%s - [%s] {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), *operationTypeString, *flow->GetName());
	}
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
				UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s - BEG %s [%s] in state [%s] in flow {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 2), *GetStepPhaseString(step->ActiveOperationType), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else if (status == EGFSStatus::Finished || status == EGFSStatus::Cancelled)
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s - END %s [%s] in state [%s] in flow {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 2), *GetStepPhaseString(step->ActiveOperationType), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());

				StepIndices.Remove(stepIndex);

				if (StepIndices.IsEmpty())
				{
					UGameFlow::ExecuteOperation(step->ActiveOperationId, step->CatchingOperationId);
				}
			}
			else if (status == EGFSStatus::Failed)
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - ERR [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else
			{
				// Nothing to do here
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - Cant find [%s] in catching operation for flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *flow->GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%s - Cant find [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
	}
}

//------------------------------------------------------
// OperationFactory
//------------------------------------------------------

namespace OperationFactory
{
	OperationId CreateEnterStateOperation(FGuid& activeState, UGameFlow* flow, const FGuid state, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, state, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateEnterStateSetOperation(FGuid& activeState, UGameFlow* flow, const FGuid state, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, state, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateEnterStateStepsOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_Steps;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateEnterStateSubFlowSetOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_SubFlow_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateEnterStateSubFlowOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::EnterState_SubFlow;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateAutoTransitionOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::AutoTransition;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateExitStateOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateExitStateSubFlowOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_SubFlow;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateExitStateSubFlowSetOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_SubFlow_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateExitStateStepsOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_Steps;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateExitStateSetOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ExitState_Set;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateCatchingOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::CatchingOperation;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateTransitionEnqueuedOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool executeSteps, const bool resetAnySubFlow, UGameFlowTransitionKey* transitionKey)
	{
		const OperationId operationId = GetOperationId() | EOperationType::MakeTransition_Enqueued;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, executeSteps, resetAnySubFlow, transitionKey);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateResetOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId, const bool resetAnySubFlow)
	{
		const OperationId operationId = GetOperationId() | EOperationType::Reset;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, false, resetAnySubFlow, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateResetSubFlowsOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId)
	{
		const OperationId operationId = GetOperationId() | EOperationType::ResetSubFlows;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, false, true, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateTransactionBeginOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId)
	{
		const OperationId operationId = GetOperationId() | EOperationType::TRANSACTION_BEGIN;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, false, false, nullptr);
		OperationContext.Add(operationId, operationInfo);
		return operationId;
	}

	OperationId CreateTransactionEndOperation(FGuid& activeState, UGameFlow* flow, const OperationId nextOperationId)
	{
		const OperationId operationId = GetOperationId() | EOperationType::TRANSACTION_END;
		FOperationInfo operationInfo = FOperationInfo(activeState, flow, activeState, nextOperationId, false, false, nullptr);
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
	ActiveOperationId = OperationId();
}

void UGameFlow::CancelOperation(const OperationId operationId, const OperationId nextOperationId)
{
	if (operationId)
	{
		if (OperationContext.Contains(operationId))
		{
			const OperationId operationType = operationId & OPERATION_TYPE_MASK;

			if (operationType == EOperationType::EnterState_Steps) return OnCancel_State_Steps(operationId, nextOperationId);
			if (operationType == EOperationType::ExitState_Steps) return OnCancel_State_Steps(operationId, nextOperationId);
			if (operationType == EOperationType::EnterState_SubFlow) return OnCancel_State_SubFlow(operationId, nextOperationId);
			if (operationType == EOperationType::ExitState_SubFlow) return OnCancel_State_SubFlow(operationId, nextOperationId);

			check(0);
		}
	}
}

void UGameFlow::ExecuteOperation(const OperationId prevOperationId, const OperationId operationId)
{
	if (prevOperationId)
	{
		if (OperationContext.Contains(prevOperationId))
		{
			const FOperationInfo& prevOperationInfo = OperationContext[prevOperationId];

			if (UGameFlow* flow = prevOperationInfo.Flow.Get())
			{
				if (flow->ActiveOperationId == prevOperationId)
				{
					flow->ActiveOperationId = OperationId();
				}
			}

			OperationContext.Remove(prevOperationId);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant find operation [%d]!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), prevOperationId);
		}
	}

	if (operationId)
	{
		if (OperationContext.Contains(operationId))
		{
			const FOperationInfo& operationInfo = OperationContext[operationId];

			if (UGameFlow* flow = operationInfo.Flow.Get())
			{
				const OperationId operationType = operationId & OPERATION_TYPE_MASK;

				// Enqueued operations are only proxies to call real opeations, so should not be set up in Flows
				
				if (operationType != EOperationType::MakeTransition_Enqueued)
				{
					flow->ActiveOperationId = operationId;
				}

				if (operationType == EOperationType::EnterState) return OnEnterState(operationId);
				if (operationType == EOperationType::EnterState_Set) return OnEnterState_Set(operationId);
				if (operationType == EOperationType::EnterState_Steps) return OnEnterState_Steps(operationId);
				if (operationType == EOperationType::EnterState_SubFlow_Set) return OnEnterState_SubFlow_Set(operationId);
				if (operationType == EOperationType::EnterState_SubFlow) return OnEnterState_SubFlow(operationId);

				if (operationType == EOperationType::AutoTransition) return OnAutoTransition(operationId);

				if (operationType == EOperationType::ExitState) return OnExitState(operationId);
				if (operationType == EOperationType::ExitState_SubFlow) return OnExitState_SubFlow(operationId);
				if (operationType == EOperationType::ExitState_SubFlow_Set) return OnExitState_SubFlow_Set(operationId);
				if (operationType == EOperationType::ExitState_Steps) return OnExitState_Steps(operationId);
				if (operationType == EOperationType::ExitState_Set) return OnExitState_Set(operationId);

				if (operationType == EOperationType::CatchingOperation) return OnCatchingOperation(operationId);

				if (operationType == EOperationType::MakeTransition_Enqueued) return OnMakeTransitionEnqueued(operationId);

				if (operationType == EOperationType::Reset) return OnReset(operationId);
				if (operationType == EOperationType::ResetSubFlows) return OnResetSubFlows(operationId);

				if (operationType == EOperationType::TRANSACTION_BEGIN) return OnTransactionBegin(operationId);
				if (operationType == EOperationType::TRANSACTION_END) return OnTransactionEnd(operationId);
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant exec operation [%d] without owning flow!"), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), *FDateTime::Now().ToString(), operationId);
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant find operation [%d]!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), operationId);
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
		nextOperationId = OperationFactory::CreateAutoTransitionOperation(operationInfo.ActiveState, flow, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
	}

	if (stateObject->SubFlow && (stateObject->bInstancedSubFlow || !stateObject->SubFlow->ActiveState.IsValid()))
	{
		const OperationId subFlowOperationId = OperationFactory::CreateEnterStateSubFlowOperation(operationInfo.ActiveState, operationInfo.Flow.Get(), nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
		nextOperationId = OperationFactory::CreateEnterStateSubFlowSetOperation(operationInfo.ActiveState, operationInfo.Flow.Get(), subFlowOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
	}

	if (operationInfo.ExecuteSteps)
	{
		nextOperationId = OperationFactory::CreateEnterStateStepsOperation(operationInfo.ActiveState, operationInfo.Flow.Get(), nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
	}

	ExecuteOperation(operationId, OperationFactory::CreateEnterStateSetOperation(operationInfo.ActiveState, operationInfo.Flow.Get(), operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
}

void UGameFlow::OnEnterState_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	stateObject->bSubFlow_Set = 0;

	LogGameFlowUtils::Depth++;
	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s--> %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());

	operationInfo.ActiveState = operationInfo.State;

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_Steps(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const OperationId catchingOperationId = OperationFactory::CreateCatchingOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
		FOperationInfo& catchingOperationInfo = OperationContext[catchingOperationId];

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
				stateObject->Steps[i]->ActiveOperationId = operationId;
				stateObject->Steps[i]->ActiveOperationType = EOperationType::EnterState_Steps;
				stateObject->Steps[i]->CatchingOperationId = catchingOperationId;

				catchingOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

				stateObject->Steps[i]->OnEnter();
			}
		}
	}
	else
	{
		ExecuteOperation(operationId, operationInfo.NextOperationId);
	}
}

void UGameFlow::OnEnterState_SubFlow_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	stateObject->bSubFlow_Set = 1;

	LogGameFlowUtils::Depth++;
	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s==> %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->SubFlow->GetName(), *flow->GetName());

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_SubFlow(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow and then enter Sub Flow

			const OperationId enterStateOperationId = OperationFactory::CreateEnterStateOperation(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
			ExecuteOperation(operationId, OperationFactory::CreateExitStateOperation(stateObject->SubFlowActiveState, stateObject->SubFlow, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
		}
		else
		{
			// Enter Sub Flow

			ExecuteOperation(operationId, OperationFactory::CreateEnterStateOperation(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
		}
	}
	else // Shared
	{
		if (stateObject->bResetSubFlowOnEnterState)
		{
			if (stateObject->SubFlow->ActiveState.IsValid())
			{
				// Exit Sub Flow and then Enter Sub Flow

				const OperationId enterStateOperationId = OperationFactory::CreateEnterStateOperation(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
				ExecuteOperation(operationId, OperationFactory::CreateExitStateOperation(stateObject->SubFlow->ActiveState, stateObject->SubFlow, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
			}
			else
			{
				// Enter Sub Flow

				ExecuteOperation(operationId, OperationFactory::CreateEnterStateOperation(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
			}
		}
		else
		{
			if (stateObject->SubFlow->ActiveState.IsValid())
			{
				ExecuteOperation(operationId, operationInfo.NextOperationId);
			}
			else
			{
				// Enter Sub Flow

				ExecuteOperation(operationId, OperationFactory::CreateEnterStateOperation(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
			}
		}
	}
}

void UGameFlow::OnExitState(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	const OperationId setOperationId = OperationFactory::CreateExitStateSetOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

	if (operationInfo.ExecuteSteps)
	{
		const OperationId stepsOperationId = OperationFactory::CreateExitStateStepsOperation(operationInfo.ActiveState, flow, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

		if (stateObject->SubFlow && stateObject->bSubFlow_Set)
		{
			const OperationId exitStateSubFlowSetOperationId = OperationFactory::CreateExitStateSubFlowSetOperation(operationInfo.ActiveState, flow, stepsOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

			if (stateObject->bInstancedSubFlow && stateObject->SubFlowActiveState.IsValid() || stateObject->SubFlow->ActiveState.IsValid())
			{
				ExecuteOperation(operationId, OperationFactory::CreateExitStateSubFlowOperation(operationInfo.ActiveState, flow, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
			}
			else
			{
				ExecuteOperation(operationId, exitStateSubFlowSetOperationId);
			}
		}
		else
		{
			ExecuteOperation(operationId, stepsOperationId);
		}
	}
	else
	{
		if (stateObject->SubFlow && stateObject->bSubFlow_Set)
		{
			const OperationId exitStateSubFlowSetOperationId = OperationFactory::CreateExitStateSubFlowSetOperation(operationInfo.ActiveState, flow, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

			if (stateObject->bInstancedSubFlow && stateObject->SubFlowActiveState.IsValid() || stateObject->SubFlow->ActiveState.IsValid())
			{
				ExecuteOperation(operationId, OperationFactory::CreateExitStateSubFlowOperation(operationInfo.ActiveState, flow, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
			}
			else
			{
				ExecuteOperation(operationId, exitStateSubFlowSetOperationId);
			}
		}
		else
		{
			ExecuteOperation(operationId, setOperationId);
		}
	}
}

void UGameFlow::OnExitState_SubFlow(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow

			ExecuteOperation(operationId, OperationFactory::CreateExitStateOperation(stateObject->SubFlowActiveState, stateObject->SubFlow, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
		}
		else
		{
			ExecuteOperation(operationId, operationInfo.NextOperationId);
		}
	}
	else // Shared
	{
		if ((stateObject->bResetSubFlowOnExitState || operationInfo.ResetSubFlow) && stateObject->SubFlow->ActiveState.IsValid())
		{
			// Exit Sub Flow

			ExecuteOperation(operationId, OperationFactory::CreateExitStateOperation(stateObject->SubFlow->ActiveState, stateObject->SubFlow, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow));
		}
		else
		{
			ExecuteOperation(operationId, operationInfo.NextOperationId);
		}
	}
}

void UGameFlow::OnExitState_SubFlow_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<== %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->SubFlow->GetName(), *flow->GetName());
	LogGameFlowUtils::Depth--;

	stateObject->bSubFlow_Set = 0;

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnExitState_Steps(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const OperationId catchingOperationId = OperationFactory::CreateCatchingOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);
		FOperationInfo& catchingOperationInfo = OperationContext[catchingOperationId];

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
				stateObject->Steps[i]->ActiveOperationId = operationId;
				stateObject->Steps[i]->ActiveOperationType = EOperationType::ExitState_Steps;
				stateObject->Steps[i]->CatchingOperationId = catchingOperationId;

				catchingOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

				stateObject->Steps[i]->OnExit();
			}
		}
	}
	else
	{
		ExecuteOperation(operationId, operationInfo.NextOperationId);
	}
}

void UGameFlow::OnExitState_Set(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	operationInfo.ActiveState.Invalidate();

	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<-- %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());
	LogGameFlowUtils::Depth--;

	stateObject->bSubFlow_Set = 0;

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnCatchingOperation(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnAutoTransition(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->TransitionKey)
	{
		if (operationInfo.ActiveState.IsValid())
		{
			const OperationId autoTransitionOperationId = flow->CreateTransitionOperation(stateObject->TransitionKey, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

			ExecuteOperation(operationId, autoTransitionOperationId);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow that is not active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *flow->GetName());
		}
	}
	else
	{
		ExecuteOperation(operationId, operationInfo.NextOperationId);
	}
}

void UGameFlow::OnMakeTransitionEnqueued(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();

	UGameFlowTransitionKey* transitionKey = operationInfo.TransitionKey.Get();

	flow->MakeTransition(transitionKey, true, false);
}

void UGameFlow::OnReset(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (operationInfo.ResetSubFlow)
	{
		nextOperationId = OperationFactory::CreateResetSubFlowsOperation(operationInfo.ActiveState, flow, operationInfo.NextOperationId);
	}

	if (flow->ActiveState.IsValid())
	{
		nextOperationId = OperationFactory::CreateExitStateOperation(operationInfo.ActiveState, flow, nextOperationId, operationInfo.ExecuteSteps, true);
	}

	ExecuteOperation(operationId, nextOperationId);
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
		const OperationId resetOperationId = GetOperationId() | EOperationType::Reset;
		FOperationInfo resetOperationInfo = FOperationInfo(subFlow->ActiveState, subFlow, subFlow->ActiveState, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(resetOperationId, resetOperationInfo);

		nextOperationId = resetOperationId;
	}

	ExecuteOperation(operationId, nextOperationId);
}

void UGameFlow::OnTransactionBegin(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnTransactionEnd(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	ExecuteOperation(operationId, operationInfo.NextOperationId);
}

void UGameFlow::OnCancel_State_Steps(const OperationId operationId, const OperationId nextOperationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(EOperationType::Cancel_Steps, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	for (int32 i = stateObject->Steps.Num() - 1; i >= 0; i--)
	{
		if (stateObject->Steps[i])
		{
			stateObject->Steps[i]->ActiveOperationType = EOperationType::Reset;

			FOperationInfo& catchingOperationInfo = OperationContext[stateObject->Steps[i]->CatchingOperationId];
			catchingOperationInfo.NextOperationId = nextOperationId;

			stateObject->Steps[i]->OnCancel();
		}
	}
}

void UGameFlow::OnCancel_State_SubFlow(const OperationId operationId, const OperationId nextOperationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(EOperationType::Cancel_SubFlow, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (!stateObject->bInstancedSubFlow && stateObject->SubFlow->ActiveState.IsValid())
	{
		stateObject->SubFlow->ResetFlow(stateObject->SubFlow->ActiveState, operationInfo.ResetSubFlow, nextOperationId);
	}
	else
	{
		ExecuteOperation(operationId, nextOperationId);
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

bool UGameFlow::IsTransitioning() const { return ActiveOperationId != OperationId(); }

void UGameFlow::EnterFlow(const bool executeSteps)
{
	if (!ActiveOperationId)
	{
		if (!ActiveState.IsValid())
		{
			if (EntryState.IsValid())
			{
				if (States.Contains(EntryState))
				{
					const OperationId transactionEndOperationId = OperationFactory::CreateTransactionEndOperation(ActiveState, this, OperationId());
					const OperationId enterStateOperationId = OperationFactory::CreateEnterStateOperation(ActiveState, this, EntryState, transactionEndOperationId, executeSteps, false);
					const OperationId transactionBeginOperationId = OperationFactory::CreateTransactionBeginOperation(ActiveState, this, enterStateOperationId);

					ExecuteOperation(OperationId(), transactionBeginOperationId);
				}
				else
				{
					UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *EntryState.ToString(), *GetName());
				}
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find Entry state in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant enter flow that is active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant enter flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

void UGameFlow::ExitFlow(const bool executeSteps, const bool resetActiveSubFlow)
{
	if (!ActiveOperationId)
	{
		if (ActiveState.IsValid())
		{
			const OperationId transactionEndOperationId = OperationFactory::CreateTransactionEndOperation(ActiveState, this, OperationId());
			const OperationId exitStateOperationId = OperationFactory::CreateExitStateOperation(ActiveState, this, transactionEndOperationId, executeSteps, resetActiveSubFlow);
			const OperationId transactionBeginOperationId = OperationFactory::CreateTransactionBeginOperation(ActiveState, this, exitStateOperationId);

			ExecuteOperation(OperationId(), transactionBeginOperationId);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is not active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

void UGameFlow::MakeTransition(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool isEnqueued)
{
	if (!ActiveOperationId)
	{
		if (const OperationId operationId = MakeTransition_Internal(transitionKey, executeSteps, false))
		{
			ExecuteOperation(OperationId(), operationId);
		}
	}
	else if (isEnqueued)
	{
		if (const OperationId operationId = MakeTransition_Internal(transitionKey, executeSteps, true))
		{
			OperationId lastOperationId = ActiveOperationId;

			while (lastOperationId && OperationContext.Contains(lastOperationId) && OperationContext[lastOperationId].NextOperationId)
			{
				lastOperationId = OperationContext[lastOperationId].NextOperationId;
			}

			if (OperationContext.Contains(lastOperationId))
			{
				OperationContext[lastOperationId].NextOperationId = operationId;
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant find operation [%d]!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), lastOperationId);
			}
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant make transition in flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

void UGameFlow::ResetFlow(FGuid& activeState, const bool resetAnySubFlow, const OperationId nextOperationId)
{
	const OperationId transactionEndOperationId = OperationFactory::CreateTransactionEndOperation(ActiveState, this, nextOperationId);
	const OperationId exitStateOperationId = OperationFactory::CreateResetOperation(ActiveState, this, transactionEndOperationId, resetAnySubFlow);
	const OperationId transactionBeginOperationId = OperationFactory::CreateTransactionBeginOperation(ActiveState, this, exitStateOperationId);

	if (!ActiveOperationId)
	{
		ExecuteOperation(OperationId(), transactionBeginOperationId);
	}
	else
	{
		CancelOperation(ActiveOperationId, transactionBeginOperationId);
	}
}

const OperationId UGameFlow::MakeTransition_Internal(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool isEnqueued)
{
	if (transitionKey)
	{
		if (ActiveState.IsValid())
		{
			if (isEnqueued)
			{
				return OperationFactory::CreateTransitionEnqueuedOperation(ActiveState, this, OperationId(), executeSteps, false, transitionKey);
			}
			else
			{
				const OperationId transactionEndOperationId = OperationFactory::CreateTransactionEndOperation(ActiveState, this, OperationId());
				const OperationId transitionOperationId = CreateTransitionOperation(transitionKey, transactionEndOperationId, executeSteps, false);
				const OperationId transactionBeginOperationId = OperationFactory::CreateTransactionBeginOperation(ActiveState, this, transitionOperationId);

				ExecuteOperation(OperationId(), transactionBeginOperationId);
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow that is not active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow without Transition Key {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}

	return OperationId();
}

OperationId UGameFlow::CreateTransitionOperation(UGameFlowTransitionKey* transitionKey, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
{
	if (States[ActiveState]->SubFlow)
	{
		// Try to find transition in Sub Flow

		if (const OperationId subFlowTransitionId = States[ActiveState]->SubFlow->CreateTransitionOperation(transitionKey, nextOperationId, executeSteps, resetActiveSubFlow))
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

				const OperationId enterStateOperationId = OperationFactory::CreateEnterStateOperation(ActiveState, this, transitionEntry.Key, nextOperationId, executeSteps, resetActiveSubFlow);
				const OperationId exitStateOperationId = OperationFactory::CreateExitStateOperation(ActiveState, this, enterStateOperationId, executeSteps, resetActiveSubFlow);

				return exitStateOperationId;
			}
		}
	}

	return OperationId();
}

void UGameFlow::SetWorldPtr(FGuid& activeState, UWorld* world, const bool force)
{
	if (!ActiveOperationId || force)
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
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant set world in flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}