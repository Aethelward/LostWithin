#include "Player/BodyContactComponent.h"

#include "AI/MonsterAIComponent.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/SphereComponent.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "LostWithin.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsEngine/SkeletalBodySetup.h"
#include "Player/PlayerDeathComponent.h"

UBodyContactComponent::UBodyContactComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
	SetAutoActivate(true);
}

USkeletalMeshComponent* UBodyContactComponent::ResolveMesh() const
{
	const ACharacter* Character = Cast<ACharacter>(GetOwner());
	if (!Character) { return nullptr; }
	if (!MeshComponentName.IsNone())
	{
		TArray<USkeletalMeshComponent*> Meshes;
		Character->GetComponents(Meshes);
		for (USkeletalMeshComponent* Mesh : Meshes)
		{
			if (Mesh->GetFName() == MeshComponentName && Mesh->GetSkeletalMeshAsset()) { return Mesh; }
		}
	}
	return Character->GetMesh();
}

void UBodyContactComponent::BeginPlay()
{
	Super::BeginPlay();
	if (ACharacter* Character = Cast<ACharacter>(GetOwner()))
	{
		if (bPlayerBody)
		{
			Character->GetCapsuleComponent()->SetCollisionObjectType(PlayerMovementChannel);
			Character->GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		}
		else
		{
			Character->GetCapsuleComponent()->SetCollisionResponseToChannel(PlayerMovementChannel, ECR_Overlap);
		}
	}
	RebuildContactBodies();
}

void UBodyContactComponent::RegisterShape(UShapeComponent* Shape, FName Bone, const FTransform& LocalTransform)
{
	if (!Shape || !SourceMesh) { return; }
	Shape->SetMobility(EComponentMobility::Movable);
	Shape->SetupAttachment(SourceMesh, Bone);
	Shape->SetRelativeTransform(LocalTransform);
	Shape->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
	Shape->SetCollisionObjectType(BodyContactChannel);
	Shape->SetCollisionResponseToAllChannels(ECR_Ignore);
	Shape->SetCollisionResponseToChannel(BodyContactChannel, ECR_Overlap);
	Shape->SetGenerateOverlapEvents(false);
	Shape->SetCanEverAffectNavigation(false);
	Shape->SetHiddenInGame(true);
	GetOwner()->AddInstanceComponent(Shape);
	Shape->RegisterComponent();
	Shapes.Add(Shape);
	PreviousTransforms.Add(Shape->GetComponentTransform());
	CurrentTransforms.Add(Shape->GetComponentTransform());
}

UShapeComponent* UBodyContactComponent::AddContactSphere(FName Bone, const FVector& LocalCenter, float Radius)
{
	if (!SourceMesh) { SourceMesh = ResolveMesh(); }
	if (!SourceMesh || Radius <= 0.0f || (!Bone.IsNone() && SourceMesh->GetBoneIndex(Bone) == INDEX_NONE)) { return nullptr; }
	USphereComponent* Sphere = NewObject<USphereComponent>(GetOwner());
	Sphere->InitSphereRadius(Radius);
	RegisterShape(Sphere, Bone, FTransform(LocalCenter));
	return Sphere;
}

void UBodyContactComponent::RebuildContactBodies()
{
	for (UShapeComponent* Shape : Shapes)
	{
		if (Shape) { GetOwner()->RemoveInstanceComponent(Shape); Shape->DestroyComponent(); }
	}
	Shapes.Empty(); PreviousTransforms.Empty(); CurrentTransforms.Empty();
	bHasPoseHistory = false;
	if (SourceMesh) { RemoveTickPrerequisiteComponent(SourceMesh); }
	SourceMesh = ResolveMesh();
	if (!SourceMesh) { return; }
	if (!bPlayerBody)
	{
		SourceMesh->SetCollisionResponseToChannel(PlayerMovementChannel, SourceMesh->GetCollisionResponseToChannel(ECC_Pawn));
	}
	SourceMesh->VisibilityBasedAnimTickOption = EVisibilityBasedAnimTickOption::AlwaysTickPoseAndRefreshBones;
	AddTickPrerequisiteComponent(SourceMesh);
	if (UPlayerDeathComponent* Death = GetOwner()->FindComponentByClass<UPlayerDeathComponent>())
	{
		Death->AddTickPrerequisiteComponent(this);
	}
	UPhysicsAsset* Profile = ContactPhysicsAsset.IsNull() ? nullptr : ContactPhysicsAsset.LoadSynchronous();
	if (!Profile)
	{
		if (!ContactPhysicsAsset.IsNull())
		{
			UE_LOG(LogLostWithin, Warning, TEXT("%s: body-contact profile %s is not available; no proximity kill fallback."), *GetNameSafe(GetOwner()), *ContactPhysicsAsset.ToString());
		}
		return;
	}
	for (const USkeletalBodySetup* Body : Profile->SkeletalBodySetups)
	{
		if (!Body || SourceMesh->GetBoneIndex(Body->BoneName) == INDEX_NONE) { continue; }
		for (const FKSphereElem& Element : Body->AggGeom.SphereElems)
		{
			USphereComponent* Shape = NewObject<USphereComponent>(GetOwner());
			Shape->InitSphereRadius(Element.Radius);
			RegisterShape(Shape, Body->BoneName, Element.GetTransform());
		}
		for (const FKSphylElem& Element : Body->AggGeom.SphylElems)
		{
			UCapsuleComponent* Shape = NewObject<UCapsuleComponent>(GetOwner());
			Shape->InitCapsuleSize(Element.Radius, Element.Length * 0.5f + Element.Radius);
			RegisterShape(Shape, Body->BoneName, Element.GetTransform());
		}
		for (const FKBoxElem& Element : Body->AggGeom.BoxElems)
		{
			UBoxComponent* Shape = NewObject<UBoxComponent>(GetOwner());
			Shape->InitBoxExtent(FVector(Element.X, Element.Y, Element.Z) * 0.5f);
			RegisterShape(Shape, Body->BoneName, Element.GetTransform());
		}
	}
	if (Shapes.IsEmpty())
	{
		UE_LOG(LogLostWithin, Error, TEXT("%s: contact profile has no supported bodies on the visible skeleton."), *GetNameSafe(GetOwner()));
	}
}

void UBodyContactComponent::RefreshContactTransforms()
{
	// A teleport moves the whole owner in one frame. Sweeping along that jump would count anything on its path as
	// contact, so a jump beyond MaxSweepDistance starts a fresh pose history (current-pose overlap still applies).
	const FVector OwnerLocation = GetOwner() ? GetOwner()->GetActorLocation() : FVector::ZeroVector;
	const bool bTeleported = bHasPoseHistory
		&& FVector::DistSquared(OwnerLocation, LastOwnerLocation) > FMath::Square(static_cast<double>(MaxSweepDistance));
	LastOwnerLocation = OwnerLocation;
	for (int32 Index = 0; Index < Shapes.Num(); ++Index)
	{
		UShapeComponent* Shape = Shapes[Index];
		if (!Shape) { continue; }
		PreviousTransforms[Index] = CurrentTransforms[Index];
		Shape->UpdateComponentToWorld();
		Shape->UpdateBounds();
		CurrentTransforms[Index] = Shape->GetComponentTransform();
		if (!bHasPoseHistory || bTeleported) { PreviousTransforms[Index] = CurrentTransforms[Index]; }
	}
	bHasPoseHistory = true;
}

FBox UBodyContactComponent::GetSweptBounds() const
{
	FBox Bounds(ForceInit);
	for (int32 Index = 0; Index < Shapes.Num(); ++Index)
	{
		if (const UShapeComponent* Shape = Shapes[Index]; Shape && Shape->IsCollisionEnabled())
		{
			Bounds += Shape->CalcBounds(CurrentTransforms[Index]).GetBox();
			Bounds += Shape->CalcBounds(PreviousTransforms[Index]).GetBox();
		}
	}
	return Bounds;
}

bool UBodyContactComponent::IsUnobstructed(const FVector& From, const FVector& To, const AActor* OtherOwner) const
{
	FHitResult Hit;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BodyContactWallCheck), false, GetOwner());
	Params.AddIgnoredActor(OtherOwner);
	return GetWorld() && !GetWorld()->LineTraceSingleByChannel(Hit, From, To, ECC_Visibility, Params);
}

bool UBodyContactComponent::QueryContactAgainst(const UBodyContactComponent& Other) const
{
	FCollisionObjectQueryParams Objects;
	Objects.AddObjectTypesToQuery(BodyContactChannel);
	FCollisionQueryParams Params(SCENE_QUERY_STAT(VisibleBodyContact), false, GetOwner());
	for (int32 Index = 0; Index < Shapes.Num(); ++Index)
	{
		const UShapeComponent* Shape = Shapes[Index];
		if (!Shape || !Shape->IsCollisionEnabled()) { continue; }
		const FTransform& Now = CurrentTransforms[Index];
		TArray<FOverlapResult> Overlaps;
		GetWorld()->OverlapMultiByObjectType(Overlaps, Now.GetLocation(), Now.GetRotation(), Objects, Shape->GetCollisionShape(), Params);
		for (const FOverlapResult& Hit : Overlaps)
		{
			if (Hit.GetActor() == Other.GetOwner() && Other.Shapes.Contains(Hit.GetComponent())
				&& IsUnobstructed(Now.GetLocation(), Hit.GetComponent()->GetComponentLocation(), Other.GetOwner())) { return true; }
		}
		const FTransform& Before = PreviousTransforms[Index];
		const float Angle = FMath::RadiansToDegrees(Before.GetRotation().AngularDistance(Now.GetRotation()));
		const int32 Steps = FMath::Clamp(FMath::CeilToInt(Angle / 12.0f), 1, 16);
		for (int32 Step = 0; Step < Steps; ++Step)
		{
			const float Alpha0 = static_cast<float>(Step) / Steps;
			const float Alpha1 = static_cast<float>(Step + 1) / Steps;
			const FVector Start = FMath::Lerp(Before.GetLocation(), Now.GetLocation(), Alpha0);
			const FVector End = FMath::Lerp(Before.GetLocation(), Now.GetLocation(), Alpha1);
			const FQuat Rotation = FQuat::Slerp(Before.GetRotation(), Now.GetRotation(), Alpha1);
			TArray<FHitResult> Hits;
			GetWorld()->SweepMultiByObjectType(Hits, Start, End, Rotation, Objects, Shape->GetCollisionShape(), Params);
			for (const FHitResult& Hit : Hits)
			{
				if (Hit.GetActor() == Other.GetOwner() && Other.Shapes.Contains(Hit.GetComponent())
					&& IsUnobstructed(FMath::Lerp(Start, End, Hit.Time), Hit.GetComponent()->GetComponentLocation(), Other.GetOwner())) { return true; }
			}
		}
	}
	return false;
}

bool UBodyContactComponent::HasBodyContactWith(const UBodyContactComponent& Other) const
{
	if (&Other == this || !GetWorld() || GetWorld() != Other.GetWorld() || bPlayerBody == Other.bPlayerBody
		|| !IsActive() || !Other.IsActive() || Shapes.IsEmpty() || Other.Shapes.IsEmpty()
		|| !GetSweptBounds().Intersect(Other.GetSweptBounds())) { return false; }
	return QueryContactAgainst(Other) || Other.QueryContactAgainst(*this);
}

void UBodyContactComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* TickFunction)
{
	Super::TickComponent(DeltaTime, TickType, TickFunction);
	RefreshContactTransforms();
}

void UBodyContactComponent::EndPlay(const EEndPlayReason::Type Reason)
{
	for (UShapeComponent* Shape : Shapes) { if (Shape) { Shape->DestroyComponent(); } }
	Shapes.Empty();
	Super::EndPlay(Reason);
}

void UBodyContactComponent::SetupMonstersForWorld(UWorld* World)
{
	if (!World) { return; }
	APawn* Player = UGameplayStatics::GetPlayerPawn(World, 0);
	UPlayerDeathComponent* Death = Player ? Player->FindComponentByClass<UPlayerDeathComponent>() : nullptr;
	for (TActorIterator<ACharacter> It(World); It; ++It)
	{
		if (!It->FindComponentByClass<UMonsterAIComponent>()) { continue; }
		UBodyContactComponent* Contact = It->FindComponentByClass<UBodyContactComponent>();
		if (!Contact)
		{
			const FString ClassName = It->GetClass()->GetName();
			const FString ProfileName = ClassName.Contains(TEXT("BoneDog")) ? TEXT("BoneDog")
				: ClassName.Contains(TEXT("Doll")) ? TEXT("Doll") : ClassName.Contains(TEXT("Ghoul")) ? TEXT("Ghoul") : TEXT("Lunger");
			Contact = NewObject<UBodyContactComponent>(*It, TEXT("VisibleBodyContact"));
			Contact->ContactPhysicsAsset = TSoftObjectPtr<UPhysicsAsset>(FSoftObjectPath(
				FString::Printf(TEXT("/Game/Gameplay/Subject09/Contact/PA_BodyContact_%s.PA_BodyContact_%s"), *ProfileName, *ProfileName)));
			It->AddInstanceComponent(Contact);
			Contact->RegisterComponent();
		}
		if (Death) { Death->AddTickPrerequisiteComponent(Contact); }
	}
}
