// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "Components/BoxComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/ProximityFloatSpinComponent.h"
#include "Components/SceneComponent.h"
#include "Components/SphereComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Actor.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/InteractableInterface.h"
#include "Interaction/PickupItemActor.h"
#include "Inventory/InventoryComponent.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"
#include "UObject/UObjectIterator.h"

namespace
{
	struct FScopedPickupTestWorld
	{
		UWorld* World = nullptr;

		FScopedPickupTestWorld()
		{
			World = UWorld::CreateWorld(EWorldType::Game, false);
			if (World && GEngine)
			{
				FWorldContext& Context = GEngine->CreateNewWorldContext(EWorldType::Game);
				Context.SetCurrentWorld(World);
				World->InitializeActorsForPlay(FURL());
				World->BeginPlay();
				if (AWorldSettings* WS = World->GetWorldSettings())
				{
					WS->NotifyBeginPlay();
				}
			}
		}

		~FScopedPickupTestWorld()
		{
			if (World)
			{
				World->DestroyWorld(false);
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}
				World = nullptr;
			}
			CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
		}
	};

	void SpawnTestFloor(UWorld* World, float TopZ)
	{
		AActor* FloorActor = World->SpawnActor<AActor>(AActor::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator);
		UBoxComponent* FloorBox = NewObject<UBoxComponent>(FloorActor, TEXT("FloorBox"));
		FloorBox->SetMobility(EComponentMobility::Static);
		FloorBox->SetBoxExtent(FVector(2000.0f, 2000.0f, 50.0f));
		FloorBox->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		FloorBox->SetCollisionObjectType(ECC_WorldStatic);
		FloorBox->SetCollisionResponseToAllChannels(ECR_Block);
		FloorActor->SetRootComponent(FloorBox);
		FloorBox->RegisterComponent();
		FloorBox->SetWorldLocation(FVector(0.0f, 0.0f, TopZ - 50.0f));
	}

	void StepWorldPhysics(UWorld* World, float Dt, int32 Steps)
	{
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			++GFrameCounter;
			World->Tick(LEVELTICK_All, Dt);
		}
	}

	AActor* SpawnTestPlayerWithInventory(UWorld* World, const FVector& Location)
	{
		AActor* PlayerActor = World->SpawnActorDeferred<AActor>(
			AActor::StaticClass(),
			FTransform(Location),
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		if (!PlayerActor)
		{
			return nullptr;
		}

		USceneComponent* Root = NewObject<USceneComponent>(PlayerActor, TEXT("Root"));
		Root->SetMobility(EComponentMobility::Movable);
		PlayerActor->SetRootComponent(Root);
		Root->RegisterComponent();

		UInventoryComponent* InvComp = NewObject<UInventoryComponent>(PlayerActor, TEXT("Inventory"));
		InvComp->RegisterComponent();

		PlayerActor->FinishSpawning(FTransform(Location));
		return PlayerActor;
	}

	void FindFlashlightLightPair(AActor* OwnerActor, USpotLightComponent*& OutSpotLight, UPointLightComponent*& OutFillLight)
	{
		OutSpotLight = OwnerActor ? OwnerActor->FindComponentByClass<USpotLightComponent>() : nullptr;
		OutFillLight = nullptr;
		if (!OwnerActor)
		{
			return;
		}

		TArray<UPointLightComponent*> PointLights;
		OwnerActor->GetComponents<UPointLightComponent>(PointLights);
		for (UPointLightComponent* PL : PointLights)
		{
			if (PL && !PL->IsA<USpotLightComponent>())
			{
				OutFillLight = PL;
				break;
			}
		}
	}

	APickupItemActor* FindWorldPickupByID(UWorld* World, FName ItemID)
	{
		if (!World)
		{
			return nullptr;
		}

		for (TActorIterator<APickupItemActor> It(World); It; ++It)
		{
			if (IsValid(*It) && !(*It)->IsActorBeingDestroyed() && (*It)->ItemData.ItemID == ItemID)
			{
				return *It;
			}
		}
		return nullptr;
	}

	bool ToggleCharacterFlashlight(ALostWithinCharacter* Char)
	{
		if (!Char)
		{
			return false;
		}
		if (UFunction* ToggleFn = Char->FindFunction(TEXT("DoToggleFlashlight")))
		{
			Char->ProcessEvent(ToggleFn, nullptr);
			return true;
		}
		return false;
	}

	bool ReloadCharacterFlashlight(ALostWithinCharacter* Char)
	{
		if (!Char)
		{
			return false;
		}
		if (UFunction* ReloadFn = Char->FindFunction(TEXT("DoReloadFlashlight")))
		{
			Char->ProcessEvent(ReloadFn, nullptr);
			return true;
		}
		return false;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinPickupItemPhysicsAndKeysAutomationTest,
	"LostWithin.Interaction.PickupItemPhysicsAndKeys",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinPickupItemPhysicsAndKeysAutomationTest::RunTest(const FString& Parameters)
{
	FScopedPickupTestWorld TestEnv;
	if (!TestNotNull(TEXT("Pickup test world created"), TestEnv.World))
	{
		return false;
	}

	SpawnTestFloor(TestEnv.World, 0.0f);

	// 1. Flashlight pickup scale is 0.15 and child CollisionSphere keeps absolute scale 1.0
	{
		APickupItemActor* FlashlightPickup = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(0.0f, 0.0f, 120.0f),
			FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Flashlight pickup spawned"), FlashlightPickup))
		{
			return false;
		}

		FlashlightPickup->ConfigureAsFlashlight();
		TestTrue(TEXT("FlashlightPickupScale defaults to 0.15"), FMath::IsNearlyEqual(FlashlightPickup->FlashlightPickupScale, 0.15f, 0.001f));

		UStaticMeshComponent* MeshComp = FlashlightPickup->FindComponentByClass<UStaticMeshComponent>();
		USphereComponent* SphereComp = FlashlightPickup->FindComponentByClass<USphereComponent>();
		TestNotNull(TEXT("Flashlight MeshComponent exists"), MeshComp);
		TestNotNull(TEXT("Flashlight CollisionSphere exists"), SphereComp);

		if (MeshComp && SphereComp)
		{
			TestEqual(TEXT("MeshComponent is RootComponent"), Cast<USceneComponent>(MeshComp), FlashlightPickup->GetRootComponent());
			TestTrue(
				TEXT("Flashlight pickup mesh scale is 0.15"),
				MeshComp->GetRelativeScale3D().Equals(FVector(0.15f), 0.001f));
			TestTrue(
				TEXT("Assist CollisionSphere maintains 28cm radius regardless of 0.15 mesh scale"),
				FMath::IsNearlyEqual(SphereComp->GetScaledSphereRadius(), 28.0f, 0.5f));
			TestNotNull(TEXT("Flashlight mesh asset loaded"), MeshComp->GetStaticMesh().Get());
		}

		FlashlightPickup->Destroy();
	}

	// 2. Placed /Game/Item/* pickups simulate physics on BeginPlay, fall under gravity, and do not spin
	{
		APickupItemActor* PlacedKey = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(200.0f, 0.0f, 150.0f),
			FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("Placed OldKey pickup spawned"), PlacedKey))
		{
			return false;
		}

		PlacedKey->ConfigureFromPreset(ELostWithinItemPreset::OldKey);
		const float InitialZ = PlacedKey->GetActorLocation().Z;
		TestTrue(TEXT("Placed /Game/Item/* pickup enables physics immediately on BeginPlay"), PlacedKey->IsSimulatingItemPhysics());

		StepWorldPhysics(TestEnv.World, 1.0f / 60.0f, 150); // 2.5s gravity fall & settle

		const float SettledZ = PlacedKey->GetActorLocation().Z;
		TestTrue(
			FString::Printf(TEXT("Placed item fell under gravity from Z=%.1f onto floor, got Z=%.1f"), InitialZ, SettledZ),
			SettledZ < InitialZ - 80.0f && SettledZ > -20.0f);

		const FRotator SettledRot = PlacedKey->GetActorRotation();
		StepWorldPhysics(TestEnv.World, 1.0f / 60.0f, 30);
		TestTrue(
			TEXT("Item has no scripted continuous spin animation after settling"),
			PlacedKey->GetActorRotation().Equals(SettledRot, 1.5f));

		PlacedKey->Destroy();
	}

	// 3. All /Game/Item/* presets resolve their dedicated meshes (and log unscaled bounds for sizing verification)
	{
		struct FPresetExpectation
		{
			ELostWithinItemPreset Preset;
			FName ExpectedID;
			const TCHAR* ExpectedName;
			const TCHAR* ExpectedPackagePrefix;
		};

		const FPresetExpectation Expectations[] = {
			{ ELostWithinItemPreset::Flashlight, APickupItemActor::FlashlightItemID, TEXT("Flashlight"), TEXT("/Game/Item/Flashlight/StaticMeshes/defaultMaterial") },
			{ ELostWithinItemPreset::Battery, APickupItemActor::BatteryItemID, TEXT("Battery"), TEXT("/Game/Item/Battery/StaticMeshes/dry_cell_-_aa_battery") },
			{ ELostWithinItemPreset::FairyTaleKittyKey, APickupItemActor::FairyTaleKittyKeyItemID, TEXT("Fairy Tale Kitty Key"), TEXT("/Game/Item/Keys/fairy_tale_kitty_key/StaticMeshes/fairy_tale_kitty_key") },
			{ ELostWithinItemPreset::Key, APickupItemActor::KeyItemID, TEXT("Key"), TEXT("/Game/Item/Keys/key/StaticMeshes/key") },
			{ ELostWithinItemPreset::OldKey, APickupItemActor::OldKeyItemID, TEXT("Old Key"), TEXT("/Game/Item/Keys/old_key/StaticMeshes/old_key") },
			{ ELostWithinItemPreset::RoyalKey, APickupItemActor::RoyalKeyItemID, TEXT("Royal Key"), TEXT("/Game/Item/Keys/royal_key_2/StaticMeshes/royal_key_2") }
		};

		for (const FPresetExpectation& Exp : Expectations)
		{
			APickupItemActor* Probe = TestEnv.World->SpawnActor<APickupItemActor>(
				APickupItemActor::StaticClass(),
				FVector(400.0f, 0.0f, 80.0f),
				FRotator::ZeroRotator);
			if (!TestNotNull(TEXT("Catalog probe pickup spawned"), Probe))
			{
				return false;
			}

			Probe->ConfigureFromPreset(Exp.Preset);
			TestEqual(TEXT("Preset ItemID matches"), Probe->ItemData.ItemID, Exp.ExpectedID);
			TestEqual(TEXT("Preset DisplayName matches"), Probe->ItemData.DisplayName.ToString(), FString(Exp.ExpectedName));

			UStaticMeshComponent* MeshComp = Probe->FindComponentByClass<UStaticMeshComponent>();
			UStaticMesh* Mesh = MeshComp ? MeshComp->GetStaticMesh() : nullptr;
			if (TestNotNull(TEXT("Catalog item loaded StaticMesh"), Mesh))
			{
				TestTrue(
					FString::Printf(TEXT("Loaded mesh '%s' matches '%s'"), *Mesh->GetPathName(), Exp.ExpectedPackagePrefix),
					Mesh->GetPathName().StartsWith(Exp.ExpectedPackagePrefix));

				const FBox LocalBox = Mesh->GetBoundingBox();
				const FVector UnscaledSize = LocalBox.GetSize();
				AddInfo(FString::Printf(
					TEXT("[ITEM BOUNDS] %s unscaled min=(%.2f, %.2f, %.2f) max=(%.2f, %.2f, %.2f) size=(%.2f, %.2f, %.2f) cm, pickup scale=%.2f"),
					Exp.ExpectedName,
					LocalBox.Min.X,
					LocalBox.Min.Y,
					LocalBox.Min.Z,
					LocalBox.Max.X,
					LocalBox.Max.Y,
					LocalBox.Max.Z,
					UnscaledSize.X,
					UnscaledSize.Y,
					UnscaledSize.Z,
					MeshComp->GetRelativeScale3D().X));

				if (Exp.Preset == ELostWithinItemPreset::Battery)
				{
					TestTrue(
						TEXT("Battery pickup mesh scale is 0.025"),
						MeshComp->GetRelativeScale3D().Equals(FVector(0.025f), 0.0005f));
				}
				else if (Exp.Preset != ELostWithinItemPreset::Flashlight)
				{
					const FVector ScaledExtentSize = UnscaledSize * MeshComp->GetRelativeScale3D();
					TestTrue(
						FString::Printf(TEXT("Key '%s' normalized max world size is ~18 cm (got %.2f cm)"), Exp.ExpectedName, ScaledExtentSize.GetMax()),
						FMath::IsNearlyEqual(ScaledExtentSize.GetMax(), 18.0f, 1.0f));

					if (USphereComponent* ProbeSphere = Probe->FindComponentByClass<USphereComponent>())
					{
						const FVector ExpectedCenterWorld = Probe->GetActorTransform().TransformPosition(LocalBox.GetCenter());
						TestTrue(
							TEXT("Assist CollisionSphere centers on visual key bounds"),
							ProbeSphere->GetComponentLocation().Equals(ExpectedCenterWorld, 1.0f));
					}

					USpotLightComponent* KeySpot = nullptr;
					UPointLightComponent* KeyFill = nullptr;
					FindFlashlightLightPair(Probe, KeySpot, KeyFill);
					TestFalse(TEXT("Non-flashlight pickup keeps SpotLight hidden"), KeySpot && KeySpot->IsVisible());
					TestFalse(TEXT("Non-flashlight pickup keeps FillLight hidden"), KeyFill && KeyFill->IsVisible());
				}
			}

			Probe->Destroy();
		}
	}

	// 4. Cat drops Fairy Tale Kitty Key (not Flashlight) under physics
	{
		AActor* CatActor = TestEnv.World->SpawnActorDeferred<AActor>(
			AActor::StaticClass(),
			FTransform(FVector(-400.0f, 0.0f, 20.0f)),
			nullptr,
			nullptr,
			ESpawnActorCollisionHandlingMethod::AlwaysSpawn);
		USceneComponent* CatRoot = NewObject<USceneComponent>(CatActor, TEXT("CatRoot"));
		CatRoot->SetMobility(EComponentMobility::Movable);
		CatActor->SetRootComponent(CatRoot);
		CatRoot->RegisterComponent();

		UProximityFloatSpinComponent* SpinComp = NewObject<UProximityFloatSpinComponent>(CatActor, TEXT("SpinComp"));
		SpinComp->bAutoSetHierarchyMovable = false;
		SpinComp->bDarkenSkyOnSpinWindow = false;
		SpinComp->bEnableDiscoEnvironmentOnSpinWindow = false;
		SpinComp->bUseMorphVicinityCheck = false;
		SpinComp->bTriggerSpinByInteractionCount = false;
		SpinComp->ItemDropInteractionThreshold = 2;
		SpinComp->InteractionSoundSearchPaths.Reset();
		SpinComp->RegisterComponent();
		CatActor->FinishSpawning(FTransform(FVector(-400.0f, 0.0f, 20.0f)));

		AActor* PlayerActor = SpawnTestPlayerWithInventory(TestEnv.World, FVector(-200.0f, 0.0f, 20.0f));

		IInteractableInterface::Execute_OnInteract(SpinComp, PlayerActor);
		IInteractableInterface::Execute_OnInteract(SpinComp, PlayerActor);

		APickupItemActor* CatDrop = nullptr;
		for (TActorIterator<APickupItemActor> It(TestEnv.World); It; ++It)
		{
			if (IsValid(*It) && !(*It)->IsActorBeingDestroyed())
			{
				CatDrop = *It;
				break;
			}
		}

		if (TestNotNull(TEXT("Cat spawned a pickup actor on drop threshold"), CatDrop))
		{
			TestEqual(TEXT("Cat dropped FairyTaleKittyKey instead of Flashlight"), CatDrop->ItemData.ItemID, APickupItemActor::FairyTaleKittyKeyItemID);
			TestEqual(TEXT("Cat drop prompt mentions Fairy Tale Kitty Key"), CatDrop->ItemData.DisplayName.ToString(), FString(TEXT("Fairy Tale Kitty Key")));
			TestTrue(TEXT("Cat-dropped Fairy Tale Kitty Key simulates physics"), CatDrop->IsSimulatingItemPhysics());

			const float DropStartZ = CatDrop->GetActorLocation().Z;
			StepWorldPhysics(TestEnv.World, 1.0f / 60.0f, 90);
			TestTrue(TEXT("Cat-dropped Fairy Tale Kitty Key landed under gravity"), CatDrop->GetActorLocation().Z < DropStartZ);
			CatDrop->Destroy();
		}

		PlayerActor->Destroy();
		CatActor->Destroy();
	}

	// 5. Picking up a placed Royal Key and dropping from inventory preserves mesh, scale, physics, and zero spin
	{
		AActor* PlayerActor = SpawnTestPlayerWithInventory(TestEnv.World, FVector(800.0f, 0.0f, 50.0f));
		UInventoryComponent* Inventory = PlayerActor ? PlayerActor->FindComponentByClass<UInventoryComponent>() : nullptr;
		if (!TestNotNull(TEXT("Player InventoryComponent exists"), Inventory))
		{
			return false;
		}

		APickupItemActor* PlacedRoyalKey = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(860.0f, 0.0f, 20.0f),
			FRotator::ZeroRotator);
		PlacedRoyalKey->ConfigureFromPreset(ELostWithinItemPreset::RoyalKey);

		TestTrue(TEXT("Player can interact with placed Royal Key"), IInteractableInterface::Execute_CanInteract(PlacedRoyalKey, PlayerActor));
		IInteractableInterface::Execute_OnInteract(PlacedRoyalKey, PlayerActor);

		TestTrue(TEXT("RoyalKey added to inventory"), Inventory->HasItem(APickupItemActor::RoyalKeyItemID));
		TestTrue(TEXT("Inventory stored WorldMesh on RoyalKey"), Inventory->GetItems()[0].WorldMesh.GetAssetName().Contains(TEXT("royal_key_2")));

		TestTrue(TEXT("Inventory DropItemAtSlot succeeds"), Inventory->DropItemAtSlot(0, 1));
		TestFalse(TEXT("RoyalKey removed from inventory after drop"), Inventory->HasItem(APickupItemActor::RoyalKeyItemID));

		APickupItemActor* DroppedRoyalKey = nullptr;
		for (TActorIterator<APickupItemActor> It(TestEnv.World); It; ++It)
		{
			if (IsValid(*It) && !(*It)->IsActorBeingDestroyed() && (*It)->ItemData.ItemID == APickupItemActor::RoyalKeyItemID)
			{
				DroppedRoyalKey = *It;
				break;
			}
		}

		if (TestNotNull(TEXT("Dropped RoyalKey spawned into world"), DroppedRoyalKey))
		{
			UStaticMeshComponent* DroppedMesh = DroppedRoyalKey->FindComponentByClass<UStaticMeshComponent>();
			TestNotNull(TEXT("Dropped RoyalKey preserved its static mesh instead of fallback cube"), DroppedMesh ? DroppedMesh->GetStaticMesh().Get() : nullptr);
			if (DroppedMesh && DroppedMesh->GetStaticMesh())
			{
				TestTrue(TEXT("Dropped RoyalKey mesh is royal_key_2"), DroppedMesh->GetStaticMesh()->GetPathName().Contains(TEXT("royal_key_2")));
			}
			TestTrue(TEXT("Dropped RoyalKey simulates physics"), DroppedRoyalKey->IsSimulatingItemPhysics());

			DroppedRoyalKey->Destroy();
		}

		PlayerActor->Destroy();
	}

	// 6. Flashlight keeps ON/OFF state across pickup <-> [F] toggle <-> drop and lights world under physics when dropped ON
	{
		UClass* CharClass = LoadClass<ALostWithinCharacter>(
			nullptr,
			TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
		if (!TestNotNull(TEXT("BP_FirstPersonCharacter loaded for flashlight drop test"), CharClass))
		{
			return false;
		}

		FActorSpawnParameters CharSpawnParams;
		CharSpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALostWithinCharacter* Char = TestEnv.World->SpawnActor<ALostWithinCharacter>(
			CharClass,
			FVector(1200.0f, 0.0f, 110.0f),
			FRotator::ZeroRotator,
			CharSpawnParams);
		if (!TestNotNull(TEXT("Character spawned for flashlight drop test"), Char))
		{
			return false;
		}

		UInventoryComponent* Inventory = Char->GetInventoryComponent();
		if (!TestNotNull(TEXT("Character InventoryComponent exists"), Inventory))
		{
			Char->Destroy();
			return false;
		}

		USpotLightComponent* CharSpot = nullptr;
		UPointLightComponent* CharFill = nullptr;
		FindFlashlightLightPair(Char, CharSpot, CharFill);
		TestNotNull(TEXT("Character SpotLight exists"), CharSpot);
		TestNotNull(TEXT("Character FillLight exists"), CharFill);

		APickupItemActor* PlacedFlashlight = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(1280.0f, 0.0f, 60.0f),
			FRotator::ZeroRotator);
		PlacedFlashlight->ConfigureAsFlashlight();
		TestFalse(TEXT("Placed flashlight defaults to OFF"), PlacedFlashlight->IsFlashlightOn());

		IInteractableInterface::Execute_OnInteract(PlacedFlashlight, Char);
		TestTrue(TEXT("Character picked up flashlight"), Char->HasFlashlight());
		TestFalse(TEXT("Picked up OFF flashlight starts OFF in hand"), Char->IsFlashlightOn());

		// Toggle ON, then drop
		TestTrue(TEXT("Called DoToggleFlashlight ON"), ToggleCharacterFlashlight(Char));
		TestTrue(TEXT("Character flashlight toggled ON"), Char->IsFlashlightOn());
		TestTrue(TEXT("Inventory tracks flashlight ON state"), Inventory->GetFlashlightOnState());
		TestTrue(TEXT("Character held SpotLight is visible while ON"), CharSpot && CharSpot->IsVisible());

		TestTrue(TEXT("Dropping ON flashlight from inventory succeeds"), Inventory->DropItemAtSlot(0, 1));
		TestFalse(TEXT("Character no longer owns flashlight after drop"), Char->HasFlashlight());
		TestFalse(TEXT("Character IsFlashlightOn resets when flashlight leaves inventory"), Char->IsFlashlightOn());
		TestFalse(TEXT("Character held SpotLight hides after drop"), CharSpot && CharSpot->IsVisible());

		APickupItemActor* DroppedLitFlashlight = FindWorldPickupByID(TestEnv.World, APickupItemActor::FlashlightItemID);
		if (TestNotNull(TEXT("Dropped ON flashlight spawned into world"), DroppedLitFlashlight))
		{
			TestTrue(TEXT("Dropped flashlight knows it was ON"), DroppedLitFlashlight->IsFlashlightOn());
			TestTrue(TEXT("Dropped flashlight simulates physics"), DroppedLitFlashlight->IsSimulatingItemPhysics());

			USpotLightComponent* DroppedSpot = nullptr;
			UPointLightComponent* DroppedFill = nullptr;
			FindFlashlightLightPair(DroppedLitFlashlight, DroppedSpot, DroppedFill);
			if (TestNotNull(TEXT("Dropped flashlight SpotLight exists"), DroppedSpot)
				&& TestNotNull(TEXT("Dropped flashlight FillLight exists"), DroppedFill))
			{
				TestTrue(TEXT("Dropped ON flashlight SpotLight is visible"), DroppedSpot->IsVisible());
				TestTrue(TEXT("Dropped ON flashlight FillLight is visible"), DroppedFill->IsVisible());
				TestTrue(TEXT("Dropped SpotLight intensity at 100% battery is 75 cd (3x)"), FMath::IsNearlyEqual(DroppedSpot->Intensity, 75.0f, 0.2f));
				TestTrue(TEXT("Dropped SpotLight attenuation radius stays 1500 cm (not scaled by 0.15)"), FMath::IsNearlyEqual(DroppedSpot->AttenuationRadius, 1500.0f, 1.0f));
				TestTrue(TEXT("Dropped SpotLight world scale stays ~1.0 via absolute scale"), DroppedSpot->GetComponentScale().Equals(FVector::OneVector, 0.01f));
				TestTrue(TEXT("Dropped FillLight intensity at 100% battery is 1.5 cd (3x)"), FMath::IsNearlyEqual(DroppedFill->Intensity, 1.5f, 0.05f));
				TestTrue(TEXT("Dropped SpotLight outer cone at 100% battery is 54 deg (3x)"), FMath::IsNearlyEqual(DroppedSpot->OuterConeAngle, 54.0f, 0.1f));

				const FVector InitialSpotLocation = DroppedSpot->GetComponentLocation();
				FHitResult SelfBlockHit;
				FCollisionQueryParams BeamParams(SCENE_QUERY_STAT(DroppedFlashlightSelfTrace), false);
				const bool bHitOwnMesh = TestEnv.World->LineTraceSingleByChannel(
					SelfBlockHit,
					InitialSpotLocation,
					InitialSpotLocation + DroppedSpot->GetForwardVector() * 40.0f,
					ECC_Visibility,
					BeamParams);
				TestFalse(
					TEXT("Dropped spotlight muzzle sits outside its own mesh without self-occlusion"),
					bHitOwnMesh && SelfBlockHit.GetActor() == DroppedLitFlashlight && SelfBlockHit.GetComponent() == DroppedLitFlashlight->GetRootComponent());

				StepWorldPhysics(TestEnv.World, 1.0f / 60.0f, 60);
				TestTrue(
					TEXT("Dropped spotlight moves together with falling physics flashlight"),
					FVector::Dist(DroppedSpot->GetComponentLocation(), InitialSpotLocation) > 20.0f);
			}

			// Pick back up the ON dropped flashlight -> remains ON in hand
			IInteractableInterface::Execute_OnInteract(DroppedLitFlashlight, Char);
			TestTrue(TEXT("Re-picking ON dropped flashlight keeps it ON in hand"), Char->IsFlashlightOn());
			TestTrue(TEXT("Held SpotLight is active again right after picking up ON flashlight"), CharSpot && CharSpot->IsVisible());
		}

		// Toggle OFF, drop, verify OFF in world, and re-pick keeps OFF
		if (Char->HasFlashlight())
		{
			TestTrue(TEXT("Called DoToggleFlashlight OFF"), ToggleCharacterFlashlight(Char));
			TestFalse(TEXT("Character flashlight toggled OFF before second drop"), Char->IsFlashlightOn());
			TestFalse(TEXT("Inventory tracks flashlight OFF state"), Inventory->GetFlashlightOnState());
			TestTrue(TEXT("Dropping OFF flashlight succeeds"), Inventory->DropItemAtSlot(0, 1));

			APickupItemActor* DroppedDarkFlashlight = FindWorldPickupByID(TestEnv.World, APickupItemActor::FlashlightItemID);
			if (TestNotNull(TEXT("Dropped OFF flashlight spawned into world"), DroppedDarkFlashlight))
			{
				TestFalse(TEXT("Dropped OFF flashlight knows it was OFF"), DroppedDarkFlashlight->IsFlashlightOn());
				USpotLightComponent* DarkSpot = nullptr;
				UPointLightComponent* DarkFill = nullptr;
				FindFlashlightLightPair(DroppedDarkFlashlight, DarkSpot, DarkFill);
				TestFalse(TEXT("Dropped OFF flashlight SpotLight is hidden"), DarkSpot && DarkSpot->IsVisible());
				TestFalse(TEXT("Dropped OFF flashlight FillLight is hidden"), DarkFill && DarkFill->IsVisible());

				IInteractableInterface::Execute_OnInteract(DroppedDarkFlashlight, Char);
				TestFalse(TEXT("Re-picking OFF flashlight keeps it OFF in hand"), Char->IsFlashlightOn());
			}
		}

		if (APickupItemActor* Leftover = FindWorldPickupByID(TestEnv.World, APickupItemActor::FlashlightItemID))
		{
			Leftover->Destroy();
		}
		Char->Destroy();
	}

	// 7. 2-battery requirement, combined percentage, brightness/cone curve (25% = 1x, 100% = 3x), [R] swaps, and timed drain
	{
		// 7a. Verify exact light parameter curve across 100%, 25%, 12.5%, and 0%
		const FLostWithinFlashlightLightParams BaseParams;
		const FLostWithinFlashlightLightParams Params100 = APickupItemActor::ComputeEffectiveFlashlightLightParams(BaseParams, 100.0f);
		TestTrue(TEXT("100% battery -> SpotIntensity 75 cd (3x)"), FMath::IsNearlyEqual(Params100.SpotIntensity, 75.0f, 0.05f));
		TestTrue(TEXT("100% battery -> FillIntensity 1.5 cd (3x)"), FMath::IsNearlyEqual(Params100.FillIntensity, 1.5f, 0.01f));
		TestTrue(TEXT("100% battery -> InnerConeAngle 24 deg (3x)"), FMath::IsNearlyEqual(Params100.InnerConeAngle, 24.0f, 0.05f));
		TestTrue(TEXT("100% battery -> OuterConeAngle 54 deg (3x)"), FMath::IsNearlyEqual(Params100.OuterConeAngle, 54.0f, 0.05f));

		const FLostWithinFlashlightLightParams Params25 = APickupItemActor::ComputeEffectiveFlashlightLightParams(BaseParams, 25.0f);
		TestTrue(TEXT("25% battery -> SpotIntensity 25 cd (1x baseline)"), FMath::IsNearlyEqual(Params25.SpotIntensity, 25.0f, 0.05f));
		TestTrue(TEXT("25% battery -> FillIntensity 0.5 cd (1x baseline)"), FMath::IsNearlyEqual(Params25.FillIntensity, 0.5f, 0.01f));
		TestTrue(TEXT("25% battery -> InnerConeAngle 8 deg (1x baseline)"), FMath::IsNearlyEqual(Params25.InnerConeAngle, 8.0f, 0.05f));
		TestTrue(TEXT("25% battery -> OuterConeAngle 18 deg (1x baseline)"), FMath::IsNearlyEqual(Params25.OuterConeAngle, 18.0f, 0.05f));

		const FLostWithinFlashlightLightParams ParamsLow = APickupItemActor::ComputeEffectiveFlashlightLightParams(BaseParams, 12.5f);
		TestTrue(TEXT("12.5% battery dims SpotIntensity linearly to 15.625 cd (0.625x)"), FMath::IsNearlyEqual(ParamsLow.SpotIntensity, 15.625f, 0.05f));
		TestTrue(TEXT("12.5% battery holds OuterConeAngle at 18 deg"), FMath::IsNearlyEqual(ParamsLow.OuterConeAngle, 18.0f, 0.05f));

		// 7b. Character with 1/2 batteries cannot operate until 2nd battery is inserted via [R]; uneven drain & swapping
		UClass* CharClass = LoadClass<ALostWithinCharacter>(
			nullptr,
			TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
		if (!TestNotNull(TEXT("BP_FirstPersonCharacter loaded for battery test"), CharClass))
		{
			return false;
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
		ALostWithinCharacter* Char = TestEnv.World->SpawnActor<ALostWithinCharacter>(
			CharClass,
			FVector(1500.0f, 0.0f, 110.0f),
			FRotator::ZeroRotator,
			SpawnParams);
		if (!TestNotNull(TEXT("Character spawned for battery test"), Char))
		{
			return false;
		}

		Char->SetFlashlightBatteryDurationSeconds(10.0f); // Speed up timed drain for test
		UInventoryComponent* Inv = Char->GetInventoryComponent();
		USpotLightComponent* CharSpot = nullptr;
		UPointLightComponent* CharFill = nullptr;
		FindFlashlightLightPair(Char, CharSpot, CharFill);

		// Spawn a partial flashlight with only 1 battery (60%) and an empty 2nd bay
		APickupItemActor* PartialFlashlight = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(1540.0f, 0.0f, 60.0f),
			FRotator::ZeroRotator);
		PartialFlashlight->ConfigureAsFlashlight();
		PartialFlashlight->ItemData.BatterySlot1Percent = 60.0f;
		PartialFlashlight->ItemData.BatterySlot2Percent = FInventoryItem::EmptyBatterySlotValue;
		IInteractableInterface::Execute_OnInteract(PartialFlashlight, Char);

		TestEqual(TEXT("Single-cell flashlight reports 1/2 batteries"), Char->GetFlashlightBatteryCount(), 1);
		TestTrue(TEXT("Cannot toggle flashlight ON with only 1/2 batteries"), ToggleCharacterFlashlight(Char) && !Char->IsFlashlightOn());

		// Pick up two distinct batteries (90% and 30%) and verify they stay separate in inventory
		APickupItemActor* Battery90 = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(1560.0f, 30.0f, 60.0f),
			FRotator::ZeroRotator);
		Battery90->ConfigureAsBattery(90.0f);
		TestTrue(
			TEXT("Battery interaction prompt includes remaining charge %"),
			IInteractableInterface::Execute_GetInteractionPrompt(Battery90).ToString().Contains(TEXT("90%")));
		IInteractableInterface::Execute_OnInteract(Battery90, Char);

		APickupItemActor* Battery30 = TestEnv.World->SpawnActor<APickupItemActor>(
			APickupItemActor::StaticClass(),
			FVector(1560.0f, -30.0f, 60.0f),
			FRotator::ZeroRotator);
		Battery30->ConfigureAsBattery(30.0f);
		IInteractableInterface::Execute_OnInteract(Battery30, Char);

		TestEqual(TEXT("Distinct batteries keep separate slots (2 batteries in inventory)"), Inv->GetItemCount(APickupItemActor::BatteryItemID), 2);
		TestEqual(TEXT("Total inventory slots = 3 (1 Flashlight + 2 separate Batteries)"), Inv->GetItems().Num(), 3);

		// Press [R] -> loads best battery (90%) into empty Slot 2 -> combined (60+90)/2 = 75%
		TestTrue(TEXT("[R] reloads highest available battery into empty bay"), ReloadCharacterFlashlight(Char));
		TestEqual(TEXT("Flashlight now has 2/2 batteries"), Char->GetFlashlightBatteryCount(), 2);
		TestTrue(TEXT("Combined battery is (60 + 90)/2 = 75%"), FMath::IsNearlyEqual(Char->GetFlashlightBatteryPercent(), 75.0f, 0.2f));
		TestEqual(TEXT("Consumed 1 battery from inventory"), Inv->GetItemCount(APickupItemActor::BatteryItemID), 1);

		// Toggle ON at 75% -> AlphaHigh = 50/75 = 2/3 -> Multiplier = 1 + 2*(2/3) = 2.3333x -> Spot = 58.33 cd, OuterCone = 42 deg
		TestTrue(TEXT("Flashlight toggles ON with 2 batteries installed"), ToggleCharacterFlashlight(Char) && Char->IsFlashlightOn());
		TestTrue(TEXT("75% combined -> SpotIntensity ~58.33 cd"), CharSpot && FMath::IsNearlyEqual(CharSpot->Intensity, 58.333f, 0.4f));
		TestTrue(TEXT("75% combined -> OuterConeAngle ~42 deg"), CharSpot && FMath::IsNearlyEqual(CharSpot->OuterConeAngle, 42.0f, 0.3f));

		// Drain for 5.0 seconds on 10.0s full scale = -50% combined -> should reach 25% combined (Spot = 25 cd, Outer = 18 deg)
		for (int32 TickIdx = 0; TickIdx < 50; ++TickIdx)
		{
			++GFrameCounter;
			Char->Tick(0.1f);
		}
		TestTrue(TEXT("After 5s of 10s life, combined drops from 75% to ~25%"), FMath::IsNearlyEqual(Char->GetFlashlightBatteryPercent(), 25.0f, 0.5f));
		TestTrue(TEXT("At 25% charge, SpotIntensity reaches 25 cd baseline"), CharSpot && FMath::IsNearlyEqual(CharSpot->Intensity, 25.0f, 0.8f));
		TestTrue(TEXT("At 25% charge, OuterConeAngle reaches 18 deg baseline"), CharSpot && FMath::IsNearlyEqual(CharSpot->OuterConeAngle, 18.0f, 0.5f));

		// At this point Slot1 drained 60 -> 10%, Slot2 drained 90 -> 40% (combined 25%).
		// Swap lowest (10% in Slot1) with remaining 30% battery -> old 10% (>0%) returns to inventory, new combined (30+40)/2 = 35%!
		TestTrue(TEXT("[R] swaps lowest >0% battery (10%) with 30% battery"), ReloadCharacterFlashlight(Char));
		TestTrue(TEXT("New combined after swap is (30 + 40)/2 = 35%"), FMath::IsNearlyEqual(Char->GetFlashlightBatteryPercent(), 35.0f, 0.5f));
		bool bFoundReturned10PercentCell = false;
		for (const FInventoryItem& Item : Inv->GetItems())
		{
			if (Item.ItemID == APickupItemActor::BatteryItemID && FMath::IsNearlyEqual(Item.BatteryChargePercent, 10.0f, 0.6f))
			{
				bFoundReturned10PercentCell = true;
				break;
			}
		}
		TestTrue(TEXT("Replaced 10% cell returned to inventory with exact remaining percentage"), bFoundReturned10PercentCell);

		// Drop the remaining 10% battery and verify world pickup keeps 10% and 0.025 scale
		int32 BatterySlotToDrop = INDEX_NONE;
		for (int32 Idx = 0; Idx < Inv->GetItems().Num(); ++Idx)
		{
			if (Inv->GetItems()[Idx].ItemID == APickupItemActor::BatteryItemID)
			{
				BatterySlotToDrop = Idx;
				break;
			}
		}
		TestTrue(TEXT("Found returned battery slot to drop"), BatterySlotToDrop != INDEX_NONE);
		if (BatterySlotToDrop != INDEX_NONE)
		{
			TestTrue(TEXT("Dropped returned battery succeeds"), Inv->DropItemAtSlot(BatterySlotToDrop, 1));
			if (APickupItemActor* DroppedBattery = FindWorldPickupByID(TestEnv.World, APickupItemActor::BatteryItemID))
			{
				TestTrue(TEXT("Dropped battery preserved ~10% charge"), FMath::IsNearlyEqual(DroppedBattery->ItemData.BatteryChargePercent, 10.0f, 0.6f));
				TestTrue(TEXT("Dropped battery preserved 0.025 scale"), DroppedBattery->GetRootComponent()->GetRelativeScale3D().Equals(FVector(0.025f), 0.0005f));
				DroppedBattery->Destroy();
			}
		}

		// Drop lit flashlight (~35% combined) into world with 10s life, step physics 4.0s -> combined reaches 0% and shuts off cleanly
		TestTrue(TEXT("Dropping lit flashlight succeeds"), Inv->DropItemAtSlot(0, 1));
		if (APickupItemActor* DroppedFlashlight = FindWorldPickupByID(TestEnv.World, APickupItemActor::FlashlightItemID))
		{
			DroppedFlashlight->FlashlightBatteryDurationSeconds = 10.0f;
			TestTrue(TEXT("Dropped lit flashlight ticks to drain battery"), DroppedFlashlight->IsActorTickEnabled());

			StepWorldPhysics(TestEnv.World, 0.1f, 40); // 4.0s -> drains 40% >= 35%, shuts off at 0%
			TestFalse(TEXT("Dropped flashlight automatically shut off when reaching 0%"), DroppedFlashlight->IsFlashlightOn());
			TestTrue(TEXT("Dropped flashlight battery combined charge reached 0%"), FMath::IsNearlyEqual(DroppedFlashlight->ItemData.GetCombinedBatteryPercent(), 0.0f, 0.1f));

			// Re-pick dead flashlight and reload two fresh 100% batteries -> old 0% cells are discarded!
			IInteractableInterface::Execute_OnInteract(DroppedFlashlight, Char);
			TestFalse(TEXT("Cannot toggle 0% flashlight ON before replacing batteries"), ToggleCharacterFlashlight(Char) && Char->IsFlashlightOn());

			APickupItemActor* FreshBatt1 = TestEnv.World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(1560.0f, 40.0f, 60.0f), FRotator::ZeroRotator);
			FreshBatt1->ConfigureAsBattery(100.0f);
			IInteractableInterface::Execute_OnInteract(FreshBatt1, Char);

			APickupItemActor* FreshBatt2 = TestEnv.World->SpawnActor<APickupItemActor>(APickupItemActor::StaticClass(), FVector(1560.0f, 60.0f, 60.0f), FRotator::ZeroRotator);
			FreshBatt2->ConfigureAsBattery(100.0f);
			IInteractableInterface::Execute_OnInteract(FreshBatt2, Char);

			TestTrue(TEXT("First reload replaces first 0% dead battery cleanly"), ReloadCharacterFlashlight(Char));
			TestTrue(TEXT("Second reload replaces second 0% dead battery cleanly"), ReloadCharacterFlashlight(Char));
			TestTrue(TEXT("Combined charge is 100% after replacing both dead batteries"), FMath::IsNearlyEqual(Char->GetFlashlightBatteryPercent(), 100.0f, 0.1f));
			TestEqual(TEXT("Dead 0% replaced batteries were discarded rather than returned to inventory"), Inv->GetItemCount(APickupItemActor::BatteryItemID), 0);
			TestTrue(TEXT("Flashlight turns back ON cleanly after full battery change"), ToggleCharacterFlashlight(Char) && Char->IsFlashlightOn());
		}

		for (TActorIterator<APickupItemActor> It(TestEnv.World); It; ++It)
		{
			if (IsValid(*It) && !(*It)->IsActorBeingDestroyed())
			{
				(*It)->Destroy();
			}
		}
		Char->Destroy();
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
