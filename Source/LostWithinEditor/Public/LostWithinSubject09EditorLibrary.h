#pragma once

#include "Kismet/BlueprintFunctionLibrary.h"
#include "LostWithinSubject09EditorLibrary.generated.h"

/** Explicit, idempotent asset setup; never runs automatically while the user edits or plays. */
UCLASS()
class LOSTWITHINEDITOR_API ULostWithinSubject09EditorLibrary : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="LostWithin|Subject09")
	static bool SetupSubject09();
};
