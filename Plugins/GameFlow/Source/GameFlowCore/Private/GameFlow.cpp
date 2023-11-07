// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "GameFlow.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogGameFlow);

int32 LogGameFlowUtils::Depth = 0;

TMap<FGuid, FOperationInfo> OperationContext;

FString LogGameFlowUtils::RepeatTab(int32 num)
{
	FString result;
	for (int32 i = 0; i < num; i++) result.AppendChar(*"\t");
	return result;
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
			if (status == EGFSStatus::Done)
			{
				StepIndices.Remove(stepIndex);

				if (StepIndices.IsEmpty())
				{
					UGameFlow::ExecuteOperation(step->StepsCatcherOperation);
				}
			}
			else if (status == EGFSStatus::Failed)
			{
				UE_LOG(LogGameFlow, Warning, TEXT("Failed to exec step [%s] in state [%s]!"), *step->GetName(), *stateObject->GetName());
			}
			else
			{
				// Nothing to do here
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant find step [%s] in catching operation!"), *step->GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant find step [%s] in state [%s]!"), *step->GetName(), *stateObject->GetName());
	}
}

//------------------------------------------------------
// UGFS_Base
//------------------------------------------------------

void UGFS_Base::OnComplete(const EGFSStatus status) const
{
	if (StepsCatcherOperation.IsValid())
	{
		if (OperationContext.Contains(StepsCatcherOperation))
		{
			OperationContext[StepsCatcherOperation].ReportStepStatus(this, status);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant find operation [%s]!"), *StepsCatcherOperation.ToString());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant exec step [%s] without catching operation!"), *GetName());
	}
}

//------------------------------------------------------
// UGameFlow
//------------------------------------------------------

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

bool UGameFlow::MakeTransition(UGameFlowTransitionKey* transitionKey)
{
	if (transitionKey)
	{
		if (ActiveState.IsValid())
		{
			if (States[ActiveState]->SubFlow)
			{
				// Try to find transition in Sub Flow

				if (States[ActiveState]->SubFlow->MakeTransition(transitionKey))
				{
					return true;
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

						const FGuid enterStateOperation = FGuid::NewGuid();
						FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, ActiveState, this, transitionEntry.Key, FGuid());
						OperationContext.Add(enterStateOperation, enterStateOperationInfo);

						const FGuid exitStateOperation = FGuid::NewGuid();
						FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, ActiveState, this, ActiveState, enterStateOperation);
						OperationContext.Add(enterStateOperation, enterStateOperationInfo);

						ExecuteOperation(exitStateOperation);

						return true;
					}
				}
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant make transition in flow that is not active [%s]!"), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant make transition without Transition Key [%s]!"), *GetName());
	}

	return false;
}

void UGameFlow::EnterFlow(FGuid& activeState, const FGuid& nextOperation)
{
	if (!activeState.IsValid())
	{
		if (EntryState.IsValid())
		{
			if (States.Contains(EntryState))
			{
				const FGuid enterStateOperation = FGuid::NewGuid();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, activeState, this, EntryState, nextOperation);
				OperationContext.Add(enterStateOperation, enterStateOperationInfo);

				ExecuteOperation(enterStateOperation);
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("Cant find state [%s] in flow [%s]!"), *EntryState.ToString(), *GetName());
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant enter flow without Entry state [%s]!"), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant enter flow that is active [%s]!"), *GetName());

		ExecuteOperation(nextOperation);
	}
}

void UGameFlow::ExitFlow(FGuid& activeState, const FGuid& nextOperation)
{
	if (activeState.IsValid())
	{
		if (States.Contains(activeState))
		{
			const FGuid exitStateOperation = FGuid::NewGuid();
			FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, activeState, this, activeState, nextOperation);
			OperationContext.Add(exitStateOperation, exitStateOperationInfo);

			ExecuteOperation(exitStateOperation);
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant find state [%s] in flow [%s]!"), *activeState.ToString(), *GetName());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant exit flow that is not active [%s]!"), *GetName());

		ExecuteOperation(nextOperation);
	}
}

void UGameFlow::SetWorldPtr(FGuid& activeState, UWorld* world)
{
	WorldPtr = world;

	for (const TPair<FGuid, TObjectPtr<UGameFlowState>>& stateEntry : States)
	{
		// Notify States
		for (TObjectPtr<UGFS_Base>& step : stateEntry.Value->Steps)
		{
			if (step)
			{
				step->OnWorldContextChanged(stateEntry.Key == activeState);
			}
		}

		// Notify Sub Flows
		if (UGameFlow* subFlow = stateEntry.Value->SubFlow)
		{
			FGuid& subFlowActiveState = stateEntry.Value->bInstancedSubFlow ? stateEntry.Value->SubFlowActiveState : subFlow->ActiveState;
			subFlow->SetWorldPtr(subFlowActiveState, world);
		}
	}
}

void UGameFlow::ExecuteOperation(const FGuid& operation)
{
	if (operation.IsValid())
	{
		if (OperationContext.Contains(operation))
		{
			FOperationInfo operationInfo = OperationContext.FindAndRemoveChecked(operation);

			if (UGameFlow* flow = operationInfo.Flow.Get())
			{
				switch (operationInfo.OperationType)
				{
				case EOperationType::EnterState: return OnEnterState(operationInfo);
				case EOperationType::EnterState_Set: return OnEnterState_Set(operationInfo);
				case EOperationType::EnterState_Steps: return OnEnterState_Steps(operationInfo);
				case EOperationType::EnterState_SubFlow: return OnEnterState_SubFlow(operationInfo);

				case EOperationType::ExitState: return OnExitState(operationInfo);
				case EOperationType::ExitState_SubFlow: return OnExitState_SubFlow(operationInfo);
				case EOperationType::ExitState_Steps: return OnExitState_Steps(operationInfo);
				case EOperationType::ExitState_Set: return OnExitState_Set(operationInfo);
				}
			}
			else
			{
				UE_LOG(LogGameFlow, Warning, TEXT("Cant exec operation [%s] without owning flow!"), *operation.ToString());
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant find operation [%s]!"), *operation.ToString());
		}
	}
}

void UGameFlow::OnEnterState(const FOperationInfo& operationInfo)
{
	const FGuid subFlowOperation = FGuid::NewGuid();
	FOperationInfo subFlowOperationInfo = FOperationInfo(EOperationType::EnterState_SubFlow, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperation);
	OperationContext.Add(subFlowOperation, subFlowOperationInfo);

	const FGuid stepsOperation = FGuid::NewGuid();
	FOperationInfo stepsOperationInfo = FOperationInfo(EOperationType::EnterState_Steps, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, subFlowOperation);
	OperationContext.Add(stepsOperation, stepsOperationInfo);

	const FGuid setOperation = FGuid::NewGuid();
	FOperationInfo setOperationInfo = FOperationInfo(EOperationType::EnterState_Set, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, stepsOperation);
	OperationContext.Add(setOperation, setOperationInfo);

	ExecuteOperation(setOperation);
}

void UGameFlow::OnEnterState_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	LogGameFlowUtils::Depth++;
	UE_LOG(LogGameFlow, Log, TEXT("[%s]%s->%s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());

	operationInfo.ActiveState = operationInfo.State;

	ExecuteOperation(operationInfo.NextOperation);
}

void UGameFlow::OnEnterState_Steps(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const FGuid stepsCatcherOperation = FGuid::NewGuid();
		FOperationInfo& stepsCatcherOperationInfo = OperationContext.Emplace(stepsCatcherOperation, FOperationInfo(EOperationType::StepsCatcher, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperation));

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
				stateObject->Steps[i]->StepsCatcherOperation = stepsCatcherOperation;
				stateObject->Steps[i]->OnEnter();
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperation);
	}
}

void UGameFlow::OnEnterState_SubFlow(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->SubFlow)
	{
		if (stateObject->bInstancedSubFlow) // Instanced
		{
			if (stateObject->SubFlowActiveState.IsValid())
			{
				// Exit Sub Flow and then enter Sub Flow

				const FGuid enterStateOperation = FGuid::NewGuid();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperation);
				OperationContext.Add(enterStateOperation, enterStateOperationInfo);

				const FGuid exitStateOperation = FGuid::NewGuid();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, enterStateOperation);
				OperationContext.Add(exitStateOperation, exitStateOperationInfo);

				ExecuteOperation(exitStateOperation);
			}
			else
			{
				// Enter Sub Flow
				const FGuid enterStateOperation = FGuid::NewGuid();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperation);
				OperationContext.Add(enterStateOperation, enterStateOperationInfo);

				ExecuteOperation(enterStateOperation);
			}
		}
		else // Shared
		{
			if (stateObject->bResetSubFlowOnEnterState && stateObject->SubFlow->ActiveState.IsValid())
			{
				// Exit Sub Flow and then enter Sub Flow

				const FGuid enterStateOperation = FGuid::NewGuid();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperation);
				OperationContext.Add(enterStateOperation, enterStateOperationInfo);

				const FGuid exitStateOperation = FGuid::NewGuid();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, enterStateOperation);
				OperationContext.Add(exitStateOperation, exitStateOperationInfo);

				ExecuteOperation(exitStateOperation);
			}
			else if (!stateObject->SubFlow->ActiveState.IsValid())
			{
				// Enter Sub Flow
				const FGuid enterStateOperation = FGuid::NewGuid();
				FOperationInfo enterStateOperationInfo = FOperationInfo(EOperationType::EnterState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->EntryState, operationInfo.NextOperation);
				OperationContext.Add(enterStateOperation, enterStateOperationInfo);

				ExecuteOperation(enterStateOperation);
			}
			else
			{
				ExecuteOperation(operationInfo.NextOperation);
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperation);
	}
}

void UGameFlow::OnExitState(const FOperationInfo& operationInfo)
{
	const FGuid setOperation = FGuid::NewGuid();
	FOperationInfo setOperationInfo = FOperationInfo(EOperationType::ExitState_Set, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperation);
	OperationContext.Add(setOperation, setOperationInfo);

	const FGuid stepsOperation = FGuid::NewGuid();
	FOperationInfo stepsOperationInfo = FOperationInfo(EOperationType::ExitState_Steps, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, setOperation);
	OperationContext.Add(stepsOperation, stepsOperationInfo);

	const FGuid subFlowOperation = FGuid::NewGuid();
	FOperationInfo subFlowOperationInfo = FOperationInfo(EOperationType::ExitState_SubFlow, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, stepsOperation);
	OperationContext.Add(subFlowOperation, subFlowOperationInfo);

	ExecuteOperation(subFlowOperation);
}

void UGameFlow::OnExitState_SubFlow(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->SubFlow)
	{
		if (stateObject->bInstancedSubFlow) // Instanced
		{
			if (stateObject->SubFlowActiveState.IsValid())
			{
				// Exit Sub Flow

				const FGuid exitStateOperation = FGuid::NewGuid();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlowActiveState, stateObject->SubFlow, stateObject->SubFlowActiveState, operationInfo.NextOperation);
				OperationContext.Add(exitStateOperation, exitStateOperationInfo);

				ExecuteOperation(exitStateOperation);
			}
			else
			{
				ExecuteOperation(operationInfo.NextOperation);
			}
		}
		else // Shared
		{
			if (stateObject->bResetSubFlowOnEnterState && stateObject->SubFlow->ActiveState.IsValid())
			{
				// Exit Sub Flow

				const FGuid exitStateOperation = FGuid::NewGuid();
				FOperationInfo exitStateOperationInfo = FOperationInfo(EOperationType::ExitState, stateObject->SubFlow->ActiveState, stateObject->SubFlow, stateObject->SubFlow->ActiveState, operationInfo.NextOperation);
				OperationContext.Add(exitStateOperation, exitStateOperationInfo);

				ExecuteOperation(exitStateOperation);
			}
			else
			{
				ExecuteOperation(operationInfo.NextOperation);
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperation);
	}
}

void UGameFlow::OnExitState_Steps(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	if (stateObject->Steps.Num() > 0)
	{
		const FGuid stepsCatcherOperation = FGuid::NewGuid();
		FOperationInfo& stepsCatcherOperationInfo = OperationContext.Emplace(stepsCatcherOperation, FOperationInfo(EOperationType::StepsCatcher, operationInfo.ActiveState, operationInfo.Flow, operationInfo.State, operationInfo.NextOperation));

		for (int32 i = stateObject->Steps.Num() - 1; i > 0; i--)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.StepIndices.Add(i);
			}
		}

		for (int32 i = stateObject->Steps.Num() - 1; i > 0; i--)
		{
			if (stateObject->Steps[i])
			{
				stepsCatcherOperationInfo.ReportStepStatus(stateObject->Steps[i], EGFSStatus::Started);

				stateObject->Steps[i]->StepsCatcherOperation = stepsCatcherOperation;
				stateObject->Steps[i]->OnEnter();
			}
		}
	}
	else
	{
		ExecuteOperation(operationInfo.NextOperation);
	}
}

void UGameFlow::OnExitState_Set(const FOperationInfo& operationInfo)
{
	UGameFlow* flow = operationInfo.Flow.Get();
	UGameFlowState* stateObject = flow->States[operationInfo.State];

	operationInfo.ActiveState.Invalidate();

	UE_LOG(LogGameFlow, Log, TEXT("[%s]%s->%s {%s}"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *stateObject->StateTitle.ToString(), *flow->GetName());
	LogGameFlowUtils::Depth--;

	ExecuteOperation(operationInfo.NextOperation);
}

void UGameFlow::OnStepsCatcher(const FOperationInfo& operationInfo)
{
	ExecuteOperation(operationInfo.NextOperation);
}