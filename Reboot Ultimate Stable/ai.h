#pragma once

#include "reboot.h"
#include "Actor.h"
#include "SoftObjectPath.h"
#include "KismetStringLibrary.h"
#include "GameplayStatics.h"
#include "FortPlayerPawn.h"
#include "FortAthenaMutator.h"
#include "FortGameModeAthena.h"
#include "FortGameStateAthena.h"
#include "FortBotNameSettings.h"
#include "KismetTextLibrary.h"
#include "FortAthenaAIBotCustomizationData.h"
#include "FortAthenaAIBotSpawnerData.h"
#include "FortItemDefinition.h"
#include "FortAthenaAIBotController.h"

using UNavigationSystemV1 = UObject;
using UNavigationSystemConfig = UObject;
using AAthenaNavSystemConfigOverride = UObject;
using UAthenaNavSystem = UObject;
using UAthenaNavSystemConfig = UObject;

enum class EFNavigationSystemRunMode : uint8_t
{
    InvalidMode = 0,
    GameMode = 1,
    EditorMode = 2,
    SimulationMode = 3,
    PIEMode = 4,
    FNavigationSystemRunMode_MAX = 5
};

enum class ENavSystemOverridePolicy : uint8_t
{
    Override = 0,
    Append = 1,
    Skip = 2,
    ENavSystemOverridePolicy_MAX = 3
};

extern inline void (*NavSystemCleanUpOriginal)(UNavigationSystemV1*, uint8) = nullptr;

static bool SetNavigationSystem(AAthenaNavSystemConfigOverride* NavSystemOverride)
{
    UAthenaNavSystem*& NavSystem = (UAthenaNavSystem*&)GetWorld()->Get("NavigationSystem");

    LOG_INFO(LogDev, "NavSystem: {}", NavSystem->IsValidLowLevel() ? NavSystem->GetFullName() : "BadRead");

    if (NavSystem)
    {
        return false;
        NavSystemCleanUpOriginal(NavSystem, 0);
        GetWorld()->Get("NavigationSystem") = nullptr;
    }

    auto WorldSettings = GetWorld()->GetWorldSettings();

    LOG_INFO(LogDev, "WorldSettings: {}", WorldSettings->IsValidLowLevel() ? WorldSettings->GetFullName() : "BadRead");

    if (!WorldSettings)
        return false;

    static auto OverridePolicyOffset = NavSystemOverride->GetOffset("OverridePolicy", false);
    
    if (OverridePolicyOffset != -1)
        NavSystemOverride->Get<ENavSystemOverridePolicy>(OverridePolicyOffset) = ENavSystemOverridePolicy::Append;
    
    static auto NavSystemOverride_NavigationSystemConfigOffset = NavSystemOverride->GetOffset("NavigationSystemConfig");

    WorldSettings->Get("NavigationSystemConfigOverride") = NavSystemOverride->Get(NavSystemOverride_NavigationSystemConfigOffset);

    LOG_INFO(LogDev, "WorldSettings_NavigationSystemConfig: {}", __int64(WorldSettings->Get("NavigationSystemConfig")));

    if (WorldSettings->Get("NavigationSystemConfig"))
        WorldSettings->Get("NavigationSystemConfig")->Get<bool>("bIsOverriden") = true;

    if (!NavSystemOverride->Get("NavigationSystemConfig"))
    {
        LOG_ERROR(LogAI, "No NavigationSystemConfig!");
        return false;
    }

    auto& ClassPath = NavSystemOverride->Get("NavigationSystemConfig")->Get<FSoftObjectPath>("NavigationSystemClass");

    auto NewNavSystemClass = FindObject<UClass>(ClassPath.AssetPathName.ToString());

    if (!NewNavSystemClass)
    {
        LOG_ERROR(LogAI, "No NavigationSystemClass!");
        return false;
    }

    LOG_INFO(LogAI, "Setup navigation system.");

    // if (Fortnite_Version >= 10.40) // idk when they added the fifth arg or does it even matter????
    {
        static void (*AddNavigationSystemToWorldOriginal)(UWorld * WorldOwner, EFNavigationSystemRunMode RunMode, UNavigationSystemConfig * NavigationSystemConfig, char bInitializeForWorld, char bOverridePreviousNavSys) =
            decltype(AddNavigationSystemToWorldOriginal)(Addresses::AddNavigationSystemToWorld);

        AddNavigationSystemToWorldOriginal(GetWorld(), EFNavigationSystemRunMode::GameMode, NavSystemOverride->Get("NavigationSystemConfig"), true, false);
    }

    return true;
}

static inline AFortAthenaMutator_Bots* SpawnBotMutator() //sets up all the classes for phoebe
{
   auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
   auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

   static auto BGAClass = FindObject<UClass>(L"/Script/Engine.BlueprintGeneratedClass");
   auto PhoebeMutatorClass = LoadObject<UClass>(L"/Game/Athena/AI/Phoebe/BP_Phoebe_Mutator.BP_Phoebe_Mutator_C", BGAClass);

   if (!PhoebeMutatorClass)
   {
       return nullptr;
   }

   auto BotMutator = GetWorld()->SpawnActor<AFortAthenaMutator_Bots>(PhoebeMutatorClass);

   if (!BotMutator)
   {
       LOG_WARN(LogAI, "Failed to spawn Bot Mutator!");
       return nullptr;
   }

   static auto CachedGameModeOffset = BotMutator->GetOffset("CachedGameMode");
   BotMutator->Get(CachedGameModeOffset) = GameMode;

   static auto CachedGameStateOffset = BotMutator->GetOffset("CachedGameState", false);

   if (CachedGameStateOffset != -1)
       BotMutator->Get(CachedGameStateOffset) = GameState;

   return BotMutator;
}

static void SetupServerBotManager()
{
    auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
    auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

    static auto FortServerBotManagerClass = FindObject<UClass>(L"/Script/FortniteGame.FortServerBotManagerAthena"); // Is there a BP for this? // GameMode->ServerBotManagerClass

    if (!FortServerBotManagerClass)
        return;

    UFortServerBotManagerAthena*& ServerBotManager = GameMode->GetServerBotManager();

    if (!ServerBotManager)
        ServerBotManager = (UFortServerBotManagerAthena*)UGameplayStatics::SpawnObject(FortServerBotManagerClass, GetTransientPackage());

    if (ServerBotManager)
    {
        static auto CachedGameModeOffset = ServerBotManager->GetOffset("CachedGameMode");
        ServerBotManager->Get(CachedGameModeOffset) = GameMode;

        static auto CachedGameStateOffset = ServerBotManager->GetOffset("CachedGameState", false);

        if (CachedGameStateOffset != -1)
            ServerBotManager->Get(CachedGameStateOffset) = GameState;

        static auto CachedBotMutatorOffset = ServerBotManager->GetOffset("CachedBotMutator");
        auto BotMutator = FindFirstMutator(FindObject<UClass>(L"/Script/FortniteGame.FortAthenaMutator_Bots"));

        if (!BotMutator)
        {
            LOG_WARN(LogAI, "Failed to find Bot Mutator! Spawning it..");
            BotMutator = SpawnBotMutator();
        }

        if (!BotMutator)
        {
            LOG_ERROR(LogAI, "Failed to spawn or find Bot Mutator!");
        }

        ServerBotManager->Get(CachedBotMutatorOffset) = BotMutator;
    }
}

static void SetupAIGoalManager()
{
    // Playlist->AISettings->bAllowAIGoalManager

    // There is some virtual function in the gamemode that calls a spawner for this, it gets the class from GameData, not sure why this doesn't work automatically or if we should even spawn this.

    auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());
    static auto AIGoalManagerOffset = GameMode->GetOffset("AIGoalManager");

    static auto AIGoalManagerClass = FindObject<UClass>(L"/Script.FortniteGame.FortAIGoalManager");

    if (!AIGoalManagerClass)
        return;

    LOG_INFO(LogDev, "AIGoalManager Before: {}", __int64(GameMode->Get(AIGoalManagerOffset)));

    if (!GameMode->Get(AIGoalManagerOffset))
        GameMode->Get(AIGoalManagerOffset) = GetWorld()->SpawnActor<AActor>(AIGoalManagerClass);

    if (GameMode->Get(AIGoalManagerOffset))
    {
        LOG_INFO(LogAI, "Successfully spawned AIGoalManager!");
    }
}

static void SetupAIDirector()
{
    // Playlist->AISettings->bAllowAIDirector

    auto GameState = Cast<AFortGameStateAthena>(GetWorld()->GetGameState());
    auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

    static auto AIDirectorClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaAIDirector"); // Probably wrong class

    if (!AIDirectorClass)
        return;

    static auto AIDirectorOffset = GameMode->GetOffset("AIDirector");
    
    LOG_INFO(LogDev, "AIDirector Before: {}", __int64(GameMode->Get(AIDirectorOffset)));

    if (!GameMode->Get(AIDirectorOffset))
        GameMode->Get(AIDirectorOffset) = GetWorld()->SpawnActor<AActor>(AIDirectorClass);

    if (GameMode->Get(AIDirectorOffset))
    {
        LOG_INFO(LogAI, "Successfully spawned AIDirector!");

        // we have to set so much more from data tables..
        
        static auto OurEncounterClass = FindObject<UClass>(L"/Script/FortniteGame.FortAIEncounterInfo"); // ???
        static auto BaseEncounterClassOffset = GameMode->Get(AIDirectorOffset)->GetOffset("BaseEncounterClass");

        GameMode->Get(AIDirectorOffset)->Get(BaseEncounterClassOffset) = OurEncounterClass;

        static auto ActivateFn = FindObject<UFunction>(L"/Script/FortniteGame.FortAIDirector.Activate");

        if (ActivateFn) // ?
            GameMode->Get(AIDirectorOffset)->ProcessEvent(ActivateFn); // ?
    }
}

static void SetupNavConfig(const FName& AgentName)
{
    static auto AthenaNavSystemConfigOverrideClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaNavSystemConfigOverride");
    auto NavSystemOverride = GetWorld()->SpawnActor<AActor>(AthenaNavSystemConfigOverrideClass);

    if (!NavSystemOverride)
        return;

    static auto AthenaNavSystemConfigClass = FindObject<UClass>(L"/Script/FortniteGame.AthenaNavSystemConfig");
    auto AthenaNavConfig = (UAthenaNavSystemConfig*)UGameplayStatics::SpawnObject(AthenaNavSystemConfigClass, NavSystemOverride);
    AthenaNavConfig->Get<bool>("bUseBuildingGridAsNavigableSpace") = false;

    static auto bUsesStreamedInNavLevelOffset = AthenaNavConfig->GetOffset("bUsesStreamedInNavLevel", false);

    if (bUsesStreamedInNavLevelOffset != -1)
        AthenaNavConfig->Get<bool>(bUsesStreamedInNavLevelOffset) = true;

    AthenaNavConfig->Get<bool>("bAllowAutoRebuild") = true;
    AthenaNavConfig->Get<bool>("bCreateOnClient") = true; // BITFIELD
    AthenaNavConfig->Get<bool>("bAutoSpawnMissingNavData") = true; // BITFIELD
    AthenaNavConfig->Get<bool>("bSpawnNavDataInNavBoundsLevel") = true; // BITFIELD

    static auto bUseNavigationInvokersOffset = AthenaNavConfig->GetOffset("bUseNavigationInvokers", false);

    if (bUseNavigationInvokersOffset != -1)
        AthenaNavConfig->Get<bool>(bUseNavigationInvokersOffset) = false;

    static auto DefaultAgentNameOffset = AthenaNavConfig->GetOffset("DefaultAgentName", false);

    if (DefaultAgentNameOffset != -1)
        AthenaNavConfig->Get<FName>(DefaultAgentNameOffset) = AgentName;

    // NavSystemOverride->Get<ENavSystemOverridePolicy>("OverridePolicy") = ENavSystemOverridePolicy::Append;
    NavSystemOverride->Get("NavigationSystemConfig") = AthenaNavConfig;

    SetNavigationSystem(NavSystemOverride);
}

static AFortPlayerPawn* SpawnAIFromSpawnerData(AActor* InSpawnLocator, UFortAthenaAIBotSpawnerData* SpawnerData)
{
    auto SpawnParamsComponent = Cast<UFortAthenaAISpawnerDataComponent_SpawnParams>(SpawnerData->GetSpawnParamsComponent().Get()->CreateDefaultObject());

    if (!SpawnParamsComponent)
    {
        LOG_INFO(LogAI, "Invalid SpawnParamsComponent for AI!");
        return nullptr;
    }

    auto PawnClass = SpawnParamsComponent->GetPawnClass().Get();

    LOG_INFO(LogAI, "PawnClass: {}", __int64(PawnClass));

    if (!PawnClass)
    {
        LOG_INFO(LogAI, "Invalid PawnClass for AI!");
        return nullptr;
    }

    LOG_INFO(LogAI, "PawnClass Name: {}", PawnClass->GetFullName());

    auto GameMode = Cast<AFortGameModeAthena>(GetWorld()->GetGameMode());

    auto Pawn = GameMode->GetServerBotManager()->GetCachedBotMutator()->SpawnBot(PawnClass, InSpawnLocator, InSpawnLocator->GetActorLocation(), InSpawnLocator->GetActorRotation(), true);

    if (!Pawn)
    {
        LOG_INFO(LogAI, "Failed to spawn pawn!");
        return nullptr;
    }

    auto Controller = Cast<AFortAthenaAIBotController>(Pawn->GetController());

    if (!Controller)
        Controller = GetWorld()->SpawnActor<AFortAthenaAIBotController>(Pawn->GetAIControllerClass());

    if (Controller->GetPawn() != Pawn)
        Controller->Possess(Pawn);

    auto PlayerState = Cast<AFortPlayerStateAthena>(Controller->GetPlayerState());

    if (!PlayerState)
        PlayerState = GetWorld()->SpawnActor<AFortPlayerStateAthena>(AFortPlayerStateAthena::StaticClass());

    PlayerState->SetIsBot(true);

    auto CosmeticComponent = Cast<UFortAthenaAISpawnerDataComponent_CosmeticLoadout>(SpawnerData->GetCosmeticComponent().Get()->CreateDefaultObject());

    if (!CosmeticComponent)
    {
        LOG_INFO(LogAI, "Invalid CosmeticComponent for AI!");
        return Pawn;
    }

    auto InventoryComponent = Cast<UFortAthenaAISpawnerDataComponent_AIBotInventory>(SpawnerData->GetInventoryComponent().Get()->CreateDefaultObject());

    if (!InventoryComponent)
    {
        LOG_INFO(LogAI, "Invalid InventoryComponent for AI!");
        return Pawn;
    }

    Controller->StartupInventory = InventoryComponent->GetItems();

    auto CharacterToApply = CosmeticComponent->GetCosmeticLoadout()->GetCharacter();
    LOG_INFO(LogAI, "CharacterToApply: {}", CharacterToApply->GetName());
    
    static auto HeroDefinitionOffset = CharacterToApply->GetOffset("HeroDefinition");

    if (HeroDefinitionOffset != 1)
    {
        auto HeroDefinition = CharacterToApply->Get(HeroDefinitionOffset);

        if (HeroDefinition)
            ApplyHID(Pawn, HeroDefinition, true);
        else
            LOG_WARN(LogBots, "Failed to find HeroDefinition!");
    }

    auto GameplayAbilityComponent = Cast<UFortAthenaAISpawnerDataComponent_AIBotGameplayAbilityBase>(SpawnerData->GetGameplayAbilityComponent().Get()->CreateDefaultObject());

    if (!GameplayAbilityComponent)
    {
        LOG_INFO(LogAI, "Invalid GameplayAbilityComponent for AI!");
        return Pawn;
    }

    if (false)
    {
        for (int i = 0; i < GameplayAbilityComponent->GetInitialGameplayEffect().Num(); ++i)
        {
            auto& CurrentGameplayEffect = GameplayAbilityComponent->GetInitialGameplayEffect().at(i);

            if (!CurrentGameplayEffect.GetGameplayEffect())
            {
                continue;
            }

            LOG_INFO(LogDev, "Giving GameplayEffect {}", CurrentGameplayEffect.GetGameplayEffect()->GetFullName());

            FGameplayEffectContextHandle EffectContext{};
            PlayerState->GetAbilitySystemComponent()->ApplyGameplayEffectToSelf(CurrentGameplayEffect.GetGameplayEffect(), CurrentGameplayEffect.GetLevel(), EffectContext);
        }

        for (int i = 0; i < GameplayAbilityComponent->GetInitialGameplayAbilitiesSet().Num(); ++i)
        {
            for (int j = 0; j < GameplayAbilityComponent->GetInitialGameplayAbilitiesSet().at(i)->GetGameplayAbilities()->Num(); ++j)
            {
                UClass* AbilityClass = GameplayAbilityComponent->GetInitialGameplayAbilitiesSet().at(i)->GetGameplayAbilities()->At(i);

                if (!AbilityClass)
                    continue;

                PlayerState->GetAbilitySystemComponent()->GiveAbilityEasy(AbilityClass);
            }
        }
    }

    auto GameplayComponent = Cast<UFortAthenaAISpawnerDataComponent_AIBotGameplay>(SpawnerData->GetGameplayComponent().Get()->CreateDefaultObject());

    if (!GameplayComponent)
    {
        LOG_INFO(LogAI, "Invalid GameplayComponent for AI!");
        return Pawn;
    }

    FString NewBotName = UKismetTextLibrary::Conv_TextToString(GameplayComponent->GetNameSettings()->GetOverrideName());

    PlayerState->SetPlayerName(NewBotName);
    Pawn->SetCanBeDamaged(true);
    Pawn->SetMaxHealth(100);
    Pawn->SetHealth(100);

    return Pawn;
}

static AFortPlayerPawn* SpawnAIFromCustomizationData(const FVector& Location, UFortAthenaAIBotCustomizationData* CustomizationData)
{
    auto PawnClass = CustomizationData->GetPawnClass();

    if (!PawnClass)
    {
        LOG_INFO(LogAI, "Invalid PawnClass for AI!");
        return nullptr;
    }

    auto Pawn = GetWorld()->SpawnActor<AFortPlayerPawn>(PawnClass, Location);

    if (!Pawn)
    {
        LOG_INFO(LogAI, "Failed to spawn pawn!");
        return nullptr;
    }

    auto Controller = Cast<AFortAthenaAIBotController>(Pawn->GetController());

    if (!Controller)
    {
        LOG_INFO(LogAI, "No controller!");
        Pawn->K2_DestroyActor();
        return nullptr;
    }

    auto PlayerState = Controller->GetPlayerState();

    if (!PlayerState)
    {
        LOG_INFO(LogAI, "No PlayerState!");
        Controller->K2_DestroyActor();
        Pawn->K2_DestroyActor();
        return nullptr;
    }

    auto CharacterToApply = CustomizationData->GetCharacterCustomization()->GetCustomizationLoadout()->GetCharacter();

    ApplyCID(Pawn, CharacterToApply, true); // bruhh

    static auto StartupInventoryOffset = CustomizationData->GetOffset("StartupInventory");
    auto StartupInventory = CustomizationData->Get(StartupInventoryOffset);
    static auto StartupInventoryItemsOffset = StartupInventory->GetOffset("Items");

    std::vector<std::pair<UFortItemDefinition*, int>> ItemsToGrant;

    if (Fortnite_Version < 13)
    {
        auto& StartupInventoryItems = StartupInventory->Get<TArray<UFortItemDefinition*>>(StartupInventoryItemsOffset);

        for (int i = 0; i < StartupInventoryItems.Num(); ++i)
        {
            ItemsToGrant.push_back({ StartupInventoryItems.at(i), 1 });
            FItemAndCount ItemAndCount;
            ItemAndCount.GetCount() = 1;
            ItemAndCount.GetItem() = StartupInventoryItems.at(i);
            Controller->StartupInventory.Add(ItemAndCount);
        }
    }
    else
    {
        auto& StartupInventoryItems = StartupInventory->Get<TArray<FItemAndCount>>(StartupInventoryItemsOffset);

        for (int i = 0; i < StartupInventoryItems.Num(); ++i)
        {
            ItemsToGrant.push_back({ StartupInventoryItems.at(i).GetItem(), StartupInventoryItems.at(i).GetCount()});
        }

        Controller->StartupInventory = StartupInventoryItems;
    }

    static auto InventoryOffset = Controller->GetOffset("Inventory");
    auto Inventory = Controller->Get<AFortInventory*>(InventoryOffset);

    if (Inventory)
    {
        for (int i = 0; i < ItemsToGrant.size(); ++i)
        {
            auto pair = Inventory->AddItem(ItemsToGrant.at(i).first, nullptr, ItemsToGrant.at(i).second);

            LOG_INFO(LogDev, "pair.first.size(): {}", pair.first.size());

            if (pair.first.size() > 0)
            {
                if (auto weaponDef = Cast<UFortWeaponItemDefinition>(ItemsToGrant.at(i).first))
                    Pawn->EquipWeaponDefinition(weaponDef, pair.first.at(0)->GetItemEntry()->GetItemGuid());
            }
        }

        Inventory->Update();
    }

    auto BotNameSettings = CustomizationData->GetBotNameSettings();

    FString Name;

    if (BotNameSettings)
    {
        static int CurrentId = 0; // scuffed!
        static auto DisplayNameOffset = FindOffsetStruct("/Script/FortniteGame.FortItemDefinition", "DisplayName");

        switch (BotNameSettings->GetNamingMode())
        {
        case EBotNamingMode::Custom:
            Name = UKismetTextLibrary::Conv_TextToString(BotNameSettings->GetOverrideName());
            break;
        case EBotNamingMode::SkinName:
            Name = CharacterToApply ? UKismetTextLibrary::Conv_TextToString(*(FText*)(__int64(CharacterToApply) + DisplayNameOffset)) : L"InvalidCharacter";
            Name.Set((std::wstring(Name.Data.Data) += std::to_wstring(CurrentId++)).c_str());
            break;
        default:
            Name = L"Unknown";
            break;
        }
    }

    if (Name.Data.Data && Name.Data.Num() > 0)
        PlayerState->SetPlayerName(Name);

    return Pawn;
}

static void SpawnMarauderSquad(AActor* DropPod)
{
    static UFortAthenaAIBotSpawnerData* EliteSpawnerData = Cast<UFortAthenaAIBotSpawnerData>(LoadObject<UClass>(L"/Gibson/AISpawnerData/Elite/AISpawnerData_Gibson_Elite_BP.AISpawnerData_Gibson_Elite_BP_C", BGACLASS)->CreateDefaultObject());
    static UFortAthenaAIBotSpawnerData* GruntSpawnerData = Cast<UFortAthenaAIBotSpawnerData>(LoadObject<UClass>(L"/Gibson/AISpawnerData/Grunt/AISpawnerData_Gibson_Grunt_BP.AISpawnerData_Gibson_Grunt_BP_C", BGACLASS)->CreateDefaultObject());
    static UFortAthenaAIBotSpawnerData* HeavySpawnerData = Cast<UFortAthenaAIBotSpawnerData>(LoadObject<UClass>(L"/Gibson/AISpawnerData/Heavy/AISpawnerData_Gibson_Heavy_BP.AISpawnerData_Gibson_Heavy_BP_C", BGACLASS)->CreateDefaultObject());

    LOG_INFO(LogDev, "EliteSpawnerData: {}", __int64(EliteSpawnerData));
    LOG_INFO(LogDev, "GruntSpawnerData: {}", __int64(GruntSpawnerData));
    LOG_INFO(LogDev, "HeavySpawnerData: {}", __int64(HeavySpawnerData));

    SpawnAIFromSpawnerData(DropPod, EliteSpawnerData);
    SpawnAIFromSpawnerData(DropPod, EliteSpawnerData);
    SpawnAIFromSpawnerData(DropPod, GruntSpawnerData);
    SpawnAIFromSpawnerData(DropPod, HeavySpawnerData);
    SpawnAIFromSpawnerData(DropPod, HeavySpawnerData);
}