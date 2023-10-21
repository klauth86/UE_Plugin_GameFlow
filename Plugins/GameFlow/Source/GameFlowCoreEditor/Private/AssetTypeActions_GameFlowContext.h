// Copyright 2023 Pentangle Studio under EULA https://www.unrealengine.com/en-US/eula/unreal

#pragma once

#include "AssetTypeActions_Base.h"

class FAssetTypeActions_GameFlowContext : public FAssetTypeActions_Base
{
public:
	virtual FText GetName() const override;
	virtual UClass* GetSupportedClass() const override;
	virtual FColor GetTypeColor() const override { return FColor(129, 50, 255); }
	virtual uint32 GetCategories() override;
};