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

	if (operationType == EOperationType::TransitionComplete) result = "TransitionComplete";

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

				if (operationType == EOperationType::TransitionComplete) return OnTransitionComplete(operationId);
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
		const OperationId autoTransitionOperationId = GetOperationId() | EOperationType::AutoTransition;
		FOperationInfo autoTransitionOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(autoTransitionOperationId, autoTransitionOperationInfo);

		nextOperationId = autoTransitionOperationId;
	}

	if (stateObject->SubFlow && (stateObject->bInstancedSubFlow || !stateObject->SubFlow->ActiveState.IsValid()))
	{
		const OperationId subFlowOperationId = GetOperationId() | EOperationType::EnterState_SubFlow;
		FOperationInfo subFlowOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(subFlowOperationId, subFlowOperationInfo);

		nextOperationId = GetOperationId() | EOperationType::EnterState_SubFlow_Set;
		FOperationInfo enterStateSubFlowSetOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, subFlowOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(nextOperationId, enterStateSubFlowSetOperationInfo);
	}

	if (operationInfo.ExecuteSteps)
	{
		const OperationId stepsOperationId = GetOperationId() | EOperationType::EnterState_Steps;
		FOperationInfo stepsOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(stepsOperationId, stepsOperationInfo);

		const OperationId setOperation = GetOperationId() | EOperationType::EnterState_Set;
		FOperationInfo setOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, stepsOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(setOperation, setOperationInfo);

		ExecuteOperation(operationId, setOperation);
	}
	else
	{
		const OperationId setOperation = GetOperationId() | EOperationType::EnterState_Set;
		FOperationInfo setOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(setOperation, setOperationInfo);

		ExecuteOperation(operationId, setOperation);
	}
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
		const OperationId catchingOperationId = GetOperationId() | EOperationType::CatchingOperation;
		FOperationInfo& catchingOperationInfo = OperationContext.Emplace(catchingOperationId, FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr));

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

			const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
			FOperationInfo enterStateOperationInfo = FOperationInfo(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

			const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
			FOperationInfo exitStateOperationInfo = FOperationInfo(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(operationId, exitStateOperationId);
		}
		else
		{
			// Enter Sub Flow

			const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
			FOperationInfo enterStateOperationInfo = FOperationInfo(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

			ExecuteOperation(operationId, enterStateOperationId);
		}
	}
	else // Shared
	{
		if (stateObject->bResetSubFlowOnEnterState)
		{
			if (stateObject->SubFlow->ActiveState.IsValid())
			{
				// Exit Sub Flow and then Enter Sub Flow

				const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
				FOperationInfo enterStateOperationInfo = FOperationInfo(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
				FOperationInfo exitStateOperationInfo = FOperationInfo(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

				ExecuteOperation(operationId, exitStateOperationId);
			}
			else
			{
				// Enter Sub Flow

				const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
				FOperationInfo enterStateOperationInfo = FOperationInfo(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				ExecuteOperation(operationId, enterStateOperationId);
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

				const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
				FOperationInfo enterStateOperationInfo = FOperationInfo(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				ExecuteOperation(operationId, enterStateOperationId);
			}
		}
	}
}

void UGameFlow::OnExitState(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	const OperationId setOperationId = GetOperationId() | EOperationType::ExitState_Set;
	FOperationInfo setOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
	OperationContext.Add(setOperationId, setOperationInfo);

	if (operationInfo.ExecuteSteps)
	{
		const OperationId stepsOperationId = GetOperationId() | EOperationType::ExitState_Steps;
		FOperationInfo stepsOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(stepsOperationId, stepsOperationInfo);

		if (stateObject->SubFlow && stateObject->bSubFlow_Set)
		{
			const OperationId exitStateSubFlowSetOperationId = GetOperationId() | EOperationType::ExitState_SubFlow_Set;
			FOperationInfo exitStateSubFlowSetOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, stepsOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(exitStateSubFlowSetOperationId, exitStateSubFlowSetOperationInfo);

			if (stateObject->bInstancedSubFlow && stateObject->SubFlowActiveState.IsValid() || stateObject->SubFlow->ActiveState.IsValid())
			{
				const OperationId subFlowOperationId = GetOperationId() | EOperationType::ExitState_SubFlow;
				FOperationInfo subFlowOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(subFlowOperationId, subFlowOperationInfo);
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
			const OperationId exitStateSubFlowSetOperationId = GetOperationId() | EOperationType::ExitState_SubFlow_Set;
			FOperationInfo exitStateSubFlowSetOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(exitStateSubFlowSetOperationId, exitStateSubFlowSetOperationInfo);

			if (stateObject->bInstancedSubFlow && stateObject->SubFlowActiveState.IsValid() || stateObject->SubFlow->ActiveState.IsValid())
			{
				const OperationId subFlowOperationId = GetOperationId() | EOperationType::ExitState_SubFlow;
				FOperationInfo subFlowOperationInfo = FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
				OperationContext.Add(subFlowOperationId, subFlowOperationInfo);

				ExecuteOperation(operationId, subFlowOperationId);
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

			const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
			FOperationInfo exitStateOperationInfo = FOperationInfo(stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(operationId, exitStateOperationId);
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

			const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
			FOperationInfo exitStateOperationInfo = FOperationInfo(stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(operationId, exitStateOperationId);
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
		const OperationId catchingOperationId = GetOperationId() | EOperationType::CatchingOperation;
		FOperationInfo& catchingOperationInfo = OperationContext.Emplace(catchingOperationId, FOperationInfo(operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr));

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
			const OperationId autoTransitionOperationId = flow->CreateMakeTransitionOperation(stateObject->TransitionKey, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow);

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
		nextOperationId = GetOperationId() | EOperationType::ResetSubFlows;
		FOperationInfo resetSubFlowsOperationInfo = FOperationInfo(operationInfo.ActiveState, flow, operationInfo.ActiveState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSubFlow, nullptr);
		OperationContext.Add(nextOperationId, resetSubFlowsOperationInfo);
	}

	if (flow->ActiveState.IsValid())
	{
		const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
		FOperationInfo exitStateOperationInfo = FOperationInfo(operationInfo.ActiveState, flow, operationInfo.ActiveState, nextOperationId, operationInfo.ExecuteSteps, true, nullptr);
		OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

		nextOperationId = exitStateOperationId;
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

void UGameFlow::OnTransitionComplete(const OperationId operationId)
{
	const FOperationInfo& operationInfo = OperationContext[operationId];

	LogOperation(operationId, operationInfo);

	UGameFlow* flow = operationInfo.Flow.Get();

	flow->ActiveOperationId = OperationId();

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
					const OperationId transitionCompleteOperationId = GetOperationId() | EOperationType::TransitionComplete;
					FOperationInfo transitionCompleteOperationInfo = FOperationInfo(ActiveState, this, EntryState, OperationId(), false, false, nullptr);
					OperationContext.Add(transitionCompleteOperationId, transitionCompleteOperationInfo);

					const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
					FOperationInfo enterStateOperationInfo = FOperationInfo(ActiveState, this, EntryState, transitionCompleteOperationId, executeSteps, false, nullptr);
					OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

					ExecuteOperation(OperationId(), enterStateOperationId);
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
			if (States.Contains(ActiveState))
			{
				const OperationId transitionCompleteOperationId = GetOperationId() | EOperationType::TransitionComplete;
				FOperationInfo transitionCompleteOperationInfo = FOperationInfo(ActiveState, this, EntryState, OperationId(), false, false, nullptr);
				OperationContext.Add(transitionCompleteOperationId, transitionCompleteOperationInfo);

				const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
				FOperationInfo exitStateOperationInfo = FOperationInfo(ActiveState, this, ActiveState, transitionCompleteOperationId, executeSteps, resetActiveSubFlow, nullptr);
				OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

				ExecuteOperation(OperationId(), exitStateOperationId);
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *ActiveState.ToString(), *GetName());
			}
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
	const OperationId transitionCompleteOperationId = GetOperationId() | EOperationType::TransitionComplete;
	FOperationInfo transitionCompleteOperationInfo = FOperationInfo(activeState, this, EntryState, nextOperationId, false, false, nullptr);
	OperationContext.Add(transitionCompleteOperationId, transitionCompleteOperationInfo);

	const OperationId resetOperationId = GetOperationId() | EOperationType::Reset;
	FOperationInfo resetOperationInfo = FOperationInfo(activeState, this, activeState, transitionCompleteOperationId, false, resetAnySubFlow, nullptr);
	OperationContext.Add(resetOperationId, resetOperationInfo);

	if (!ActiveOperationId)
	{
		ExecuteOperation(OperationId(), resetOperationId);
	}
	else if (OperationContext.Contains(ActiveOperationId))
	{
		CancelOperation(ActiveOperationId, resetOperationId);
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][OPER]%s - Cant find operation [%d]!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), ActiveOperationId);
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
				const OperationId makeTransitionEnqueuedOperationId = GetOperationId() | EOperationType::MakeTransition_Enqueued;
				FOperationInfo makeTransitionEnqueuedOperationInfo = FOperationInfo(ActiveState, this, ActiveState, OperationId(), executeSteps, false, transitionKey);
				OperationContext.Add(makeTransitionEnqueuedOperationId, makeTransitionEnqueuedOperationInfo);

				return makeTransitionEnqueuedOperationId;
			}
			else
			{
				const OperationId transitionCompleteOperationId = GetOperationId() | EOperationType::TransitionComplete;
				FOperationInfo transitionCompleteOperationInfo = FOperationInfo(ActiveState, this, EntryState, OperationId(), false, false, nullptr);
				OperationContext.Add(transitionCompleteOperationId, transitionCompleteOperationInfo);

				return CreateMakeTransitionOperation(transitionKey, transitionCompleteOperationId, executeSteps, false);
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

OperationId UGameFlow::CreateMakeTransitionOperation(UGameFlowTransitionKey* transitionKey, const OperationId nextOperationId, const bool executeSteps, const bool resetActiveSubFlow)
{
	if (States[ActiveState]->SubFlow)
	{
		// Try to find transition in Sub Flow

		if (const OperationId subFlowTransitionId = States[ActiveState]->SubFlow->CreateMakeTransitionOperation(transitionKey, nextOperationId, executeSteps, resetActiveSubFlow))
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

				const OperationId enterStateOperationId = GetOperationId() | EOperationType::EnterState;
				FOperationInfo enterStateOperationInfo = FOperationInfo(ActiveState, this, transitionEntry.Key, nextOperationId, executeSteps, resetActiveSubFlow, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				const OperationId exitStateOperationId = GetOperationId() | EOperationType::ExitState;
				FOperationInfo exitStateOperationInfo = FOperationInfo(ActiveState, this, ActiveState, enterStateOperationId, executeSteps, resetActiveSubFlow, nullptr);
				OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

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