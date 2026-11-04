// Copyright Epic Games, Inc. All Rights Reserved.
/*===========================================================================
	Generated code exported from UnrealHeaderTool.
	DO NOT modify this manually! Edit the corresponding .h files instead!
===========================================================================*/

#include "UObject/GeneratedCppIncludes.h"
#include "LostWithinCameraManager.h"

PRAGMA_DISABLE_DEPRECATION_WARNINGS

void EmptyLinkFunctionForGeneratedCodeLostWithinCameraManager() {}

// ********** Begin Cross Module References ********************************************************
ENGINE_API UClass* Z_Construct_UClass_APlayerCameraManager();
LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinCameraManager();
LOSTWITHIN_API UClass* Z_Construct_UClass_ALostWithinCameraManager_NoRegister();
UPackage* Z_Construct_UPackage__Script_LostWithin();
// ********** End Cross Module References **********************************************************

// ********** Begin Class ALostWithinCameraManager *************************************************
void ALostWithinCameraManager::StaticRegisterNativesALostWithinCameraManager()
{
}
FClassRegistrationInfo Z_Registration_Info_UClass_ALostWithinCameraManager;
UClass* ALostWithinCameraManager::GetPrivateStaticClass()
{
	using TClass = ALostWithinCameraManager;
	if (!Z_Registration_Info_UClass_ALostWithinCameraManager.InnerSingleton)
	{
		GetPrivateStaticClassBody(
			StaticPackage(),
			TEXT("LostWithinCameraManager"),
			Z_Registration_Info_UClass_ALostWithinCameraManager.InnerSingleton,
			StaticRegisterNativesALostWithinCameraManager,
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
	return Z_Registration_Info_UClass_ALostWithinCameraManager.InnerSingleton;
}
UClass* Z_Construct_UClass_ALostWithinCameraManager_NoRegister()
{
	return ALostWithinCameraManager::GetPrivateStaticClass();
}
struct Z_Construct_UClass_ALostWithinCameraManager_Statics
{
#if WITH_METADATA
	static constexpr UECodeGen_Private::FMetaDataPairParam Class_MetaDataParams[] = {
#if !UE_BUILD_SHIPPING
		{ "Comment", "/**\n *  Basic First Person camera manager.\n *  Limits min/max look pitch.\n */" },
#endif
		{ "IncludePath", "LostWithinCameraManager.h" },
		{ "ModuleRelativePath", "LostWithinCameraManager.h" },
#if !UE_BUILD_SHIPPING
		{ "ToolTip", "Basic First Person camera manager.\nLimits min/max look pitch." },
#endif
	};
#endif // WITH_METADATA
	static UObject* (*const DependentSingletons[])();
	static constexpr FCppClassTypeInfoStatic StaticCppClassTypeInfo = {
		TCppClassTypeTraits<ALostWithinCameraManager>::IsAbstract,
	};
	static const UECodeGen_Private::FClassParams ClassParams;
};
UObject* (*const Z_Construct_UClass_ALostWithinCameraManager_Statics::DependentSingletons[])() = {
	(UObject* (*)())Z_Construct_UClass_APlayerCameraManager,
	(UObject* (*)())Z_Construct_UPackage__Script_LostWithin,
};
static_assert(UE_ARRAY_COUNT(Z_Construct_UClass_ALostWithinCameraManager_Statics::DependentSingletons) < 16);
const UECodeGen_Private::FClassParams Z_Construct_UClass_ALostWithinCameraManager_Statics::ClassParams = {
	&ALostWithinCameraManager::StaticClass,
	"Engine",
	&StaticCppClassTypeInfo,
	DependentSingletons,
	nullptr,
	nullptr,
	nullptr,
	UE_ARRAY_COUNT(DependentSingletons),
	0,
	0,
	0,
	0x008003ACu,
	METADATA_PARAMS(UE_ARRAY_COUNT(Z_Construct_UClass_ALostWithinCameraManager_Statics::Class_MetaDataParams), Z_Construct_UClass_ALostWithinCameraManager_Statics::Class_MetaDataParams)
};
UClass* Z_Construct_UClass_ALostWithinCameraManager()
{
	if (!Z_Registration_Info_UClass_ALostWithinCameraManager.OuterSingleton)
	{
		UECodeGen_Private::ConstructUClass(Z_Registration_Info_UClass_ALostWithinCameraManager.OuterSingleton, Z_Construct_UClass_ALostWithinCameraManager_Statics::ClassParams);
	}
	return Z_Registration_Info_UClass_ALostWithinCameraManager.OuterSingleton;
}
DEFINE_VTABLE_PTR_HELPER_CTOR(ALostWithinCameraManager);
ALostWithinCameraManager::~ALostWithinCameraManager() {}
// ********** End Class ALostWithinCameraManager ***************************************************

// ********** Begin Registration *******************************************************************
struct Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCameraManager_h__Script_LostWithin_Statics
{
	static constexpr FClassRegisterCompiledInInfo ClassInfo[] = {
		{ Z_Construct_UClass_ALostWithinCameraManager, ALostWithinCameraManager::StaticClass, TEXT("ALostWithinCameraManager"), &Z_Registration_Info_UClass_ALostWithinCameraManager, CONSTRUCT_RELOAD_VERSION_INFO(FClassReloadVersionInfo, sizeof(ALostWithinCameraManager), 1273469604U) },
	};
};
static FRegisterCompiledInInfo Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCameraManager_h__Script_LostWithin_4062378611(TEXT("/Script/LostWithin"),
	Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCameraManager_h__Script_LostWithin_Statics::ClassInfo, UE_ARRAY_COUNT(Z_CompiledInDeferFile_FID__User_Files_Nextcloud_Documents_Unreal_Projects_LostWithin_Source_LostWithin_LostWithinCameraManager_h__Script_LostWithin_Statics::ClassInfo),
	nullptr, 0,
	nullptr, 0);
// ********** End Registration *********************************************************************

PRAGMA_ENABLE_DEPRECATION_WARNINGS
