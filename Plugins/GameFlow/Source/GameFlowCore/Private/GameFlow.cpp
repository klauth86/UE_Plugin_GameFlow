// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "GameFlow.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY(LogGameFlow);

int32 LogGameFlowUtils::Depth = 0;

FString LogGameFlowUtils::RepeatTab(int32 num)
{
	FString result;
	for (int32 i = 0; i < num; i++) result.AppendChar(*"\t");
	return result;
}

#if WITH_EDITORONLY_DATA

//------------------------------------------------------
// UGameFlow
//------------------------------------------------------

UGameFlowState* UGameFlow::AddState(const FGuid& guid, const FName stateTitle)
{
	States.Add(guid, NewObject<UGameFlowState>(this));
	States[guid]->StateTitle = stateTitle;
	return States[guid];
}

void UGameFlow::DestroyState(const FGuid& guid)
{
	ActiveStates.Remove(guid);

	if (EntryState == guid)
	{
		InvalidateEntryState();
	}

	DestroyStateTransition(guid);

	if (TObjectPtr<UGameFlowState> stateToDestroy = States.FindAndRemoveChecked(guid))
	{
		stateToDestroy->Rename(nullptr, GetTransientPackage());
	}
}

void UGameFlow::DestroyStateTransition(const FGuid& guid)
{
	if (TransitionCollections.Contains(guid))
	{
		TSet<FGuid> outTransitions;

		for (TPair<FGuid, TObjectPtr<UGameFlowTransition>> transitionEntry : TransitionCollections[guid].Transitions)
		{
			outTransitions.FindOrAdd(transitionEntry.Key);
		}

		for (const FGuid& ourTransition : outTransitions)
		{
			DestroyTransition(guid, ourTransition);
		}
	}

	TMap<FGuid, FGuid> inTransitions;

	for (TPair<FGuid, FGameFlowTransitionCollection>& transitionCollectionEntry : TransitionCollections)
	{
		if (transitionCollectionEntry.Value.Transitions.Contains(guid))
		{
			inTransitions.FindOrAdd(transitionCollectionEntry.Key, guid);
		}
	}

	for (const TPair<FGuid, FGuid>& inTransition : inTransitions)
	{
		DestroyTransition(inTransition.Key, inTransition.Value);
	}
}

UGameFlowTransition* UGameFlow::AddTransition(const FGuid& fromGuid, const FGuid& toGuid)
{
	FGameFlowTransitionCollection& transitionCollection = TransitionCollections.FindOrAdd(fromGuid);

	transitionCollection.Transitions.FindOrAdd(toGuid) = NewObject<UGameFlowTransition>(this);

	return transitionCollection.Transitions[toGuid];
}

void UGameFlow::DestroyTransition(const FGuid& fromGuid, const FGuid& toGuid)
{
	if (TransitionCollections.Contains(fromGuid))
	{
		FGameFlowTransitionCollection& transitionCollection = TransitionCollections[fromGuid];

		if (transitionCollection.Transitions.Contains(toGuid))
		{
			if (UGameFlowTransition* transition = transitionCollection.Transitions.FindAndRemoveChecked(toGuid))
			{
				transition->Rename(nullptr, GetTransientPackage());

				if (transitionCollection.Transitions.IsEmpty())
				{
					TransitionCollections.Remove(fromGuid);
				}
			}
		}
	}
}

#endif

void UGameFlow::FindStateByTitle(FName stateTitle, TArray<UGameFlowState*>& outStates) const
{
	for (const TPair<FGuid, TSoftObjectPtr<UGameFlowState>>& stateEntry : States)
	{
		if (stateEntry.Value.IsValid() && stateEntry.Value->StateTitle == stateTitle)
		{
			outStates.Add(stateEntry.Value.Get());
		}
	}
}

bool UGameFlow::MakeTransition(UGameFlowTransitionKey* transitionKey, bool executeSteps)
{
	if (transitionKey)
	{
		for (int32 i = ActiveStates.Num() - 1; i >= 0; i--)
		{
			if (States[ActiveStates[i]]->SubFlow)
			{
				// Try to find transition in Sub Flow

				if (States[ActiveStates[i]]->SubFlow->MakeTransition(transitionKey, executeSteps))
				{
					return true;
				}
			}

			// Try to find transition in node links

			if (TransitionCollections.Contains(ActiveStates[i]))
			{
				const FGameFlowTransitionCollection& transitionCollection = TransitionCollections[ActiveStates[i]];

				for (const TPair<FGuid, TObjectPtr<UGameFlowTransition>>& transitionEntry : transitionCollection.Transitions)
				{
					if (transitionEntry.Value->TransitionKey == transitionKey)
					{
						if (ActiveStates.Num() > 0) ExitState(ActiveStates, ActiveStates[i], executeSteps);
						EnterState(ActiveStates, transitionEntry.Key, executeSteps);
						return true;
					}
				}
			}
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant make transition without Transition Key [%s]!"), *GetName());
	}

	return false;
}

void UGameFlow::EnterState(TArray<FGuid>& activeStates, const FGuid& guid, bool executeSteps)
{
	if (States.Contains(guid))
	{
		if (!activeStates.Contains(guid))
		{
			LogGameFlowUtils::Depth++;

			UE_LOG(LogGameFlow, Log, TEXT("[%s]%s->%s (%s)"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *States[guid]->StateTitle.ToString(), *GetName());

			activeStates.Add(guid);

			if (executeSteps)
			{
				for (int32 i = 0; i < States[guid]->Steps.Num(); i++)
				{
					if (States[guid]->Steps[i])
					{
						States[guid]->Steps[i]->OnEnter(this);
					}
				}
			}

			if (States[guid]->SubFlow)
			{
				if (States[guid]->bInstancedSubFlow) // Instanced
				{
					// Exit Sub Flow if Sub Flow is active
					if (States[guid]->SubFlowActiveStates.Num() > 0) States[guid]->SubFlow->ExitFlow(States[guid]->SubFlowActiveStates, executeSteps);

					// Enter Sub Flow
					States[guid]->SubFlow->EnterFlow(States[guid]->SubFlowActiveStates, executeSteps);
				}
				else // Shared
				{
					// Exit Sub Flow if bResetSubFlowOnEnterState is true and Sub Flow is active
					if (States[guid]->bResetSubFlowOnEnterState && States[guid]->SubFlow->ActiveStates.Num() > 0) States[guid]->SubFlow->ExitFlow(States[guid]->SubFlow->ActiveStates, executeSteps);

					// Enter Sub Flow if Sub Flow is not active
					if (States[guid]->SubFlow->ActiveStates.Num() == 0) States[guid]->SubFlow->EnterFlow(States[guid]->SubFlow->ActiveStates, executeSteps);
				}
			}
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant enter State that is active [%s]!"), *States[guid]->StateTitle.ToString());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant find State [%s]!"), *guid.ToString());
	}
}

void UGameFlow::ExitState(TArray<FGuid>& activeStates, const FGuid& guid, bool executeSteps)
{
	if (States.Contains(guid))
	{
		if (activeStates.Contains(guid))
		{
			if (States[guid]->SubFlow)
			{
				if (States[guid]->bInstancedSubFlow) // Instanced
				{
					// Exit Sub Flow if Sub Flow is active
					if (States[guid]->SubFlowActiveStates.Num() > 0) States[guid]->SubFlow->ExitFlow(States[guid]->SubFlowActiveStates, executeSteps);
				}
				else // Shared
				{
					// Exit Sub Flow if bResetSubFlowOnEnterState is true and Sub Flow is active
					if (!States[guid]->bResetSubFlowOnExitState && States[guid]->SubFlow->ActiveStates.Num() > 0) States[guid]->SubFlow->ExitFlow(States[guid]->SubFlow->ActiveStates, executeSteps);
				}
			}

			if (executeSteps)
			{
				for (int32 i = States[guid]->Steps.Num() - 1; i >= 0; i--)
				{
					if (States[guid]->Steps[i])
					{
						States[guid]->Steps[i]->OnExit(this);
					}
				}
			}

			activeStates.RemoveAll([&guid](const FGuid& guidItem) { return guidItem == guid; });

			UE_LOG(LogGameFlow, Log, TEXT("[%s]%s<-%s (%s)"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *States[guid]->StateTitle.ToString(), *GetName());

			LogGameFlowUtils::Depth--;
		}
		else
		{
			UE_LOG(LogGameFlow, Warning, TEXT("Cant exit State that is not active [%s]!"), *States[guid]->StateTitle.ToString());
		}
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant find State [%s]!"), *guid.ToString());
	}
}

void UGameFlow::EnterFlow(TArray<FGuid>& activeStates, bool executeSteps)
{
	if (activeStates.Num() == 0)
	{
		LogGameFlowUtils::Depth++;

		UE_LOG(LogGameFlow, Log, TEXT("[%s]%s->%s"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());

		if (executeSteps)
		{
			for (int32 i = 0; i < Steps.Num(); i++)
			{
				if (Steps[i])
				{
					Steps[i]->OnEnter(this);
				}
			}
		}

		EnterState(activeStates, EntryState, executeSteps);
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant enter Game Flow that is active [%s]!"), *GetName());
	}
}

void UGameFlow::ExitFlow(TArray<FGuid>& activeStates, bool executeSteps)
{
	if (activeStates.Num() != 0)
	{
		for (int32 i = activeStates.Num() - 1; i >= 0; i--)
		{
			ExitState(activeStates, activeStates[i], executeSteps);
		}

		if (executeSteps)
		{
			for (int32 i = Steps.Num() - 1; i >= 0; i--)
			{
				if (Steps[i])
				{
					Steps[i]->OnExit(this);
				}
			}
		}

		UE_LOG(LogGameFlow, Log, TEXT("[%s]%s<-%s"), *FDateTime::Now().ToString(), *LogGameFlowUtils::RepeatTab(LogGameFlowUtils::Depth), *GetName());

		LogGameFlowUtils::Depth--;
	}
	else
	{
		UE_LOG(LogGameFlow, Warning, TEXT("Cant exit Game Flow that is not active [%s]!"), *GetName());
	}
}

void UGameFlow::SetWorldPtr(TArray<FGuid>& activeStates, UWorld* world)
{
	WorldPtr = world;

	// Notify flow steps
	for (TObjectPtr<UGFS_Base>& step : Steps)
	{
		if (step)
		{
			step->OnWorldContextChanged(this, activeStates.Num() > 0);
		}
	}

	for (const TPair<FGuid, TObjectPtr<UGameFlowState>>& stateEntry : States)
	{
		// Notify states steps
		for (TObjectPtr<UGFS_Base>& step : stateEntry.Value->Steps)
		{
			if (step)
			{
				step->OnWorldContextChanged(this, activeStates.Contains(stateEntry.Key));
			}
		}

		// Notify Sub Flows
		if (stateEntry.Value->SubFlow)
		{
			if (stateEntry.Value->bInstancedSubFlow) // Instanced
			{
				stateEntry.Value->SubFlow->SetWorldPtr(stateEntry.Value->SubFlowActiveStates, world);
			}
			else // Shared
			{
				stateEntry.Value->SubFlow->SetWorldPtr(stateEntry.Value->SubFlow->ActiveStates, world);
			}
		}
	}
}