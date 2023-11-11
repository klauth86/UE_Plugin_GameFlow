// Fill out your copyright notice in the Description page of Project Settings.

using UnrealBuildTool;

public class UE_Plugin_GameFlowTarget : TargetRules
{
	public UE_Plugin_GameFlowTarget(TargetInfo Target) : base(Target)
	{
		Type = TargetType.Game;
		DefaultBuildSettings = BuildSettingsVersion.V2;

		ExtraModuleNames.AddRange( new string[] { "UE_Plugin_GameFlow" } );
	}
}