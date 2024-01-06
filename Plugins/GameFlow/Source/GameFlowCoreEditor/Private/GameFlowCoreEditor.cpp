// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#include "GameFlowCoreEditor.h"
#include "Graph/GameFlowGraph.h"
#include "Graph/GameFlowGraphSchema.h"
#include "Graph/GameFlowGraphFactory.h"
#include "Graph/GameFlowGraphNode.h"
#include "UObject/ObjectSaveContext.h"
#include "EdGraphNode_Comment.h"

#include "AssetTypeActions.h"
#include "Factories.h"
#include "AssetTypeCategories.h"
#include "GameFlow.h"

#include "EditorUndoClient.h"
#include "SGraphPin.h"
#include "SGraphPanel.h"
#include "Toolkits/AssetEditorToolkit.h"
#include "GraphEditorActions.h"
#include "HAL/PlatformApplicationMisc.h"
#include "Framework/Commands/GenericCommands.h"
#include "EdGraphUtilities.h"
#include "Settings/EditorStyleSettings.h"
#include "Widgets/Text/SInlineEditableTextBlock.h"
#include "Widgets/Layout/SGridPanel.h"
#include "BlueprintConnectionDrawingPolicy.h"
#include "KismetPins/SGraphPinExec.h"

#define LOCTEXT_NAMESPACE "FGameFlowCoreEditorModule"

const float NodeTitlePadding = 16.f;
const float TransitionKeyPadding = 3.f;
const FMargin ZeroMargin = FMargin(0);
const FMargin StepsVerticalBoxPadding = FMargin(8);

template<class T>
TSharedPtr<T> AddNewActionAs(FGraphContextMenuBuilder& ContextMenuBuilder, const FText& Category, const FText& MenuDesc, const FText& Tooltip, const int32 Grouping = 0)
{
	TSharedPtr<T> Action(new T(Category, MenuDesc, Tooltip, Grouping));
	ContextMenuBuilder.AddAction(Action);
	return Action;
}

//------------------------------------------------------
// UGameFlowGraphNode_Start
//------------------------------------------------------

void UGameFlowGraphNode_Start::AllocateDefaultPins()
{
	UEdGraphPin* Outputs = CreatePin(EGPD_Output, UGameFlowGraphSchema::PC_Exec, TEXT("Entry"));
}

FText UGameFlowGraphNode_Start::GetNodeTitle(ENodeTitleType::Type TitleType) const { return FText::FromString(GetGraph()->GetName()); }

FText UGameFlowGraphNode_Start::GetTooltipText() const { return LOCTEXT("UGameFlowGraphNode_Start_TooltipText", "Entry point for State machine"); }

void UGameFlowGraphNode_Start::PinConnectionListChanged(UEdGraphPin* Pin)
{
	Super::PinConnectionListChanged(Pin);

	RefreshOwningAssetEntryState();
}

void UGameFlowGraphNode_Start::NodeConnectionListChanged()
{
	Super::NodeConnectionListChanged();

	RefreshOwningAssetEntryState();
}

UEdGraphNode* UGameFlowGraphNode_Start::GetOutputNode() const
{
	if (Pins.Num() > 0 && Pins[0] != NULL)
	{
		check(Pins[0]->LinkedTo.Num() <= 1);
		if (Pins[0]->LinkedTo.Num() > 0 && Pins[0]->LinkedTo[0]->GetOwningNode() != NULL)
		{
			return Pins[0]->LinkedTo[0]->GetOwningNode();
		}
	}
	return NULL;
}

void UGameFlowGraphNode_Start::RefreshOwningAssetEntryState()
{
	UGameFlow* gameFlow = GetGraph()->GetTypedOuter<UGameFlow>();
	
	const FGuid entryStateId = Pins[0]->LinkedTo.Num() == 1 ? Pins[0]->LinkedTo[0]->GetOwningNode()->NodeGuid : FGuid();
	
	if (gameFlow->GetEntryStateId() != entryStateId)
	{
		gameFlow->Modify();
		gameFlow->SetEntryStateId(entryStateId);
	}
}

//------------------------------------------------------
// UGameFlowGraphNode_Base
//------------------------------------------------------

bool UGameFlowGraphNode_Base::CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const
{
	return Schema->IsA(UGameFlowGraphSchema::StaticClass());
}

void UGameFlowGraphNode_Base::GetTransitionList(TArray<UGameFlowGraphNode_Transition*>& OutTransitions) const
{
	for (UEdGraphPin* linkedPin : Pins[1]->LinkedTo)
	{
		if (UGameFlowGraphNode_Transition* Transition = Cast<UGameFlowGraphNode_Transition>(linkedPin->GetOwningNode()))
		{
			OutTransitions.Add(Transition);
		}
	}
}

//------------------------------------------------------
// UGameFlowGraphNode_State
//------------------------------------------------------

UGameFlowGraphNode_State::UGameFlowGraphNode_State(const FObjectInitializer& ObjectInitializer) : Super(ObjectInitializer)
{
	bCanRenameNode = true;
}

void UGameFlowGraphNode_State::AllocateDefaultPins()
{
	UEdGraphPin* Inputs = CreatePin(EGPD_Input, UGameFlowGraphSchema::PC_Transition, TEXT("In"));
	UEdGraphPin* Outputs = CreatePin(EGPD_Output, UGameFlowGraphSchema::PC_Transition, TEXT("Out"));
}

void UGameFlowGraphNode_State::AutowireNewNode(UEdGraphPin* FromPin)
{
	Super::AutowireNewNode(FromPin);

	if (FromPin)
	{
		if (GetSchema()->TryCreateConnection(FromPin, GetInputPin()))
		{
			FromPin->GetOwningNode()->NodeConnectionListChanged();
		}
	}
}

FText UGameFlowGraphNode_State::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	return CachedNodeTitle.IsEmpty() ? LOCTEXT("UGameFlowGraphNode_State_NodeTitle", "State") : CachedNodeTitle;
}

FText UGameFlowGraphNode_State::GetTooltipText() const { return LOCTEXT("UGameFlowGraphNode_State_TooltipText", "This is a State"); }

void UGameFlowGraphNode_State::OnRenameNode(const FString& NewName)
{
	CachedNodeTitle = FText::FromString(NewName);

	const TMap<FGuid, TObjectPtr<UGameFlowState>>& states = GetGraph()->GetTypedOuter<UGameFlow>()->GetStates();

	if (states.Contains(NodeGuid))
	{
		states[NodeGuid]->StateTitle = FName(NewName);
	}
}

//------------------------------------------------------
// UGameFlowGraphNode_Transition
//------------------------------------------------------

void UGameFlowGraphNode_Transition::AllocateDefaultPins()
{
	UEdGraphPin* Inputs = CreatePin(EGPD_Input, UGameFlowGraphSchema::PC_Transition, TEXT("In"));
	Inputs->bHidden = true;
	UEdGraphPin* Outputs = CreatePin(EGPD_Output, UGameFlowGraphSchema::PC_Transition, TEXT("Out"));
	Outputs->bHidden = true;
}

FText UGameFlowGraphNode_Transition::GetNodeTitle(ENodeTitleType::Type TitleType) const
{
	FFormatNamedArguments Args;
	Args.Add(TEXT("PrevState"), GetPreviousState()->GetNodeTitle(ENodeTitleType::EditableTitle));
	Args.Add(TEXT("NextState"), GetNextState()->GetNodeTitle(ENodeTitleType::EditableTitle));

	return FText::Format(LOCTEXT("UGameFlowGraphNode_Transition_NodeTitle", "{PrevState} to {NextState}"), Args);
}

FText UGameFlowGraphNode_Transition::GetTooltipText() const { return LOCTEXT("UGameFlowGraphNode_Transition_TooltipText", "This is a Transition"); }

void UGameFlowGraphNode_Transition::PinConnectionListChanged(UEdGraphPin* Pin)
{
	if (Pin->LinkedTo.Num() == 0 && IsValid(this)) // To avoid double destroy
	{
		GetGraph()->Modify();
		Modify();
		DestroyNode();
	}
}

void UGameFlowGraphNode_Transition::PostPasteNode()
{
	Super::PostPasteNode();

	for (UEdGraphPin* Pin : Pins)
	{
		if (Pin->LinkedTo.Num() == 0)
		{
			DestroyNode();
			break;
		}
	}
}

UGameFlowGraphNode_Base* UGameFlowGraphNode_Transition::GetPreviousState() const
{
	return Pins[0]->LinkedTo.Num() > 0 ? Cast<UGameFlowGraphNode_Base>(Pins[0]->LinkedTo[0]->GetOwningNode()) : nullptr;
}

UGameFlowGraphNode_Base* UGameFlowGraphNode_Transition::GetNextState() const
{
	return Pins[1]->LinkedTo.Num() > 0 ? Cast<UGameFlowGraphNode_Base>(Pins[1]->LinkedTo[0]->GetOwningNode()) : nullptr;
}

void UGameFlowGraphNode_Transition::CreateConnections(UGameFlowGraphNode_Base* PreviousState, UGameFlowGraphNode_Base* NextState)
{
	Pins[0]->Modify();
	Pins[0]->LinkedTo.Empty();
	PreviousState->GetOutputPin()->Modify();
	Pins[0]->MakeLinkTo(PreviousState->GetOutputPin());

	Pins[1]->Modify();
	Pins[1]->LinkedTo.Empty();
	NextState->GetInputPin()->Modify();
	Pins[1]->MakeLinkTo(NextState->GetInputPin());
}

//------------------------------------------------------
// SGameFlowGraphNodeOutputPin
//------------------------------------------------------

class SGameFlowGraphNodeOutputPin : public SGraphPin
{
public:
	SLATE_BEGIN_ARGS(SGameFlowGraphNodeOutputPin) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UEdGraphPin* InPin);

protected:
	virtual TSharedRef<SWidget>	GetDefaultValueWidget() override { return SNew(STextBlock); }
	const FSlateBrush* GetPinBorder() const { return FAppStyle::GetBrush(IsHovered() ? TEXT("Graph.StateNode.Pin.BackgroundHovered") : TEXT("Graph.StateNode.Pin.Background")); }
};

void SGameFlowGraphNodeOutputPin::Construct(const FArguments& InArgs, UEdGraphPin* InPin)
{
	this->SetCursor(EMouseCursor::Default);

	bShowLabel = true;

	GraphPinObj = InPin;
	check(GraphPinObj != NULL);

	SBorder::Construct(SBorder::FArguments()
		.BorderImage(this, &SGameFlowGraphNodeOutputPin::GetPinBorder)
		.BorderBackgroundColor(this, &SGameFlowGraphNodeOutputPin::GetPinColor)
		.OnMouseButtonDown(this, &SGameFlowGraphNodeOutputPin::OnPinMouseDown)
		.Cursor(this, &SGameFlowGraphNodeOutputPin::GetPinCursor)
	);
}

//------------------------------------------------------
// SGameFlowGraphNode_Start
//------------------------------------------------------

void SGameFlowGraphNode_Start::Construct(const FArguments& InArgs, UGameFlowGraphNode_Start* InNode)
{
	this->SetCursor(EMouseCursor::CardinalCross);

	this->GraphNode = InNode;

	this->UpdateGraphNode();
}

FSlateColor SGameFlowGraphNode_Start::GetBorderBackgroundColor() const
{
	FLinearColor InactiveStateColor(0.08f, 0.08f, 0.08f);
	FLinearColor ActiveStateColorDim(0.4f, 0.3f, 0.15f);
	FLinearColor ActiveStateColorBright(1.f, 0.6f, 0.35f);

	return InactiveStateColor;
}

void SGameFlowGraphNode_Start::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.StateNode.Body"))
				.Padding(0)
				.BorderBackgroundColor(this, &SGameFlowGraphNode_Start::GetBorderBackgroundColor)
				[
					SNew(SOverlay)

						+ SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						.Padding(10.0f)
						[
							SAssignNew(RightNodeBox, SVerticalBox)
						]
				]
		];

	CreatePinWidgets();
}

void SGameFlowGraphNode_Start::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));

	RightNodeBox->AddSlot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		.FillHeight(1.0f)[PinToAdd];
	OutputPins.Add(PinToAdd);
}

FText SGameFlowGraphNode_Start::GetPreviewCornerText() const
{
	return LOCTEXT("SGameFlowGraphNode_Start_PreviewCornerText", "Entry point for State machine");
}

//------------------------------------------------------
// SGameFlowGraphNode_State
//------------------------------------------------------

void SGameFlowGraphNode_State::Construct(const FArguments& InArgs, UGameFlowGraphNode_Base* InNode)
{
	OwningGameFlow = InNode->GetGraph()->GetTypedOuter<UGameFlow>();

	this->SetCursor(EMouseCursor::CardinalCross);

	this->GraphNode = InNode;

	this->UpdateGraphNode();
}

FSlateColor SGameFlowGraphNode_State::GetBorderBackgroundColor() const
{
	FLinearColor InactiveStateColor(0.08f, 0.08f, 0.08f);
	FLinearColor ActiveStateColorDim(0.4f, 0.3f, 0.15f);
	FLinearColor ActiveStateColorBright(1.f, 0.6f, 0.35f);

	return GetBorderBackgroundColor_Internal(InactiveStateColor, ActiveStateColorDim, ActiveStateColorBright);
}

FSlateColor SGameFlowGraphNode_State::GetBorderBackgroundColor_Internal(FLinearColor InactiveStateColor, FLinearColor ActiveStateColorDim, FLinearColor ActiveStateColorBright) const
{
	if (OwningGameFlow->IsStateActive(GraphNode->NodeGuid))
	{
		return ActiveStateColorBright;
		////// TODO return FMath::Lerp<FLinearColor>(ActiveStateColorDim, ActiveStateColorBright, StateData.Weight);
	}

	return InactiveStateColor;
}

void SGameFlowGraphNode_State::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SGraphNode::OnMouseEnter(MyGeometry, MouseEvent);

	const UGameFlowGraphNode_Base* BaseNode = Cast<UGameFlowGraphNode_Base>(GraphNode);
	const UEdGraphPin* OutputPin = BaseNode->GetOutputPin();

	TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel();
	check(OwnerPanel.IsValid());

	for (int32 LinkIndex = 0; LinkIndex < OutputPin->LinkedTo.Num(); ++LinkIndex)
	{
		OwnerPanel->AddPinToHoverSet(OutputPin->LinkedTo[LinkIndex]);
	}
}

void SGameFlowGraphNode_State::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	const UGameFlowGraphNode_Base* BaseNode = Cast<UGameFlowGraphNode_Base>(GraphNode);
	const UEdGraphPin* OutputPin = BaseNode->GetOutputPin();

	TSharedPtr<SGraphPanel> OwnerPanel = GetOwnerPanel();
	check(OwnerPanel.IsValid());

	for (int32 LinkIndex = 0; LinkIndex < OutputPin->LinkedTo.Num(); ++LinkIndex)
	{
		OwnerPanel->RemovePinFromHoverSet(OutputPin->LinkedTo[LinkIndex]);
	}

	SGraphNode::OnMouseLeave(MouseEvent);
}

void SGameFlowGraphNode_State::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	FLinearColor TitleShadowColor(0.6f, 0.6f, 0.6f);
	TSharedPtr<SNodeTitle> NodeTitle = SNew(SNodeTitle, GraphNode);

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.StateNode.Body"))
				.Padding(0)
				.BorderBackgroundColor(this, &SGameFlowGraphNode_State::GetBorderBackgroundColor)
				[
					SNew(SOverlay)

						// PIN AREA
						+ SOverlay::Slot()
						.HAlign(HAlign_Fill)
						.VAlign(VAlign_Fill)
						[
							SAssignNew(RightNodeBox, SVerticalBox)
						]

						// STATE NAME AREA
						+ SOverlay::Slot()
						.HAlign(HAlign_Center)
						.VAlign(VAlign_Center)
						.Padding(10.0f)
						[
							SNew(SBorder)
								.BorderImage(FAppStyle::GetBrush("Graph.StateNode.ColorSpill"))
								.BorderBackgroundColor(TitleShadowColor)
								.HAlign(HAlign_Center)
								.VAlign(VAlign_Center)
								.Visibility(EVisibility::Visible)
								[
									SNew(SVerticalBox)
										+ SVerticalBox::Slot()
										.AutoHeight()
										[
											SAssignNew(InlineEditableText, SInlineEditableTextBlock)
												.Style(FAppStyle::Get(), "Graph.StateNode.NodeTitleInlineEditableText")
												.Text(NodeTitle.Get(), &SNodeTitle::GetHeadTitle)
												.OnVerifyTextChanged(this, &SGameFlowGraphNode_State::OnVerifyNameTextChanged)
												.OnTextCommitted(this, &SGameFlowGraphNode_State::OnNameTextCommited)
												.IsReadOnly(this, &SGameFlowGraphNode_State::IsNameReadOnly)
												.IsSelected(this, &SGameFlowGraphNode_State::IsSelectedExclusively)
										]
										+ SVerticalBox::Slot()
										.AutoHeight()
										[
											NodeTitle.ToSharedRef()
										]
										+ SVerticalBox::Slot()
										.AutoHeight()
										[
											SNew(SBox).Padding(this, &SGameFlowGraphNode_State::StepsPadding)
												[
													SAssignNew(StepsVerticalBoxPtr, SVerticalBox)
														.Visibility(this, &SGameFlowGraphNode_State::StepsVisibility)
												]
										]
								]
						]
				]
		];

	CreatePinWidgets();
}

void SGameFlowGraphNode_State::CreatePinWidgets()
{
	UGameFlowGraphNode_Base* baseNode = CastChecked<UGameFlowGraphNode_Base>(GraphNode);
	AddPin(SNew(SGameFlowGraphNodeOutputPin, baseNode->GetOutputPin()));
	AddPin(SNew(SGraphPin, baseNode->GetInputPin()));
}

void SGameFlowGraphNode_State::AddPin(const TSharedRef<SGraphPin>& PinToAdd)
{
	PinToAdd->SetOwner(SharedThis(this));

	if (PinToAdd->GetPinObj()->Direction == EEdGraphPinDirection::EGPD_Output)
	{
		RightNodeBox->AddSlot()
			.HAlign(HAlign_Fill)
			.VAlign(VAlign_Fill)
			.FillHeight(1.0f)[PinToAdd];
		OutputPins.Add(PinToAdd);
	}
	else
	{
		InputPins.Add(PinToAdd);
	}
}

FText SGameFlowGraphNode_State::GetPreviewCornerText() const
{
	return FText::Format(LOCTEXT("SGameFlowGraphNode_State_PreviewCornerText", "{0} State"), GraphNode->GetNodeTitle(ENodeTitleType::EditableTitle));
}

FText SGameFlowGraphNode_State::GetStepDescription(const TObjectPtr<UGameFlowStep>& step) const
{
	return step ? step->GenerateDescription() : LOCTEXT("SGameFlowGraphNode_State_StepDescription_Default", "None");
}

FMargin SGameFlowGraphNode_State::StepsPadding() const
{
	return StepsVerticalBoxPtr->GetVisibility() == EVisibility::Visible ? StepsVerticalBoxPadding : ZeroMargin;
}

EVisibility SGameFlowGraphNode_State::StepsVisibility() const ////// TODO Think if can be done with property changed instead of every tick binding
{
	StepsVerticalBoxPtr->ClearChildren();

	const TMap<FGuid, TObjectPtr<UGameFlowState>>& states = OwningGameFlow->GetStates();

	if (states.Contains(GraphNode->NodeGuid))
	{
		for (const TObjectPtr<UGameFlowStep>& step : states[GraphNode->NodeGuid]->Steps)
		{
			StepsVerticalBoxPtr->AddSlot().AutoHeight()[SNew(STextBlock).Text(GetStepDescription(step)).TextStyle(FAppStyle::Get(), "SmallText")];
		}
	}

	return StepsVerticalBoxPtr->GetChildren()->Num() > 0 ? EVisibility::Visible : EVisibility::Collapsed;
}

//------------------------------------------------------
// SGameFlowGraphNode_Transition
//------------------------------------------------------

void SGameFlowGraphNode_Transition::Construct(const FArguments& InArgs, UGameFlowGraphNode_Transition* InNode)
{
	OwningGameFlow = InNode->GetGraph()->GetTypedOuter<UGameFlow>();
	
	TransitionGraphNode = InNode;

	this->GraphNode = InNode;

	this->UpdateGraphNode();
}

void SGameFlowGraphNode_Transition::PerformSecondPassLayout(const TMap< UObject*, TSharedRef<SNode> >& NodeToWidgetLookup) const
{
	// Find the geometry of the state nodes we're connecting
	FGeometry StartGeom;
	FGeometry EndGeom;

	int32 TransIndex = 0;
	int32 NumOfTrans = 1;

	UGameFlowGraphNode_Base* PrevState = TransitionGraphNode->GetPreviousState();
	UGameFlowGraphNode_Base* NextState = TransitionGraphNode->GetNextState();
	if ((PrevState != NULL) && (NextState != NULL))
	{
		const TSharedRef<SNode>* pPrevNodeWidget = NodeToWidgetLookup.Find(PrevState);
		const TSharedRef<SNode>* pNextNodeWidget = NodeToWidgetLookup.Find(NextState);
		if ((pPrevNodeWidget != NULL) && (pNextNodeWidget != NULL))
		{
			const TSharedRef<SNode>& PrevNodeWidget = *pPrevNodeWidget;
			const TSharedRef<SNode>& NextNodeWidget = *pNextNodeWidget;

			StartGeom = FGeometry(FVector2D(PrevState->NodePosX, PrevState->NodePosY), FVector2D::ZeroVector, PrevNodeWidget->GetDesiredSize(), 1.0f);
			EndGeom = FGeometry(FVector2D(NextState->NodePosX, NextState->NodePosY), FVector2D::ZeroVector, NextNodeWidget->GetDesiredSize(), 1.0f);

			TArray<UGameFlowGraphNode_Transition*> Transitions;
			PrevState->GetTransitionList(Transitions);

			Transitions = Transitions.FilterByPredicate([NextState](const UGameFlowGraphNode_Transition* InTransition) -> bool
				{
					return InTransition->GetNextState() == NextState;
				});

			TransIndex = Transitions.IndexOfByKey(TransitionGraphNode);
			NumOfTrans = Transitions.Num();

			PrevStateNodeWidgetPtr = PrevNodeWidget;
		}
	}

	//Position Node
	PositionBetweenTwoNodesWithOffset(StartGeom, EndGeom, TransIndex, NumOfTrans);
}

void SGameFlowGraphNode_Transition::UpdateGraphNode()
{
	InputPins.Empty();
	OutputPins.Empty();

	RightNodeBox.Reset();
	LeftNodeBox.Reset();

	this->ContentScale.Bind(this, &SGraphNode::GetContentScale);
	this->GetOrAddSlot(ENodeZone::Center)
		.HAlign(HAlign_Center)
		.VAlign(VAlign_Center)
		[
			SNew(SBorder)
				.BorderImage(FAppStyle::GetBrush("Graph.StateNode.Body"))
				.Padding(TransitionKeyPadding)
				.BorderBackgroundColor(FLinearColor(0.08f, 0.08f, 0.08f))
				[
					SNew(STextBlock).Text(this, &SGameFlowGraphNode_Transition::GetTransitionKey).TextStyle(FAppStyle::Get(), "SmallText")
				]
		];
}

FText SGameFlowGraphNode_Transition::GetPreviewCornerText(bool bReverse) const
{
	UGameFlowGraphNode_Base* PrevState = (bReverse ? TransitionGraphNode->GetNextState() : TransitionGraphNode->GetPreviousState());
	UGameFlowGraphNode_Base* NextState = (bReverse ? TransitionGraphNode->GetPreviousState() : TransitionGraphNode->GetNextState());

	FText Result = LOCTEXT("SGameFlowGraphNode_Transition_BadTransition", "Bad transition (missing source or target)");

	if (PrevState != NULL && NextState != NULL)
	{
		Result = FText::Format(LOCTEXT("SGameFlowGraphNode_Transition_TransitionXToY", "{0} to {1}"), PrevState->GetNodeTitle(ENodeTitleType::EditableTitle), NextState->GetNodeTitle(ENodeTitleType::EditableTitle));
	}

	return Result;
}

FLinearColor SGameFlowGraphNode_Transition::StaticGetTransitionColor(UGameFlowGraphNode_Transition* TransNode, bool bIsHovered)
{
	const FLinearColor ActiveColor(1.0f, 0.4f, 0.3f, 1.0f);
	const FLinearColor HoverColor(0.724f, 0.256f, 0.0f, 1.0f);
	FLinearColor BaseColor(0.9f, 0.9f, 0.9f, 1.0f);

	return bIsHovered ? HoverColor : BaseColor;
}

FSlateColor SGameFlowGraphNode_Transition::GetTransitionColor() const
{
	return StaticGetTransitionColor(TransitionGraphNode, (IsHovered() || (PrevStateNodeWidgetPtr.IsValid() && PrevStateNodeWidgetPtr.Pin()->IsHovered())));
}

FText SGameFlowGraphNode_Transition::GetTransitionKey() const
{
	const UGameFlowGraphNode_Base* prevNode = TransitionGraphNode->GetPreviousState();
	const UGameFlowGraphNode_Base* nextNode = TransitionGraphNode->GetNextState();

	if (prevNode && nextNode)
	{
		const TMap<FGuid, FGameFlowTransitionCollection>& transitionCollections = OwningGameFlow->GetTransitionCollections();

		if (transitionCollections.Contains(prevNode->NodeGuid))
		{
			if (transitionCollections[prevNode->NodeGuid].Transitions.Contains(nextNode->NodeGuid))
			{
				return FText::FromString(GetNameSafe(transitionCollections[prevNode->NodeGuid].Transitions[nextNode->NodeGuid]->TransitionKey));
			}
		}
	}

	return FText::GetEmpty();
}

void SGameFlowGraphNode_Transition::OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent)
{
	SGraphNode::OnMouseEnter(MyGeometry, MouseEvent);

	GetOwnerPanel()->AddPinToHoverSet(TransitionGraphNode->GetInputPin());
}

void SGameFlowGraphNode_Transition::OnMouseLeave(const FPointerEvent& MouseEvent)
{
	GetOwnerPanel()->RemovePinFromHoverSet(TransitionGraphNode->GetInputPin());

	SGraphNode::OnMouseLeave(MouseEvent);
}

void SGameFlowGraphNode_Transition::PositionBetweenTwoNodesWithOffset(const FGeometry& StartGeom, const FGeometry& EndGeom, int32 NodeIndex, int32 MaxNodes) const
{
	// Get a reasonable seed point (halfway between the boxes)
	const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

	// Find the (approximate) closest points between the two boxes
	const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	// Position ourselves halfway along the connecting line between the nodes, elevated away perpendicular to the direction of the line
	const float Height = 30.0f;

	const FVector2D DesiredNodeSize = GetDesiredSize();

	FVector2D DeltaPos(EndAnchorPoint - StartAnchorPoint);

	if (DeltaPos.IsNearlyZero())
	{
		DeltaPos = FVector2D(10.0f, 0.0f);
	}

	const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	const FVector2D NewCenter = StartAnchorPoint + (0.5f * DeltaPos) + (Height * Normal);

	FVector2D DeltaNormal = DeltaPos.GetSafeNormal();

	// Calculate node offset in the case of multiple transitions between the same two nodes
	// MultiNodeOffset: the offset where 0 is the centre of the transition, -1 is 1 <size of node>
	// towards the PrevStateNode and +1 is 1 <size of node> towards the NextStateNode.

	const float MutliNodeSpace = 0.2f; // Space between multiple transition nodes (in units of <size of node> )
	const float MultiNodeStep = (1.f + MutliNodeSpace); //Step between node centres (Size of node + size of node spacer)

	const float MultiNodeStart = -((MaxNodes - 1) * MultiNodeStep) / 2.f;
	const float MultiNodeOffset = MultiNodeStart + (NodeIndex * MultiNodeStep);

	// Now we need to adjust the new center by the node size, zoom factor and multi node offset
	const FVector2D NewCorner = NewCenter - (0.5f * DesiredNodeSize) + (DeltaNormal * MultiNodeOffset * DesiredNodeSize.Size());

	GraphNode->NodePosX = NewCorner.X;
	GraphNode->NodePosY = NewCorner.Y;
}

//------------------------------------------------------
// FGameFlowGraphNodeFactory
//------------------------------------------------------

class FGameFlowGraphConnectionDrawingPolicy : public FConnectionDrawingPolicy
{
protected:
	UEdGraph* GraphObj;

	TMap<UEdGraphNode*, int32> NodeWidgetMap;
public:
	//
	FGameFlowGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj);

	// FConnectionDrawingPolicy interface
	virtual void DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params) override;
	virtual void Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& PinGeometries, FArrangedChildren& ArrangedNodes) override;
	virtual void DetermineLinkGeometry(
		FArrangedChildren& ArrangedNodes,
		TSharedRef<SWidget>& OutputPinWidget,
		UEdGraphPin* OutputPin,
		UEdGraphPin* InputPin,
		/*out*/ FArrangedWidget*& StartWidgetGeometry,
		/*out*/ FArrangedWidget*& EndWidgetGeometry
	) override;
	virtual void DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params) override;
	virtual void DrawSplineWithArrow(const FVector2D& StartPoint, const FVector2D& EndPoint, const FConnectionParams& Params) override;
	virtual void DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2D& StartPoint, const FVector2D& EndPoint, UEdGraphPin* Pin) override;
	virtual FVector2D ComputeSplineTangent(const FVector2D& Start, const FVector2D& End) const override;
	// End of FConnectionDrawingPolicy interface

protected:
	void Internal_DrawLineWithArrow(const FVector2D& StartAnchorPoint, const FVector2D& EndAnchorPoint, const FConnectionParams& Params);
};

FGameFlowGraphConnectionDrawingPolicy::FGameFlowGraphConnectionDrawingPolicy(int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const FSlateRect& InClippingRect, FSlateWindowElementList& InDrawElements, UEdGraph* InGraphObj)
	: FConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements), GraphObj(InGraphObj) {}

void FGameFlowGraphConnectionDrawingPolicy::DetermineWiringStyle(UEdGraphPin* OutputPin, UEdGraphPin* InputPin, /*inout*/ FConnectionParams& Params)
{
	Params.AssociatedPin1 = OutputPin;
	Params.AssociatedPin2 = InputPin;
	Params.WireThickness = 1.5f;

	if (InputPin)
	{
		if (UGameFlowGraphNode_Transition* TransNode = Cast<UGameFlowGraphNode_Transition>(InputPin->GetOwningNode()))
		{
			Params.WireColor = SGameFlowGraphNode_Transition::StaticGetTransitionColor(TransNode, HoveredPins.Contains(InputPin));
		}
	}

	const bool bDeemphasizeUnhoveredPins = HoveredPins.Num() > 0;
	if (bDeemphasizeUnhoveredPins)
	{
		ApplyHoverDeemphasis(OutputPin, InputPin, /*inout*/ Params.WireThickness, /*inout*/ Params.WireColor);
	}
}

void FGameFlowGraphConnectionDrawingPolicy::DetermineLinkGeometry(
	FArrangedChildren& ArrangedNodes,
	TSharedRef<SWidget>& OutputPinWidget,
	UEdGraphPin* OutputPin,
	UEdGraphPin* InputPin,
	/*out*/ FArrangedWidget*& StartWidgetGeometry,
	/*out*/ FArrangedWidget*& EndWidgetGeometry
)
{
	if (UGameFlowGraphNode_Start* EntryNode = Cast<UGameFlowGraphNode_Start>(OutputPin->GetOwningNode()))
	{
		StartWidgetGeometry = PinGeometries->Find(OutputPinWidget);

		UGameFlowGraphNode_Base* State = CastChecked<UGameFlowGraphNode_Base>(InputPin->GetOwningNode());
		int32 StateIndex = NodeWidgetMap.FindChecked(State);
		EndWidgetGeometry = &(ArrangedNodes[StateIndex]);
	}
	else if (UGameFlowGraphNode_Transition* TransNode = Cast<UGameFlowGraphNode_Transition>(InputPin->GetOwningNode()))
	{
		UGameFlowGraphNode_Base* PrevState = TransNode->GetPreviousState();
		UGameFlowGraphNode_Base* NextState = TransNode->GetNextState();
		if ((PrevState != NULL) && (NextState != NULL))
		{
			int32* PrevNodeIndex = NodeWidgetMap.Find(PrevState);
			int32* NextNodeIndex = NodeWidgetMap.Find(NextState);
			if ((PrevNodeIndex != NULL) && (NextNodeIndex != NULL))
			{
				StartWidgetGeometry = &(ArrangedNodes[*PrevNodeIndex]);
				EndWidgetGeometry = &(ArrangedNodes[*NextNodeIndex]);
			}
		}
	}
	else
	{
		StartWidgetGeometry = PinGeometries->Find(OutputPinWidget);

		if (TSharedPtr<SGraphPin>* pTargetWidget = PinToPinWidgetMap.Find(InputPin))
		{
			TSharedRef<SGraphPin> InputWidget = (*pTargetWidget).ToSharedRef();
			EndWidgetGeometry = PinGeometries->Find(InputWidget);
		}
	}
}

void FGameFlowGraphConnectionDrawingPolicy::Draw(TMap<TSharedRef<SWidget>, FArrangedWidget>& InPinGeometries, FArrangedChildren& ArrangedNodes)
{
	// Build an acceleration structure to quickly find geometry for the nodes
	NodeWidgetMap.Empty();
	for (int32 NodeIndex = 0; NodeIndex < ArrangedNodes.Num(); ++NodeIndex)
	{
		FArrangedWidget& CurWidget = ArrangedNodes[NodeIndex];
		TSharedRef<SGraphNode> ChildNode = StaticCastSharedRef<SGraphNode>(CurWidget.Widget);
		NodeWidgetMap.Add(ChildNode->GetNodeObj(), NodeIndex);
	}

	// Now draw
	FConnectionDrawingPolicy::Draw(InPinGeometries, ArrangedNodes);
}

void FGameFlowGraphConnectionDrawingPolicy::DrawPreviewConnector(const FGeometry& PinGeometry, const FVector2D& StartPoint, const FVector2D& EndPoint, UEdGraphPin* Pin)
{
	FConnectionParams Params;
	DetermineWiringStyle(Pin, nullptr, /*inout*/ Params);

	const FVector2D SeedPoint = EndPoint;
	const FVector2D AdjustedStartPoint = FGeometryHelper::FindClosestPointOnGeom(PinGeometry, SeedPoint);

	DrawSplineWithArrow(AdjustedStartPoint, EndPoint, Params);
}


void FGameFlowGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FVector2D& StartAnchorPoint, const FVector2D& EndAnchorPoint, const FConnectionParams& Params)
{
	Internal_DrawLineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

void FGameFlowGraphConnectionDrawingPolicy::Internal_DrawLineWithArrow(const FVector2D& StartAnchorPoint, const FVector2D& EndAnchorPoint, const FConnectionParams& Params)
{
	//@TODO: Should this be scaled by zoom factor?
	const float LineSeparationAmount = 4.5f;

	const FVector2D DeltaPos = EndAnchorPoint - StartAnchorPoint;
	const FVector2D UnitDelta = DeltaPos.GetSafeNormal();
	const FVector2D Normal = FVector2D(DeltaPos.Y, -DeltaPos.X).GetSafeNormal();

	// Come up with the final start/end points
	const FVector2D DirectionBias = Normal * LineSeparationAmount;
	const FVector2D LengthBias = ArrowRadius.X * UnitDelta;
	const FVector2D StartPoint = StartAnchorPoint + DirectionBias + LengthBias;
	const FVector2D EndPoint = EndAnchorPoint + DirectionBias - LengthBias;

	// Draw a line/spline
	DrawConnection(WireLayerID, StartPoint, EndPoint, Params);

	// Draw the arrow
	const FVector2D ArrowDrawPos = EndPoint - ArrowRadius;
	const float AngleInRadians = FMath::Atan2(DeltaPos.Y, DeltaPos.X);

	FSlateDrawElement::MakeRotatedBox(
		DrawElementsList,
		ArrowLayerID,
		FPaintGeometry(ArrowDrawPos, ArrowImage->ImageSize * ZoomFactor, ZoomFactor),
		ArrowImage,
		ESlateDrawEffect::None,
		AngleInRadians,
		TOptional<FVector2D>(),
		FSlateDrawElement::RelativeToElement,
		Params.WireColor
	);
}

void FGameFlowGraphConnectionDrawingPolicy::DrawSplineWithArrow(const FGeometry& StartGeom, const FGeometry& EndGeom, const FConnectionParams& Params)
{
	// Get a reasonable seed point (halfway between the boxes)
	const FVector2D StartCenter = FGeometryHelper::CenterOf(StartGeom);
	const FVector2D EndCenter = FGeometryHelper::CenterOf(EndGeom);
	const FVector2D SeedPoint = (StartCenter + EndCenter) * 0.5f;

	// Find the (approximate) closest points between the two boxes
	const FVector2D StartAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(StartGeom, SeedPoint);
	const FVector2D EndAnchorPoint = FGeometryHelper::FindClosestPointOnGeom(EndGeom, SeedPoint);

	DrawSplineWithArrow(StartAnchorPoint, EndAnchorPoint, Params);
}

FVector2D FGameFlowGraphConnectionDrawingPolicy::ComputeSplineTangent(const FVector2D& Start, const FVector2D& End) const
{
	const FVector2D Delta = End - Start;
	const FVector2D NormDelta = Delta.GetSafeNormal();

	return NormDelta;
}

//------------------------------------------------------
// FGameFlowGraphNodeFactory
//------------------------------------------------------

TSharedPtr<class SGraphNode> FGameFlowGraphNodeFactory::CreateNode(class UEdGraphNode* InNode) const
{
	if (UGameFlowGraphNode_State* StateNode = Cast<UGameFlowGraphNode_State>(InNode))
	{
		return SNew(SGameFlowGraphNode_State, StateNode);
	}
	else if (UGameFlowGraphNode_Start* EntryNode = Cast<UGameFlowGraphNode_Start>(InNode))
	{
		return SNew(SGameFlowGraphNode_Start, EntryNode);
	}
	else if (UGameFlowGraphNode_Transition* TransitionNode = Cast<UGameFlowGraphNode_Transition>(InNode))
	{
		return SNew(SGameFlowGraphNode_Transition, TransitionNode);
	}

	return nullptr;
}

//------------------------------------------------------
// FGameFlowGraphPinFactory
//------------------------------------------------------

TSharedPtr<class SGraphPin> FGameFlowGraphPinFactory::CreatePin(class UEdGraphPin* InPin) const
{
	if (InPin->GetSchema()->IsA<UGameFlowGraphSchema>() && InPin->PinType.PinCategory == UGameFlowGraphSchema::PC_Exec)
	{
		return SNew(SGraphPinExec, InPin);
	}

	return nullptr;
}

//------------------------------------------------------
// FGameFlowGraphPinConnectionFactory
//------------------------------------------------------

class FConnectionDrawingPolicy* FGameFlowGraphPinConnectionFactory::CreateConnectionPolicy(const class UEdGraphSchema* Schema, int32 InBackLayerID, int32 InFrontLayerID, float ZoomFactor, const class FSlateRect& InClippingRect, class FSlateWindowElementList& InDrawElements, class UEdGraph* InGraphObj) const
{
	if (Schema->IsA(UGameFlowGraphSchema::StaticClass()))
	{
		return new FGameFlowGraphConnectionDrawingPolicy(InBackLayerID, InFrontLayerID, ZoomFactor, InClippingRect, InDrawElements, InGraphObj);
	}

	return nullptr;
}

//------------------------------------------------------
// UGameFlowGraph
//------------------------------------------------------

UGameFlowGraph::UGameFlowGraph(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	Schema = UGameFlowGraphSchema::StaticClass();
}

//------------------------------------------------------
// FGameFlowGraphSchemaAction_NewComment
//------------------------------------------------------

UEdGraphNode* FGameFlowGraphSchemaAction_NewComment::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode/* = true*/)
{
	UEdGraphNode_Comment* CommentTemplate = NewObject<UEdGraphNode_Comment>();

	FVector2D SpawnLocation = Location;

	CommentTemplate->SetBounds(SelectedNodesBounds);
	SpawnLocation.X = CommentTemplate->NodePosX;
	SpawnLocation.Y = CommentTemplate->NodePosY;

	return FEdGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UEdGraphNode_Comment>(ParentGraph, CommentTemplate, SpawnLocation);
}

//------------------------------------------------------
// FGameFlowGraphSchemaAction_NewNode
//------------------------------------------------------

UEdGraphNode* FGameFlowGraphSchemaAction_NewNode::PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode)
{
	if (NodeTemplate != NULL)
	{
		const FScopedTransaction Transaction(LOCTEXT("Transaction_FGameFlowGraphSchemaAction_NewNode::PerformAction", "Add Node"));
		
		ParentGraph->Modify();
		
		if (FromPin)
		{
			FromPin->Modify();
		}

		NodeTemplate->Rename(NULL, ParentGraph);		
		ParentGraph->AddNode(NodeTemplate, true, bSelectNewNode);

		NodeTemplate->CreateNewGuid();
		NodeTemplate->PostPlacedNewNode();
		NodeTemplate->AllocateDefaultPins();
		NodeTemplate->AutowireNewNode(FromPin);

		NodeTemplate->NodePosX = Location.X;
		NodeTemplate->NodePosY = Location.Y;
		NodeTemplate->SnapToGrid(GetDefault<UEditorStyleSettings>()->GridSnapSize);

		NodeTemplate->SetFlags(RF_Transactional);

		if (NodeTemplate->IsA<UGameFlowGraphNode_State>())
		{
			UGameFlow* gameFlow = ParentGraph->GetTypedOuter<UGameFlow>();
			gameFlow->Modify();
			gameFlow->AddState(NodeTemplate->NodeGuid, FName(NodeTemplate->GetNodeTitle(ENodeTitleType::EditableTitle).ToString()));
		}
	}

	return NodeTemplate;
}

void FGameFlowGraphSchemaAction_NewNode::AddReferencedObjects(FReferenceCollector& Collector)
{
	FEdGraphSchemaAction::AddReferencedObjects(Collector);

	Collector.AddReferencedObject(NodeTemplate);
}

//------------------------------------------------------
// UGameFlowGraphSchema
//------------------------------------------------------

const FName UGameFlowGraphSchema::PC_Exec(TEXT("exec"));

const FName UGameFlowGraphSchema::PC_Transition = FName("Transition");

UGameFlowGraphSchema::UGameFlowGraphSchema(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer) {}

void UGameFlowGraphSchema::CreateDefaultNodesForGraph(UEdGraph& Graph) const
{
	FGraphNodeCreator<UGameFlowGraphNode_Start> NodeCreator(Graph);
	UGameFlowGraphNode_Start* EntryNode = NodeCreator.CreateNode();
	NodeCreator.Finalize();
	SetNodeMetaData(EntryNode, FNodeMetadata::DefaultGraphNode);
}

const FPinConnectionResponse UGameFlowGraphSchema::CanCreateConnection(const UEdGraphPin* PinA, const UEdGraphPin* PinB) const
{
	if (PinA->GetOwningNode() == PinB->GetOwningNode())
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Both are on the same node"));
	}

	const bool bPinAIsEntry = PinA->GetOwningNode()->IsA(UGameFlowGraphNode_Start::StaticClass());
	const bool bPinBIsEntry = PinB->GetOwningNode()->IsA(UGameFlowGraphNode_Start::StaticClass());
	const bool bPinAIsStateNode = PinA->GetOwningNode()->IsA(UGameFlowGraphNode_Base::StaticClass());
	const bool bPinBIsStateNode = PinB->GetOwningNode()->IsA(UGameFlowGraphNode_Base::StaticClass());

	if (bPinAIsEntry || bPinBIsEntry)
	{
		if (bPinAIsEntry && bPinBIsStateNode)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT(""));
		}

		if (bPinBIsEntry && bPinAIsStateNode)
		{
			return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, TEXT(""));
		}

		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Entry must be wired to a State node"));
	}

	const bool bPinAIsTransition = PinA->GetOwningNode()->IsA(UGameFlowGraphNode_Transition::StaticClass());
	const bool bPinBIsTransition = PinB->GetOwningNode()->IsA(UGameFlowGraphNode_Transition::StaticClass());

	if (bPinAIsTransition && bPinBIsTransition)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Cannot wire Transition node to a Transition node"));
	}

	const bool bDirectionsOK = (PinA->Direction == EGPD_Input) && (PinB->Direction == EGPD_Output) || (PinB->Direction == EGPD_Input) && (PinA->Direction == EGPD_Output);

	/*
	if (!bDirectionsOK)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_DISALLOW, TEXT("Directions are not compatible"));
	}
	*/

	if (bPinAIsTransition)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_A, TEXT(""));
	}
	else if (bPinBIsTransition)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_BREAK_OTHERS_B, TEXT(""));
	}
	else if (!bPinAIsTransition && !bPinBIsTransition)
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE_WITH_CONVERSION_NODE, TEXT("Create a transition"));
	}
	else
	{
		return FPinConnectionResponse(CONNECT_RESPONSE_MAKE, TEXT(""));
	}
}

bool UGameFlowGraphSchema::TryCreateConnection(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	if (PinB->Direction == PinA->Direction)
	{
		if (UGameFlowGraphNode_Base* Node = Cast<UGameFlowGraphNode_Base>(PinB->GetOwningNode()))
		{
			if (PinA->Direction == EGPD_Input)
			{
				PinB = Node->GetOutputPin();
			}
			else
			{
				PinB = Node->GetInputPin();
			}
		}
	}

	return UEdGraphSchema::TryCreateConnection(PinA, PinB);
}

bool UGameFlowGraphSchema::CreateAutomaticConversionNodeAndConnections(UEdGraphPin* PinA, UEdGraphPin* PinB) const
{
	UGameFlowGraphNode_Base* NodeA = Cast<UGameFlowGraphNode_Base>(PinA->GetOwningNode());
	UGameFlowGraphNode_Base* NodeB = Cast<UGameFlowGraphNode_Base>(PinB->GetOwningNode());

	if ((NodeA != NULL) && (NodeB != NULL)
		&& (NodeA->GetInputPin() != NULL) && (NodeA->GetOutputPin() != NULL)
		&& (NodeB->GetInputPin() != NULL) && (NodeB->GetOutputPin() != NULL))
	{
		UGameFlowGraphNode_Transition* TransitionNode = FGameFlowGraphSchemaAction_NewNode::SpawnNodeFromTemplate<UGameFlowGraphNode_Transition>(NodeA->GetGraph(), NewObject<UGameFlowGraphNode_Transition>(), FVector2D(0.0f, 0.0f), false);

		// We cant implement this logic in PerformAction because links will be created only here
		UGameFlow* gameFlow = PinA->GetOwningNode()->GetGraph()->GetTypedOuter<UGameFlow>();
		gameFlow->Modify();

		if (PinA->Direction == EGPD_Output)
		{
			TransitionNode->CreateConnections(NodeA, NodeB);
			gameFlow->AddTransition(NodeA->NodeGuid, NodeB->NodeGuid);
		}
		else
		{
			TransitionNode->CreateConnections(NodeB, NodeA);
			gameFlow->AddTransition(NodeB->NodeGuid, NodeA->NodeGuid);
		}

		return true;
	}

	return false;
}

void UGameFlowGraphSchema::GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const
{
	// Add state node
	{
		TSharedPtr<FGameFlowGraphSchemaAction_NewNode> Action = AddNewActionAs<FGameFlowGraphSchemaAction_NewNode>(ContextMenuBuilder, FText::GetEmpty(), LOCTEXT("FGameFlowGraphSchema_Action_MenuDesc_AddState", "Add State"), LOCTEXT("FGameFlowGraphSchema_Action_Tooltip_AddState", "Add new State"));
		Action->NodeTemplate = NewObject<UGameFlowGraphNode_State>(ContextMenuBuilder.OwnerOfTemporaries);
	}

	// Entry point (only if doesn't already exist)
	{
		bool bHasEntry = false;
		for (auto NodeIt = ContextMenuBuilder.CurrentGraph->Nodes.CreateConstIterator(); NodeIt; ++NodeIt)
		{
			UEdGraphNode* Node = *NodeIt;
			if (const UGameFlowGraphNode_Start* StateNode = Cast<UGameFlowGraphNode_Start>(Node))
			{
				bHasEntry = true;
				break;
			}
		}

		if (!bHasEntry)
		{
			TSharedPtr<FGameFlowGraphSchemaAction_NewNode> Action = AddNewActionAs<FGameFlowGraphSchemaAction_NewNode>(ContextMenuBuilder, FText::GetEmpty(), LOCTEXT("FGameFlowGraphSchema_Action_MenuDesc_AddEntryPoint", "Add Entry Point"), LOCTEXT("FGameFlowGraphSchema_Action_Tooltip_AddEntryPoint", "Define Entry Point"));
			Action->NodeTemplate = NewObject<UGameFlowGraphNode_Start>(ContextMenuBuilder.OwnerOfTemporaries);
		}
	}
}

EGraphType UGameFlowGraphSchema::GetGraphType(const UEdGraph* TestEdGraph) const
{
	return GT_StateMachine;
}

void UGameFlowGraphSchema::GetContextMenuActions(UToolMenu* Menu, UGraphNodeContextMenuContext* Context) const
{
	check(Context && Context->Graph);

	if (Context->Node)
	{
		{
			FToolMenuSection& Section = Menu->AddSection("GameFlowGraphSchema_Section_Label_NodeActions", LOCTEXT("FGameFlowGraphSchema_Section_Label_NodeActions", "Node Actions"));
			if (!Context->bIsDebugging)
			{
				// Node contextual actions
				Section.AddMenuEntry(FGenericCommands::Get().Delete);
				Section.AddMenuEntry(FGenericCommands::Get().Cut);
				Section.AddMenuEntry(FGenericCommands::Get().Copy);
				Section.AddMenuEntry(FGenericCommands::Get().Duplicate);
				Section.AddMenuEntry(FGraphEditorCommands::Get().ReconstructNodes);
				Section.AddMenuEntry(FGraphEditorCommands::Get().BreakNodeLinks);
				if (Context->Node->bCanRenameNode)
				{
					Section.AddMenuEntry(FGenericCommands::Get().Rename);
				}
			}
		}
	}

	Super::GetContextMenuActions(Menu, Context);
}

FLinearColor UGameFlowGraphSchema::GetPinTypeColor(const FEdGraphPinType& PinType) const
{
	if (PinType.PinCategory == TEXT("Transition"))
	{
		return FLinearColor::White;
	}

	return GetDefault<UEdGraphSchema_K2>()->GetPinTypeColor(PinType);
}

void UGameFlowGraphSchema::GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const
{
	DisplayInfo.PlainName = FText::FromString(Graph.GetName());
	DisplayInfo.DisplayName = DisplayInfo.PlainName;
}

void UGameFlowGraphSchema::GetAssetsNodeHoverMessage(const TArray<FAssetData>& Assets, const UEdGraphNode* HoverNode, FString& OutTooltipText, bool& OutOkIcon) const
{
	UAnimationAsset* Asset = FAssetData::GetFirstAsset<UAnimationAsset>(Assets);
	if ((Asset == NULL) || (HoverNode == NULL))
	{
		OutTooltipText = TEXT("");
		OutOkIcon = false;
		return;
	}

	const UGameFlowGraphNode_State* StateNodeUnderCursor = Cast<const UGameFlowGraphNode_State>(HoverNode);
	if (StateNodeUnderCursor != NULL)
	{
		OutOkIcon = true;
		OutTooltipText = FString::Printf(TEXT("Change node to play %s"), *(Asset->GetName()));
	}
	else
	{
		OutTooltipText = TEXT("");
		OutOkIcon = false;
	}
}

void UGameFlowGraphSchema::GetAssetsPinHoverMessage(const TArray<FAssetData>& Assets, const UEdGraphPin* HoverPin, FString& OutTooltipText, bool& OutOkIcon) const
{
	// unused for state machines?

	OutTooltipText = TEXT("");
	OutOkIcon = false;
}

void UGameFlowGraphSchema::BreakNodeLinks(UEdGraphNode& TargetNode) const
{
	const FScopedTransaction Transaction(LOCTEXT("Transaction_UGameFlowGraphSchema::BreakNodeLinks", "Break Node Links"));

	Super::BreakNodeLinks(TargetNode);
}

void UGameFlowGraphSchema::BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const
{
	const FScopedTransaction Transaction(LOCTEXT("Transaction_UGameFlowGraphSchema::BreakPinLinks", "Break Pin Links"));

	Super::BreakPinLinks(TargetPin, bSendsNodeNotification);
}

void UGameFlowGraphSchema::BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const
{
	const FScopedTransaction Transaction(LOCTEXT("Transaction_UGameFlowGraphSchema::BreakSinglePinLink", "Break Pin Link"));

	Super::BreakSinglePinLink(SourcePin, TargetPin);
}

TSharedPtr<FEdGraphSchemaAction> UGameFlowGraphSchema::GetCreateCommentAction() const
{
	return TSharedPtr<FEdGraphSchemaAction>(static_cast<FEdGraphSchemaAction*>(new FGameFlowGraphSchemaAction_NewComment));
}

//------------------------------------------------------
// UFactory_GameFlow
//------------------------------------------------------

UFactory_GameFlow::UFactory_GameFlow(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bCreateNew = true;
	bEditAfterNew = true;
	SupportedClass = UGameFlow::StaticClass();
}

UObject* UFactory_GameFlow::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<UGameFlow>(InParent, InClass, InName, Flags);
}

FText UFactory_GameFlow::GetDisplayName() const { return LOCTEXT("UFactory_GameFlow_DisplayName", "Game Flow"); }

uint32 UFactory_GameFlow::GetMenuCategories() const { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// UFactory_GameFlowContext
//------------------------------------------------------

UFactory_GameFlowContext::UFactory_GameFlowContext(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bCreateNew = true;
	SupportedClass = UGameFlowContext_MapBased::StaticClass();
}

UObject* UFactory_GameFlowContext::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<UGameFlowContext_MapBased>(InParent, InClass, InName, Flags);
}

FText UFactory_GameFlowContext::GetDisplayName() const { return LOCTEXT("UFactory_GameFlowContext_DisplayName", "Game Flow Context (Map based)"); }

uint32 UFactory_GameFlowContext::GetMenuCategories() const { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// UFactory_GameFlowTransitionKey
//------------------------------------------------------

UFactory_GameFlowTransitionKey::UFactory_GameFlowTransitionKey(const FObjectInitializer& ObjectInitializer) :Super(ObjectInitializer)
{
	bCreateNew = true;
	SupportedClass = UGameFlowTransitionKey::StaticClass();
}

UObject* UFactory_GameFlowTransitionKey::FactoryCreateNew(UClass* InClass, UObject* InParent, FName InName, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext)
{
	return NewObject<UGameFlowTransitionKey>(InParent, InClass, InName, Flags);
}

FText UFactory_GameFlowTransitionKey::GetDisplayName() const { return LOCTEXT("UFactory_GameFlowTransitionKey_DisplayName", "Game Flow Transition Key"); }

uint32 UFactory_GameFlowTransitionKey::GetMenuCategories() const { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// FGameFlowEditor
//------------------------------------------------------

class FGameFlowEditor : public FEditorUndoClient, public FAssetEditorToolkit
{
public:

	static const FName AppIdentifier;
	static const FName DetailsTabId;
	static const FName GraphTabId;

	void InitGameFlowEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UGameFlow* gameFlow);

	FGameFlowEditor()
	{
		GEditor->RegisterForUndo(this);
	}

	~FGameFlowEditor()
	{
		GEditor->UnregisterForUndo(this);
	}

	virtual void RegisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;
	virtual void UnregisterTabSpawners(const TSharedRef<FTabManager>& TabManager) override;

	virtual FLinearColor GetWorldCentricTabColorScale() const override { return FLinearColor::White; }

	virtual FName GetToolkitFName() const override { return FName("GameFlowEditor"); }
	virtual FText GetBaseToolkitName() const override { return LOCTEXT("FGameFlowEditor_BaseToolkitName", "Game Flow Editor"); }
	virtual FString GetWorldCentricTabPrefix() const override { return "GameFlowEditor"; }

protected:

	TSharedRef<SDockTab> SpawnTab_DetailsTab(const FSpawnTabArgs& Args);
	TSharedRef<SDockTab> SpawnTab_GraphTab(const FSpawnTabArgs& Args);

	void CreateCommandList();

	void OnSelectionChanged(const TSet<UObject*>& selectedNodes);

	void OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged);

	//~ Begin FEditorUndoClient Interface
	virtual void PostUndo(bool bSuccess) override;
	virtual void PostRedo(bool bSuccess) override;
	// End of FEditorUndoClient

	FGraphPanelSelectionSet GetSelectedNodes() const;

	void SelectAllNodes();

	bool CanSelectAllNodes() const { return true; }

	void DeleteSelectedNodes();

	bool CanDeleteNodes() const;

	void CopySelectedNodes();

	bool CanCopyNodes() const;

	void DeleteSelectedDuplicatableNodes();

	void CutSelectedNodes() { CopySelectedNodes(); DeleteSelectedDuplicatableNodes(); }

	bool CanCutNodes() const { return CanCopyNodes() && CanDeleteNodes(); }

	void PasteNodes();

	bool CanPasteNodes() const;

	void DuplicateNodes() { CopySelectedNodes(); PasteNodes(); }

	bool CanDuplicateNodes() const { return CanCopyNodes(); }

	void OnCreateComment();

	bool CanCreateComment() const;

protected:

	UGameFlow* GameFlow;

	TSharedPtr<FUICommandList> GraphEditorCommands;

	TWeakPtr<SGraphEditor> GraphEditorPtr;

	TSharedPtr<IDetailsView> DetailsView;
};

const FName FGameFlowEditor::AppIdentifier(TEXT("FGameFlowEditor_AppIdentifier"));
const FName FGameFlowEditor::DetailsTabId(TEXT("FGameFlowEditor_DetailsTab_Id"));
const FName FGameFlowEditor::GraphTabId(TEXT("FGameFlowEditor_GraphTab_Id"));

void FGameFlowEditor::InitGameFlowEditor(const EToolkitMode::Type Mode, const TSharedPtr< class IToolkitHost >& InitToolkitHost, UGameFlow* gameFlow)
{
	check(gameFlow != NULL);

	GameFlow = gameFlow;

	GameFlow->SetFlags(RF_Transactional);

	TSharedRef<FTabManager::FLayout> StandaloneDefaultLayout = FTabManager::NewLayout("FGameFlowEditor_StandaloneDefaultLayout")
		->AddArea
		(
			FTabManager::NewPrimaryArea()->SetOrientation(Orient_Vertical)
			->Split
			(
				FTabManager::NewStack()
				->SetSizeCoefficient(0.1f)
				->SetHideTabWell(true)
			)
			->Split
			(
				FTabManager::NewSplitter()->SetOrientation(Orient_Horizontal)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.3f)
					->AddTab(DetailsTabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
				->Split
				(
					FTabManager::NewStack()
					->SetSizeCoefficient(0.7f)
					->AddTab(GraphTabId, ETabState::OpenedTab)
					->SetHideTabWell(true)
				)
			)
		);

	FAssetEditorToolkit::InitAssetEditor(Mode, InitToolkitHost, AppIdentifier, StandaloneDefaultLayout, true, true, GameFlow);
}

void FGameFlowEditor::RegisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	WorkspaceMenuCategory = InTabManager->AddLocalWorkspaceMenuCategory(LOCTEXT("FGameFlowEditor_WorkspaceMenuCategory", "Game Flow Editor"));
	TSharedRef<FWorkspaceItem> WorkspaceMenuCategoryRef = WorkspaceMenuCategory.ToSharedRef();

	FAssetEditorToolkit::RegisterTabSpawners(InTabManager);

	InTabManager->RegisterTabSpawner(DetailsTabId, FOnSpawnTab::CreateSP(this, &FGameFlowEditor::SpawnTab_DetailsTab))
		.SetDisplayName(LOCTEXT("FGameFlowEditor_Tab_DisplayName_DetailsTab", "Details"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "LevelEditor.Tabs.Details"));

	InTabManager->RegisterTabSpawner(GraphTabId, FOnSpawnTab::CreateSP(this, &FGameFlowEditor::SpawnTab_GraphTab))
		.SetDisplayName(LOCTEXT("FGameFlowEditor_Tab_DisplayName_GraphTab", "Graph"))
		.SetGroup(WorkspaceMenuCategoryRef)
		.SetIcon(FSlateIcon(FAppStyle::GetAppStyleSetName(), "GraphEditor.EventGraph_16x"));
}

void FGameFlowEditor::UnregisterTabSpawners(const TSharedRef<class FTabManager>& InTabManager)
{
	FAssetEditorToolkit::UnregisterTabSpawners(InTabManager);

	InTabManager->UnregisterTabSpawner(GraphTabId);
	InTabManager->UnregisterTabSpawner(DetailsTabId);
}

TSharedRef<SDockTab> FGameFlowEditor::SpawnTab_DetailsTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId() == DetailsTabId);

	FPropertyEditorModule& PropertyEditorModule = FModuleManager::GetModuleChecked<FPropertyEditorModule>("PropertyEditor");

	FDetailsViewArgs DetailsViewArgs = FDetailsViewArgs();
	DetailsViewArgs.bUpdatesFromSelection = false;
	DetailsViewArgs.bLockable = false;
	DetailsViewArgs.bAllowSearch = false;
	DetailsViewArgs.NameAreaSettings = FDetailsViewArgs::HideNameArea;
	DetailsViewArgs.bHideSelectionTip = true;

	DetailsView = PropertyEditorModule.CreateDetailView(DetailsViewArgs);
	DetailsView->SetObject(GameFlow);

	return SNew(SDockTab).Label(LOCTEXT("FGameFlowEditor_Tab_Label_DetailsTab", "Details"))[DetailsView.ToSharedRef()];
}

TSharedRef<SDockTab> FGameFlowEditor::SpawnTab_GraphTab(const FSpawnTabArgs& Args)
{
	check(Args.GetTabId().TabType == GraphTabId);

	check(GameFlow != NULL);

	if (GameFlow->EdGraph == NULL)
	{
		UGameFlowGraph* gameFlowGraph = NewObject<UGameFlowGraph>(GameFlow, NAME_None, RF_Transactional);

		GameFlow->EdGraph = gameFlowGraph;
		GameFlow->EdGraph->GetSchema()->CreateDefaultNodesForGraph(*GameFlow->EdGraph);
	}

	check(GameFlow->EdGraph != NULL);

	FGraphAppearanceInfo AppearanceInfo;
	AppearanceInfo.CornerText = LOCTEXT("FGameFlowEditor_Tab_AppearanceInfo_CornerText_GraphTab", "Game Flow");

	SGraphEditor::FGraphEditorEvents InEvents;
	InEvents.OnSelectionChanged = SGraphEditor::FOnSelectionChanged::CreateSP(this, &FGameFlowEditor::OnSelectionChanged);
	InEvents.OnTextCommitted = FOnNodeTextCommitted::CreateSP(this, &FGameFlowEditor::OnNodeTitleCommitted);

	CreateCommandList();

	return SNew(SDockTab)
		.Label(LOCTEXT("FGameFlowEditor_Tab_Label_GraphTab", "Graph"))
		.TabColorScale(GetTabColorScale())
		[
			SAssignNew(GraphEditorPtr, SGraphEditor)
				.AdditionalCommands(GraphEditorCommands)
				.Appearance(AppearanceInfo)
				.GraphEvents(InEvents)
				.TitleBar(SNew(STextBlock).Text(LOCTEXT("FGameFlowEditor_Tab_Title_GraphTab", "Game Flow")).TextStyle(FAppStyle::Get(), TEXT("GraphBreadcrumbButtonText")))
				.GraphToEdit(GameFlow->EdGraph)
		];
}

void FGameFlowEditor::CreateCommandList()
{
	if (GraphEditorCommands.IsValid()) return;

	GraphEditorCommands = MakeShareable(new FUICommandList);

	GraphEditorCommands->MapAction(FGenericCommands::Get().SelectAll,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::SelectAllNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanSelectAllNodes)
	);

	GraphEditorCommands->MapAction(FGenericCommands::Get().Delete,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::DeleteSelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanDeleteNodes)
	);

	GraphEditorCommands->MapAction(FGenericCommands::Get().Copy,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::CopySelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanCopyNodes)
	);

	GraphEditorCommands->MapAction(FGenericCommands::Get().Cut,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::CutSelectedNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanCutNodes)
	);

	GraphEditorCommands->MapAction(FGenericCommands::Get().Paste,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::PasteNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanPasteNodes)
	);

	GraphEditorCommands->MapAction(FGenericCommands::Get().Duplicate,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::DuplicateNodes),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanDuplicateNodes)
	);

	GraphEditorCommands->MapAction(
		FGraphEditorCommands::Get().CreateComment,
		FExecuteAction::CreateRaw(this, &FGameFlowEditor::OnCreateComment),
		FCanExecuteAction::CreateRaw(this, &FGameFlowEditor::CanCreateComment)
	);
}

void FGameFlowEditor::OnSelectionChanged(const TSet<UObject*>& selectedNodes)
{
	if (selectedNodes.Num() == 1)
	{
		if (const UGameFlowGraphNode_State* stateNode = Cast<UGameFlowGraphNode_State>(*selectedNodes.begin()))
		{
			const TMap<FGuid, TObjectPtr<UGameFlowState>>& states = GameFlow->GetStates();

			if (states.Contains(stateNode->NodeGuid))
			{
				return DetailsView->SetObject(states[stateNode->NodeGuid]);
			}
		}
		else if (const UGameFlowGraphNode_Transition* transitionNode = Cast<UGameFlowGraphNode_Transition>(*selectedNodes.begin()))
		{
			const UGameFlowGraphNode_Base* prevNode = transitionNode->GetPreviousState();
			const UGameFlowGraphNode_Base* nextNode = transitionNode->GetNextState();

			if (prevNode && nextNode)
			{
				const TMap<FGuid, FGameFlowTransitionCollection>& transitionCollections = GameFlow->GetTransitionCollections();

				if (transitionCollections.Contains(prevNode->NodeGuid))
				{
					if (transitionCollections[prevNode->NodeGuid].Transitions.Contains(nextNode->NodeGuid))
					{
						return DetailsView->SetObject(transitionCollections[prevNode->NodeGuid].Transitions[nextNode->NodeGuid]);
					}
				}
			}
		}
	}

	return DetailsView->SetObject(GameFlow);
}

void FGameFlowEditor::OnNodeTitleCommitted(const FText& NewText, ETextCommit::Type CommitInfo, UEdGraphNode* NodeBeingChanged)
{
	if (NodeBeingChanged)
	{
		const FScopedTransaction Transaction(LOCTEXT("Transaction_FGameFlowEditor::OnNodeTitleCommitted", "Rename Node"));
		NodeBeingChanged->Modify();
		NodeBeingChanged->OnRenameNode(NewText.ToString());
	}
}

void FGameFlowEditor::PostUndo(bool bSuccess)
{
	if (bSuccess)
	{
		// Clear selection, to avoid holding refs to nodes that go away
		if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
		{
			if (graphEditor.IsValid())
			{
				graphEditor->ClearSelectionSet();
				graphEditor->NotifyGraphChanged();
			}
		}
		FSlateApplication::Get().DismissAllMenus();
	}
}

void FGameFlowEditor::PostRedo(bool bSuccess)
{
	if (bSuccess)
	{
		// Clear selection, to avoid holding refs to nodes that go away
		if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
		{
			if (graphEditor.IsValid())
			{
				graphEditor->ClearSelectionSet();
				graphEditor->NotifyGraphChanged();
			}
		}
		FSlateApplication::Get().DismissAllMenus();
	}
}

FGraphPanelSelectionSet FGameFlowEditor::GetSelectedNodes() const
{
	FGraphPanelSelectionSet CurrentSelection;

	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			CurrentSelection = graphEditor->GetSelectedNodes();
		}
	}

	return CurrentSelection;
}

void FGameFlowEditor::SelectAllNodes()
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			graphEditor->SelectAllNodes();
		}
	}
}

void FGameFlowEditor::DeleteSelectedNodes()
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			const FScopedTransaction Transaction(FGenericCommands::Get().Delete->GetDescription());

			UEdGraph* graph = graphEditor->GetCurrentGraph();
			graph->Modify();

			const FGraphPanelSelectionSet SelectedNodes = graphEditor->GetSelectedNodes();
			graphEditor->ClearSelectionSet();

			TArray<UGameFlowGraphNode_Transition*> transitionNodes;
			TArray<UEdGraphNode*> otherNodes;

			for (FGraphPanelSelectionSet::TConstIterator NodeIt(SelectedNodes); NodeIt; ++NodeIt)
			{
				if (UEdGraphNode* node = Cast<UEdGraphNode>(*NodeIt))
				{
					if (node->CanUserDeleteNode())
					{
						if (UGameFlowGraphNode_Transition* transitionNode = Cast<UGameFlowGraphNode_Transition>(node))
						{
							transitionNodes.Add(transitionNode);
						}
						else
						{
							otherNodes.Add(node);
						}
					}
				}
			}

			if (transitionNodes.Num() > 0 || otherNodes.ContainsByPredicate([](const UEdGraphNode& graphNode) { return graphNode.IsA<UGameFlowGraphNode_State>(); }))
			{
				GameFlow->Modify();
			}

			// We cant implement this logic in DestroyNode because links will be already broken and we cant get prevNode and nextNode

			for (UGameFlowGraphNode_Transition* transitionNode : transitionNodes)
			{
				const UGameFlowGraphNode_Base* prevNode = transitionNode->GetPreviousState();
				const UGameFlowGraphNode_Base* nextNode = transitionNode->GetNextState();
				GameFlow->DestroyTransition(prevNode->NodeGuid, nextNode->NodeGuid);

				graph->GetSchema()->BreakPinLinks(*transitionNode->Pins[0], false);
			}

			for (UEdGraphNode* otherNode : otherNodes)
			{
				GameFlow->DestroyState(otherNode->NodeGuid);

				otherNode->Modify();
				graph->GetSchema()->BreakNodeLinks(*otherNode);

				otherNode->DestroyNode();
			}
		}
	}
}

bool FGameFlowEditor::CanDeleteNodes() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if (Node && Node->CanUserDeleteNode()) return true;
	}

	return false;
}

void FGameFlowEditor::CopySelectedNodes()
{
	FGraphPanelSelectionSet InitialSelectedNodes = GetSelectedNodes();

	TSet<UEdGraphNode*> graphNodesToSelect;

	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{

			for (UEdGraphNode* graphNodeToSelect : graphNodesToSelect)
			{
				graphEditor->SetNodeSelection(graphNodeToSelect, true);
			}
		}
	}

	FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();

	for (FGraphPanelSelectionSet::TIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);

		if (Node == nullptr)
		{
			SelectedIter.RemoveCurrent();
			continue;
		}

		Node->PrepareForCopying();

		if (UGameFlowGraphNode_State* stateNode = Cast<UGameFlowGraphNode_State>(Node))
		{
			stateNode->PreviousOuter = stateNode->GetGraph()->GetTypedOuter<UGameFlow>();
		}
	}

	FString ExportedText;
	FEdGraphUtilities::ExportNodesToText(SelectedNodes, ExportedText);
	FPlatformApplicationMisc::ClipboardCopy(*ExportedText);

	for (FGraphPanelSelectionSet::TIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);

		if (UGameFlowGraphNode_State* stateNode = Cast<UGameFlowGraphNode_State>(Node))
		{
			stateNode->PreviousOuter = nullptr;
		}
	}
}

bool FGameFlowEditor::CanCopyNodes() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	for (FGraphPanelSelectionSet::TConstIterator SelectedIter(SelectedNodes); SelectedIter; ++SelectedIter)
	{
		UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
		if (Node && Node->CanDuplicateNode()) return true;
	}

	return false;
}

void FGameFlowEditor::DeleteSelectedDuplicatableNodes()
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			const FGraphPanelSelectionSet OldSelectedNodes = graphEditor->GetSelectedNodes();
			graphEditor->ClearSelectionSet();

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
			{
				UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter);
				if (Node && Node->CanDuplicateNode())
				{
					graphEditor->SetNodeSelection(Node, true);
				}
			}

			DeleteSelectedNodes();

			graphEditor->ClearSelectionSet();

			for (FGraphPanelSelectionSet::TConstIterator SelectedIter(OldSelectedNodes); SelectedIter; ++SelectedIter)
			{
				if (UEdGraphNode* Node = Cast<UEdGraphNode>(*SelectedIter))
				{
					graphEditor->SetNodeSelection(Node, true);
				}
			}
		}
	}
}

void FGameFlowEditor::PasteNodes()
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			FVector2D Location = graphEditor->GetPasteLocation();

			UEdGraph* EdGraph = graphEditor->GetCurrentGraph();

			// Undo/Redo support
			const FScopedTransaction Transaction(FGenericCommands::Get().Paste->GetDescription());

			EdGraph->Modify();

			// Clear the selection set (newly pasted stuff will be selected)
			graphEditor->ClearSelectionSet();

			// Grab the text to paste from the clipboard.
			FString TextToImport;
			FPlatformApplicationMisc::ClipboardPaste(TextToImport);

			// Import the nodes
			TSet<UEdGraphNode*> PastedNodes;
			FEdGraphUtilities::ImportNodesFromText(EdGraph, TextToImport, /*out*/ PastedNodes);

			TMap<FGuid, FGuid> guidRemapping;
			TMap<FGuid, UGameFlowState*> stateRemapping;
			TMap<FGuid, const FGameFlowTransitionCollection*> transitionRemapping;

			TSet<UGameFlowGraphNode_Transition*> transitionNodes;

			// Duplicate Game Flow States
			for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
			{
				UEdGraphNode* Node = *It;

				guidRemapping.Add(Node->NodeGuid, FGuid::NewGuid());

				if (UGameFlowGraphNode_Transition* transitionNode = Cast<UGameFlowGraphNode_Transition>(Node))
				{
					transitionNodes.Add(transitionNode);
				}

				if (UGameFlowGraphNode_State* stateNode = Cast<UGameFlowGraphNode_State>(Node))
				{
					const TMap<FGuid, TObjectPtr<UGameFlowState>>& prevOuterStates = stateNode->PreviousOuter->GetStates();
					stateRemapping.Add(Node->NodeGuid, prevOuterStates[Node->NodeGuid]);

					if (transitionRemapping.IsEmpty())
					{
						const TMap<FGuid, FGameFlowTransitionCollection>& transitionCollections = stateNode->PreviousOuter->GetTransitionCollections();

						if (!transitionCollections.IsEmpty())
						{
							transitionRemapping.Reserve(transitionCollections.Num());

							for (const TPair<FGuid, FGameFlowTransitionCollection>& transitionCollectionEntry : transitionCollections)
							{
								transitionRemapping.Add(transitionCollectionEntry.Key, &transitionCollectionEntry.Value);
							}
						}
					}

					stateNode->PreviousOuter = nullptr;
				}
			}

			for (const TPair<FGuid, UGameFlowState*>& stateRemappingEntry : stateRemapping)
			{
				UGameFlowState* state = GameFlow->AddState(guidRemapping[stateRemappingEntry.Key], stateRemappingEntry.Value->StateTitle);
				state->SubFlow = stateRemappingEntry.Value->SubFlow;
				state->bInstancedSubFlow = stateRemappingEntry.Value->bInstancedSubFlow;
				state->bResetSubFlowOnEnterState = stateRemappingEntry.Value->bResetSubFlowOnEnterState;
				state->bResetSubFlowOnExitState = stateRemappingEntry.Value->bResetSubFlowOnExitState;

				for (const TObjectPtr<UGameFlowStep>& step : stateRemappingEntry.Value->Steps)
				{
					state->Steps.Add(DuplicateObject(step, state));
				}

				state->TransitionKey = stateRemappingEntry.Value->TransitionKey;
			}

			for (TPair<FGuid, const FGameFlowTransitionCollection*>& transitionRemappingEntry : transitionRemapping)
			{
				for (const TPair<FGuid, TObjectPtr<UGameFlowTransition>>& transitionEntry : transitionRemappingEntry.Value->Transitions)
				{
					if (guidRemapping.Contains(transitionRemappingEntry.Key) && guidRemapping.Contains(transitionEntry.Key))
					{
						bool shouldBeAdded = false;

						for (UGameFlowGraphNode_Transition* transitionNode : transitionNodes)
						{
							if (transitionNode->GetPreviousState()->NodeGuid == transitionRemappingEntry.Key && transitionNode->GetNextState()->NodeGuid == transitionEntry.Key)
							{
								shouldBeAdded = true;
								break;
							}
						}

						if (shouldBeAdded)
						{
							UGameFlowTransition* transition = GameFlow->AddTransition(guidRemapping[transitionRemappingEntry.Key], guidRemapping[transitionEntry.Key]);
							transition->TransitionKey = transitionEntry.Value->TransitionKey;
						}
					}
				}
			}

			//Average position of nodes so we can move them while still maintaining relative distances to each other
			FVector2D AvgNodePosition(0.0f, 0.0f);

			for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
			{
				UEdGraphNode* Node = *It;
				AvgNodePosition.X += Node->NodePosX;
				AvgNodePosition.Y += Node->NodePosY;
			}

			if (PastedNodes.Num() > 0)
			{
				float InvNumNodes = 1.0f / float(PastedNodes.Num());
				AvgNodePosition.X *= InvNumNodes;
				AvgNodePosition.Y *= InvNumNodes;
			}

			for (TSet<UEdGraphNode*>::TIterator It(PastedNodes); It; ++It)
			{
				UEdGraphNode* Node = *It;

				// Select the newly pasted stuff
				graphEditor->SetNodeSelection(Node, true);

				Node->NodePosX = (Node->NodePosX - AvgNodePosition.X) + Location.X;
				Node->NodePosY = (Node->NodePosY - AvgNodePosition.Y) + Location.Y;

				Node->SnapToGrid(GetDefault<UEditorStyleSettings>()->GridSnapSize);

				// Give new node a different Guid from the old one
				Node->NodeGuid = guidRemapping[Node->NodeGuid];
			}

			EdGraph->NotifyGraphChanged();

			GameFlow->PostEditChange();
			GameFlow->MarkPackageDirty();
		}
	}
}

bool FGameFlowEditor::CanPasteNodes() const
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			FString ClipboardContent;
			FPlatformApplicationMisc::ClipboardPaste(ClipboardContent);

			return FEdGraphUtilities::CanImportNodesFromText(graphEditor->GetCurrentGraph(), ClipboardContent);
		}
	}

	return false;
}

void FGameFlowEditor::OnCreateComment()
{
	if (TSharedPtr<SGraphEditor> graphEditor = GraphEditorPtr.Pin())
	{
		if (graphEditor.IsValid())
		{
			TSharedPtr<FEdGraphSchemaAction> Action = graphEditor->GetCurrentGraph()->GetSchema()->GetCreateCommentAction();

			TSharedPtr<FGameFlowGraphSchemaAction_NewComment> newCommentAction = StaticCastSharedPtr<FGameFlowGraphSchemaAction_NewComment>(Action);

			if (newCommentAction.IsValid())
			{
				graphEditor->GetBoundsForSelectedNodes(newCommentAction->SelectedNodesBounds, 50);
				newCommentAction->PerformAction(graphEditor->GetCurrentGraph(), nullptr, FVector2D());
			}
		}
	}
}

bool FGameFlowEditor::CanCreateComment() const
{
	const FGraphPanelSelectionSet SelectedNodes = GetSelectedNodes();
	return SelectedNodes.Num() > 0;
}

//------------------------------------------------------
// FAssetTypeActions_GameFlow
//------------------------------------------------------

FText FAssetTypeActions_GameFlow::GetName() const { return LOCTEXT("FAssetTypeActions_GameFlow_Name", "Game Flow"); }

UClass* FAssetTypeActions_GameFlow::GetSupportedClass() const { return UGameFlow::StaticClass(); }

void FAssetTypeActions_GameFlow::OpenAssetEditor(const TArray<UObject*>& InObjects, TSharedPtr<IToolkitHost> EditWithinLevelEditor)
{
	EToolkitMode::Type Mode = EditWithinLevelEditor.IsValid() ? EToolkitMode::WorldCentric : EToolkitMode::Standalone;

	for (TArray<UObject*>::TConstIterator ObjIt = InObjects.CreateConstIterator(); ObjIt; ++ObjIt)
	{
		if (UGameFlow* gameFlow = Cast<UGameFlow>(*ObjIt))
		{
			TSharedRef<FGameFlowEditor> NewEditor(new FGameFlowEditor());
			NewEditor->InitGameFlowEditor(Mode, EditWithinLevelEditor, gameFlow);
		}
	}
}

uint32 FAssetTypeActions_GameFlow::GetCategories() { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// FAssetTypeActions_GameFlowContext
//------------------------------------------------------

FText FAssetTypeActions_GameFlowContext::GetName() const { return LOCTEXT("FAssetTypeActions_GameFlowContext_Name", "Game Flow Context (Map based)"); }

UClass* FAssetTypeActions_GameFlowContext::GetSupportedClass() const { return UGameFlowContext_MapBased::StaticClass(); }

uint32 FAssetTypeActions_GameFlowContext::GetCategories() { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// FAssetTypeActions_GameFlowTransitionKey
//------------------------------------------------------

FText FAssetTypeActions_GameFlowTransitionKey::GetName() const { return LOCTEXT("FAssetTypeActions_GameFlowTransitionKey_Name", "Game Flow Transition Key"); }

UClass* FAssetTypeActions_GameFlowTransitionKey::GetSupportedClass() const { return UGameFlowTransitionKey::StaticClass(); }

uint32 FAssetTypeActions_GameFlowTransitionKey::GetCategories() { return EAssetTypeCategories::Gameplay; }

//------------------------------------------------------
// FGameFlowCoreEditorModule
//------------------------------------------------------

const FName AssetToolsModuleName("AssetTools");

void FGameFlowCoreEditorModule::StartupModule()
{
	RegisteredAssetTypeActions.Add(MakeShared<FAssetTypeActions_GameFlow>());
	RegisteredAssetTypeActions.Add(MakeShared<FAssetTypeActions_GameFlowContext>());
	RegisteredAssetTypeActions.Add(MakeShared<FAssetTypeActions_GameFlowTransitionKey>());

	IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
	for (TSharedPtr<FAssetTypeActions_Base>& registeredAssetTypeAction : RegisteredAssetTypeActions)
	{
		if (registeredAssetTypeAction.IsValid()) AssetTools.RegisterAssetTypeActions(registeredAssetTypeAction.ToSharedRef());
	}

	GameFlowGraphNodeFactory = MakeShareable(new FGameFlowGraphNodeFactory());
	FEdGraphUtilities::RegisterVisualNodeFactory(GameFlowGraphNodeFactory);

	GameFlowGraphPinFactory = MakeShareable(new FGameFlowGraphPinFactory());
	FEdGraphUtilities::RegisterVisualPinFactory(GameFlowGraphPinFactory);

	GameFlowGraphPinConnectionFactory = MakeShareable(new FGameFlowGraphPinConnectionFactory);
	FEdGraphUtilities::RegisterVisualPinConnectionFactory(GameFlowGraphPinConnectionFactory);
}

void FGameFlowCoreEditorModule::ShutdownModule()
{
	FEdGraphUtilities::UnregisterVisualPinConnectionFactory(GameFlowGraphPinConnectionFactory);
	GameFlowGraphPinConnectionFactory.Reset();

	FEdGraphUtilities::UnregisterVisualPinFactory(GameFlowGraphPinFactory);
	GameFlowGraphPinFactory.Reset();

	FEdGraphUtilities::UnregisterVisualNodeFactory(GameFlowGraphNodeFactory);
	GameFlowGraphNodeFactory.Reset();

	if (FModuleManager::Get().IsModuleLoaded(AssetToolsModuleName))
	{
		IAssetTools& AssetTools = FModuleManager::LoadModuleChecked<FAssetToolsModule>(AssetToolsModuleName).Get();
		for (TSharedPtr<FAssetTypeActions_Base>& registeredAssetTypeAction : RegisteredAssetTypeActions)
		{
			if (registeredAssetTypeAction.IsValid()) AssetTools.UnregisterAssetTypeActions(registeredAssetTypeAction.ToSharedRef());
		}
	}

	RegisteredAssetTypeActions.Empty();
}

#undef LOCTEXT_NAMESPACE

IMPLEMENT_MODULE(FGameFlowCoreEditorModule, GameFlowCoreEditor)