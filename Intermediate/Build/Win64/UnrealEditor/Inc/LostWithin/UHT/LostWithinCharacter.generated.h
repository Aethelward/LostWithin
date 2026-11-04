// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

// IWYU pragma: private, include "LostWithinCharacter.h"

#ifdef LOSTWITHIN_LostWithinCharacter_generated_h
#error "LostWithinCharacter.generated.h already included, missing '#pragma once' in LostWithinCharacter.h"
#endif
#define LOSTWITHIN_LostWithinCharacter_generated_h

#include "UObject/ObjectMacros.h"
#include "UObject/ScriptMacros.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

// ********** Begin Class ALostWithinCharacter *****************************************************
#define FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_RPC_WRAPPERS_NO_PURE_DECLS \
	DECLARE_FUNCTION(execDoJumpEnd); \
	DECLARE_FUNCTION(execDoJumpStart); \
	DECLARE_FUNCTION(execDoMove); \
	DECLARE_FUNCTION(execDoAim);


LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinCharacter_NoRegister();

#define FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_INCLASS_NO_PURE_DECLS \
private: \
	static void StaticRegisterNativesALostWithinCharacter(); \
	friend struct Z_Construct_UClass_ALostWithinCharacter_Statics; \
	static UClass* GetPrivateStaticClass(); \
	friend LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinCharacter_NoRegister(); \
public: \
	DECLARE_CLASS2(ALostWithinCharacter, ACharacter, COMPILED_IN_FLAGS(CLASS_Abstract | CLASS_Config), CASTCLASS_None, TEXT("/Script/LostWithin"), Z_Construct_UClass_ALostWithinCharacter_NoRegister) \
	DECLARE_SERIALIZER(ALostWithinCharacter)


#define FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_ENHANCED_CONSTRUCTORS \
	/** Deleted move- and copy-constructors, should never be used */ \
	ALostWithinCharacter(ALostWithinCharacter&&) = delete; \
	ALostWithinCharacter(const ALostWithinCharacter&) = delete; \
	DECLARE_VTABLE_PTR_HELPER_CTOR(NO_API, ALostWithinCharacter); \
	DEFINE_VTABLE_PTR_HELPER_CTOR_CALLER(ALostWithinCharacter); \
	DEFINE_ABSTRACT_DEFAULT_CONSTRUCTOR_CALL(ALostWithinCharacter) \
	NO_API virtual ~ALostWithinCharacter();


#define FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_21_PROLOG
#define FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_GENERATED_BODY \
PRAGMA_DISABLE_DEPRECATION_WARNINGS \
public: \
	FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_RPC_WRAPPERS_NO_PURE_DECLS \
	FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_INCLASS_NO_PURE_DECLS \
	FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h_24_ENHANCED_CONSTRUCTORS \
private: \
PRAGMA_ENABLE_DEPRECATION_WARNINGS


class ALostWithinCharacter;

// ********** End Class ALostWithinCharacter *******************************************************

#undef CURRENT_FILE_ID
#define CURRENT_FILE_ID FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCharacter_h

PRAGMA_ENABLE_DEPRECATION_WARNINGS
