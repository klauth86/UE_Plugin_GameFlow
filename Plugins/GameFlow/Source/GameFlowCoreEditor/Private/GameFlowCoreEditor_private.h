// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "Factories/Factory.h"
#include "EdGraph/EdGraphSchema.h"
#include "SGraphNode.h"
#include "GameFlowCoreEditor_private.generated.h"

class UGameFlow;

//------------------------------------------------------
// UFactory_GameFlow
//------------------------------------------------------

UCLASS()
class UFactory_GameFlow : public UFactory
{
	GENERATED_UCLASS_BODY()

public:

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

//------------------------------------------------------
// UFactory_GameFlowContext
//------------------------------------------------------

UCLASS()
class UFactory_GameFlowContext : public UFactory
{
	GENERATED_UCLASS_BODY()

public:

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

//------------------------------------------------------
// UFactory_GameFlowTransitionKey
//------------------------------------------------------

UCLASS()
class UFactory_GameFlowTransitionKey : public UFactory
{
	GENERATED_UCLASS_BODY()

public:

	virtual UObject* FactoryCreateNew(UClass* Class, UObject* InParent, FName Name, EObjectFlags Flags, UObject* Context, FFeedbackContext* Warn, FName CallingContext) override;
	virtual FText GetDisplayName() const override;
	virtual uint32 GetMenuCategories() const override;
};

//------------------------------------------------------
// FGameFlowGraphSchemaAction_NewComment
//------------------------------------------------------

USTRUCT()
struct FGameFlowGraphSchemaAction_NewComment : public FEdGraphSchemaAction
{
	GENERATED_BODY();

	FGameFlowGraphSchemaAction_NewComment() : FEdGraphSchemaAction() {}

	FGameFlowGraphSchemaAction_NewComment(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping)
	{}

	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;

	FSlateRect SelectedNodesBounds;
};

//------------------------------------------------------
// FGameFlowGraphSchemaAction_NewNode
//------------------------------------------------------

USTRUCT()
struct FGameFlowGraphSchemaAction_NewNode : public FEdGraphSchemaAction
{
	GENERATED_BODY()

	UEdGraphNode* NodeTemplate;

	FGameFlowGraphSchemaAction_NewNode() : FEdGraphSchemaAction(), NodeTemplate(nullptr) {}

	FGameFlowGraphSchemaAction_NewNode(FText InNodeCategory, FText InMenuDesc, FText InToolTip, const int32 InGrouping)
		: FEdGraphSchemaAction(MoveTemp(InNodeCategory), MoveTemp(InMenuDesc), MoveTemp(InToolTip), InGrouping), NodeTemplate(nullptr)
	{}

	virtual UEdGraphNode* PerformAction(class UEdGraph* ParentGraph, UEdGraphPin* FromPin, const FVector2D Location, bool bSelectNewNode = true) override;

	virtual void AddReferencedObjects(FReferenceCollector& Collector) override;

	template <typename NodeType>
	static NodeType* SpawnNodeFromTemplate(class UEdGraph* ParentGraph, NodeType* InTemplateNode, const FVector2D Location = FVector2D(0.0f, 0.0f), bool bSelectNewNode = true)
	{
		FGameFlowGraphSchemaAction_NewNode Action;
		Action.NodeTemplate = InTemplateNode;

		return Cast<NodeType>(Action.PerformAction(ParentGraph, NULL, Location, bSelectNewNode));
	}
};

//------------------------------------------------------
// UGameFlowGraphSchema
//------------------------------------------------------

UCLASS()
class UGameFlowGraphSchema : public UEdGraphSchema
{
	GENERATED_UCLASS_BODY()

public:

	static const FName PC_Exec;
	static const FName PC_Transition;

	virtual void CreateDefaultNodesForGraph(UEdGraph& Graph) const override;
	virtual const FPinConnectionResponse CanCreateConnection(const UEdGraphPin* A, const UEdGraphPin* B) const override;
	virtual bool TryCreateConnection(UEdGraphPin* A, UEdGraphPin* B) const override;
	virtual bool CreateAutomaticConversionNodeAndConnections(UEdGraphPin* PinA, UEdGraphPin* PinB) const override;
	virtual void GetGraphContextActions(FGraphContextMenuBuilder& ContextMenuBuilder) const override;
	virtual EGraphType GetGraphType(const UEdGraph* TestEdGraph) const override;
	virtual void GetContextMenuActions(class UToolMenu* Menu, class UGraphNodeContextMenuContext* Context) const override;
	virtual FLinearColor GetPinTypeColor(const FEdGraphPinType& PinType) const override;
	virtual void GetGraphDisplayInformation(const UEdGraph& Graph, /*out*/ FGraphDisplayInfo& DisplayInfo) const override;
	virtual void GetAssetsNodeHoverMessage(const TArray<FAssetData>& Assets, const UEdGraphNode* HoverNode, FString& OutTooltipText, bool& OutOkIcon) const override;
	virtual void GetAssetsPinHoverMessage(const TArray<FAssetData>& Assets, const UEdGraphPin* HoverPin, FString& OutTooltipText, bool& OutOkIcon) const override;

	virtual void BreakNodeLinks(UEdGraphNode& TargetNode) const override;
	virtual void BreakPinLinks(UEdGraphPin& TargetPin, bool bSendsNodeNotification) const override;
	virtual void BreakSinglePinLink(UEdGraphPin* SourcePin, UEdGraphPin* TargetPin) const override;

	virtual TSharedPtr<FEdGraphSchemaAction> GetCreateCommentAction() const override;
};

//------------------------------------------------------
// UGameFlowGraph
//------------------------------------------------------

UCLASS()
class UGameFlowGraph : public UEdGraph
{
	GENERATED_UCLASS_BODY()
};

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

	template<class T>
	T* GetPinLinkedNodeAs(const int32 pinIndex) const
	{
		return Pins.IsValidIndex(pinIndex) && Pins[pinIndex] && Pins[pinIndex]->LinkedTo.Num() > 0 ? Cast<T>(Pins[pinIndex]->LinkedTo[0]->GetOwningNode()) : nullptr;
	}

	//~ Begin UEdGraphNode Interface
	virtual void AllocateDefaultPins() override;
	virtual FText GetNodeTitle(ENodeTitleType::Type TitleType) const override;
	virtual FText GetTooltipText() const override;
	virtual FLinearColor GetNodeTitleColor() const override { return FColorList::Red; }
	virtual void PinConnectionListChanged(UEdGraphPin* Pin) override;
	virtual bool CanDuplicateNode() const override { return true; }
	virtual void PostPasteNode() override;
	//~ End UEdGraphNode Interface

	UGameFlowGraphNode_Base* GetPrevNode() const { return GetPinLinkedNodeAs<UGameFlowGraphNode_Base>(0); }
	UGameFlowGraphNode_Base* GetNextNode() const { return GetPinLinkedNodeAs<UGameFlowGraphNode_Base>(1); }
	void CreateConnections(UGameFlowGraphNode_Base* PreviousState, UGameFlowGraphNode_Base* NextState);
};