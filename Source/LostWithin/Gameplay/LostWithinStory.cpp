#include "Gameplay/LostWithinStory.h"

namespace LostWithinStory
{
	const FName Orientation(TEXT("Subject09.Orientation"));
	const FName Warning(TEXT("Subject09.Warning"));
	const FName Survival(TEXT("Subject09.Survival"));
	const FName Report(TEXT("Subject09.Report"));
	const FName Journal(TEXT("Subject09.Journal"));
	const FName FinalEntry(TEXT("Subject09.Final"));

	FName NoteForDigit(int32 DigitIndex)
	{
		return DigitIndex == 0 ? Warning : DigitIndex == 1 ? Report : DigitIndex == 2 ? FinalEntry : NAME_None;
	}

	FNote FormatNote(FName ID, const TArray<int32>& Digits)
	{
		FNote Note;
		FString Body;
		FString Reaction;
		if (ID == Orientation)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "OrientationTitle", "SUBJECT 09 - ORIENTATION NOTICE");
			Body = TEXT("Welcome to the subterranean navigation assessment, Subject 09. Temporary memory loss is a normal side effect of your sedative. Take the flashlight on this table and proceed into the labyrinth. One locked exit in the southeastern grid is secured by a 3-digit combination. Search the facility's toolboxes and scattered notes to recover the code. Some toolboxes require a matching key. Good luck.\n\n- Dr. Victor Vance, Lead Supervisor");
			Reaction = TEXT("A 'navigation assessment'? Why would they wipe my memory and lock me in this maze? I need the flashlight, the notes, and the code for that exit.");
		}
		else if (ID == Warning)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "WarningTitle", "SCATTERED NOTE #1 - SCRATCHED WARNING");
			Body = TEXT("Don't believe that orientation notice! This isn't a test - it's a slaughterhouse. Whoever built this place locked the southeastern exit with a 3-digit code so none of us could run out right away. I pried the first digit off a maintenance log before running: [ {digit} ]. Keep searching the toolboxes and scattered notes!");
			Reaction = TEXT("So that doctor lied to me... {digit} is the first digit for the exit lock. I have to remember it and keep moving.");
			Note.DigitIndex = 0;
		}
		else if (ID == Survival)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "SurvivalTitle", "SCATTERED NOTE #2 - BLOOD-STAINED SURVIVAL LOG");
			Body = TEXT("Four creatures stalk these halls, and each hunts differently. The BoneDog follows sound but can't pinpoint it - in the dark it misses by more - and dropped objects can distract it; a lit flashlight draws it. The Doll freezes only while you watch it in light, from your flashlight or a torch; in the dark it keeps coming even under your eyes. The Ghoul recoils from the flashlight and will not step into a torch's light. The Lunger is blind and deaf, but senses sprinting, landings, and movement close to its body - stand completely still when it is near. Corners alone won't fool every sense. There are no weapons or hiding cabinets. The strange cat can protect you while its chaos sanctuary is active.");
			Reaction = TEXT("Four of them, each with a different weakness. Listen, keep the Doll in my light, stay near the torches when the Ghoul comes... and stay still near the blind one.");
		}
		else if (ID == Report)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "ReportTitle", "SCATTERED NOTE #3 - TORN LAB REPORT");
			Body = TEXT("I found out what those four creatures are. They used to be Dr. Vance's research assistants and earlier test subjects before he injected them with the Happy Virus. It erased their humanity, twisted their bodies, and turned them into hyper-aggressive predators - Vance's personal underlings. The second exit code digit is [ {digit} ].");
			Reaction = TEXT("They used to be human... Vance mutated his own staff with the 'Happy Virus' and let them loose. The second digit is {digit}.");
			Note.DigitIndex = 1;
		}
		else if (ID == Journal)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "JournalTitle", "SCATTERED NOTE #4 - STOLEN JOURNAL PAGE (DR. V. VANCE)");
			Body = TEXT("Entry 27: Eight subjects have failed my trial; my four underlings tore them apart. Tonight I placed Subject 09 into the labyrinth with only a flashlight. Creating monsters is only half my goal - to prove the Happy Virus can forge a superior human mind, I must pit my creations against an unenhanced, terrified subject. If pure fear forces Subject 09 to outsmart four predators and decode the exit, I will have proof that terror drives human evolution.");
			Reaction = TEXT("He's using me as bait to test his 'evolution' theory... Dr. Vance is completely insane. I'm not dying in here for his experiment.");
		}
		else if (ID == FinalEntry)
		{
			Note.Title = NSLOCTEXT("LostWithinStory", "FinalTitle", "SCATTERED NOTE #5 - FINAL SURVIVOR ENTRY");
			Body = TEXT("I made it to the southeastern exit grid and uncovered the final digit for the locked door: [ {digit} ] - but two of Vance's underlings cornered me before I could enter it! Whoever finds this note: put all three digits together in order, enter them at the southeastern exit, and RUN!");
			Reaction = TEXT("That's the third number - {digit}! I need all three digits in order to open the southeastern exit.");
			Note.DigitIndex = 2;
		}
		if (Note.DigitIndex != INDEX_NONE)
		{
			const FString Digit = Digits.IsValidIndex(Note.DigitIndex) ? FString::FromInt(Digits[Note.DigitIndex]) : TEXT("?");
			Body.ReplaceInline(TEXT("{digit}"), *Digit);
			Reaction.ReplaceInline(TEXT("{digit}"), *Digit);
		}
		Note.Body = FText::FromString(Body);
		Note.Reaction = FText::FromString(Reaction);
		return Note;
	}

	void ApplyToItem(FInventoryItem& Item, FName ID, const TArray<int32>& Digits)
	{
		const FNote Note = FormatNote(ID, Digits);
		if (Note.Title.IsEmpty())
		{
			return;
		}
		Item.StoryNoteID = ID;
		Item.ClueDigitIndex = Note.DigitIndex;
		Item.ClueDigit = Digits.IsValidIndex(Note.DigitIndex) ? Digits[Note.DigitIndex] : INDEX_NONE;
		Item.DisplayName = Note.Title;
		// Keep the original generated riddle available to the player and legacy tooling.
		Item.Description = Note.DigitIndex != INDEX_NONE && !Item.Description.IsEmpty()
			? FText::FromString(Note.Body.ToString() + TEXT("\n\nMAINTENANCE CLUE\n") + Item.Description.ToString())
			: Note.Body;
	}
}
