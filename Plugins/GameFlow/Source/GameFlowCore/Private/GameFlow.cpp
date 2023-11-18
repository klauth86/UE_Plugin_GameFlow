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
	
	counter++;

	if (counter == 0)
	{
		counter++;
	}

	return counter;
}

//------------------------------------------------------
// FOperationInfo
//------------------------------------------------------

void FOperationInfo::ReportStepStatus(const UGFS_Base* step, const EGFSStatus status)
{
	check(OperationType == EOperationType::StepsCatcher);

	UGameFlow* flow = Flow.Get();
	const UGameFlowState* stateObject = flow->GetStateObject(State);

	const int32 stepIndex = stateObject->Steps.IndexOfByKey(step);

	if (stepIndex != INDEX_NONE)
	{
		if (StepIndices.Contains(stepIndex))
		{
			if (status == EGFSStatus::Started)
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][STEP]%s - BEG Step [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else if (status == EGFSStatus::Finished)
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][STEP]%s - END Step [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());

				StepIndices.Remove(stepIndex);

				if (StepIndices.IsEmpty())
				{
					UGameFlow::ExecuteOperation(step->StepsCatcherOperationId);
				}
			}
			else if (status == EGFSStatus::Failed)
			{
				UE_LOG(LogGameFlow, Warning, TEXT("[%s][STEP]%s - ERR Step [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
			}
			else
			{
				// Nothing to do here
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][STEP]%s - Cant find step [%s] in catching operation for flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *flow->GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][STEP]%s - Cant find step [%s] in state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *step->GenerateDescription().ToString(), *stateObject->StateTitle.ToString(), *flow->GetName());
	}
}

//------------------------------------------------------
// UGFS_Base
//------------------------------------------------------

void UGFS_Base::OnComplete(const EGFSStatus status) const
{
	if (StepsCatcherOperationId)
	{
		if (OperationContext.Contains(StepsCatcherOperationId))
		{
			OperationContext[StepsCatcherOperationId].ReportStepStatus(this, status);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][STEP]%s - Cant exec step [%s] with invalid catching operation for flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GenerateDescription().ToString(), *GetOwningState()->GetOwningFlow()->GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][STEP]%s - Cant exec step [%s] without catching operation for flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GenerateDescription().ToString(), *GetOwningState()->GetOwningFlow()->GetName());
	}
}

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
	ExitTransitionOperationId = OperationId();
}

void UGameFlow::ExecuteOperation(const OperationId& operationId)
{
	if (operationId)
	{
		if (OperationContext.Contains(operationId))
		{
			FOperationInfo operationInfo = OperationContext.FindAndRemoveChecked(operationId);

			if (UGameFlow* flow = operationInfo.Flow.Get())
			{
				UE_LOG(LogGameFlow, Log, TEXT("[%s][OPER]%s - [%s] {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth + 1), *StaticEnum<EOperationType>()->GetNameStringByValue(static_cast<int64>(operationInfo.OperationType)), *flow->GetName());

				switch (operationInfo.OperationType)
				{
				case EOperationType::EnterState: return OnEnterState(operationInfo);
				case EOperationType::EnterState_Set: return OnEnterState_Set(operationInfo);
				case EOperationType::EnterState_Steps: return OnEnterState_Steps(operationInfo);
				case EOperationType::EnterState_SubFlow_Set: return OnEnterState_SubFlow_Set(operationInfo);
				case EOperationType::EnterState_SubFlow: return OnEnterState_SubFlow(operationInfo);

				case EOperationType::AutoTransition: return OnAutoTransition(operationInfo);

				case EOperationType::ExitState: return OnExitState(operationInfo);
				case EOperationType::ExitState_SubFlow: return OnExitState_SubFlow(operationInfo);
				case EOperationType::ExitState_SubFlow_Set: return OnExitState_SubFlow_Set(operationInfo);
				case EOperationType::ExitState_Steps: return OnExitState_Steps(operationInfo);
				case EOperationType::ExitState_Set: return OnExitState_Set(operationInfo);
				case EOperationType::StepsCatcher: return OnStepsCatcher(operationInfo);

				case EOperationType::EnterTransition: return OnEnterTransition(operationId, operationInfo);
				case EOperationType::ExitTransition: return OnExitTransition(operationId, operationInfo);

				case EOperationType::MakeTransition_Enqueued: return OnMakeTransitionEnqueued(operationInfo);
				}
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

void UGameFlow::OnEnterState(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	OperationId nextOperationId = operationInfo.NextOperationId;

	if (stateObject->TransitionKey)
	{
		const OperationId autoTransitionOperationId = GetOperationId();
		FOperationInfo autoTransitionOperationInfo = FOperationInfo(EOperationType::AutoTransition, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(autoTransitionOperationId, autoTransitionOperationInfo);

		nextOperationId = autoTransitionOperationId;
	}

	if (stateObject->SubFlow)
	{
		const OperationId subFlowOperationId = GetOperationId();
		FOperationInfo subFlowOperationInfo = FOperationInfo(EOperationType::EnterState_SubFlow, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(subFlowOperationId, subFlowOperationInfo);

		nextOperationId = GetOperationId();
		FOperationInfo enterStateSubFlowSetOperationInfo = FOperationInfo(EOperationType::EnterState_SubFlow_Set, flow->ActiveState, operationInfo.Flow, operationInfo.State, subFlowOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(nextOperationId, enterStateSubFlowSetOperationInfo);
	}

	if (operationInfo.ExecuteSteps)
	{
		const OperationId stepsOperationId = GetOperationId();
		FOperationInfo stepsOperationInfo = FOperationInfo(EOperationType::EnterState_Steps, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(stepsOperationId, stepsOperationInfo);

		const OperationId setOperation = GetOperationId();
		FOperationInfo setOperationInfo = FOperationInfo(EOperationType::EnterState_Set, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, stepsOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(setOperation, setOperationInfo);

		ExecuteOperation(setOperation);
	}
	else
	{
		const OperationId setOperation = GetOperationId();
		FOperationInfo setOperationInfo = FOperationInfo(EOperationType::EnterState_Set, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, nextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(setOperation, setOperationInfo);

		ExecuteOperation(setOperation);
	}
}

void UGameFlow::OnEnterState_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	LogGameFlowUtils::Depth++;
	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s--> %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());

	operationInfo.ActiveState = operationInfo.State;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_Steps(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const OperationId stepsCatcherOperationId = GetOperationId();
		FOperationInfo& stepsCatcherOperationInfo = OperationContext.Emplace(stepsCatcherOperationId, FOperationInfo(EOperationType::StepsCatcher, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr));

		for (int32 i = 0; i < stateObject->Steps.Num(); i++)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.StepIndices.Add(i);
			}
		}

		for (int32 i = 0; i < stateObject->Steps.Num(); i++)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

				stateObject->Steps[i]->StepsCatcherOperationId = stepsCatcherOperationId;
				stateObject->Steps[i]->OnEnter();
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperationId);
	}
}

void UGameFlow::OnEnterState_SubFlow_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	LogGameFlowUtils::Depth++;
	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s==> %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->SubFlow->GetName(), *flow->GetName());

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnEnterState_SubFlow(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow and then enter Sub Flow

			const OperationId enterStateOperationId = GetOperationId();
			FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

			const OperationId exitStateOperationId = GetOperationId();
			FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(exitStateOperationId);
		}
		else
		{
			// Enter Sub Flow
			const OperationId enterStateOperationId = GetOperationId();
			FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

			ExecuteOperation(enterStateOperationId);
		}
	}
	else // Shared
	{
		if (stateObject->bResetSubFlowOnEnterState)
		{
			if (stateObject->SubFlow->ActiveState.IsValid())
			{
				// Exit Sub Flow and then Enter Sub Flow

				const OperationId enterStateOperationId = GetOperationId();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				const OperationId exitStateOperationId = GetOperationId();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, enterStateOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
				OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

				ExecuteOperation(exitStateOperationId);
			}
			else
			{
				// Enter Sub Flow

				const OperationId enterStateOperationId = GetOperationId();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				ExecuteOperation(enterStateOperationId);
			}
		}
		else
		{
			if (stateObject->SubFlow->ActiveState.IsValid())
			{
				ExecuteOperation(operationInfo.NextOperationId);
			}
			else
			{
				// Enter Sub Flow

				const OperationId enterStateOperationId = GetOperationId();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				ExecuteOperation(enterStateOperationId);
			}
		}
	}
}

void UGameFlow::OnExitState(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	const OperationId setOperationId = GetOperationId();
	FOperationInfo setOperationInfo = FOperationInfo(EOperationType::ExitState_Set, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
	OperationContext.Add(setOperationId, setOperationInfo);

	if (operationInfo.ExecuteSteps)
	{
		const OperationId stepsOperationId = GetOperationId();
		FOperationInfo stepsOperationInfo = FOperationInfo(EOperationType::ExitState_Steps, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
		OperationContext.Add(stepsOperationId, stepsOperationInfo);

		if (stateObject->SubFlow)
		{
			const OperationId exitStateSubFlowSetOperationId = GetOperationId();
			FOperationInfo exitStateSubFlowSetOperationInfo = FOperationInfo(EOperationType::ExitState_SubFlow_Set, flow->ActiveState, operationInfo.Flow, operationInfo.State, stepsOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateSubFlowSetOperationId, exitStateSubFlowSetOperationInfo);

			const OperationId subFlowOperationId = GetOperationId();
			FOperationInfo subFlowOperationInfo = FOperationInfo(EOperationType::ExitState_SubFlow, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(subFlowOperationId, subFlowOperationInfo);

			ExecuteOperation(subFlowOperationId);
		}
		else
		{
			ExecuteOperation(stepsOperationId);
		}
	}
	else
	{
		if (stateObject->SubFlow)
		{
			const OperationId exitStateSubFlowSetOperationId = GetOperationId();
			FOperationInfo exitStateSubFlowSetOperationInfo = FOperationInfo(EOperationType::ExitState_SubFlow_Set, flow->ActiveState, operationInfo.Flow, operationInfo.State, setOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateSubFlowSetOperationId, exitStateSubFlowSetOperationInfo);

			const OperationId subFlowOperationId = GetOperationId();
			FOperationInfo subFlowOperationInfo = FOperationInfo(EOperationType::ExitState_SubFlow, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, exitStateSubFlowSetOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(subFlowOperationId, subFlowOperationInfo);

			ExecuteOperation(subFlowOperationId);
		}
		else
		{
			ExecuteOperation(setOperationId);
		}
	}
}

void UGameFlow::OnExitState_SubFlow(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->bInstancedSubFlow) // Instanced
	{
		if (stateObject->SubFlowActiveState.IsValid())
		{
			// Exit Sub Flow

			const OperationId exitStateOperationId = GetOperationId();
			FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(exitStateOperationId);
		}
		else
		{
			ExecuteOperation(operationInfo.NextOperationId);
		}
	}
	else // Shared
	{
		if ((stateObject->bResetSubFlowOnEnterState || operationInfo.ResetSharedSubFlows) && stateObject->SubFlow->ActiveState.IsValid())
		{
			// Exit Sub Flow

			const OperationId exitStateOperationId = GetOperationId();
			FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			ExecuteOperation(exitStateOperationId);
		}
		else
		{
			ExecuteOperation(operationInfo.NextOperationId);
		}
	}
}

void UGameFlow::OnExitState_SubFlow_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<== %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->SubFlow->GetName(), *flow->GetName());
	LogGameFlowUtils::Depth--;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnExitState_Steps(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const OperationId stepsCatcherOperationId = GetOperationId();
		FOperationInfo& stepsCatcherOperationInfo = OperationContext.Emplace(stepsCatcherOperationId, FOperationInfo(EOperationType::StepsCatcher, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows, nullptr));

		for (int32 i = stateObject->Steps.Num() - 1; i >= 0; i--)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.StepIndices.Add(i);
			}
		}

		for (int32 i = stateObject->Steps.Num() - 1; i >= 0; i--)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

				stateObject->Steps[i]->StepsCatcherOperationId = stepsCatcherOperationId;
				stateObject->Steps[i]->OnExit();
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperationId);
	}
}

void UGameFlow::OnExitState_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	operationInfo.ActiveState.Invalidate();

	UE_LOG(LogGameFlow, Log, TEXT("[%s][FLOW]%s<-- %s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());
	LogGameFlowUtils::Depth--;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnAutoTransition(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->TransitionKey)
	{
		if (operationInfo.ActiveState.IsValid())
		{
			const OperationId autoTransitionOperationId = flow->CreateMakeTransitionOperation(stateObject->TransitionKey, operationInfo.NextOperationId, operationInfo.ExecuteSteps, operationInfo.ResetSharedSubFlows);

			ExecuteOperation(autoTransitionOperationId);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant transition in flow that is not active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *flow->GetName());
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperationId);
	}
}

void UGameFlow::OnEnterTransition(const OperationId operationId, const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = operationInfo.ActiveState.IsValid() ? flow->States[operationInfo.ActiveState] : nullptr;

	OperationId exitTransitionOpertionId = operationInfo.NextOperationId;

	while (OperationContext[exitTransitionOpertionId].NextOperationId)
	{
		exitTransitionOpertionId = OperationContext[exitTransitionOpertionId].NextOperationId;
	}

	flow->ExitTransitionOperationId = exitTransitionOpertionId;

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnExitTransition(const OperationId operationId, const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = operationInfo.ActiveState.IsValid() ? flow->States[operationInfo.ActiveState] : nullptr;

	flow->ExitTransitionOperationId = OperationId();

	ExecuteOperation(operationInfo.NextOperationId);
}

void UGameFlow::OnMakeTransitionEnqueued(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowTransitionKey* transitionKey = operationInfo.TransitionKey.Get();

	flow->MakeTransition(transitionKey, true, false);
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

bool UGameFlow::IsTransitioning() const { if (ExitTransitionOperationId) { return true; } else { return false; } }

void UGameFlow::EnterFlow(const bool executeSteps)
{
	if (!ExitTransitionOperationId)
	{
		EnterFlow(ActiveState, OperationId(), executeSteps);
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant enter flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

void UGameFlow::ExitFlow(const bool executeSteps, const bool resetSharedSubFlows)
{
	if (!ExitTransitionOperationId)
	{
		ExitFlow(ActiveState, OperationId(), executeSteps, resetSharedSubFlows);
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

void UGameFlow::MakeTransition(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool executeAsQueued)
{
	if (!ExitTransitionOperationId)
	{
		if (const OperationId operationId = MakeTransition_Internal(transitionKey, executeSteps, false))
		{
			ExecuteOperation(operationId);
		}
	}
	else if (executeAsQueued)
	{
		if (const OperationId operationId = MakeTransition_Internal(transitionKey, executeSteps, true))
		{
			OperationContext[ExitTransitionOperationId].NextOperationId = operationId;
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant make transition in flow that is transitioning {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());
	}
}

const OperationId UGameFlow::MakeTransition_Internal(UGameFlowTransitionKey* transitionKey, const bool executeSteps, const bool executeAsQueued)
{
	if (transitionKey)
	{
		if (ActiveState.IsValid())
		{
			if (executeAsQueued)
			{
				const OperationId makeEnqueuedTransitionOperationId = GetOperationId();
				FOperationInfo makeEnqueuedTransitionOperationInfo = FOperationInfo(EOperationType::MakeTransition_Enqueued, ActiveState, this, ActiveState, OperationId(), executeSteps, false, transitionKey);
				OperationContext.Add(makeEnqueuedTransitionOperationId, makeEnqueuedTransitionOperationInfo);

				return makeEnqueuedTransitionOperationId;
			}
			else
			{
				const OperationId exitTransitionOperationId = GetOperationId();
				FOperationInfo exitTransitionOperationInfo = FOperationInfo(EOperationType::ExitTransition, ActiveState, this, ActiveState, OperationId(), executeSteps, false, nullptr);
				OperationContext.Add(exitTransitionOperationId, exitTransitionOperationInfo);

				const OperationId makeTransitionOperationId = CreateMakeTransitionOperation(transitionKey, exitTransitionOperationId, executeSteps, false);

				const OperationId enterTransitionOperationId = GetOperationId();
				FOperationInfo enterTransitionOperationInfo = FOperationInfo(EOperationType::EnterTransition, ActiveState, this, ActiveState, makeTransitionOperationId, executeSteps, false, nullptr);
				OperationContext.Add(enterTransitionOperationId, enterTransitionOperationInfo);

				return enterTransitionOperationId;
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

void UGameFlow::EnterFlow(FGuid& activeState, const OperationId& nextOperationId, const bool executeSteps)
{
	if (!activeState.IsValid())
	{
		if (EntryState.IsValid())
		{
			if (States.Contains(EntryState))
			{
				const OperationId exitTransitionOperationId = GetOperationId();
				FOperationInfo exitTransitionOperationInfo = FOperationInfo(EOperationType::ExitTransition, activeState, this, activeState, nextOperationId, executeSteps, false, nullptr);
				OperationContext.Add(exitTransitionOperationId, exitTransitionOperationInfo);

				const OperationId enterStateOperationId = GetOperationId();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, activeState, this, EntryState, exitTransitionOperationId, executeSteps, false, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				const OperationId enterTransitionOperationId = GetOperationId();
				FOperationInfo enterTransitionOperationInfo = FOperationInfo(EOperationType::EnterTransition, activeState, this, activeState, enterStateOperationId, executeSteps, false, nullptr);
				OperationContext.Add(enterTransitionOperationId, enterTransitionOperationInfo);

				ExecuteOperation(enterTransitionOperationId);
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

		ExecuteOperation(nextOperationId);
	}
}

void UGameFlow::ExitFlow(FGuid& activeState, const OperationId& nextOperationId, const bool executeSteps, const bool resetSharedSubFlows)
{
	if (activeState.IsValid())
	{
		if (States.Contains(activeState))
		{
			const OperationId exitTransitionOperationId = GetOperationId();
			FOperationInfo exitTransitionOperationInfo = FOperationInfo(EOperationType::ExitTransition, activeState, this, activeState, nextOperationId, executeSteps, resetSharedSubFlows, nullptr);
			OperationContext.Add(exitTransitionOperationId, exitTransitionOperationInfo);

			const OperationId exitStateOperationId = GetOperationId();
			FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, activeState, this, activeState, exitTransitionOperationId, executeSteps, resetSharedSubFlows, nullptr);
			OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

			const OperationId enterTransitionOperationId = GetOperationId();
			FOperationInfo enterTransitionOperationInfo = FOperationInfo(EOperationType::EnterTransition, activeState, this, activeState, exitStateOperationId, executeSteps, resetSharedSubFlows, nullptr);
			OperationContext.Add(enterTransitionOperationId, enterTransitionOperationInfo);

			ExecuteOperation(enterTransitionOperationId);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant find state [%s] in flow {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *activeState.ToString(), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("[%s][FLOW]%sCant exit flow that is not active {%s}!"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());

		ExecuteOperation(nextOperationId);
	}
}

OperationId UGameFlow::CreateMakeTransitionOperation(UGameFlowTransitionKey* transitionKey, const OperationId& nextOperationId, const bool executeSteps, const bool resetSharedSubFlows)
{
	if (States[ActiveState]->SubFlow)
	{
		// Try to find transition in Sub Flow

		if (const OperationId subFlowTransitionId = States[ActiveState]->SubFlow->CreateMakeTransitionOperation(transitionKey, nextOperationId, executeSteps, resetSharedSubFlows))
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

				const OperationId enterStateOperationId = GetOperationId();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, ActiveState, this, transitionEntry.Key, nextOperationId, executeSteps, resetSharedSubFlows, nullptr);
				OperationContext.Add(enterStateOperationId, enterStateOperationInfo);

				const OperationId exitStateOperationId = GetOperationId();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, ActiveState, this, ActiveState, enterStateOperationId, executeSteps, resetSharedSubFlows, nullptr);
				OperationContext.Add(exitStateOperationId, exitStateOperationInfo);

				return exitStateOperationId;
			}
		}
	}

	return OperationId();
}

void UGameFlow::SetWorldPtr(FGuid& activeState, UWorld* world, const bool force)
{
	if (!ExitTransitionOperationId || force)
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