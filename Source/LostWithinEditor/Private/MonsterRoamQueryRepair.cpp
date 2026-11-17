// Copyright Epic Games, Inc. All Rights Reserved.

#include "MonsterMovementSetup.h"

#include "AI/MonsterAIComponent.h"
#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphPin.h"
#include "EdGraphSchema_K2.h"
#include "Engine/Blueprint.h"
#include "GameFramework/Actor.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_ExecutionSequence.h"
#include "K2Node_IfThenElse.h"
#include "LostWithin.h"

namespace
{
	bool IsGoalMove(const UEdGraphNode* Node)
	{
		const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
		const FName Name = Call ? Call->FunctionReference.GetMemberName() : NAME_None;
		return Name == TEXT("SimpleMoveToLocation") || Name == TEXT("MoveToLocation")
			|| Name == TEXT("K2_SetActorLocation") || Name == TEXT("K2_SetActorLocationAndRotation");
	}

	void GatherConsumers(UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited, TSet<UEdGraphNode*>& Consumers)
	{
		if (Visited.Contains(Node))
		{
			return;
		}
		Visited.Add(Node);
		if (Node->FindPin(UEdGraphSchema_K2::PN_Execute))
		{
			Consumers.Add(Node);
			return;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory != UEdGraphSchema_K2::PC_Exec)
			{
				for (UEdGraphPin* Link : Pin->LinkedTo)
				{
					GatherConsumers(Link->GetOwningNode(), Visited, Consumers);
				}
			}
		}
	}

	void GatherGoalMoves(UEdGraphNode* Node, TSet<UEdGraphNode*>& Visited, TSet<UEdGraphNode*>& Moves)
	{
		if (Visited.Contains(Node))
		{
			return;
		}
		Visited.Add(Node);
		if (IsGoalMove(Node))
		{
			Moves.Add(Node);
			return;
		}
		for (UEdGraphPin* Pin : Node->Pins)
		{
			if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
			{
				for (UEdGraphPin* Link : Pin->LinkedTo)
				{
					GatherGoalMoves(Link->GetOwningNode(), Visited, Moves);
				}
			}
		}
	}

	bool RepairQuery(UK2Node_CallFunction* Original)
	{
		UEdGraphPin* Location = Original->FindPin(TEXT("RandomLocation"));
		if (!Location || Location->LinkedTo.IsEmpty())
		{
			return true;
		}
		if (Original->GetReturnValuePin() && !Original->GetReturnValuePin()->LinkedTo.IsEmpty())
		{
			// An authored success check already exists. Preserve its control flow.
			return true;
		}
		TSet<UEdGraphNode*> Visited, Consumers, Moves;
		for (UEdGraphPin* Link : Location->LinkedTo)
		{
			GatherConsumers(Link->GetOwningNode(), Visited, Consumers);
		}
		if (Consumers.Num() != 1)
		{
			UE_LOG(LogLostWithin, Error, TEXT("Roaming query %s needs one execution consumer; inspect its destination wiring before setup."), *Original->GetPathName());
			return false;
		}
		UEdGraphNode* Consumer = *Consumers.CreateConstIterator();
		UEdGraphPin* Input = Consumer->FindPin(UEdGraphSchema_K2::PN_Execute);
		if (Input->LinkedTo.IsEmpty())
		{
			return true;
		}
		Visited.Reset();
		GatherGoalMoves(Consumer, Visited, Moves);
		UEdGraphPin* MoveThen = Moves.Num() == 1 ? (*Moves.CreateConstIterator())->FindPin(UEdGraphSchema_K2::PN_Then) : nullptr;
		if (!MoveThen || MoveThen->LinkedTo.IsEmpty())
		{
			UE_LOG(LogLostWithin, Error, TEXT("Cannot locate the delayed continuation of roaming query %s. Blueprint was not saved."), *Original->GetPathName());
			return false;
		}
		UEdGraph* Graph = Original->GetGraph();
		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
		Graph->Modify();
		Original->Modify();
		Consumer->Modify();
		const TArray<UEdGraphPin*> Previous = Input->LinkedTo;
		const TArray<UEdGraphPin*> Continuation = MoveThen->LinkedTo;

		FGraphNodeCreator<UK2Node_CallFunction> QueryCreator(*Graph);
		UK2Node_CallFunction* Query = QueryCreator.CreateNode();
		Query->SetFromFunction(UMonsterAIComponent::StaticClass()->FindFunctionByName(GET_FUNCTION_NAME_CHECKED(UMonsterAIComponent, FindReachableRoamLocation)));
		Query->NodePosX = Consumer->NodePosX - 640;
		Query->NodePosY = Consumer->NodePosY;
		QueryCreator.Finalize();
		FGraphNodeCreator<UK2Node_IfThenElse> BranchCreator(*Graph);
		UK2Node_IfThenElse* Branch = BranchCreator.CreateNode();
		Branch->NodePosX = Consumer->NodePosX - 280;
		Branch->NodePosY = Consumer->NodePosY;
		Branch->NodeComment = TEXT("A failed destination skips movement and keeps the original roaming retry scheduled.");
		Branch->bCommentBubbleVisible = true;
		BranchCreator.Finalize();

		struct FPinTransfer
		{
			UEdGraphPin* OldPin;
			UEdGraphPin* NewPin;
			TArray<UEdGraphPin*> Links;
		};
		TArray<FPinTransfer> Transfers;
		bool bConnected = true;
		for (UEdGraphPin* Pin : Original->Pins)
		{
			if (Pin->PinName == UEdGraphSchema_K2::PN_Self)
			{
				continue;
			}
			UEdGraphPin* Replacement = Query->FindPin(Pin->PinName, Pin->Direction);
			if (!Replacement)
			{
				bConnected = false;
				break;
			}
			Replacement->DefaultValue = Pin->DefaultValue;
			Replacement->DefaultObject = Pin->DefaultObject;
			Replacement->DefaultTextValue = Pin->DefaultTextValue;
			Transfers.Add({Pin, Replacement, Pin->LinkedTo});
		}
		if (bConnected)
		{
			Input->BreakAllPinLinks();
			bConnected &= Schema->TryCreateConnection(Query->GetThenPin(), Branch->GetExecPin());
			bConnected &= Schema->TryCreateConnection(Query->GetReturnValuePin(), Branch->GetConditionPin());
			bConnected &= Schema->TryCreateConnection(Branch->GetThenPin(), Input);
			for (UEdGraphPin* Link : Previous)
			{
				bConnected &= Schema->TryCreateConnection(Link, Query->GetExecPin());
			}
			for (UEdGraphPin* Link : Continuation)
			{
				bConnected &= Schema->TryCreateConnection(Branch->GetElsePin(), Link);
			}
			for (const FPinTransfer& Transfer : Transfers)
			{
				Transfer.OldPin->BreakAllPinLinks();
				for (UEdGraphPin* Link : Transfer.Links)
				{
					bConnected &= Schema->TryCreateConnection(Transfer.NewPin, Link);
				}
			}
		}
		if (!bConnected)
		{
			for (UEdGraphNode* Added : TArray<UEdGraphNode*>{Query, Branch})
			{
				Added->BreakAllNodeLinks();
				Graph->RemoveNode(Added);
			}
			for (const FPinTransfer& Transfer : Transfers)
			{
				for (UEdGraphPin* Link : Transfer.Links)
				{
					Transfer.OldPin->MakeLinkTo(Link);
				}
			}
			for (UEdGraphPin* Link : Previous)
			{
				Link->MakeLinkTo(Input);
			}
			UE_LOG(LogLostWithin, Error, TEXT("Could not reconnect roaming query %s; Blueprint was not saved."), *Original->GetPathName());
			return false;
		}
		Original->BreakAllNodeLinks();
		Graph->RemoveNode(Original);
		return true;
	}

	bool EnsureRoamStarts(UBlueprint* Blueprint, const TArray<UEdGraph*>& Graphs)
	{
		UK2Node_Event* BeginPlay = nullptr;
		for (UEdGraph* Graph : Graphs)
		{
			for (UEdGraphNode* Node : Graph->Nodes)
			{
				UK2Node_Event* Event = Cast<UK2Node_Event>(Node);
				if (Event && Event->EventReference.GetMemberName() == TEXT("ReceiveBeginPlay"))
				{
					BeginPlay = Event;
				}
			}
		}
		if (BeginPlay)
		{
			TSet<UEdGraphNode*> Visited;
			TArray<UEdGraphNode*> Pending{BeginPlay};
			while (!Pending.IsEmpty())
			{
				UEdGraphNode* Node = Pending.Pop(EAllowShrinking::No);
				if (Visited.Contains(Node))
				{
					continue;
				}
				Visited.Add(Node);
				const UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
				if (Call && Call->FunctionReference.GetMemberName() == TEXT("RoamStep"))
				{
					return true;
				}
				for (UEdGraphPin* Pin : Node->Pins)
				{
					if (Pin->Direction == EGPD_Output && Pin->PinType.PinCategory == UEdGraphSchema_K2::PC_Exec)
					{
						for (UEdGraphPin* Link : Pin->LinkedTo)
						{
							Pending.Add(Link->GetOwningNode());
							// Also recognise an existing timer that starts this event.
							const UEdGraphPin* FunctionPin = Link->GetOwningNode()->FindPin(TEXT("FunctionName"));
							if (FunctionPin && FunctionPin->DefaultValue == TEXT("RoamStep"))
							{
								return true;
							}
						}
					}
				}
			}
		}
		UFunction* RoamFunction = Blueprint->GeneratedClass->FindFunctionByName(TEXT("RoamStep"));
		if (!RoamFunction || Blueprint->UbergraphPages.IsEmpty())
		{
			UE_LOG(LogLostWithin, Error, TEXT("BP_Monster needs its existing RoamStep event and event graph; setup will not replace the authored AI."));
			return false;
		}
		UEdGraph* Graph = BeginPlay ? BeginPlay->GetGraph() : Blueprint->UbergraphPages[0].Get();
		Graph->Modify();
		if (!BeginPlay)
		{
			FGraphNodeCreator<UK2Node_Event> Creator(*Graph);
			BeginPlay = Creator.CreateNode();
			BeginPlay->EventReference.SetExternalMember(TEXT("ReceiveBeginPlay"), AActor::StaticClass());
			BeginPlay->bOverrideFunction = true;
			Creator.Finalize();
		}
		FGraphNodeCreator<UK2Node_CallFunction> RoamCreator(*Graph);
		UK2Node_CallFunction* Roam = RoamCreator.CreateNode();
		Roam->FunctionReference.SetSelfMember(TEXT("RoamStep"));
		Roam->NodePosX = BeginPlay->NodePosX + 500;
		Roam->NodePosY = BeginPlay->NodePosY + 180;
		RoamCreator.Finalize();
		const UEdGraphSchema_K2* Schema = CastChecked<UEdGraphSchema_K2>(Graph->GetSchema());
		UEdGraphPin* Then = BeginPlay->FindPinChecked(UEdGraphSchema_K2::PN_Then);
		if (Then->LinkedTo.IsEmpty())
		{
			if (Schema->TryCreateConnection(Then, Roam->GetExecPin()))
			{
				return true;
			}
			Graph->RemoveNode(Roam);
			UE_LOG(LogLostWithin, Error, TEXT("Could not connect BP_Monster BeginPlay to RoamStep."));
			return false;
		}
		const TArray<UEdGraphPin*> Existing = Then->LinkedTo;
		FGraphNodeCreator<UK2Node_ExecutionSequence> SequenceCreator(*Graph);
		UK2Node_ExecutionSequence* Sequence = SequenceCreator.CreateNode();
		Sequence->NodePosX = BeginPlay->NodePosX + 220;
		Sequence->NodePosY = BeginPlay->NodePosY;
		SequenceCreator.Finalize();
		Then->BreakAllPinLinks();
		bool bConnected = Schema->TryCreateConnection(Then, Sequence->GetExecPin())
			&& Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(1), Roam->GetExecPin());
		for (UEdGraphPin* Link : Existing)
		{
			bConnected &= Schema->TryCreateConnection(Sequence->GetThenPinGivenIndex(0), Link);
		}
		if (!bConnected)
		{
			Sequence->BreakAllNodeLinks();
			Roam->BreakAllNodeLinks();
			Graph->RemoveNode(Sequence);
			Graph->RemoveNode(Roam);
			for (UEdGraphPin* Link : Existing)
			{
				Then->MakeLinkTo(Link);
			}
		}
		return bConnected;
	}
}

bool LostWithinMonsterSetup::RepairRoamQueries(UBlueprint* Blueprint)
{
	TArray<UEdGraph*> Graphs;
	Blueprint->GetAllGraphs(Graphs);
	for (UEdGraph* Graph : Graphs)
	{
		TArray<UK2Node_CallFunction*> Queries;
		for (UEdGraphNode* Node : Graph->Nodes)
		{
			UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node);
			if (Call && Call->FunctionReference.GetMemberName() == TEXT("K2_GetRandomReachablePointInRadius"))
			{
				Queries.Add(Call);
			}
		}
		for (UK2Node_CallFunction* Query : Queries)
		{
			if (!RepairQuery(Query))
			{
				return false;
			}
		}
	}
	return EnsureRoamStarts(Blueprint, Graphs);
}
