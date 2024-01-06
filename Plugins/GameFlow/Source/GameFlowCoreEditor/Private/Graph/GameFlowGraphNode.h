// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "EdGraph/EdGraphNode.h"
#include "SGraphPin.h"
#include "SGraphNode.h"
#include "GameFlowGraphNode.generated.h"

class UGameFlow;
class UGameFlowStep;

//------------------------------------------------------
// UGameFlowGraphNode_Start
//------------------------------------------------------

UCLASS()
class UGameFlowGraphNode_Start : public UEdGraphNode
{
	GENERATED_BODY()

public:

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanUserDeleteNode() const override { return false; }
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual void NodeConnectionListChanged() override;
	//~ End UEdGraphNode Interface

	UEdGraphNode* GetOutputNode() const;

protected:

	void RefreshOwningAssetEntryState();
};

//------------------------------------------------------
// UGameFlowGraphNode_Base
//------------------------------------------------------

UCLASS()
class UGameFlowGraphNode_Base : public UEdGraphNode
{
	GENERATED_BODY()

public:

	virtual bool CanCreateUnderSpecifiedSchema(const UEdGraphSchema* Schema) const override;

	virtual UEdGraphPin* GetInputPin() const { return Pins[0]; }
	virtual UEdGraphPin* GetOutputPin() const { return Pins[1]; }

	virtual void GetTransitionList(TArray<class UGameFlowGraphNode_Transition*>& OutTransitions) const;
};

//------------------------------------------------------
// UGameFlowGraphNode_State
//------------------------------------------------------

UCLASS()
class UGameFlowGraphNode_State : public UGameFlowGraphNode_Base
{
	GENERATED_UCLASS_BODY()

public:

	// UEdGraphNode interface
	virtual void AllocateDefaultPins() override;
	virtual void AutowireNewNode(UEdGraphPin* FromPin) override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void OnRenameNode(const FString& NewName) override;
	// End of UEdGraphNode interface

	UPROPERTY()
		UGameFlow* PreviousOuter;

protected:

	UPROPERTY()
		FText CachedNodeTitle;
};

//------------------------------------------------------
// SGameFlowGraphNodeOutputPin
//------------------------------------------------------

UCLASS(MinimalAPI, config = Editor)
class UGameFlowGraphNode_Transition : public UGameFlowGraphNode_Base
{
	GENERATED_BODY()

public:

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FColorList::Red; }
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPasteNode() override;
	//~ End UEdGraphNode Interface

	UGameFlowGraphNode_Base* GetPreviousState() const;
	UGameFlowGraphNode_Base* GetNextState() const;
	void CreateConnections(UGameFlowGraphNode_Base* PreviousState, UGameFlowGraphNode_Base* NextState);
};

//------------------------------------------------------
// SGameFlowGraphNode_Start
//------------------------------------------------------

class SGameFlowGraphNode_Start : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGameFlowGraphNode_Start) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGameFlowGraphNode_Start* InNode);

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;

	// End of SGraphNode interface

protected:
	FSlateColor GetBorderBackgroundColor() const;

	FText GetPreviewCornerText() const;
};

//------------------------------------------------------
// SGameFlowGraphNode_State
//------------------------------------------------------

class SGameFlowGraphNode_State : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGameFlowGraphNode_State) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGameFlowGraphNode_Base* InNode);

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	virtual void CreatePinWidgets() override;
	virtual void AddPin(const TSharedRef<SGraphPin>& PinToAdd) override;
	// End of SGraphNode interface

	// SWidget interface
	void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// End of SWidget interface

protected:
	FSlateColor GetBorderBackgroundColor() const;
	virtual FSlateColor GetBorderBackgroundColor_Internal(FLinearColor InactiveStateColor, FLinearColor ActiveStateColorDim, FLinearColor ActiveStateColorBright) const;

	virtual FText GetPreviewCornerText() const;

	FText GetStepDescription(const TObjectPtr<UGameFlowStep>& step) const;
	FMargin StepsPadding() const;
	EVisibility StepsVisibility() const;

	TSharedPtr<SVerticalBox> StepsVerticalBoxPtr;

	UGameFlow* OwningGameFlow;
};

//------------------------------------------------------
// SGameFlowGraphNode_Transition
//------------------------------------------------------

class SGameFlowGraphNode_Transition : public SGraphNode
{
public:
	SLATE_BEGIN_ARGS(SGameFlowGraphNode_Transition) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs, UGameFlowGraphNode_Transition* InNode);

	// SNodePanel::SNode interface
	virtual void MoveTo(const FVector2D& NewPosition, FNodeSet& NodeFilter, bool bMarkDirty = true) override {} // Ignored; position is set by the location of the attached state nodes
	virtual bool RequiresSecondPassLayout() const override { return true; }
	virtual void PerformSecondPassLayout(const TMap< UObject*, TSharedRef<SNode> >& NodeToWidgetLookup) const override;
	// End of SNodePanel::SNode interface

	// SGraphNode interface
	virtual void UpdateGraphNode() override;
	// End of SGraphNode interface

	// SWidget interface
	void OnMouseEnter(const FGeometry& MyGeometry, const FPointerEvent& MouseEvent) override;
	void OnMouseLeave(const FPointerEvent& MouseEvent) override;
	// End of SWidget interface

	// Calculate position for multiple nodes to be placed between a start and end point, by providing this nodes index and max expected nodes 
	void PositionBetweenTwoNodesWithOffset(const FGeometry& StartGeom, const FGeometry& EndGeom, int32 NodeIndex, int32 MaxNodes) const;

	static FLinearColor StaticGetTransitionColor(UGameFlowGraphNode_Transition* TransNode, bool bIsHovered);
private:
	TSharedPtr<STextEntryPopup> TextEntryWidget;

	/** Cache of the widget representing the previous state node */
	mutable TWeakPtr<SNode> PrevStateNodeWidgetPtr;

	UGameFlow* OwningGameFlow;

	UGameFlowGraphNode_Transition* TransitionGraphNode;

private:
	FText GetPreviewCornerText(bool reverse) const;
	FSlateColor GetTransitionColor() const;
	FText GetTransitionKey() const;
};