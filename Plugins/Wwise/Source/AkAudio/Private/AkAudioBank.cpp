// Copyright (c) 2006-2019 Audiokinetic Inc. / All Rights Reserved
#include "AkAudioBank.h"

#include "AkAudioDevice.h"
#include "AkCustomVersion.h"
#include "AkUnrealHelper.h"
#include "AssetRegistry/Public/AssetRegistryModule.h"
#include "IntegrationBehavior/AkIntegrationBehavior.h"

#if WITH_EDITOR
#include "AssetTools/Public/AssetToolsModule.h"
#include "UnrealEd/Public/ObjectTools.h"
#endif

void UAkAudioBank::Load()
{
	AkIntegrationBehavior::Get()->AkAudioBank_Load(this);

#if WITH_EDITOR
	if (!ID.IsValid())
	{
		ID = FGuid::NewGuid();
	}
#endif
}

void UAkAudioBank::Unload()
{
	AkIntegrationBehavior::Get()->AkAudioBank_Unload(this);
}

bool UAkAudioBank::SwitchLanguage(const FString& newAudioCulture, const SwitchLanguageCompletedFunction& Function)
{
	auto localizedAssetSoftPointer = LocalizedPlatformAssetDataMap.Find(newAudioCulture);
	if (localizedAssetSoftPointer)
	{
		auto& assetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
		auto assetData = assetRegistryModule.Get().GetAssetByObjectPath(*localizedAssetSoftPointer->ToSoftObjectPath().ToString(), true);

		if (!assetData.IsValid())
		{
			if (auto* audioDevice = FAkAudioDevice::Get())
			{
				FString pathWithDefaultLanguage = localizedAssetSoftPointer->ToSoftObjectPath().ToString().Replace(*newAudioCulture, *audioDevice->GetDefaultLanguage());
				assetData = assetRegistryModule.Get().GetAssetByObjectPath(FName(*pathWithDefaultLanguage), true);
			}
		}

		if (assetData.IsValid() && !assetData.PackagePath.IsNone())
		{
			unloadLocalizedData();

			loadLocalizedData(newAudioCulture, Function);
			return true;
		}
	}

	return false;
}

bool UAkAudioBank::LegacyLoad()
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AkBankID BankID;
			AKRESULT eResult = AudioDevice->LoadBank(this, BankID);
			return (eResult == AK_Success) ? true : false;
		}
	}

	return false;
}

bool UAkAudioBank::LegacyLoad(FWaitEndBankAction* LoadBankLatentAction)
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AKRESULT eResult = AudioDevice->LoadBank(this, LoadBankLatentAction);
			return (eResult == AK_Success) ? true : false;
		}
	}

	return false;
}

bool UAkAudioBank::LegacyLoadAsync(void* in_pfnBankCallback, void* in_pCookie)
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AkBankID BankID;
			AKRESULT eResult = AudioDevice->LoadBank(this, (AkBankCallbackFunc)in_pfnBankCallback, in_pCookie, BankID);
			return (eResult == AK_Success) ? true : false;
		}
	}

	return false;
}

bool UAkAudioBank::LegacyLoadAsync(const FOnAkBankCallback& BankLoadedCallback)
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AkBankID BankID;
			AKRESULT eResult = AudioDevice->LoadBankAsync(this, BankLoadedCallback, BankID);
			return (eResult == AK_Success) ? true : false;
		}
	}

	return false;
}

void UAkAudioBank::LegacyUnload()
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AudioDevice->UnloadBank(this);
		}
	}
}

void UAkAudioBank::LegacyUnload(FWaitEndBankAction* UnloadBankLatentAction)
{
	if (!IsRunningCommandlet())
	{
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			AudioDevice->UnloadBank(this, UnloadBankLatentAction);
		}
	}
}

void UAkAudioBank::LegacyUnloadAsync(void* in_pfnBankCallback, void* in_pCookie)
{
	if (!IsRunningCommandlet())
	{
		AKRESULT eResult = AK_Fail;
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			eResult = AudioDevice->UnloadBank(this, (AkBankCallbackFunc)in_pfnBankCallback, in_pCookie);
			if (eResult != AK_Success)
			{
				UE_LOG(LogAkAudio, Warning, TEXT("Failed to unload SoundBank %s"), *GetName());
			}
		}
	}
}

void UAkAudioBank::LegacyUnloadAsync(const FOnAkBankCallback& BankUnloadedCallback)
{
	if (!IsRunningCommandlet())
	{
		AKRESULT eResult = AK_Fail;
		FAkAudioDevice* AudioDevice = FAkAudioDevice::Get();
		if (AudioDevice)
		{
			eResult = AudioDevice->UnloadBankAsync(this, BankUnloadedCallback);
			if (eResult != AK_Success)
			{
				UE_LOG(LogAkAudio, Warning, TEXT("Failed to unload SoundBank %s"), *GetName());
			}
		}
	}
}

UAkAssetData* UAkAudioBank::createAssetData(UObject* parent) const
{
	return NewObject<UAkAssetDataWithMedia>(parent);
}

UAkAssetData* UAkAudioBank::getAssetData() const
{
#if WITH_EDITORONLY_DATA
	if (IsLocalized() && CurrentLocalizedPlatformAssetData)
	{
		const FString runningPlatformName(FPlatformProperties::IniPlatformName());

		if (auto assetData = CurrentLocalizedPlatformAssetData->AssetDataPerPlatform.Find(runningPlatformName))
		{
			return *assetData;
		}
	}

	return Super::getAssetData();
#else
	if (IsLocalized() && CurrentLocalizedPlatformAssetData)
	{
		return CurrentLocalizedPlatformAssetData->CurrentAssetData;
	}

	return Super::getAssetData();
#endif
}

void UAkAudioBank::loadLocalizedData(const FString& audioCulture, const SwitchLanguageCompletedFunction& Function)
{
	if (auto* audioDevice = FAkAudioDevice::Get())
	{
		TSoftObjectPtr<UAkAssetPlatformData>* eventDataSoftObjectPtr = LocalizedPlatformAssetDataMap.Find(audioCulture);
		if (eventDataSoftObjectPtr)
		{
			auto& assetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");

			FSoftObjectPath localizedDataPath = eventDataSoftObjectPtr->ToSoftObjectPath();

			if (!assetRegistryModule.Get().GetAssetByObjectPath(*localizedDataPath.ToString(), true).IsValid())
			{
				FString pathWithDefaultLanguage = eventDataSoftObjectPtr->ToSoftObjectPath().ToString().Replace(*audioCulture, *audioDevice->GetDefaultLanguage());
				auto assetData = assetRegistryModule.Get().GetAssetByObjectPath(FName(*pathWithDefaultLanguage), true);
				if (assetRegistryModule.Get().GetAssetByObjectPath(FName(*pathWithDefaultLanguage), true).IsValid())
				{
					localizedDataPath = FSoftObjectPath(pathWithDefaultLanguage);
				}
			}

			localizedStreamHandle = audioDevice->GetStreamableManager().RequestAsyncLoad(localizedDataPath, [this, Function] {
				onLocalizedDataLoaded();

				if (Function)
				{
					Function(localizedStreamHandle.IsValid());
				}
			});
		}
	}
}

void UAkAudioBank::onLocalizedDataLoaded()
{
	if (localizedStreamHandle.IsValid())
	{
		CurrentLocalizedPlatformAssetData = Cast<UAkAssetPlatformData>(localizedStreamHandle->GetLoadedAsset());

		Super::Load();
	}
}

void UAkAudioBank::unloadLocalizedData()
{
	if (localizedStreamHandle.IsValid())
	{
		Super::Unload();

		CurrentLocalizedPlatformAssetData = nullptr;

		localizedStreamHandle->ReleaseHandle();
		localizedStreamHandle.Reset();
	}
}

void UAkAudioBank::superLoad()
{
	Super::Load();
}

void UAkAudioBank::superUnload()
{
	Super::Unload();
}

#if WITH_EDITOR
UAkAssetData* UAkAudioBank::FindOrAddAssetData(const FString& platform, const FString& language)
{
	check(IsInGameThread());

	UAkAssetPlatformData* eventData = nullptr;
	UObject* parent = this;

	if (language.Len() > 0)
	{
		auto* languageIt = LocalizedPlatformAssetDataMap.Find(language);
		if (languageIt)
		{
			eventData = languageIt->LoadSynchronous();

			if (eventData)
			{
				parent = eventData;
			}
		}
		else
		{
			auto& assetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>("AssetRegistry");
			auto& assetToolModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>("AssetTools");

			FSoftObjectPath objectPath(this);

			auto basePackagePath = AkUnrealHelper::GetBaseAssetPackagePath();

			auto packagePath = objectPath.GetLongPackageName();
			packagePath.RemoveFromStart(basePackagePath);
			packagePath.RemoveFromEnd(objectPath.GetAssetName());
			packagePath = FPaths::Combine(AkUnrealHelper::GetLocalizedAssetPackagePath(), language, packagePath);

			auto assetName = GetName();

			auto foundAssetData = assetRegistryModule.Get().GetAssetByObjectPath(*FPaths::Combine(packagePath, FString::Printf(TEXT("%s.%s"), *assetName, *assetName)));
			if (foundAssetData.IsValid())
			{
				eventData = Cast<UAkAssetPlatformData>(foundAssetData.GetAsset());
			}
			else
			{
				eventData = Cast<UAkAssetPlatformData>(assetToolModule.Get().CreateAsset(assetName, packagePath, UAkAssetPlatformData::StaticClass(), nullptr));
			}

			if (eventData)
			{
				parent = eventData;

				LocalizedPlatformAssetDataMap.Add(language, eventData);
			}
		}

		if (eventData)
		{
			return internalFindOrAddAssetData(eventData, platform, parent);
		}
	}

	return Super::FindOrAddAssetData(platform, language);
}

void UAkAudioBank::Reset()
{
	LocalizedPlatformAssetDataMap.Empty();

	Super::Reset();
}
#endif
