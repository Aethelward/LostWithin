// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "LostWithinGameMode.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeLostWithinGameMode() {}

// ********** Begin Cross Module References ********************************************************
ENGINE_API UClass* Z_Construct_UClass_AGameModeBase();
LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinGameMode();
LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinGameMode_NoRegister();
UPackage* Z_Construct_UPackage__Script_LostWithin();
// ********** End Cross Module References **********************************************************

// ********** Begin Class ALostWithinGameMode ******************************************************
void ALostWithinGameMode::StaticRegisterNativesALostWithinGameMode()
{
}
FClassRegistrationInfo Z_Registration_Info_UClass_ALostWithinGameMode;
UClass* ALostWithinGameMode::GetPrivateStaticClass()
{
	using TClass = ALostWithinGameMode;
	if (!Z_Registration_Info_UClass_ALostWithinGameMode.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("LostWithinGameMode"),
			Z_Registration_Info_UClass_ALostWithinGameMode.InnerSingleton,
			StaticRegisterNativesALostWithinGameMode,
			sizeof(TClass),
			alignof(TClass),
			TClass::StaticClassFlags,
			TClass::StaticClassCastFlags(),
			TClass::StaticConfigName(),
			(UClass::ClassConstructorType)InternalConstructor<TClass>,
			(UClass::ClassVTableHelperCtorCallerType)InternalVTableHelperCtorCaller<TClass>,
			UOBJECT_CPPCLASS_STATICFUNCTIONS_FORCLASS(TClass),
			&TClass::Super::StaticClass,
			&TClass::WithinClass::StaticClass
		);
	}
	return Z_Registration_Info_UClass_ALostWithinGameMode.InnerSingleton;
}
UClass* Z_Construct_UClass_ALostWithinGameMode_NoRegister()
{
	return ALostWithinGameMode::GetPrivateStaticClass();
}
struct Z_Construct_UClass_ALostWithinGameMode_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n *  Simple GameMode for a first person game\n */" },
#endif
		{ "HideCategories", "Info Rendering MovementReplication Replication Actor Input Movement Collision Rendering HLOD WorldPartition DataLayers Transformation" },
		{ "IncludePath", "LostWithinGameMode.h" },
		{ "ModuleRelativePath", "LostWithinGameMode.h" },
		{ "ShowCategories", "Input|MouseInput Input|TouchInput" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Simple GameMode for a first person game" },
#endif
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<ALostWithinGameMode>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_ALostWithinGameMode_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_AGameModeBase,
	(UObject* (*)())Z_Construct_UPackage__Script_LostWithin,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_ALostWithinGameMode_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_ALostWithinGameMode_Statics::ClassParams = {
	&ALostWithinGameMode::StaticClass,
	"Game",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x008003ADu,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_ALostWithinGameMode_Statics::Class_MetaDataParams), Z_Construct_UClass_ALostWithinGameMode_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_ALostWithinGameMode()
{
	if (!Z_Registration_Info_UClass_ALostWithinGameMode.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_ALostWithinGameMode.OuterSingleton, Z_Construct_UClass_ALostWithinGameMode_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_ALostWithinGameMode.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR(ALostWithinGameMode);
ALostWithinGameMode::~ALostWithinGameMode() {}
// ********** End Class ALostWithinGameMode ********************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinGameMode_h__Script_LostWithin_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_ALostWithinGameMode, ALostWithinGameMode::StaticClass, TEXT("ALostWithinGameMode"), &Z_Registration_Info_UClass_ALostWithinGameMode, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(ALostWithinGameMode), 3498719856U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinGameMode_h__Script_LostWithin_2880301451(TEXT("/Script/LostWithin"),
	Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinGameMode_h__Script_LostWithin_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinGameMode_h__Script_LostWithin_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
