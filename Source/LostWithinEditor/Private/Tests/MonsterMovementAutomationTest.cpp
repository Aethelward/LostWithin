// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AI/MonsterAIComponent.h"
#include "AIController.h"
#include "Components/CapsuleComponent.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "HAL/PlatformTime.h"
#include "Kismet/GameplayStatics.h"
#include "Misc/AutomationTest.h"
#include "NavigationSystem.h"
#include "Tests/AutomationCommon.h"
#include "Tests/AutomationEditorCommon.h"
#include "UObject/Package.h"

namespace
{
	constexpr const TCHAR* MonsterClassPath = TEXT("/Game/NPC/Monster/Lunger.Lunger_C");

	class FObserveMonsterMovement final : public IAutomationLatentCommand
	{
	public:
		explicit FObserveMonsterMovement(FAutomationTestBase* InTest)
			: Test(InTest), StartedAt(FPlatformTime::Seconds()) {}

		virtual bool Update() override
		{
			if (FPlatformTime::Seconds() - StartedAt > 40.0)
			{
				Test->AddError(TEXT("PIE monster movement timed out. Check navigation coverage, possession and the roaming retry loop."));
				return true;
			}
			UWorld* World = GEditor ? GEditor->PlayWorld : nullptr;
			if (!World || !World->HasBegunPlay())
			{
				return false;
			}
			if (!bSpawned)
			{
				APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
				if (!Player)
				{
					return false;
				}
				UClass* Class = LoadClass<ACharacter>(nullptr, MonsterClassPath);
				UNavigationSystemV1* Navigation = FNavigationSystem::GetCurrent<UNavigationSystemV1>(World);
				if (!Class || !Navigation)
				{
					Test->AddError(TEXT("The production monster class or level navigation is missing. Run setup first."));
					return true;
				}
				ACharacter* Defaults = Class->GetDefaultObject<ACharacter>();
				Test->TestEqual(TEXT("Placed and spawned AI possession"), Defaults->AutoPossessAI, EAutoPossessAI::PlacedInWorldOrSpawned);
				FNavAgentProperties Properties = Defaults->GetNavAgentPropertiesRef();
				Properties.AgentRadius = Defaults->GetCapsuleComponent()->GetScaledCapsuleRadius();
				Properties.AgentHeight = Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() * 2.0f;
				ANavigationData* Data = Navigation->GetNavDataForProps(Properties);
				if (!Data)
				{
					Test->AddError(TEXT("Saved monster navigation is absent in PIE. Rebuild and save navigation using setup."));
					return true;
				}
				FNavLocation Origin;
				if (!Navigation->ProjectPointToNavigation(Player->GetActorLocation(), Origin, FVector(200.0, 200.0, 500.0), Data))
				{
					// Allow World Partition's initial player region to finish loading.
					return false;
				}
				for (TActorIterator<ACharacter> It(World, Class); It; ++It)
				{
					PlacedMonster = *It;
					PlacedStart = It->GetActorLocation();
					bHadPlacedMonster = true;
					break;
				}
				for (int32 Attempt = 0; Attempt < 32 && !SpawnedMonster.IsValid(); ++Attempt)
				{
					FNavLocation Candidate;
					if (!Navigation->GetRandomReachablePointInRadius(Origin.Location, 1200.0f, Candidate, Data)
						|| FVector::Dist2D(Candidate.Location, Origin.Location) < 200.0)
					{
						continue;
					}
					FActorSpawnParameters Params;
					Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AdjustIfPossibleButDontSpawnIfColliding;
					const FVector Position = Candidate.Location + FVector(0.0, 0.0, Defaults->GetCapsuleComponent()->GetScaledCapsuleHalfHeight() + 5.0);
					SpawnedMonster = World->SpawnActor<ACharacter>(Class, Position, FRotator::ZeroRotator, Params);
				}
				if (!SpawnedMonster.IsValid())
				{
					Test->AddError(TEXT("Could not spawn Lunger on clear navigation near the player."));
					return true;
				}
				SpawnedStart = SpawnedMonster->GetActorLocation();
				SpawnedAt = World->GetTimeSeconds();
				bSpawned = true;
				if (!PlacedMonster.IsValid())
				{
					Test->AddInfo(TEXT("No saved placed monster in the loaded player region; verify a placed instance manually as documented."));
				}
			}
			if (!Observe(SpawnedMonster.Get(), SpawnedStart, bSpawnedVelocity, bSpawnedMoved))
			{
				return true;
			}
			if (bHadPlacedMonster && !Observe(PlacedMonster.Get(), PlacedStart, bPlacedVelocity, bPlacedMoved))
			{
				return true;
			}
			if (bSpawnedMoved && bSpawnedVelocity && (!bHadPlacedMonster || (bPlacedMoved && bPlacedVelocity)))
			{
				Test->AddInfo(TEXT("Production Lunger moved more than 50 cm with nonzero horizontal velocity, using normal Blueprint AI in PIE."));
				return true;
			}
			if (World->GetTimeSeconds() - SpawnedAt > 20.0f)
			{
				Test->TestTrue(TEXT("Spawned monster displaced over 50 cm"), bSpawnedMoved);
				Test->TestTrue(TEXT("Spawned monster had horizontal velocity"), bSpawnedVelocity);
				if (PlacedMonster.IsValid())
				{
					Test->TestTrue(TEXT("Placed monster displaced over 50 cm"), bPlacedMoved);
					Test->TestTrue(TEXT("Placed monster had horizontal velocity"), bPlacedVelocity);
				}
				return true;
			}
			return false;
		}

	private:
		bool Observe(ACharacter* Monster, const FVector& Start, bool& bSawVelocity, bool& bMoved)
		{
			if (!IsValid(Monster) || !Cast<AAIController>(Monster->GetController()))
			{
				Test->AddError(TEXT("Lunger was destroyed or failed automatic AIController possession."));
				return false;
			}
			TInlineComponentArray<UMonsterAIComponent*> Components(Monster);
			if (Components.Num() != 1 || !UMonsterAIComponent::CanRunNormalAI(Monster))
			{
				Test->AddError(TEXT("Movement regression requires one reaction component and inactive nearby cats. Stop cat sequences and rerun."));
				return false;
			}
			if (!Monster->IsActorTickEnabled() || !Monster->GetCharacterMovement()->IsComponentTickEnabled())
			{
				Test->AddError(TEXT("Monster actor or movement ticking is disabled."));
				return false;
			}
			bSawVelocity |= Monster->GetVelocity().SizeSquared2D() > 1.0;
			bMoved |= FVector::DistSquared2D(Monster->GetActorLocation(), Start) > FMath::Square(50.0);
			return true;
		}

		FAutomationTestBase* Test;
		double StartedAt;
		float SpawnedAt = 0.0f;
		TWeakObjectPtr<ACharacter> SpawnedMonster;
		TWeakObjectPtr<ACharacter> PlacedMonster;
		FVector SpawnedStart = FVector::ZeroVector;
		FVector PlacedStart = FVector::ZeroVector;
		bool bSpawned = false;
		bool bHadPlacedMonster = false;
		bool bSpawnedVelocity = false;
		bool bSpawnedMoved = false;
		bool bPlacedVelocity = false;
		bool bPlacedMoved = false;
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FMonsterMovementPIETest, "LostWithin.AI.MonsterMovement.PIE",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FMonsterMovementPIETest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!World || GEditor->PlayWorld || World->GetOutermost()->GetName() != TEXT("/Game/FirstPerson/Lvl_FirstPerson"))
	{
		AddError(TEXT("Open Lvl_FirstPerson, finish setup and stop PIE before running this regression."));
		return false;
	}
	ADD_LATENT_AUTOMATION_COMMAND(FStartPIECommand(false));
	FAutomationTestFramework::Get().EnqueueLatentCommand(MakeShared<FObserveMonsterMovement>(this));
	ADD_LATENT_AUTOMATION_COMMAND(FEndPlayMapCommand());
	return true;
}

#endif
