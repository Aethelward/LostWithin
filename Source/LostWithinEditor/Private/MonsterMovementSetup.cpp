// Copyright Epic Games, Inc. All Rights Reserved.

#include "MonsterMovementSetup.h"

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Builders/CubeBuilder.h"
#include "Components/BrushComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Editor.h"
#include "Engine/Blueprint.h"
#include "Engine/Level.h"
#include "Model.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "FileHelpers.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerStart.h"
#include "LostWithin.h"
#include "Misc/PackageName.h"
#include "Misc/SlowTask.h"
#include "NavMesh/NavMeshBoundsVolume.h"
#include "NavMesh/RecastNavMesh.h"
#include "NavigationData.h"
#include "NavigationSystem.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "WorldPartition/WorldPartition.h"
#include "WorldPartition/WorldPartitionHelpers.h"

namespace
{
	constexpr const TCHAR* TargetMap = TEXT("/Game/FirstPerson/Lvl_FirstPerson");
	const FName NavigationTag(TEXT("LostWithin.MonsterNavigation"));
	const FName AgentName(TEXT("LostWithinMonster"));

	bool EnableAutoActivation(UActorComponent* Component)
	{
		if (!Component->IsRegistered())
		{
			Component->SetAutoActivate(true);
			return true;
		}
		FBoolProperty* AutoActivate = FindFProperty<FBoolProperty>(UActorComponent::StaticClass(), TEXT("bAutoActivate"));
		if (!AutoActivate)
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot configure component auto activation: bAutoActivate is unavailable."));
			return false;
		}
		// Edit the same instance property as Details; the construction-only setter refuses registered components.
		AutoActivate->SetPropertyValue_InContainer(Component, true);
		Component->Activate(true);
		return true;
	}

	bool RepairCharacter(ACharacter* Character)
	{
		UCharacterMovementComponent* Movement = Character ? Character->GetCharacterMovement() : nullptr;
		UCapsuleComponent* Capsule = Character ? Character->GetCapsuleComponent() : nullptr;
		if (!Movement || !Capsule || !Character->GetMesh())
		{
			UE_LOG(LogLostWithin, Error, TEXT("Monster movement requires a Character with a capsule, mesh and movement component."));
			return false;
		}
		Character->Modify();
		Movement->Modify();
		Capsule->Modify();
		Character->AutoPossessAI = EAutoPossessAI::PlacedInWorldOrSpawned;
		Character->AutoPossessPlayer = EAutoReceiveInput::Disabled;
		if (!Character->AIControllerClass || !Character->AIControllerClass->IsChildOf(AAIController::StaticClass()))
		{
			Character->AIControllerClass = AAIController::StaticClass();
		}
		Character->PrimaryActorTick.bCanEverTick = true;
		Character->PrimaryActorTick.bStartWithTickEnabled = true;
		Movement->PrimaryComponentTick.bCanEverTick = true;
		Movement->PrimaryComponentTick.bStartWithTickEnabled = true;
		Movement->DefaultLandMovementMode = MOVE_Walking;
		Movement->MovementMode = MOVE_Walking;
		Movement->CustomMovementMode = 0;
		Movement->SetUpdateNavAgentWithOwnersCollisions(true);
		if (!Character->HasAnyFlags(RF_ClassDefaultObject | RF_ArchetypeObject))
		{
			Movement->UpdateNavAgent(*Character);
		}
		if (!EnableAutoActivation(Movement))
		{
			return false;
		}
		if (!FMath::IsFinite(Movement->MaxWalkSpeed) || Movement->MaxWalkSpeed <= 0.0f)
		{
			Movement->MaxWalkSpeed = GetDefault<ACharacter>()->GetCharacterMovement()->MaxWalkSpeed;
		}
		if (!Capsule->IsQueryCollisionEnabled() || Capsule->GetCollisionResponseToChannel(ECC_WorldStatic) != ECR_Block)
		{
			Capsule->SetCollisionProfileName(TEXT("Pawn"));
		}
		// The monster must not carve a navigation hole underneath its own capsule.
		Capsule->SetCanEverAffectNavigation(false);
		Capsule->SetSimulatePhysics(false);
		Character->GetMesh()->SetCanEverAffectNavigation(false);
		Character->GetMesh()->SetSimulatePhysics(false);
		Character->GetMesh()->SetAllBodiesSimulatePhysics(false);
		Character->MarkPackageDirty();
		return true;
	}

	ANavMeshBoundsVolume* EnsureBounds(UWorld* World, const FBox& GeometryBounds)
	{
		const FBox Padded = GeometryBounds.ExpandBy(200.0);
		ANavMeshBoundsVolume* Volume = nullptr;
		for (TActorIterator<ANavMeshBoundsVolume> It(World); It; ++It)
		{
			const FBox Existing = It->GetComponentsBoundingBox(true);
			if (Existing.IsValid && Existing.IsInsideOrOn(Padded.Min) && Existing.IsInsideOrOn(Padded.Max))
			{
				It->Modify();
				It->SetIsSpatiallyLoaded(false);
				return *It;
			}
			if (It->ActorHasTag(NavigationTag))
			{
				Volume = *It;
			}
		}
		if (!Volume)
		{
			FActorSpawnParameters Spawn;
			Spawn.OverrideLevel = World->PersistentLevel;
			Spawn.ObjectFlags |= RF_Transactional;
			Volume = World->SpawnActor<ANavMeshBoundsVolume>(ANavMeshBoundsVolume::StaticClass(), FTransform::Identity, Spawn);
		}
		if (!Volume)
		{
			UE_LOG(LogLostWithin, Error, TEXT("Could not create the monster navigation bounds volume."));
			return nullptr;
		}
		Volume->Modify();
		Volume->Tags.AddUnique(NavigationTag);
		Volume->SetActorLabel(TEXT("MonsterNavigationBounds"));
		Volume->SetIsSpatiallyLoaded(false);
		if (!Volume->Brush)
		{
			Volume->Brush = NewObject<UModel>(Volume, NAME_None, RF_Transactional);
			Volume->Brush->Initialize(nullptr, true);
		}
		Volume->GetBrushComponent()->Brush = Volume->Brush;
		UCubeBuilder* Builder = NewObject<UCubeBuilder>(Volume, NAME_None, RF_Transactional);
		const FVector Size = Padded.GetSize();
		Builder->X = static_cast<float>(Size.X);
		Builder->Y = static_cast<float>(Size.Y);
		Builder->Z = static_cast<float>(Size.Z);
		Volume->BrushBuilder = Builder;
		if (!Builder->Build(World, Volume))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Could not build brush geometry for the navigation bounds."));
			return nullptr;
		}
		Volume->SetActorTransform(FTransform(Padded.GetCenter()));
		Volume->GetBrushComponent()->BuildSimpleBrushCollision();
		Volume->GetBrushComponent()->ReregisterComponent();
		Volume->PostEditChange();
		Volume->MarkPackageDirty();
		return Volume;
	}

	int32 ConfigureAgent(UNavigationSystemV1* Navigation, const ACharacter* Character, FNavDataConfig& OutConfig)
	{
		UNavigationSystemV1* Defaults = GetMutableDefault<UNavigationSystemV1>();
		TArray<FNavDataConfig> Agents = Defaults->GetSupportedAgents();
		const float Radius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
		const float Height = Character->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f;
		int32 Index = Agents.IndexOfByPredicate([](const FNavDataConfig& Config) { return Config.Name == AgentName; });
		if (Index == INDEX_NONE)
		{
			// Recast registers one data instance per agent size; avoid a duplicate-size profile.
			Index = Agents.IndexOfByPredicate([&](const FNavDataConfig& Config)
			{
				return FMath::IsNearlyEqual(Config.AgentRadius, Radius) && FMath::IsNearlyEqual(Config.AgentHeight, Height);
			});
		}
		OutConfig = Index == INDEX_NONE ? Navigation->GetDefaultSupportedAgentConfig() : Agents[Index];
		if (Index == INDEX_NONE)
		{
			OutConfig.Name = AgentName;
			OutConfig.Color = FColor::Green;
		}
		OutConfig.AgentRadius = Radius;
		OutConfig.AgentHeight = Height;
		OutConfig.DefaultQueryExtent = FVector(200.0, 200.0, FMath::Max(500.0f, OutConfig.AgentHeight));
		OutConfig.SetNavDataClass(ARecastNavMesh::StaticClass());
		if (Index == INDEX_NONE)
		{
			const FName AgentBit(*FString::Printf(TEXT("bSupportsAgent%d"), Agents.Num()));
			if (!FindFProperty<FBoolProperty>(FNavAgentSelector::StaticStruct(), AgentBit))
			{
				UE_LOG(LogLostWithin, Error, TEXT("No free navigation agent slot for the monster."));
				return INDEX_NONE;
			}
			Index = Agents.Add(OutConfig);
		}
		else
		{
			Agents[Index] = OutConfig;
		}
		FArrayProperty* Property = FindFProperty<FArrayProperty>(UNavigationSystemV1::StaticClass(), TEXT("SupportedAgents"));
		if (!Property)
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot configure monster navigation: SupportedAgents is unavailable."));
			return INDEX_NONE;
		}
		Property->CopyCompleteValue(Property->ContainerPtrToValuePtr<void>(Defaults), &Agents);
		FNavAgentSelector Mask = Navigation->GetSupportedAgentsMask();
		Mask.Set(Index);
		Mask.MarkInitialized();
		Navigation->SetSupportedAgentsMask(Mask);
		return Index;
	}

	bool SaveAgentSettings()
	{
		UNavigationSystemV1* Defaults = GetMutableDefault<UNavigationSystemV1>();
		// ConfigureAgent preserves unfiltered authored profiles in the CDO.
		if (!Defaults->TryUpdateDefaultConfigFile(Defaults->GetDefaultConfigFilename(), true))
		{
			UE_LOG(LogLostWithin, Error, TEXT("Could not save the supported navigation agents to DefaultEngine.ini."));
			return false;
		}
		return true;
	}
}

UWorld* LostWithinMonsterSetup::GetSetupWorld()
{
	if (!GEditor || GEditor->PlayWorld)
	{
		UE_LOG(LogLostWithin, Error, TEXT("Stop Play In Editor before repairing monster movement."));
		return nullptr;
	}
	UWorld* World = GEditor->GetEditorWorldContext().World();
	if (IsRunningCommandlet() && (!World || World->GetOutermost()->GetName() != TargetMap))
	{
		World = UEditorLoadingAndSavingUtils::LoadMap(FPackageName::LongPackageNameToFilename(TargetMap, FPackageName::GetMapPackageExtension()));
	}
	if (!World || World->GetOutermost()->GetName() != TargetMap)
	{
		UE_LOG(LogLostWithin, Error, TEXT("Open /Game/FirstPerson/Lvl_FirstPerson, stop PIE and run LostWithin.SetupMonsterCatReaction. Other open maps will not be replaced."));
		return nullptr;
	}
	return World;
}

bool LostWithinMonsterSetup::RepairMovementDefaults(UBlueprint* Blueprint)
{
	ACharacter* Character = Blueprint && Blueprint->GeneratedClass ? Cast<ACharacter>(Blueprint->GeneratedClass->GetDefaultObject()) : nullptr;
	if (!RepairCharacter(Character))
	{
		return false;
	}
	Blueprint->MarkPackageDirty();
	return true;
}

bool LostWithinMonsterSetup::BuildAndSaveNavigation(UWorld* World, UClass* MonsterClass)
{
	ACharacter* Defaults = MonsterClass ? Cast<ACharacter>(MonsterClass->GetDefaultObject()) : nullptr;
	UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
	if (!Navigation || !Defaults)
	{
		UE_LOG(LogLostWithin, Error, TEXT("The target level needs an enabled Navigation System and a valid monster Character class."));
		return false;
	}
	FScopedSlowTask Progress(4.0f, FText::FromString(TEXT("Repairing monster navigation")));
	if (!IsRunningCommandlet())
	{
		Progress.MakeDialog();
	}
	Progress.EnterProgressFrame(1.0f, FText::FromString(TEXT("Loading maze collision")));
	FWorldPartitionHelpers::FForEachActorWithLoadingResult LoadedActors;
	if (UWorldPartition* Partition = World->GetWorldPartition())
	{
		FWorldPartitionHelpers::FForEachActorWithLoadingParams Params;
		Params.bKeepReferences = true;
		FWorldPartitionHelpers::ForEachActorWithLoading(Partition, [](const FWorldPartitionActorDescInstance*) { return true; }, Params, LoadedActors);
	}
	FBox GeometryBounds(ForceInit);
	TArray<FVector> RequiredSamples;
	TArray<FVector> FloorSamples;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		if (It->IsA(MonsterClass))
		{
			ACharacter* Monster = CastChecked<ACharacter>(*It);
			if (!Monster->FindComponentByClass<UMonsterAIComponent>())
			{
				// This actor may have been unloaded during Blueprint compilation.
				Monster->RerunConstructionScripts();
			}
			TInlineComponentArray<UMonsterAIComponent*> Reactions(Monster);
			if (Reactions.Num() != 1)
			{
				UE_LOG(LogLostWithin, Error, TEXT("%s needs exactly one cat-reaction component after construction; found %d."), *Monster->GetActorLabel(), Reactions.Num());
				return false;
			}
			Reactions[0]->Modify();
			Reactions[0]->bCatIsNear = false;
			Reactions[0]->bIsFleeing = false;
			Reactions[0]->bIsRagdolled = false;
			Reactions[0]->PrimaryComponentTick.bCanEverTick = true;
			Reactions[0]->PrimaryComponentTick.bStartWithTickEnabled = true;
			if (!EnableAutoActivation(Reactions[0]))
			{
				return false;
			}
			if (!RepairCharacter(Monster))
			{
				return false;
			}
			Monster->SetActorTickEnabled(true);
			Monster->GetCharacterMovement()->SetComponentTickEnabled(true);
			RequiredSamples.Add(Monster->GetNavAgentLocation());
		}
		if (It->IsA<APlayerStart>())
		{
			RequiredSamples.Add(It->GetActorLocation());
		}
		TInlineComponentArray<UStaticMeshComponent*> Meshes(*It);
		for (UStaticMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetStaticMesh() && Mesh->IsRegistered() && Mesh->Mobility != EComponentMobility::Movable
				&& Mesh->CanEverAffectNavigation() && Mesh->GetCollisionEnabled() != ECollisionEnabled::NoCollision
				&& Mesh->GetCollisionResponseToChannel(ECC_Pawn) == ECR_Block)
			{
				const FBox Box = Mesh->Bounds.GetBox();
				GeometryBounds += Box;
				if (Box.GetSize().Z < Box.GetSize().X && Box.GetSize().Z < Box.GetSize().Y)
				{
					FloorSamples.Add(FVector(Box.GetCenter().X, Box.GetCenter().Y, Box.Max.Z + 10.0));
				}
			}
		}
	}
	if (!GeometryBounds.IsValid)
	{
		UE_LOG(LogLostWithin, Error, TEXT("No collidable, navigation-relevant maze geometry was loaded. Check floor collision and World Partition loading."));
		return false;
	}
	Progress.EnterProgressFrame(1.0f, FText::FromString(TEXT("Configuring navigation bounds and agent")));
	ANavMeshBoundsVolume* Volume = EnsureBounds(World, GeometryBounds);
	FNavDataConfig Agent;
	const int32 AgentIndex = ConfigureAgent(Navigation, Defaults, Agent);
	if (!Volume || AgentIndex == INDEX_NONE)
	{
		return false;
	}
	Volume->Modify();
	Volume->SupportedAgents.Set(AgentIndex);
	Volume->MarkPackageDirty();
	ANavigationData* Data = Navigation->GetNavDataForAgentName(Agent.Name);
	if (!Data)
	{
		Data = Navigation->CreateNavigationDataInstanceInLevel(Agent, World->PersistentLevel);
		if (Data)
		{
			Navigation->RequestRegistrationDeferred(*Data);
		}
	}
	if (!IsValid(Data) || !Cast<ARecastNavMesh>(Data))
	{
		UE_LOG(LogLostWithin, Error, TEXT("Could not create Recast navigation for the monster capsule."));
		return false;
	}
	Data->Modify();
	Data->SetIsSpatiallyLoaded(false);
	if (ARecastNavMesh* Recast = Cast<ARecastNavMesh>(Data))
	{
		const bool bSizeChanged = !FMath::IsNearlyEqual(Recast->GetConfig().AgentRadius, Agent.AgentRadius)
			|| !FMath::IsNearlyEqual(Recast->GetConfig().AgentHeight, Agent.AgentHeight);
		Recast->AgentRadius = Agent.AgentRadius;
		Recast->AgentHeight = Agent.AgentHeight;
		if (bSizeChanged)
		{
			// Apply the same config refresh as an editor Details-panel size change.
			FPropertyChangedEvent RadiusChange(FindFProperty<FProperty>(ARecastNavMesh::StaticClass(), TEXT("AgentRadius")));
			Recast->PostEditChangeProperty(RadiusChange);
			FPropertyChangedEvent HeightChange(FindFProperty<FProperty>(ARecastNavMesh::StaticClass(), TEXT("AgentHeight")));
			Recast->PostEditChangeProperty(HeightChange);
		}
	}
	Navigation->OnNavigationBoundsUpdated(Volume);
	// Flush queued bounds/geometry registration before the explicit editor build.
	Navigation->Tick(0.0f);
	Navigation->ReleaseInitialBuildingLock();
	Progress.EnterProgressFrame(1.0f, FText::FromString(TEXT("Building and validating walkable navigation")));
	Navigation->Build();
	for (TActorIterator<ANavigationData> It(World); It; ++It)
	{
		It->EnsureBuildCompletion();
		It->MarkPackageDirty();
	}
	if (Navigation->GetNavDataForAgentName(Agent.Name) != Data
		|| !FMath::IsNearlyEqual(Data->GetConfig().AgentRadius, Agent.AgentRadius)
		|| !FMath::IsNearlyEqual(Data->GetConfig().AgentHeight, Agent.AgentHeight))
	{
		UE_LOG(LogLostWithin, Error, TEXT("Recast did not register the expected monster agent dimensions. Check Supported Agents and the Recast actor settings before rerunning setup."));
		return false;
	}
	auto HasReachableSurface = [&](const FVector& Sample)
	{
		FNavLocation Projected, Reachable;
		if (!Navigation->ProjectPointToNavigation(Sample, Projected, Agent.DefaultQueryExtent, Data))
		{
			return false;
		}
		for (int32 Attempt = 0; Attempt < 16; ++Attempt)
		{
			if (Navigation->GetRandomReachablePointInRadius(Projected.Location, 1500.0f, Reachable, Data)
				&& FVector::Dist2D(Projected.Location, Reachable.Location) > FMath::Max(50.0f, Agent.AgentRadius * 2.0f))
			{
				return true;
			}
		}
		return false;
	};
	for (const FVector& Sample : RequiredSamples)
	{
		if (!HasReachableSurface(Sample))
		{
			UE_LOG(LogLostWithin, Error, TEXT("No usable monster navigation near %s. Check floor collision, corridor width and capsule height; setup has not succeeded."), *Sample.ToString());
			return false;
		}
	}
	if (RequiredSamples.IsEmpty() && !FloorSamples.ContainsByPredicate(HasReachableSurface))
	{
		UE_LOG(LogLostWithin, Error, TEXT("Navigation build produced no reachable maze floor. Check walkable collision and agent dimensions."));
		return false;
	}
	Progress.EnterProgressFrame(1.0f, FText::FromString(TEXT("Saving navigation and target level")));
	if (!SaveAgentSettings())
	{
		return false;
	}
	TArray<UPackage*> ActorPackages;
	for (TActorIterator<AActor> It(World); It; ++It)
	{
		UPackage* Package = It->GetExternalPackage();
		if (Package && Package->IsDirty())
		{
			ActorPackages.AddUnique(Package);
		}
	}
	if (!UEditorLoadingAndSavingUtils::SaveMap(World, TargetMap)
		|| (!ActorPackages.IsEmpty() && !UEditorLoadingAndSavingUtils::SavePackages(ActorPackages, true)))
	{
		UE_LOG(LogLostWithin, Error, TEXT("Could not save the repaired level/navigation actor packages."));
		return false;
	}
	UE_LOG(LogLostWithin, Display, TEXT("Monster movement setup saved: %s, bounds %s, agent radius %.1f / height %.1f. Press P to inspect coverage, then verify movement in PIE."),
		TargetMap, *Volume->GetActorLabel(), Agent.AgentRadius, Agent.AgentHeight);
	return true;
}
