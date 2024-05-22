// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "Modules/ModuleManager.h"

class FAssetTypeActions_Base;
struct FGameFlowGraphNodeFactory;
struct FGameFlowGraphPinFactory;
struct FGameFlowGraphPinConnectionFactory;

class FGameFlowCoreEditorModule : public IModuleInterface
{
public:
	virtual void StartupModule() override;
	virtual void ShutdownModule() override;

	static void OpenAssetEditor(const TArray<UObject*>& InObjects);

private:
	TArray<TSharedPtr<FAssetTypeActions_Base>> RegisteredAssetTypeActions;
	TSharedPtr<FGameFlowGraphNodeFactory> GameFlowGraphNodeFactory;
	TSharedPtr<FGameFlowGraphPinFactory> GameFlowGraphPinFactory;
	TSharedPtr<FGameFlowGraphPinConnectionFactory> GameFlowGraphPinConnectionFactory;
};