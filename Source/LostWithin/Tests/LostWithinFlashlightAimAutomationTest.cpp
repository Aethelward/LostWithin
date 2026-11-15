// Copyright Epic Games, Inc. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS

#include "AIController.h"
#include "Animation/LostWithinFlashlightAnimInstance.h"
#include "Camera/CameraComponent.h"
#include "Components/PointLightComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SpotLightComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/SpringArmComponent.h"
#include "GameFramework/WorldSettings.h"
#include "Interaction/PickupItemActor.h"
#include "Inventory/InventoryComponent.h"
#include "LostWithinCharacter.h"
#include "Misc/AutomationTest.h"

namespace
{
	struct FScopedFlashlightTestWorld
	{
		UWorld* World = nullptr;

		FScopedFlashlightTestWorld()
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

		~FScopedFlashlightTestWorld()
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

	float ComputePitchDeltaDeg(const FQuat& TestQuat, const FQuat& NeutralQuat)
	{
		const FVector DeltaFwd = (TestQuat * NeutralQuat.Inverse()).RotateVector(FVector::ForwardVector);
		const float Horiz = FMath::Sqrt(DeltaFwd.X * DeltaFwd.X + DeltaFwd.Y * DeltaFwd.Y);
		return FMath::RadiansToDegrees(FMath::Atan2(DeltaFwd.Z, FMath::Max(Horiz, KINDA_SMALL_NUMBER)));
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(
	FLostWithinFlashlightAndAimAutomationTest,
	"LostWithin.Animation.FlashlightAndAim",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::ProductFilter)

bool FLostWithinFlashlightAndAimAutomationTest::RunTest(const FString& Parameters)
{
	FScopedFlashlightTestWorld TestEnv;
	if (!TestNotNull(TEXT("Test world created"), TestEnv.World))
	{
		return false;
	}

	UClass* CharClass = LoadClass<ALostWithinCharacter>(
		nullptr,
		TEXT("/Game/FirstPerson/Blueprints/BP_FirstPersonCharacter.BP_FirstPersonCharacter_C"));
	if (!TestNotNull(TEXT("BP_FirstPersonCharacter loaded"), CharClass))
	{
		return false;
	}

	FActorSpawnParameters SpawnParams;
	SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	ALostWithinCharacter* Char = TestEnv.World->SpawnActor<ALostWithinCharacter>(
		CharClass,
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		SpawnParams);
	if (!TestNotNull(TEXT("Character spawned"), Char))
	{
		return false;
	}
	TestTrue(TEXT("Character BeginPlay executed"), Char->HasActorBegunPlay());

	AAIController* AI = TestEnv.World->SpawnActor<AAIController>(AAIController::StaticClass(), FVector::ZeroVector, FRotator::ZeroRotator, SpawnParams);
	if (!TestNotNull(TEXT("AIController spawned"), AI))
	{
		return false;
	}
	AI->Possess(Char);

	USkeletalMeshComponent* Mesh3P = Char->GetMesh();
	USpringArmComponent* Boom3P = Char->FindComponentByClass<USpringArmComponent>();
	UCameraComponent* Cam3P = Char->GetThirdPersonCameraComponent();
	if (!TestNotNull(TEXT("3P Mesh exists"), Mesh3P)
		|| !TestNotNull(TEXT("3P Camera Boom exists"), Boom3P)
		|| !TestNotNull(TEXT("3P Camera exists"), Cam3P))
	{
		return false;
	}

	// Verify 1st-person view systems are completely absent and 3P over-the-shoulder defaults are active
	{
		TArray<UCameraComponent*> Cameras;
		Char->GetComponents<UCameraComponent>(Cameras);
		TestEqual(TEXT("Only 3P camera component exists"), Cameras.Num(), 1);

		TArray<USkeletalMeshComponent*> SkelMeshes;
		Char->GetComponents<USkeletalMeshComponent>(SkelMeshes);
		TestEqual(TEXT("Only 3P character skeletal mesh component exists"), SkelMeshes.Num(), 1);

		for (UActorComponent* Comp : Char->GetComponents())
		{
			if (Comp)
			{
				TestFalse(FString::Printf(TEXT("Component '%s' does not belong to removed 1P setup"), *Comp->GetName()),
					Comp->GetName().Contains(TEXT("First Person"), ESearchCase::IgnoreCase));
			}
		}
	}

	TestEqual(TEXT("Active gameplay camera is 3P camera"), Char->GetActiveGameplayCamera(), Cam3P);
	TestTrue(TEXT("3P Camera is active"), Cam3P->IsActive());
	TestTrue(TEXT("3P Boom TargetArmLength is 200 cm"), FMath::IsNearlyEqual(Boom3P->TargetArmLength, 200.0f, 0.1f));
	TestTrue(TEXT("3P Boom SocketOffset is (0, 45, 15)"), Boom3P->SocketOffset.Equals(FVector(0.0f, 45.0f, 15.0f), 0.1f));
	TestFalse(TEXT("3P Mesh is visible to owner"), Mesh3P->bOwnerNoSee);
	TestEqual(TEXT("3P Mesh FirstPersonPrimitiveType is None"), Mesh3P->FirstPersonPrimitiveType, EFirstPersonPrimitiveType::None);

	UStaticMeshComponent* FlashlightMesh3P = nullptr;
	{
		TArray<UStaticMeshComponent*> StaticMeshes;
		Char->GetComponents<UStaticMeshComponent>(StaticMeshes);
		for (UStaticMeshComponent* SM : StaticMeshes)
		{
			if (SM && SM->GetName().Contains(TEXT("Flashlight")))
			{
				FlashlightMesh3P = SM;
				break;
			}
		}
	}
	TestNotNull(TEXT("Flashlight 3P mesh component exists"), FlashlightMesh3P);
	if (FlashlightMesh3P)
	{
		TestFalse(TEXT("Flashlight 3P mesh is visible to owner"), FlashlightMesh3P->bOwnerNoSee);
		TestEqual(TEXT("Flashlight 3P mesh FirstPersonPrimitiveType is None"), FlashlightMesh3P->FirstPersonPrimitiveType, EFirstPersonPrimitiveType::None);
		TestFalse(TEXT("Flashlight 3P mesh starts hidden while unarmed"), FlashlightMesh3P->IsVisible());
		TestTrue(
			TEXT("Flashlight 3P held scale is updated to 0.15"),
			FlashlightMesh3P->GetRelativeScale3D().Equals(FVector(0.15f), 0.001f));
	}

	Mesh3P->bEnableUpdateRateOptimizations = false;

	ULostWithinFlashlightAnimInstance* Anim3P = Cast<ULostWithinFlashlightAnimInstance>(Mesh3P->GetAnimInstance());
	if (!TestNotNull(TEXT("3P AnimInstance is ULostWithinFlashlightAnimInstance"), Anim3P))
	{
		return false;
	}

	USpotLightComponent* SpotLight = Char->FindComponentByClass<USpotLightComponent>();
	UPointLightComponent* FillLight = nullptr;
	{
		TArray<UPointLightComponent*> PointLights;
		Char->GetComponents<UPointLightComponent>(PointLights);
		for (UPointLightComponent* PL : PointLights)
		{
			if (PL && !PL->IsA<USpotLightComponent>())
			{
				FillLight = PL;
				break;
			}
		}
	}
	TestNotNull(TEXT("Flashlight SpotLight exists"), SpotLight);
	TestNotNull(TEXT("Flashlight FillLight exists"), FillLight);

	if (SpotLight && FillLight)
	{
		TestTrue(TEXT("SpotLight dimmed to 25 cd"), FMath::IsNearlyEqual(SpotLight->Intensity, 25.0f, 0.1f));
		TestTrue(TEXT("FillLight dimmed to 0.5 cd"), FMath::IsNearlyEqual(FillLight->Intensity, 0.5f, 0.05f));
		TestTrue(TEXT("SpotLight inner cone narrowed to 8 deg"), FMath::IsNearlyEqual(SpotLight->InnerConeAngle, 8.0f, 0.1f));
		TestTrue(TEXT("SpotLight outer cone narrowed to 18 deg"), FMath::IsNearlyEqual(SpotLight->OuterConeAngle, 18.0f, 0.1f));
		TestTrue(TEXT("SpotLight attenuation radius narrowed to 1500 cm"), FMath::IsNearlyEqual(SpotLight->AttenuationRadius, 1500.0f, 1.0f));
	}

	auto StepFrames = [&](float PitchDeg, float YawDeg, float Dt, int32 Count)
	{
		AI->SetControlRotation(FRotator(PitchDeg, YawDeg, 0.0f));
		Char->SetActorRotation(FRotator(0.0f, YawDeg, 0.0f));
		for (int32 Step = 0; Step < Count; ++Step)
		{
			++GFrameCounter;
			Char->Tick(Dt);
			Mesh3P->TickAnimation(Dt, false);
			Mesh3P->RefreshBoneTransforms();
		}
	};

	auto SamplePose = [&](
		float PitchDeg,
		float YawDeg,
		FQuat& Out3PHeadActor,
		FQuat& Out3PSpine05Actor)
	{
		StepFrames(PitchDeg, YawDeg, 0.02f, 20);
		Out3PHeadActor = Mesh3P->GetSocketTransform(TEXT("head"), RTS_Actor).GetRotation();
		Out3PSpine05Actor = Mesh3P->GetSocketTransform(TEXT("spine_05"), RTS_Actor).GetRotation();
	};

	// Settle initial idle frame
	StepFrames(0.0f, 0.0f, 0.033f, 8);

	// 1. Verify crouch smoothly lowers ThirdPersonCameraBoom Z without 1P dependencies
	{
		UCharacterMovementComponent* MoveComp = Char->GetCharacterMovement();
		const float SavedGravityScale = MoveComp ? MoveComp->GravityScale : 1.0f;
		if (MoveComp)
		{
			MoveComp->GravityScale = 0.0f;
			MoveComp->Velocity = FVector::ZeroVector;
			MoveComp->SetMovementMode(MOVE_Walking);
		}

		const float StandingBoomZ = Boom3P->GetRelativeLocation().Z;
		Char->Crouch();
		if (MoveComp)
		{
			MoveComp->TickComponent(0.02f, LEVELTICK_All, nullptr);
		}
		StepFrames(0.0f, 0.0f, 0.02f, 35);
		const float CrouchedBoomZ = Boom3P->GetRelativeLocation().Z;
		TestTrue(TEXT("Character entered crouch state"), Char->bIsCrouched);
		TestTrue(TEXT("Crouch lowers ThirdPersonCameraBoom Z smoothly"),
			FMath::IsNearlyEqual(CrouchedBoomZ, StandingBoomZ + Char->GetCrouchCameraZOffset(), 2.0f));

		Char->UnCrouch();
		if (MoveComp)
		{
			MoveComp->TickComponent(0.02f, LEVELTICK_All, nullptr);
		}
		StepFrames(0.0f, 0.0f, 0.02f, 35);
		const float RecoveredBoomZ = Boom3P->GetRelativeLocation().Z;
		TestFalse(TEXT("Character exited crouch state"), Char->bIsCrouched);
		TestTrue(TEXT("Uncrouch restores ThirdPersonCameraBoom Z"),
			FMath::IsNearlyEqual(RecoveredBoomZ, StandingBoomZ, 2.0f));

		if (MoveComp)
		{
			MoveComp->GravityScale = SavedGravityScale;
			MoveComp->Velocity = FVector::ZeroVector;
			MoveComp->SetMovementMode(MOVE_Walking);
		}
	}

	// 2. Test Unarmed 3P pitch response (-60, 0, +60)
	{
		FQuat H3_Dn, S3_Dn;
		FQuat H3_Mid, S3_Mid;
		FQuat H3_Up, S3_Up;

		SamplePose(-60.0f, 0.0f, H3_Dn, S3_Dn);
		SamplePose(  0.0f, 0.0f, H3_Mid, S3_Mid);
		SamplePose(+60.0f, 0.0f, H3_Up, S3_Up);

		const float Head3P_Dn = ComputePitchDeltaDeg(H3_Dn, H3_Mid);
		const float Head3P_Up = ComputePitchDeltaDeg(H3_Up, H3_Mid);
		const float Spine3P_Dn = ComputePitchDeltaDeg(S3_Dn, S3_Mid);
		const float Spine3P_Up = ComputePitchDeltaDeg(S3_Up, S3_Mid);

		AddInfo(FString::Printf(
			TEXT("[UNARMED 3P] Head=(%.1f, %.1f) Spine05=(%.1f, %.1f)"),
			Head3P_Dn, Head3P_Up, Spine3P_Dn, Spine3P_Up));

		TestTrue(TEXT("Unarmed 3P head pitches down/up with POV (-60..+60)"), Head3P_Dn < -40.0f && Head3P_Up > 40.0f);
		TestTrue(TEXT("Unarmed 3P spine_05 pitches down/up with POV (-60..+60)"), Spine3P_Dn < -25.0f && Spine3P_Up > 25.0f);
	}

	// 3. Grant flashlight, toggle light on, and verify held 3P pitch & world Yaw
	UInventoryComponent* Inv = Char->GetInventoryComponent();
	if (TestNotNull(TEXT("InventoryComponent exists"), Inv))
	{
		FInventoryItem ItemData;
		ItemData.ItemID = APickupItemActor::FlashlightItemID;
		ItemData.Quantity = 1;
		ItemData.MaxStackSize = 1;
		int32 Remaining = 0;
		Inv->AddItem(ItemData, Remaining);
		Char->PlayPickupAnimation();
		if (UFunction* ToggleFn = Char->FindFunction(TEXT("DoToggleFlashlight")))
		{
			Char->ProcessEvent(ToggleFn, nullptr);
		}
	}

	TestTrue(TEXT("3P AnimInstance reports holding flashlight"), Anim3P->IsHoldingFlashlight());
	if (FlashlightMesh3P)
	{
		TestTrue(TEXT("Flashlight 3P mesh visible when held in inventory"), FlashlightMesh3P->IsVisible());
	}

	if (SpotLight && FillLight)
	{
		TestTrue(TEXT("SpotLight visible when toggled on"), SpotLight->IsVisible());
		TestTrue(TEXT("SpotLight scales to 75 cd (3x) at 100% battery"), FMath::IsNearlyEqual(SpotLight->Intensity, 75.0f, 0.2f));
		TestTrue(TEXT("FillLight scales to 1.5 cd (3x) at 100% battery"), FMath::IsNearlyEqual(FillLight->Intensity, 1.5f, 0.05f));
		TestTrue(TEXT("SpotLight inner cone scales to 24 deg (3x) at 100% battery"), FMath::IsNearlyEqual(SpotLight->InnerConeAngle, 24.0f, 0.1f));
		TestTrue(TEXT("SpotLight outer cone scales to 54 deg (3x) at 100% battery"), FMath::IsNearlyEqual(SpotLight->OuterConeAngle, 54.0f, 0.1f));
	}

	// Wait past pickup animation blend-out and settle HoldBlendWeight -> 1.0
	StepFrames(0.0f, 0.0f, 0.05f, 40);

	{
		FQuat H3_Dn, S3_Dn;
		FQuat H3_Mid, S3_Mid;
		FQuat H3_Up, S3_Up;

		SamplePose(-60.0f, 0.0f, H3_Dn, S3_Dn);
		SamplePose(  0.0f, 0.0f, H3_Mid, S3_Mid);
		SamplePose(+60.0f, 0.0f, H3_Up, S3_Up);

		const float Head3P_Dn = ComputePitchDeltaDeg(H3_Dn, H3_Mid);
		const float Head3P_Up = ComputePitchDeltaDeg(H3_Up, H3_Mid);
		const float Spine3P_Dn = ComputePitchDeltaDeg(S3_Dn, S3_Mid);
		const float Spine3P_Up = ComputePitchDeltaDeg(S3_Up, S3_Mid);

		AddInfo(FString::Printf(
			TEXT("[FLASHLIGHT 3P] Head=(%.1f, %.1f) Spine05=(%.1f, %.1f)"),
			Head3P_Dn, Head3P_Up, Spine3P_Dn, Spine3P_Up));

		TestTrue(TEXT("Flashlight 3P head pitches down/up with POV (-60..+60)"), Head3P_Dn < -40.0f && Head3P_Up > 40.0f);
		TestTrue(TEXT("Flashlight 3P spine_05 pitches down/up with POV (-60..+60)"), Spine3P_Dn < -25.0f && Spine3P_Up > 25.0f);

		// Verify controller Yaw turns world head facing together with body
		const FVector WorldHeadFwd0 = Mesh3P->GetSocketTransform(TEXT("head"), RTS_World).GetRotation().GetAxisY();
		StepFrames(0.0f, 75.0f, 0.02f, 5);
		const FVector WorldHeadFwd75 = Mesh3P->GetSocketTransform(TEXT("head"), RTS_World).GetRotation().GetAxisY();
		const float YawTurnDeg = FMath::RadiansToDegrees(FMath::Acos(FMath::Clamp(FVector::DotProduct(WorldHeadFwd0.GetSafeNormal2D(), WorldHeadFwd75.GetSafeNormal2D()), -1.0, 1.0)));
		AddInfo(FString::Printf(TEXT("[YAW] World head horizontal rotation delta = %.1f deg for 75.0 deg POV yaw"), YawTurnDeg));
		TestTrue(TEXT("World head turns ~75 deg horizontally with controller Yaw"), FMath::IsNearlyEqual(YawTurnDeg, 75.0f, 5.0f));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
