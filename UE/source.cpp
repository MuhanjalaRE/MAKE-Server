#define LOGGING

#include "pch.h"

using json = nlohmann::json;
using namespace std;
using namespace CG;

static DWORD64 base_address = NULL;
static const DWORD64 uworld_offset = 0x5390D98;
static const DWORD64 processevent_offset = 0xEE57C0;

struct Timer {
    LARGE_INTEGER time_;
    int period_in_ms_ = 1000;

    Timer(int period_in_ms) {
        QueryPerformanceCounter(&time_);
        period_in_ms_ = period_in_ms;
    }

    bool Tick(void) {
        LARGE_INTEGER time;
        QueryPerformanceCounter(&time);
        static LARGE_INTEGER frequency = {0};
        if (frequency.QuadPart == 0) {
            QueryPerformanceFrequency(&frequency);
        }

        double delta_ms = (double)(time.QuadPart - time_.QuadPart) / frequency.QuadPart * 1000.0;
        if (delta_ms > period_in_ms_) {
            time_ = time;
            return true;
        }
        return false;
    }
};

class TimerWithFunctionAndData : public Timer {
   private:
    void* data = NULL;
    void (*function)(void*) = NULL;

   public:
    TimerWithFunctionAndData(int period_in_ms, void (*function)(void*), void* data) : Timer(period_in_ms) {
        this->function = function;
        this->data = data;
    }

    bool Tick(void) {
        bool return_value = Timer::Tick();
        if (return_value)
            function(data);
        return return_value;
    }
};

namespace math {
#define M_PI 3.14159265358979323846
#define M_PI_F ((float)(M_PI))
#define PI M_PI
#define DEG2RAD(x) ((float)(x) * (float)(M_PI_F / 180.f))
#define RAD2DEG(x) ((float)(x) * (float)(180.f / M_PI_F))
#define INV_PI (0.31830988618f)
#define HALF_PI (1.57079632679f)

void SinCos(float* ScalarSin, float* ScalarCos, float Value) {
    // Map Value to y in [-pi,pi], x = 2*pi*quotient + remainder.
    float quotient = (INV_PI * 0.5f) * Value;
    if (Value >= 0.0f) {
        quotient = (float)((int)(quotient + 0.5f));
    } else {
        quotient = (float)((int)(quotient - 0.5f));
    }
    float y = Value - (2.0f * PI) * quotient;

    // Map y to [-pi/2,pi/2] with sin(y) = sin(Value).
    float sign;
    if (y > HALF_PI) {
        y = PI - y;
        sign = -1.0f;
    } else if (y < -HALF_PI) {
        y = -PI - y;
        sign = -1.0f;
    } else {
        sign = +1.0f;
    }

    float y2 = y * y;

    // 11-degree minimax approximation
    *ScalarSin = (((((-2.3889859e-08f * y2 + 2.7525562e-06f) * y2 - 0.00019840874f) * y2 + 0.0083333310f) * y2 - 0.16666667f) * y2 + 1.0f) * y;

    // 10-degree minimax approximation
    float p = ((((-2.6051615e-07f * y2 + 2.4760495e-05f) * y2 - 0.0013888378f) * y2 + 0.041666638f) * y2 - 0.5f) * y2 + 1.0f;
    *ScalarCos = sign * p;
}

FVector RotatorToVector(FRotator rotation) {
    float CP, SP, CY, SY;
    SinCos(&SP, &CP, DEG2RAD(rotation.Pitch));
    SinCos(&SY, &CY, DEG2RAD(rotation.Yaw));
    FVector V = FVector(CP * CY, CP * SY, SP);

    return V;
}

}  // namespace math

namespace game_data {
static UWorld* world = NULL;
static ULocalPlayer* local_player = NULL;
static AMAPlayerController* local_player_controller = NULL;
static AMACharacter* local_player_character = NULL;
}  // namespace game_data

namespace Server {

// static bool initialised = false;
static bool match_started = false;
// static float match_start_delay = 1;

list<TimerWithFunctionAndData> timers;
Timer server_timer(10);  // 100 Hz

static struct ServerSettingsStruct {
    string ServerName = "Server";
    string PlayerName = "[Server]";
    string AdminPassword = "Password";
    string ExecOnInject = "open CTF-Elite?listen";
    bool ForceSelfToSpectator;

    struct GameModeStruct {
        bool bStartPlayersAsSpectators = false;
        bool bPausable = true;
        bool bDelayedStart = true;
        float MinRespawnDelay = 2.50000000;
        int EndMatchVoteLength = 30;
        int MatchLength = 1200;
        int PostMatchLength = 20;
        int WarmupLength = 30;
        int PreRoundLength = 0;
        int PostRoundLength = 0;
        int WinningScore = 8;
        int MinPlayersToStart = 2;
        bool bRandomizeTeams = true;
        float SpawnProtectionTime = 1.00000000;
        bool bPlayersDropAmmoOnDeath = true;
        bool bPlayersDropHealthOnDeath = true;
        bool bRequireLoadoutForSpawn = true;
        float MinSuicideDelay = 1.00000000;
        bool bLockTeamSpectators = false;
        bool bUseTeamStarts = true;
        bool bBalanceTeams = true;
        int MinPlayersForRebalance = 6;
        int StartRebalanceWarningTime = 15;
        int StartRebalanceTime = 30;
        bool bAllowOvertime = true;
        bool bTeamDamageAllowed = true;
        bool bUseDistanceSpawnWeighting = false;
        bool bUseTeammateDistanceSpawnWeighting = false;
        float SelfDamagePct = 1.00000000;
        float KillAssistThreshold = 0.250000000;
        bool bSupportsLoadouts = true;
        bool bDeferredRebalanceTeams = false;
        bool bUpdateMatchBalance = false;
    } GameMode;

    struct VitalsStruct {
        float HealthMax;
        float LowHealthPct;
        bool bRegenHealth;
        float HealthRegenRate;
        float EnergyMax;
        float EnergyRechargeRate;
        float EnergyRegenDisableThreshold;
        float EnergyRegenDisableDuration;
        bool bEnergyDamageBypassShield;
    } Vitals;

    struct GameSessionStruct {
        int MaxSpectators = 2;
        int MaxPlayers = 14;
    } GameSession;

    struct CharacterMovementStruct {
        float GravityScale = 1.00000000;
        float MaxStepHeight = 45.0000000;
        float JumpZVelocity = 550.000000;
        float JumpOffJumpZFactor = 0.500000000;
        float WalkableFloorAngle = 70.0000000;
        float WalkableFloorZ = 0.342020154;
        float GroundFriction = 4.00000000;
        float MaxWalkSpeed = 800.000000;
        float MaxWalkSpeedCrouched = 400.000000;
        float MaxSwimSpeed = 300.000000;
        float MaxFlySpeed = 600.000000;
        float MaxCustomMovementSpeed = 600.000000;
        float MaxAcceleration = 3600.00000;
        float MinAnalogWalkSpeed = 0.00000000;
        float BrakingFrictionFactor = 2.00000000;
        float BrakingFriction = 0.00000000;
        float BrakingSubStepTime = 0.0303030312;
        float BrakingDecelerationWalking = 500.000000;
        float BrakingDecelerationFalling = 0.00000000;
        float BrakingDecelerationSwimming = 0.00000000;
        float BrakingDecelerationFlying = 0.00000000;
        float AirControl = 0.150000006;
        float AirControlBoostMultiplier = 2.00000000;
        float AirControlBoostVelocityThreshold = 25.0000000;
        float FallingLateralFriction = 0.00000000;
        float CrouchedHalfHeight = 40.0000000;
        float Buoyancy = 1.00000000;
        float PerchRadiusThreshold = 0.00000000;
        float PerchAdditionalHeight = 40.0000000;
        bool bUseSeparateBrakingFriction = false;
        bool bApplyGravityWhileJumping = true;
        bool bUseControllerDesiredRotation = false;
        bool bOrientRotationToMovement = false;
        float MaxOutOfWaterStepHeight = 40.0000000;
        float OutofWaterZ = 420.000000;
        float Mass = 100.000000;
        float StandingDownwardForceScale = 1.00000000;
        float InitialPushForceFactor = 500.000000;
        float PushForceFactor = 750000.000;
        float PushForcePointZOffsetFactor = -0.750000000;
        float TouchForceFactor = 1.00000000;
        float MinTouchForce = -1.00000000;
        float MaxTouchForce = 250.000000;
        float RepulsionForce = 2.50000000;
    } CharacterMovement;
    struct MACharacterMovementStruct {
        float JetAcceleration;
        float MaxJetLateralSpeed;
        float MaxJetLateralPercent;
        float MaxJetVerticalSpeedStart;
        float MaxJetVerticalSpeedEnd;
        float UpwardJetBonusMaxSpeed;
        float MaxUpwardJetBonus;
        float UpwardJetBonusRegenRate;
        float UpwardJetBonusWalkingBonusRegenRate;
        float UpwardJetBonusRegenWaitTime;
        float UpwardJetBonusBurnRate;
        float UpwardJetBurnPower;
        bool bSeparateSkateJumpVelocity;
        float SkateJumpZVelocity;
        float JetModifier;
        float MaxFallingLateralSpeed;
        float UpwardsDamping;
        float UpwardsDampingSpeed;
        float MaxUpwardsSpeed;
        float UpwardJetBonusEnergy;
    } MACharacterMovement;

    struct WeaponStruct {
        int StoredAmmo;
        int LoadedAmmo;
        float FireSpeed;
        int InitialAmmo;
        int MaxAmmo;
        float TakeOutTime;
        int ProjectilesPerShot;
        float FireIntervalFrom;
        float FireIntervalTo;
        float FireAccelerationTime;
        float FireDecelerationTime;
        bool bLockFireInterval;
        int BurstSize;
        int AmmoCost;
        int EnergyCost;
        float SoftRecoveryWindow;
        float DryFireTime;
        bool bClearTriggerAfterRecovery;
        bool bClearTriggerWhenAbsolutelyEmpty;
        bool bChargeBeforeFiring;
        float BloomAngle;
        int BloomPatternRegions;
        float ChargeTime;
        float ChargeDissipationTime;
        float OverloadTime;
        float ReloadTime;
        bool bReloadEveryRound;
        bool bWasteAmmoWhenReloaded;
        bool bAutoReloadOnEmpty;
        bool bFireCanInterruptReload;
        float HeatPerShot;
        float OverheatThreshold;
        float OverheatRecoveryThreshold;
        float HeatLossPerSecond;
        float OverheatedHeatLossPerSecond;
        struct FireOffsetStruct {
            float X, Y, Z;
        } FireOffset;
        struct ProjectileStruct {
            struct DamageParamsStruct {
                float BaseDamage;
                float MinimumDamage;
                float InnerRadius;
                float OuterRadius;
                float DamageFalloff;
                float BaseImpulseMag;
                float MinImpulseMag;
            } DamageParams;
            struct ProjectileMovementStruct {
                float InitialSpeed;
                float MaxSpeed;
                bool bShouldBounce;
                float Mass;
                float ProjectileGravityScale;
                float Buoyancy;
                float Bounciness;
                float Friction;
            } ProjectileMovement;
            struct InheritanceStruct {
                float X, Y, Z;
            } InheritVelocityScale;
        } Projectile;
    } RingLauncher, Chaingun, GrenadeLauncher;

    void Reload(void) {
        timers.clear();

        std::ifstream ifs("ServerSettings.json");
        json server_settings_json = json::parse(ifs);

        server_settings_json = server_settings_json["ServerSettings"];

        ServerName = server_settings_json["ServerName"].get<string>();
        PlayerName = server_settings_json["PlayerName"].get<string>();
        AdminPassword = server_settings_json["AdminPassword"].get<string>();
        ExecOnInject = server_settings_json["ExecOnInject"].get<string>();
        ForceSelfToSpectator = server_settings_json["ForceSelfToSpectator"].get<bool>();

        json game_mode_settings_json = server_settings_json["GameMode"];
        GameMode.bStartPlayersAsSpectators = game_mode_settings_json["bStartPlayersAsSpectators"].get<bool>();
        GameMode.bPausable = game_mode_settings_json["bPausable"].get<bool>();
        GameMode.bDelayedStart = game_mode_settings_json["bDelayedStart"].get<bool>();
        GameMode.MinRespawnDelay = game_mode_settings_json["MinRespawnDelay"].get<float>();
        GameMode.EndMatchVoteLength = game_mode_settings_json["EndMatchVoteLength"].get<int>();
        GameMode.MatchLength = game_mode_settings_json["MatchLength"].get<int>();
        GameMode.PostMatchLength = game_mode_settings_json["PostMatchLength"].get<int>();
        GameMode.WarmupLength = game_mode_settings_json["WarmupLength"].get<int>();
        GameMode.PreRoundLength = game_mode_settings_json["PreRoundLength"].get<int>();
        GameMode.PostRoundLength = game_mode_settings_json["PostRoundLength"].get<int>();
        GameMode.WinningScore = game_mode_settings_json["WinningScore"].get<int>();
        GameMode.MinPlayersToStart = game_mode_settings_json["MinPlayersToStart"].get<int>();
        GameMode.bRandomizeTeams = game_mode_settings_json["bRandomizeTeams"].get<bool>();
        GameMode.SpawnProtectionTime = game_mode_settings_json["SpawnProtectionTime"].get<float>();
        GameMode.bPlayersDropAmmoOnDeath = game_mode_settings_json["bPlayersDropAmmoOnDeath"].get<bool>();
        GameMode.bPlayersDropHealthOnDeath = game_mode_settings_json["bPlayersDropHealthOnDeath"].get<bool>();
        GameMode.bRequireLoadoutForSpawn = game_mode_settings_json["bRequireLoadoutForSpawn"].get<bool>();
        GameMode.MinSuicideDelay = game_mode_settings_json["MinSuicideDelay"].get<float>();
        GameMode.bLockTeamSpectators = game_mode_settings_json["bLockTeamSpectators"].get<bool>();
        GameMode.bUseTeamStarts = game_mode_settings_json["bUseTeamStarts"].get<bool>();
        GameMode.bBalanceTeams = game_mode_settings_json["bBalanceTeams"].get<bool>();
        GameMode.MinPlayersForRebalance = game_mode_settings_json["MinPlayersForRebalance"].get<int>();
        GameMode.StartRebalanceWarningTime = game_mode_settings_json["StartRebalanceWarningTime"].get<int>();
        GameMode.StartRebalanceTime = game_mode_settings_json["StartRebalanceTime"].get<int>();
        GameMode.bAllowOvertime = game_mode_settings_json["bAllowOvertime"].get<bool>();
        GameMode.bTeamDamageAllowed = game_mode_settings_json["bTeamDamageAllowed"].get<bool>();
        GameMode.bUseDistanceSpawnWeighting = game_mode_settings_json["bUseDistanceSpawnWeighting"].get<bool>();
        GameMode.bUseTeammateDistanceSpawnWeighting = game_mode_settings_json["bUseTeammateDistanceSpawnWeighting"].get<bool>();
        GameMode.SelfDamagePct = game_mode_settings_json["SelfDamagePct"].get<float>();
        GameMode.KillAssistThreshold = game_mode_settings_json["KillAssistThreshold"].get<float>();
        GameMode.bSupportsLoadouts = game_mode_settings_json["bSupportsLoadouts"].get<bool>();
        GameMode.bDeferredRebalanceTeams = game_mode_settings_json["bDeferredRebalanceTeams"].get<bool>();
        GameMode.bUpdateMatchBalance = game_mode_settings_json["bUpdateMatchBalance"].get<bool>();

        json game_session_settings_json = server_settings_json["GameSession"];
        GameSession.MaxSpectators = game_session_settings_json["MaxSpectators"].get<int>();
        GameSession.MaxPlayers = game_session_settings_json["MaxPlayers"].get<int>();

        json vitals_settings_json = server_settings_json["Vitals"];
        Vitals.HealthMax = vitals_settings_json["HealthMax"];
        Vitals.LowHealthPct = vitals_settings_json["LowHealthPct"];
        Vitals.bRegenHealth = vitals_settings_json["bRegenHealth"];
        Vitals.HealthRegenRate = vitals_settings_json["HealthRegenRate"];
        Vitals.EnergyMax = vitals_settings_json["EnergyMax"];
        Vitals.EnergyRechargeRate = vitals_settings_json["EnergyRechargeRate"];
        Vitals.EnergyRegenDisableThreshold = vitals_settings_json["EnergyRegenDisableThreshold"];
        Vitals.EnergyRegenDisableDuration = vitals_settings_json["EnergyRegenDisableDuration"];
        Vitals.bEnergyDamageBypassShield = vitals_settings_json["bEnergyDamageBypassShield"];

        json character_movement_settings_json = server_settings_json["CharacterMovement"];
        CharacterMovement.GravityScale = character_movement_settings_json["GravityScale"].get<float>();
        CharacterMovement.MaxStepHeight = character_movement_settings_json["MaxStepHeight"].get<float>();
        CharacterMovement.JumpZVelocity = character_movement_settings_json["JumpZVelocity"].get<float>();
        CharacterMovement.JumpOffJumpZFactor = character_movement_settings_json["JumpOffJumpZFactor"].get<float>();
        CharacterMovement.WalkableFloorAngle = character_movement_settings_json["WalkableFloorAngle"].get<float>();
        CharacterMovement.WalkableFloorZ = character_movement_settings_json["WalkableFloorZ"].get<float>();
        CharacterMovement.GroundFriction = character_movement_settings_json["GroundFriction"].get<float>();
        CharacterMovement.MaxWalkSpeed = character_movement_settings_json["MaxWalkSpeed"].get<float>();
        CharacterMovement.MaxWalkSpeedCrouched = character_movement_settings_json["MaxWalkSpeedCrouched"].get<float>();
        CharacterMovement.MaxSwimSpeed = character_movement_settings_json["MaxSwimSpeed"].get<float>();
        CharacterMovement.MaxFlySpeed = character_movement_settings_json["MaxFlySpeed"].get<float>();
        CharacterMovement.MaxCustomMovementSpeed = character_movement_settings_json["MaxCustomMovementSpeed"].get<float>();
        CharacterMovement.MaxAcceleration = character_movement_settings_json["MaxAcceleration"].get<float>();
        CharacterMovement.MinAnalogWalkSpeed = character_movement_settings_json["MinAnalogWalkSpeed"].get<float>();
        CharacterMovement.BrakingFrictionFactor = character_movement_settings_json["BrakingFrictionFactor"].get<float>();
        CharacterMovement.BrakingFriction = character_movement_settings_json["BrakingFriction"].get<float>();
        CharacterMovement.BrakingSubStepTime = character_movement_settings_json["BrakingSubStepTime"].get<float>();
        CharacterMovement.BrakingDecelerationWalking = character_movement_settings_json["BrakingDecelerationWalking"].get<float>();
        CharacterMovement.BrakingDecelerationFalling = character_movement_settings_json["BrakingDecelerationFalling"].get<float>();
        CharacterMovement.BrakingDecelerationSwimming = character_movement_settings_json["BrakingDecelerationSwimming"].get<float>();
        CharacterMovement.BrakingDecelerationFlying = character_movement_settings_json["BrakingDecelerationFlying"].get<float>();
        CharacterMovement.AirControl = character_movement_settings_json["AirControl"].get<float>();
        CharacterMovement.AirControlBoostMultiplier = character_movement_settings_json["AirControlBoostMultiplier"].get<float>();
        CharacterMovement.AirControlBoostVelocityThreshold = character_movement_settings_json["AirControlBoostVelocityThreshold"].get<float>();
        CharacterMovement.FallingLateralFriction = character_movement_settings_json["FallingLateralFriction"].get<float>();
        CharacterMovement.CrouchedHalfHeight = character_movement_settings_json["CrouchedHalfHeight"].get<float>();
        CharacterMovement.Buoyancy = character_movement_settings_json["Buoyancy"].get<float>();
        CharacterMovement.PerchRadiusThreshold = character_movement_settings_json["PerchRadiusThreshold"].get<float>();
        CharacterMovement.PerchAdditionalHeight = character_movement_settings_json["PerchAdditionalHeight"].get<float>();
        CharacterMovement.bUseSeparateBrakingFriction = character_movement_settings_json["bUseSeparateBrakingFriction"].get<bool>();
        CharacterMovement.bApplyGravityWhileJumping = character_movement_settings_json["bApplyGravityWhileJumping"].get<bool>();
        CharacterMovement.bUseControllerDesiredRotation = character_movement_settings_json["bUseControllerDesiredRotation"].get<bool>();
        CharacterMovement.bOrientRotationToMovement = character_movement_settings_json["bOrientRotationToMovement"].get<bool>();
        CharacterMovement.MaxOutOfWaterStepHeight = character_movement_settings_json["MaxOutOfWaterStepHeight"].get<float>();
        CharacterMovement.OutofWaterZ = character_movement_settings_json["OutofWaterZ"].get<float>();
        CharacterMovement.Mass = character_movement_settings_json["Mass"].get<float>();
        CharacterMovement.StandingDownwardForceScale = character_movement_settings_json["StandingDownwardForceScale"].get<float>();
        CharacterMovement.InitialPushForceFactor = character_movement_settings_json["InitialPushForceFactor"].get<float>();
        CharacterMovement.PushForceFactor = character_movement_settings_json["PushForceFactor"].get<float>();
        CharacterMovement.PushForcePointZOffsetFactor = character_movement_settings_json["PushForcePointZOffsetFactor"].get<float>();
        CharacterMovement.TouchForceFactor = character_movement_settings_json["TouchForceFactor"].get<float>();
        CharacterMovement.MinTouchForce = character_movement_settings_json["MinTouchForce"].get<float>();
        CharacterMovement.MaxTouchForce = character_movement_settings_json["MaxTouchForce"].get<float>();
        CharacterMovement.RepulsionForce = character_movement_settings_json["RepulsionForce"].get<float>();

        json macharacter_movement_settings_json = server_settings_json["MACharacterMovement"];

        MACharacterMovement.JetAcceleration = macharacter_movement_settings_json["JetAcceleration"].get<float>();
        MACharacterMovement.MaxJetLateralSpeed = macharacter_movement_settings_json["MaxJetLateralSpeed"].get<float>();
        MACharacterMovement.MaxJetLateralPercent = macharacter_movement_settings_json["MaxJetLateralPercent"].get<float>();
        MACharacterMovement.MaxJetVerticalSpeedStart = macharacter_movement_settings_json["MaxJetVerticalSpeedStart"].get<float>();
        MACharacterMovement.MaxJetVerticalSpeedEnd = macharacter_movement_settings_json["MaxJetVerticalSpeedEnd"].get<float>();
        MACharacterMovement.UpwardJetBonusMaxSpeed = macharacter_movement_settings_json["UpwardJetBonusMaxSpeed"].get<float>();
        MACharacterMovement.MaxUpwardJetBonus = macharacter_movement_settings_json["MaxUpwardJetBonus"].get<float>();
        MACharacterMovement.UpwardJetBonusRegenRate = macharacter_movement_settings_json["UpwardJetBonusRegenRate"].get<float>();
        MACharacterMovement.UpwardJetBonusWalkingBonusRegenRate = macharacter_movement_settings_json["UpwardJetBonusWalkingBonusRegenRate"].get<float>();
        MACharacterMovement.UpwardJetBonusRegenWaitTime = macharacter_movement_settings_json["UpwardJetBonusRegenWaitTime"].get<float>();
        MACharacterMovement.UpwardJetBonusBurnRate = macharacter_movement_settings_json["UpwardJetBonusBurnRate"].get<float>();
        MACharacterMovement.UpwardJetBurnPower = macharacter_movement_settings_json["UpwardJetBurnPower"].get<float>();
        MACharacterMovement.bSeparateSkateJumpVelocity = macharacter_movement_settings_json["bSeparateSkateJumpVelocity"].get<bool>();
        MACharacterMovement.SkateJumpZVelocity = macharacter_movement_settings_json["SkateJumpZVelocity"].get<float>();
        MACharacterMovement.JetModifier = macharacter_movement_settings_json["JetModifier"].get<float>();
        MACharacterMovement.MaxFallingLateralSpeed = macharacter_movement_settings_json["MaxFallingLateralSpeed"].get<float>();
        MACharacterMovement.UpwardsDamping = macharacter_movement_settings_json["UpwardsDamping"].get<float>();
        MACharacterMovement.UpwardsDampingSpeed = macharacter_movement_settings_json["UpwardsDampingSpeed"].get<float>();
        MACharacterMovement.MaxUpwardsSpeed = macharacter_movement_settings_json["MaxUpwardsSpeed"].get<float>();
        MACharacterMovement.UpwardJetBonusEnergy = macharacter_movement_settings_json["UpwardJetBonusEnergy"].get<float>();

        //////////////////////////////////////////////////
        //////////////////////////////////////////////////

        json ring_launcher_settings_json = server_settings_json["RingLauncher"];
        RingLauncher.StoredAmmo = ring_launcher_settings_json["StoredAmmo"].get<int>();
        RingLauncher.LoadedAmmo = ring_launcher_settings_json["LoadedAmmo"].get<int>();
        RingLauncher.FireSpeed = ring_launcher_settings_json["FireSpeed"].get<float>();
        RingLauncher.InitialAmmo = ring_launcher_settings_json["InitialAmmo"].get<int>();
        RingLauncher.MaxAmmo = ring_launcher_settings_json["MaxAmmo"].get<int>();
        RingLauncher.TakeOutTime = ring_launcher_settings_json["TakeOutTime"].get<float>();
        RingLauncher.ProjectilesPerShot = ring_launcher_settings_json["ProjectilesPerShot"].get<int>();
        RingLauncher.FireIntervalFrom = ring_launcher_settings_json["FireIntervalFrom"].get<float>();
        RingLauncher.FireIntervalTo = ring_launcher_settings_json["FireIntervalTo"].get<float>();
        RingLauncher.FireAccelerationTime = ring_launcher_settings_json["FireAccelerationTime"].get<float>();
        RingLauncher.FireDecelerationTime = ring_launcher_settings_json["FireDecelerationTime"].get<float>();
        RingLauncher.bLockFireInterval = ring_launcher_settings_json["bLockFireInterval"].get<bool>();
        RingLauncher.BurstSize = ring_launcher_settings_json["BurstSize"].get<int>();
        RingLauncher.AmmoCost = ring_launcher_settings_json["AmmoCost"].get<int>();
        RingLauncher.EnergyCost = ring_launcher_settings_json["EnergyCost"].get<int>();
        RingLauncher.SoftRecoveryWindow = ring_launcher_settings_json["SoftRecoveryWindow"].get<float>();
        RingLauncher.DryFireTime = ring_launcher_settings_json["DryFireTime"].get<float>();
        RingLauncher.bClearTriggerAfterRecovery = ring_launcher_settings_json["bClearTriggerAfterRecovery"].get<bool>();
        RingLauncher.bClearTriggerWhenAbsolutelyEmpty = ring_launcher_settings_json["bClearTriggerWhenAbsolutelyEmpty"].get<bool>();
        RingLauncher.bChargeBeforeFiring = ring_launcher_settings_json["bChargeBeforeFiring"].get<bool>();
        RingLauncher.BloomAngle = ring_launcher_settings_json["BloomAngle"].get<float>();
        RingLauncher.BloomPatternRegions = ring_launcher_settings_json["BloomPatternRegions"].get<int>();
        RingLauncher.ChargeTime = ring_launcher_settings_json["ChargeTime"].get<float>();
        RingLauncher.ChargeDissipationTime = ring_launcher_settings_json["ChargeDissipationTime"].get<float>();
        RingLauncher.OverloadTime = ring_launcher_settings_json["OverloadTime"].get<float>();
        RingLauncher.ReloadTime = ring_launcher_settings_json["ReloadTime"].get<float>();
        RingLauncher.bReloadEveryRound = ring_launcher_settings_json["bReloadEveryRound"].get<bool>();
        RingLauncher.bWasteAmmoWhenReloaded = ring_launcher_settings_json["bWasteAmmoWhenReloaded"].get<bool>();
        RingLauncher.bAutoReloadOnEmpty = ring_launcher_settings_json["bAutoReloadOnEmpty"].get<bool>();
        RingLauncher.bFireCanInterruptReload = ring_launcher_settings_json["bFireCanInterruptReload"].get<bool>();
        RingLauncher.HeatPerShot = ring_launcher_settings_json["HeatPerShot"].get<float>();
        RingLauncher.OverheatThreshold = ring_launcher_settings_json["OverheatThreshold"].get<float>();
        RingLauncher.OverheatRecoveryThreshold = ring_launcher_settings_json["OverheatRecoveryThreshold"].get<float>();
        RingLauncher.HeatLossPerSecond = ring_launcher_settings_json["HeatLossPerSecond"].get<float>();
        RingLauncher.OverheatedHeatLossPerSecond = ring_launcher_settings_json["OverheatedHeatLossPerSecond"].get<float>();

        RingLauncher.FireOffset.X = ring_launcher_settings_json["FireOffset"]["X"].get<float>();
        RingLauncher.FireOffset.Y = ring_launcher_settings_json["FireOffset"]["Y"].get<float>();
        RingLauncher.FireOffset.Z = ring_launcher_settings_json["FireOffset"]["Z"].get<float>();

        RingLauncher.Projectile.DamageParams.BaseDamage = ring_launcher_settings_json["Projectile"]["DamageParams"]["BaseDamage"].get<float>();
        RingLauncher.Projectile.DamageParams.MinimumDamage = ring_launcher_settings_json["Projectile"]["DamageParams"]["MinimumDamage"].get<float>();
        RingLauncher.Projectile.DamageParams.InnerRadius = ring_launcher_settings_json["Projectile"]["DamageParams"]["InnerRadius"].get<float>();
        RingLauncher.Projectile.DamageParams.OuterRadius = ring_launcher_settings_json["Projectile"]["DamageParams"]["OuterRadius"].get<float>();
        RingLauncher.Projectile.DamageParams.DamageFalloff = ring_launcher_settings_json["Projectile"]["DamageParams"]["DamageFalloff"].get<float>();
        RingLauncher.Projectile.DamageParams.BaseImpulseMag = ring_launcher_settings_json["Projectile"]["DamageParams"]["BaseImpulseMag"].get<float>();
        RingLauncher.Projectile.DamageParams.MinImpulseMag = ring_launcher_settings_json["Projectile"]["DamageParams"]["MinImpulseMag"].get<float>();

        RingLauncher.Projectile.ProjectileMovement.InitialSpeed = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["InitialSpeed"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.MaxSpeed = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["MaxSpeed"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.bShouldBounce = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["bShouldBounce"].get<bool>();
        RingLauncher.Projectile.ProjectileMovement.Mass = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["Mass"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.ProjectileGravityScale = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["ProjectileGravityScale"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.Buoyancy = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["Buoyancy"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.Bounciness = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["Bounciness"].get<float>();
        RingLauncher.Projectile.ProjectileMovement.Friction = ring_launcher_settings_json["Projectile"]["ProjectileMovement"]["Friction"].get<float>();

        RingLauncher.Projectile.InheritVelocityScale.X = ring_launcher_settings_json["Projectile"]["InheritVelocityScale"]["X"].get<float>();
        RingLauncher.Projectile.InheritVelocityScale.Y = ring_launcher_settings_json["Projectile"]["InheritVelocityScale"]["Y"].get<float>();
        RingLauncher.Projectile.InheritVelocityScale.Z = ring_launcher_settings_json["Projectile"]["InheritVelocityScale"]["Z"].get<float>();

        //////////////////////////////////////////////////
        //////////////////////////////////////////////////

        json grenade_launcher_settings_json = server_settings_json["GrenadeLauncher"];
        GrenadeLauncher.StoredAmmo = grenade_launcher_settings_json["StoredAmmo"].get<int>();
        GrenadeLauncher.LoadedAmmo = grenade_launcher_settings_json["LoadedAmmo"].get<int>();
        GrenadeLauncher.FireSpeed = grenade_launcher_settings_json["FireSpeed"].get<float>();
        GrenadeLauncher.InitialAmmo = grenade_launcher_settings_json["InitialAmmo"].get<int>();
        GrenadeLauncher.MaxAmmo = grenade_launcher_settings_json["MaxAmmo"].get<int>();
        GrenadeLauncher.TakeOutTime = grenade_launcher_settings_json["TakeOutTime"].get<float>();
        GrenadeLauncher.ProjectilesPerShot = grenade_launcher_settings_json["ProjectilesPerShot"].get<int>();
        GrenadeLauncher.FireIntervalFrom = grenade_launcher_settings_json["FireIntervalFrom"].get<float>();
        GrenadeLauncher.FireIntervalTo = grenade_launcher_settings_json["FireIntervalTo"].get<float>();
        GrenadeLauncher.FireAccelerationTime = grenade_launcher_settings_json["FireAccelerationTime"].get<float>();
        GrenadeLauncher.FireDecelerationTime = grenade_launcher_settings_json["FireDecelerationTime"].get<float>();
        GrenadeLauncher.bLockFireInterval = grenade_launcher_settings_json["bLockFireInterval"].get<bool>();
        GrenadeLauncher.BurstSize = grenade_launcher_settings_json["BurstSize"].get<int>();
        GrenadeLauncher.AmmoCost = grenade_launcher_settings_json["AmmoCost"].get<int>();
        GrenadeLauncher.EnergyCost = grenade_launcher_settings_json["EnergyCost"].get<int>();
        GrenadeLauncher.SoftRecoveryWindow = grenade_launcher_settings_json["SoftRecoveryWindow"].get<float>();
        GrenadeLauncher.DryFireTime = grenade_launcher_settings_json["DryFireTime"].get<float>();
        GrenadeLauncher.bClearTriggerAfterRecovery = grenade_launcher_settings_json["bClearTriggerAfterRecovery"].get<bool>();
        GrenadeLauncher.bClearTriggerWhenAbsolutelyEmpty = grenade_launcher_settings_json["bClearTriggerWhenAbsolutelyEmpty"].get<bool>();
        GrenadeLauncher.bChargeBeforeFiring = grenade_launcher_settings_json["bChargeBeforeFiring"].get<bool>();
        GrenadeLauncher.BloomAngle = grenade_launcher_settings_json["BloomAngle"].get<float>();
        GrenadeLauncher.BloomPatternRegions = grenade_launcher_settings_json["BloomPatternRegions"].get<int>();
        GrenadeLauncher.ChargeTime = grenade_launcher_settings_json["ChargeTime"].get<float>();
        GrenadeLauncher.ChargeDissipationTime = grenade_launcher_settings_json["ChargeDissipationTime"].get<float>();
        GrenadeLauncher.OverloadTime = grenade_launcher_settings_json["OverloadTime"].get<float>();
        GrenadeLauncher.ReloadTime = grenade_launcher_settings_json["ReloadTime"].get<float>();
        GrenadeLauncher.bReloadEveryRound = grenade_launcher_settings_json["bReloadEveryRound"].get<bool>();
        GrenadeLauncher.bWasteAmmoWhenReloaded = grenade_launcher_settings_json["bWasteAmmoWhenReloaded"].get<bool>();
        GrenadeLauncher.bAutoReloadOnEmpty = grenade_launcher_settings_json["bAutoReloadOnEmpty"].get<bool>();
        GrenadeLauncher.bFireCanInterruptReload = grenade_launcher_settings_json["bFireCanInterruptReload"].get<bool>();
        GrenadeLauncher.HeatPerShot = grenade_launcher_settings_json["HeatPerShot"].get<float>();
        GrenadeLauncher.OverheatThreshold = grenade_launcher_settings_json["OverheatThreshold"].get<float>();
        GrenadeLauncher.OverheatRecoveryThreshold = grenade_launcher_settings_json["OverheatRecoveryThreshold"].get<float>();
        GrenadeLauncher.HeatLossPerSecond = grenade_launcher_settings_json["HeatLossPerSecond"].get<float>();
        GrenadeLauncher.OverheatedHeatLossPerSecond = grenade_launcher_settings_json["OverheatedHeatLossPerSecond"].get<float>();

        GrenadeLauncher.FireOffset.X = grenade_launcher_settings_json["FireOffset"]["X"].get<float>();
        GrenadeLauncher.FireOffset.Y = grenade_launcher_settings_json["FireOffset"]["Y"].get<float>();
        GrenadeLauncher.FireOffset.Z = grenade_launcher_settings_json["FireOffset"]["Z"].get<float>();

        GrenadeLauncher.Projectile.DamageParams.BaseDamage = grenade_launcher_settings_json["Projectile"]["DamageParams"]["BaseDamage"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.MinimumDamage = grenade_launcher_settings_json["Projectile"]["DamageParams"]["MinimumDamage"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.InnerRadius = grenade_launcher_settings_json["Projectile"]["DamageParams"]["InnerRadius"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.OuterRadius = grenade_launcher_settings_json["Projectile"]["DamageParams"]["OuterRadius"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.DamageFalloff = grenade_launcher_settings_json["Projectile"]["DamageParams"]["DamageFalloff"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.BaseImpulseMag = grenade_launcher_settings_json["Projectile"]["DamageParams"]["BaseImpulseMag"].get<float>();
        GrenadeLauncher.Projectile.DamageParams.MinImpulseMag = grenade_launcher_settings_json["Projectile"]["DamageParams"]["MinImpulseMag"].get<float>();

        GrenadeLauncher.Projectile.ProjectileMovement.InitialSpeed = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["InitialSpeed"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.MaxSpeed = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["MaxSpeed"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.bShouldBounce = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["bShouldBounce"].get<bool>();
        GrenadeLauncher.Projectile.ProjectileMovement.Mass = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["Mass"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.ProjectileGravityScale = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["ProjectileGravityScale"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.Buoyancy = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["Buoyancy"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.Bounciness = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["Bounciness"].get<float>();
        GrenadeLauncher.Projectile.ProjectileMovement.Friction = grenade_launcher_settings_json["Projectile"]["ProjectileMovement"]["Friction"].get<float>();

        GrenadeLauncher.Projectile.InheritVelocityScale.X = grenade_launcher_settings_json["Projectile"]["InheritVelocityScale"]["X"].get<float>();
        GrenadeLauncher.Projectile.InheritVelocityScale.Y = grenade_launcher_settings_json["Projectile"]["InheritVelocityScale"]["Y"].get<float>();
        GrenadeLauncher.Projectile.InheritVelocityScale.Z = grenade_launcher_settings_json["Projectile"]["InheritVelocityScale"]["Z"].get<float>();

        ///////////////////////////////////////////////
        ///////////////////////////////////////////////

        json chaingun_settings_json = server_settings_json["Chaingun"];
        Chaingun.StoredAmmo = chaingun_settings_json["StoredAmmo"].get<int>();
        Chaingun.LoadedAmmo = chaingun_settings_json["LoadedAmmo"].get<int>();
        Chaingun.FireSpeed = chaingun_settings_json["FireSpeed"].get<float>();
        Chaingun.InitialAmmo = chaingun_settings_json["InitialAmmo"].get<int>();
        Chaingun.MaxAmmo = chaingun_settings_json["MaxAmmo"].get<int>();
        Chaingun.TakeOutTime = chaingun_settings_json["TakeOutTime"].get<float>();
        Chaingun.ProjectilesPerShot = chaingun_settings_json["ProjectilesPerShot"].get<int>();
        Chaingun.FireIntervalFrom = chaingun_settings_json["FireIntervalFrom"].get<float>();
        Chaingun.FireIntervalTo = chaingun_settings_json["FireIntervalTo"].get<float>();
        Chaingun.FireAccelerationTime = chaingun_settings_json["FireAccelerationTime"].get<float>();
        Chaingun.FireDecelerationTime = chaingun_settings_json["FireDecelerationTime"].get<float>();
        Chaingun.bLockFireInterval = chaingun_settings_json["bLockFireInterval"].get<bool>();
        Chaingun.BurstSize = chaingun_settings_json["BurstSize"].get<int>();
        Chaingun.AmmoCost = chaingun_settings_json["AmmoCost"].get<int>();
        Chaingun.EnergyCost = chaingun_settings_json["EnergyCost"].get<int>();
        Chaingun.SoftRecoveryWindow = chaingun_settings_json["SoftRecoveryWindow"].get<float>();
        Chaingun.DryFireTime = chaingun_settings_json["DryFireTime"].get<float>();
        Chaingun.bClearTriggerAfterRecovery = chaingun_settings_json["bClearTriggerAfterRecovery"].get<bool>();
        Chaingun.bClearTriggerWhenAbsolutelyEmpty = chaingun_settings_json["bClearTriggerWhenAbsolutelyEmpty"].get<bool>();
        Chaingun.bChargeBeforeFiring = chaingun_settings_json["bChargeBeforeFiring"].get<bool>();
        Chaingun.BloomAngle = chaingun_settings_json["BloomAngle"].get<float>();
        Chaingun.BloomPatternRegions = chaingun_settings_json["BloomPatternRegions"].get<int>();
        Chaingun.ChargeTime = chaingun_settings_json["ChargeTime"].get<float>();
        Chaingun.ChargeDissipationTime = chaingun_settings_json["ChargeDissipationTime"].get<float>();
        Chaingun.OverloadTime = chaingun_settings_json["OverloadTime"].get<float>();
        Chaingun.ReloadTime = chaingun_settings_json["ReloadTime"].get<float>();
        Chaingun.bReloadEveryRound = chaingun_settings_json["bReloadEveryRound"].get<bool>();
        Chaingun.bWasteAmmoWhenReloaded = chaingun_settings_json["bWasteAmmoWhenReloaded"].get<bool>();
        Chaingun.bAutoReloadOnEmpty = chaingun_settings_json["bAutoReloadOnEmpty"].get<bool>();
        Chaingun.bFireCanInterruptReload = chaingun_settings_json["bFireCanInterruptReload"].get<bool>();
        Chaingun.HeatPerShot = chaingun_settings_json["HeatPerShot"].get<float>();
        Chaingun.OverheatThreshold = chaingun_settings_json["OverheatThreshold"].get<float>();
        Chaingun.OverheatRecoveryThreshold = chaingun_settings_json["OverheatRecoveryThreshold"].get<float>();
        Chaingun.HeatLossPerSecond = chaingun_settings_json["HeatLossPerSecond"].get<float>();
        Chaingun.OverheatedHeatLossPerSecond = chaingun_settings_json["OverheatedHeatLossPerSecond"].get<float>();

        Chaingun.FireOffset.X = chaingun_settings_json["FireOffset"]["X"].get<float>();
        Chaingun.FireOffset.Y = chaingun_settings_json["FireOffset"]["Y"].get<float>();
        Chaingun.FireOffset.Z = chaingun_settings_json["FireOffset"]["Z"].get<float>();

        Chaingun.Projectile.DamageParams.BaseDamage = chaingun_settings_json["Projectile"]["DamageParams"]["BaseDamage"].get<float>();
        Chaingun.Projectile.DamageParams.MinimumDamage = chaingun_settings_json["Projectile"]["DamageParams"]["MinimumDamage"].get<float>();
        Chaingun.Projectile.DamageParams.InnerRadius = chaingun_settings_json["Projectile"]["DamageParams"]["InnerRadius"].get<float>();
        Chaingun.Projectile.DamageParams.OuterRadius = chaingun_settings_json["Projectile"]["DamageParams"]["OuterRadius"].get<float>();
        Chaingun.Projectile.DamageParams.DamageFalloff = chaingun_settings_json["Projectile"]["DamageParams"]["DamageFalloff"].get<float>();
        Chaingun.Projectile.DamageParams.BaseImpulseMag = chaingun_settings_json["Projectile"]["DamageParams"]["BaseImpulseMag"].get<float>();
        Chaingun.Projectile.DamageParams.MinImpulseMag = chaingun_settings_json["Projectile"]["DamageParams"]["MinImpulseMag"].get<float>();

        Chaingun.Projectile.ProjectileMovement.InitialSpeed = chaingun_settings_json["Projectile"]["ProjectileMovement"]["InitialSpeed"].get<float>();
        Chaingun.Projectile.ProjectileMovement.MaxSpeed = chaingun_settings_json["Projectile"]["ProjectileMovement"]["MaxSpeed"].get<float>();
        Chaingun.Projectile.ProjectileMovement.bShouldBounce = chaingun_settings_json["Projectile"]["ProjectileMovement"]["bShouldBounce"].get<bool>();
        Chaingun.Projectile.ProjectileMovement.Mass = chaingun_settings_json["Projectile"]["ProjectileMovement"]["Mass"].get<float>();
        Chaingun.Projectile.ProjectileMovement.ProjectileGravityScale = chaingun_settings_json["Projectile"]["ProjectileMovement"]["ProjectileGravityScale"].get<float>();
        Chaingun.Projectile.ProjectileMovement.Buoyancy = chaingun_settings_json["Projectile"]["ProjectileMovement"]["Buoyancy"].get<float>();
        Chaingun.Projectile.ProjectileMovement.Bounciness = chaingun_settings_json["Projectile"]["ProjectileMovement"]["Bounciness"].get<float>();
        Chaingun.Projectile.ProjectileMovement.Friction = chaingun_settings_json["Projectile"]["ProjectileMovement"]["Friction"].get<float>();

        Chaingun.Projectile.InheritVelocityScale.X = chaingun_settings_json["Projectile"]["InheritVelocityScale"]["X"].get<float>();
        Chaingun.Projectile.InheritVelocityScale.Y = chaingun_settings_json["Projectile"]["InheritVelocityScale"]["Y"].get<float>();
        Chaingun.Projectile.InheritVelocityScale.Z = chaingun_settings_json["Projectile"]["InheritVelocityScale"]["Z"].get<float>();
    }

} ServerSettings;

void SetIngameSettings(void) {
    AMAGameMode* game_mode = ((AMAGameMode*)game_data::world->AuthorityGameMode);
    if (game_mode) {
        // game_mode->MinSuicideDelay = 0;
        // game_mode->MinRespawnDelay = 0;
        // game_mode->bRequireLoadoutForSpawn = true;

        if (game_mode->DefaultLoadout.Weapons.Data) {
            game_mode->DefaultLoadout.Weapons[0] = UWeaponInfo_RingLauncher_C::StaticClass();
            game_mode->DefaultLoadout.Weapons[1] = UWeaponInfo_Chaingun_C::StaticClass();
            game_mode->DefaultLoadout.Weapons[2] = UWeaponInfo_GrenadeLauncher_C::StaticClass();
        }
        // static bool init = false;
        // if (!init)
        /*
        UClass** wep_skins = new UClass*[4];
        wep_skins[0] = AWSkin_Tempest_Gold_C::StaticClass();
        wep_skins[1] = AWSkin_Chaingun_Demonic_C::StaticClass();
        wep_skins[2] = AWSkin_GrenadeLauncher_Rainbow_C::StaticClass();
        wep_skins[3] = NULL;

        game_mode->DefaultLoadout.WeaponSkins.Data = wep_skins;
        game_mode->DefaultLoadout.WeaponSkins.Count = 3;
        game_mode->DefaultLoadout.WeaponSkins.Max = 4;
        */
        // init = true;
        //}

        /*
        for (int i = 0; i < 5; i++) {
            UClass** wep_skins1 = new UClass*[3];
            wep_skins1[0] = AWSkin_Tempest_Gold_C::StaticClass();
            wep_skins1[1] = AWSkin_Chaingun_Demonic_C::StaticClass();
            wep_skins1[2] = AWSkin_GrenadeLauncher_Rainbow_C::StaticClass();

            game_mode->DefaultLoadouts[i].WeaponSkins.Data = wep_skins1;
            game_mode->DefaultLoadouts[i].WeaponSkins.Count = 3;
        }
        */

        // return;

        game_mode->bPauseable = Server::ServerSettings.GameMode.bPausable;
        game_mode->bDelayedStart = Server::ServerSettings.GameMode.bDelayedStart;
        game_mode->MinRespawnDelay = Server::ServerSettings.GameMode.MinRespawnDelay;
        game_mode->EndMatchVoteLength = Server::ServerSettings.GameMode.EndMatchVoteLength;
        game_mode->MatchLength = Server::ServerSettings.GameMode.MatchLength;
        game_mode->PostMatchLength = Server::ServerSettings.GameMode.PostMatchLength;
        game_mode->WarmupLength = Server::ServerSettings.GameMode.WarmupLength;
        game_mode->PreRoundLength = Server::ServerSettings.GameMode.PreRoundLength;
        game_mode->PostRoundLength = Server::ServerSettings.GameMode.PostRoundLength;
        game_mode->WinningScore = Server::ServerSettings.GameMode.WinningScore;
        game_mode->MinPlayersToStart = Server::ServerSettings.GameMode.MinPlayersToStart;
        game_mode->bRandomizeTeams = Server::ServerSettings.GameMode.bRandomizeTeams;
        game_mode->SpawnProtectionTime = Server::ServerSettings.GameMode.SpawnProtectionTime;
        game_mode->bPlayersDropAmmoOnDeath = Server::ServerSettings.GameMode.bPlayersDropAmmoOnDeath;
        game_mode->bPlayersDropHealthOnDeath = Server::ServerSettings.GameMode.bPlayersDropHealthOnDeath;
        game_mode->bRequireLoadoutForSpawn = Server::ServerSettings.GameMode.bRequireLoadoutForSpawn;
        game_mode->MinSuicideDelay = Server::ServerSettings.GameMode.MinSuicideDelay;
        game_mode->bLockTeamSpectators = Server::ServerSettings.GameMode.bLockTeamSpectators;
        game_mode->bUseTeamStarts = Server::ServerSettings.GameMode.bUseTeamStarts;
        game_mode->bBalanceTeams = Server::ServerSettings.GameMode.bBalanceTeams;
        game_mode->MinPlayersForRebalance = Server::ServerSettings.GameMode.MinPlayersForRebalance;
        game_mode->StartRebalanceWarningTime = Server::ServerSettings.GameMode.StartRebalanceWarningTime;
        game_mode->StartRebalanceTime = Server::ServerSettings.GameMode.StartRebalanceTime;
        game_mode->bAllowOvertime = Server::ServerSettings.GameMode.bAllowOvertime;
        game_mode->bTeamDamageAllowed = Server::ServerSettings.GameMode.bTeamDamageAllowed;
        game_mode->bUseDistanceSpawnWeighting = Server::ServerSettings.GameMode.bUseDistanceSpawnWeighting;
        game_mode->bUseTeammateDistanceSpawnWeighting = Server::ServerSettings.GameMode.bUseTeammateDistanceSpawnWeighting;
        game_mode->SelfDamagePct = Server::ServerSettings.GameMode.SelfDamagePct;
        game_mode->KillAssistThreshold = Server::ServerSettings.GameMode.KillAssistThreshold;

        if (game_mode->GameSession) {
            game_mode->GameSession->MaxSpectators = Server::ServerSettings.GameSession.MaxSpectators;
            game_mode->GameSession->MaxPlayers = Server::ServerSettings.GameSession.MaxPlayers;
        }
    }
}

}  // namespace Server

namespace hooks {
static enum class HookType { kPre, kPost };
template <typename T>
struct Hook {
    T hook_;
    HookType hook_type_ = kPre;
    bool absorb_ = true;
};

#define PROCESSEVENT_HOOK_FUNCTION(x) void x(UObject* object, UFunction* function, void* params)
typedef void(__fastcall* _ProcessEvent)(UObject*, UFunction*, void*);
_ProcessEvent original_processevent = NULL;
unordered_map<UFunction*, vector<Hook<_ProcessEvent>>> processevent_hooks;

void ProcessEvent(UObject* object, UFunction* function, void* params);

bool AddProcessEventHook(UFunction* function, Hook<_ProcessEvent> hook) {
    if (processevent_hooks.find(function) == processevent_hooks.end()) {
        processevent_hooks[function] = vector<Hook<_ProcessEvent>>();
    }
    processevent_hooks[function].push_back(hook);
    return true;
}
}  // namespace hooks

namespace functions {

struct RespawnPlayerParams {
    AController* player_controller;
};
void RespawnPlayer(void* params) {
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)((RespawnPlayerParams*)params)->player_controller;
    if (player_controller && !player_controller->ControlledCharacter && !player_controller->Pawn && player_controller->PlayerState && !player_controller->PlayerState->bIsSpectator) {
        /*
        UClass** wep_skins = new UClass*[3];
        wep_skins[0] = AWSkin_Tempest_Gold_C::StaticClass();
        wep_skins[1] = AWSkin_Tempest_Gold_C::StaticClass();
        wep_skins[2] = AWSkin_Tempest_Gold_C::StaticClass();
        FMALoadout& loadout = player_controller->PlayerLoadout;
        loadout.WeaponSkins.Data = wep_skins;
        loadout.WeaponSkins.Count = 3;
        */

        if (((AMAGameMode*)game_data::world->AuthorityGameMode)->DefaultLoadout.Weapons.Data)
            player_controller->ServerSetLoadout(((AMAGameMode*)game_data::world->AuthorityGameMode)->DefaultLoadout);

        ((AMAGameMode*)game_data::world->AuthorityGameMode)->RestartPlayer(player_controller);
        //((AMAGameMode*)game_data::world->AuthorityGameMode)->RestartPlayer(player_controller);
    }
    delete ((RespawnPlayerParams*)params);
}

struct PingDelayProjectile {
    AMAProjectile* projectile;
    FVector original_velocity;
    FVector original_location;
};

void PingDelayProjectile(void* params) {
    struct PingDelayProjectile* p = (struct PingDelayProjectile*)params;
    // p->projectile->ProjectileMovement->Velocity = p->original_velocity;
    p->projectile->K2_SetActorLocation(p->original_location, false, NULL, false);
    delete ((struct PingDelayProjectile*)params);
}

}  // namespace functions

namespace ue {
namespace uobjects {
static UGameplayStatics* ugameplaystatics;
}

PROCESSEVENT_HOOK_FUNCTION(Nothing) {}

PROCESSEVENT_HOOK_FUNCTION(OnRep_TeamID) {
    AMAProjectile* projectile = (AMAProjectile*)object;
    projectile->DamageParams.BaseDamage *= 1;
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)projectile->GetInstigatorController();
    AMAWeapon* weapon = player_controller->ControlledCharacter->Weapon;

    if (projectile->IsA(ABP_RingLauncherProjectile_C::StaticClass())) {
        projectile->DamageParams.BaseDamage = Server::ServerSettings.RingLauncher.Projectile.DamageParams.BaseDamage;
        projectile->DamageParams.BaseImpulseMag = Server::ServerSettings.RingLauncher.Projectile.DamageParams.BaseImpulseMag;
        projectile->DamageParams.DamageFalloff = Server::ServerSettings.RingLauncher.Projectile.DamageParams.DamageFalloff;
        projectile->DamageParams.InnerRadius = Server::ServerSettings.RingLauncher.Projectile.DamageParams.InnerRadius;
        projectile->DamageParams.MinImpulseMag = Server::ServerSettings.RingLauncher.Projectile.DamageParams.MinImpulseMag;
        projectile->DamageParams.MinimumDamage = Server::ServerSettings.RingLauncher.Projectile.DamageParams.MinimumDamage;
        projectile->DamageParams.OuterRadius = Server::ServerSettings.RingLauncher.Projectile.DamageParams.OuterRadius;

        projectile->ProjectileMovement->InitialSpeed = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->Velocity = projectile->ProjectileMovement->Velocity.Unit() * Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->MaxSpeed = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.MaxSpeed;
        projectile->ProjectileMovement->bShouldBounce = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.bShouldBounce;
        projectile->ProjectileMovement->Mass = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.Mass;
        projectile->ProjectileMovement->ProjectileGravityScale = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.ProjectileGravityScale;
        projectile->ProjectileMovement->Buoyancy = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.Buoyancy;
        projectile->ProjectileMovement->Bounciness = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.Bounciness;
        projectile->ProjectileMovement->Friction = Server::ServerSettings.RingLauncher.Projectile.ProjectileMovement.Friction;

        projectile->InheritVelocityScale.X = Server::ServerSettings.RingLauncher.Projectile.InheritVelocityScale.X;
        projectile->InheritVelocityScale.Y = Server::ServerSettings.RingLauncher.Projectile.InheritVelocityScale.Y;
        projectile->InheritVelocityScale.Z = Server::ServerSettings.RingLauncher.Projectile.InheritVelocityScale.Z;
    } else if (projectile->IsA(ABP_Chaingun_Bullet_C::StaticClass())) {
        projectile->DamageParams.BaseDamage = Server::ServerSettings.Chaingun.Projectile.DamageParams.BaseDamage;
        projectile->DamageParams.BaseImpulseMag = Server::ServerSettings.Chaingun.Projectile.DamageParams.BaseImpulseMag;
        projectile->DamageParams.DamageFalloff = Server::ServerSettings.Chaingun.Projectile.DamageParams.DamageFalloff;
        projectile->DamageParams.InnerRadius = Server::ServerSettings.Chaingun.Projectile.DamageParams.InnerRadius;
        projectile->DamageParams.MinImpulseMag = Server::ServerSettings.Chaingun.Projectile.DamageParams.MinImpulseMag;
        projectile->DamageParams.MinimumDamage = Server::ServerSettings.Chaingun.Projectile.DamageParams.MinimumDamage;
        projectile->DamageParams.OuterRadius = Server::ServerSettings.Chaingun.Projectile.DamageParams.OuterRadius;

        projectile->ProjectileMovement->InitialSpeed = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->Velocity = projectile->ProjectileMovement->Velocity.Unit() * Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->MaxSpeed = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.MaxSpeed;
        projectile->ProjectileMovement->bShouldBounce = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.bShouldBounce;
        projectile->ProjectileMovement->Mass = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.Mass;
        projectile->ProjectileMovement->ProjectileGravityScale = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.ProjectileGravityScale;
        projectile->ProjectileMovement->Buoyancy = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.Buoyancy;
        projectile->ProjectileMovement->Bounciness = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.Bounciness;
        projectile->ProjectileMovement->Friction = Server::ServerSettings.Chaingun.Projectile.ProjectileMovement.Friction;

        projectile->InheritVelocityScale.X = Server::ServerSettings.Chaingun.Projectile.InheritVelocityScale.X;
        projectile->InheritVelocityScale.Y = Server::ServerSettings.Chaingun.Projectile.InheritVelocityScale.Y;
        projectile->InheritVelocityScale.Z = Server::ServerSettings.Chaingun.Projectile.InheritVelocityScale.Z;
    } else if (projectile->IsA(ABP_GrenadeShell_C::StaticClass())) {
        projectile->DamageParams.BaseDamage = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.BaseDamage;
        projectile->DamageParams.BaseImpulseMag = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.BaseImpulseMag;
        projectile->DamageParams.DamageFalloff = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.DamageFalloff;
        projectile->DamageParams.InnerRadius = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.InnerRadius;
        projectile->DamageParams.MinImpulseMag = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.MinImpulseMag;
        projectile->DamageParams.MinimumDamage = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.MinimumDamage;
        projectile->DamageParams.OuterRadius = Server::ServerSettings.GrenadeLauncher.Projectile.DamageParams.OuterRadius;

        projectile->ProjectileMovement->InitialSpeed = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->Velocity = projectile->ProjectileMovement->Velocity.Unit() * Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.InitialSpeed;
        projectile->ProjectileMovement->MaxSpeed = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.MaxSpeed;
        projectile->ProjectileMovement->bShouldBounce = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.bShouldBounce;
        projectile->ProjectileMovement->Mass = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.Mass;
        projectile->ProjectileMovement->ProjectileGravityScale = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.ProjectileGravityScale;
        projectile->ProjectileMovement->Buoyancy = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.Buoyancy;
        projectile->ProjectileMovement->Bounciness = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.Bounciness;
        projectile->ProjectileMovement->Friction = Server::ServerSettings.GrenadeLauncher.Projectile.ProjectileMovement.Friction;

        projectile->InheritVelocityScale.X = Server::ServerSettings.GrenadeLauncher.Projectile.InheritVelocityScale.X;
        projectile->InheritVelocityScale.Y = Server::ServerSettings.GrenadeLauncher.Projectile.InheritVelocityScale.Y;
        projectile->InheritVelocityScale.Z = Server::ServerSettings.GrenadeLauncher.Projectile.InheritVelocityScale.Z;
    }

    if (player_controller == game_data::local_player_controller && false) {
        // ping delay projectile
        FVector projectile_velocity = projectile->ProjectileMovement->Velocity;
        FVector projectile_location = projectile->K2_GetActorLocation();

        // projectile->ProjectileMovement->Velocity = {0, 0, 0};
        projectile->K2_SetActorLocation({0, 0, 0}, false, NULL, false);

        struct functions::PingDelayProjectile* p = new struct functions::PingDelayProjectile;

        p->original_velocity = projectile_velocity;
        p->original_location = projectile_location;

        p->projectile = projectile;
        Server::timers.push_back(TimerWithFunctionAndData(50.0 / 1000.0, functions::PingDelayProjectile, p));
    }
}

PROCESSEVENT_HOOK_FUNCTION(GetVisibility_1) {
    UWBP_LoadingScreen_C* loading_screen_object = (UWBP_LoadingScreen_C*)object;
    int complete_visibility = (int)(loading_screen_object->Get_LoadingComplete_Visibility_1());
    if (complete_visibility == (int)(UMG_ESlateVisibility::ESlateVisibility__Visible)) {
        UMAGameInstance* game_instance = (UMAGameInstance*)ue::uobjects::ugameplaystatics->STATIC_GetGameInstance(game_data::world);
        if (game_instance) {
            game_instance->HideLoadingScreen();
        }
    }
}

/*
 * When a player joins the server
 */
PROCESSEVENT_HOOK_FUNCTION(K2_PostLogin) {
    cout << "K2_PostLogin" << endl;

    AGameModeBase_K2_PostLogin_Params* p = (AGameModeBase_K2_PostLogin_Params*)params;
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)p->NewPlayer;
    AMAGameMode* game_mode = (AMAGameMode*)object;
    game_mode->bStartPlayersAsSpectators = false;

    if (player_controller) {
        if (player_controller == game_data::local_player_controller) {
            cout << "Reloading config DUE to client-server login" << endl;
            Server::ServerSettings.Reload();
            Server::SetIngameSettings();

            //((AMAPlayerState*)player_controller->PlayerState)->bIsAdmin = true;
        }

        if (game_mode->DefaultLoadout.Weapons.Data)
            player_controller->ServerSetLoadout(game_mode->DefaultLoadout);

        // Disable their chat limiting features
        player_controller->MaxMessageRate = 1E5;
        player_controller->SpamCooloffTime = 0;
        player_controller->RemainingSpamCooloff = 0;
        player_controller->ExpressionRateCost = 0;
        player_controller->ChatRateCost = 0;

        // Set the server name on the new client
        string& server_name_string = Server::ServerSettings.ServerName;
        wstring server_name_wstring = wstring(server_name_string.begin(), server_name_string.end());
        player_controller->SetServerName(FString(server_name_wstring.c_str()));

        // Set the players server sided name if its above 20 characters
        if (player_controller->PlayerState) {
            wstring player_name = player_controller->PlayerState->PlayerName.ToWString();
            if (player_name.length() >= 20) {
                player_controller->ServerChangeName(FString(wstring(L"Player").append(to_wstring(player_controller->PlayerState->PlayerId).c_str()).c_str()));
            }
        }

        // game_mode->DefaultLoadout.Weapons[0] = UWeaponInfo_RingLauncher_C::StaticClass();
        // game_mode->DefaultLoadout.Weapons[1] = UWeaponInfo_Chaingun_C::StaticClass();
        // game_mode->DefaultLoadout.Weapons[2] = UWeaponInfo_GrenadeLauncher_C::StaticClass();

        /*
        UClass** wep_skins = new UClass*[3];
        wep_skins[0] = AWSkin_Tempest_Gold_C::StaticClass();
        wep_skins[1] = AWSkin_Chaingun_Demonic_C::StaticClass();
        wep_skins[2] = AWSkin_GrenadeLauncher_Rainbow_C::StaticClass();

        game_mode->DefaultLoadout.WeaponSkins.Data = wep_skins;
        game_mode->DefaultLoadout.WeaponSkins.Count = 3;
        */

        // game_mode->DefaultLoadout.WeaponSkins[0] = AWSkin_Tempest_Gold_C::StaticClass();

        if (!player_controller->ControlledCharacter) {
            functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
            p->player_controller = player_controller;
            functions::RespawnPlayer(p);
            // Respawn all new players that are not the client (client spawns by default)
        } else {
            // player_controller->ControlledCharacter->OnDied();
        }

        if (player_controller == game_data::local_player_controller) {
            if (Server::ServerSettings.ForceSelfToSpectator) {
                // player_controller->Spectate();
                player_controller->ServerSpectate();
            }

            wstring player_name = wstring(Server::ServerSettings.PlayerName.begin(), Server::ServerSettings.PlayerName.end());
            // player_controller->ServerChangeName(FString(player_name.c_str()));
        }
    }
}

PROCESSEVENT_HOOK_FUNCTION(ServerUpdateRTT) {
    AMAPlayerController* player_controller = (AMAPlayerController*)object;
    if (player_controller != game_data::local_player_controller || true) {  // Any player that isnt us needs their mesh made visible
        if (player_controller->ControlledCharacter) {
            player_controller->ControlledCharacter->Mesh->SetVisibility(true, true);
            player_controller->ControlledCharacter->Mesh1P->SetVisibility(false, true);
            if (player_controller->ControlledCharacter->WeaponAttachment) {
                player_controller->ControlledCharacter->WeaponAttachment->Mesh1P->SetVisibility(false, true);
                player_controller->ControlledCharacter->WeaponAttachment->Mesh3P->SetVisibility(true, true);
            }
        }
    }
}

PROCESSEVENT_HOOK_FUNCTION(ReadyToEndMatch) {
    // cout << "ReadyToEndMatch" << endl;
    if (!Server::match_started) {
        static TArray<AActor*> out;
        ue::uobjects::ugameplaystatics->STATIC_GetAllActorsOfClass(game_data::world, AController::StaticClass(), &out);
        for (int i = 0; i < out.Num(); i++) {
            AController* c = (AController*)out[i];
            if (c && c->IsPlayerController()) {
                AMAPlayerController* player_controller = (AMAPlayerController*)c;
                if (player_controller->IsA(ABP_PlayerController_Bot_C::StaticClass()) || !player_controller->PlayerState) {
                    out[i] = NULL;
                    continue;
                }
                functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
                p->player_controller = player_controller;
                functions::RespawnPlayer(p);
                // Server::timers.push_back(TimerWithFunctionAndData(Server::server_settings.game_settings.respawnDelay * 0 + Server::match_start_delay, functions::RespawnPlayer, p));
            }
        }
        Server::match_started = true;
    }
}

PROCESSEVENT_HOOK_FUNCTION(MACharacterOnDied) {
    cout << "OnDied" << endl;
    // The magic happens here
    ABP_BaseCharacter_C* victim_character = (ABP_BaseCharacter_C*)object;
    if (!victim_character || victim_character->IsA(ABP_LightCharacter_Bot_C::StaticClass()) || !victim_character->PlayerState || victim_character->PlayerState->bIsSpectator) {
        hooks::original_processevent(object, function, params);
        return;
    }

    ABP_PlayerController_C* victim_controller = (ABP_PlayerController_C*)victim_character->Controller;  // Store the controller
    APawn* victim_pawn = victim_controller->Pawn;
    hooks::original_processevent(object, function, params);  // This destroys the character and sets its controller to NULL
    if (!victim_controller->ControlledCharacter) {           // Make sure the character is destroyed
        functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
        p->player_controller = victim_controller;  // Use the controller we saved
        Server::timers.push_back(TimerWithFunctionAndData(Server::ServerSettings.GameMode.MinRespawnDelay*1000, functions::RespawnPlayer, p));
    }

    //((AMAGameMode*)game_data::world->AuthorityGameMode)->RestartPlayer(victim_controller);

    return;
}

PROCESSEVENT_HOOK_FUNCTION(DamageImpactedActor) {}

PROCESSEVENT_HOOK_FUNCTION(ReceiveBeginPlay) {
    cout << "ReceiveBeginPlay" << endl;
    ABP_LightCharacter_C* character = (ABP_LightCharacter_C*)object;
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)character->Controller;

    if (character && character->CharacterMovement && character->MACharacterMovement) {
        UCharacterMovementComponent* character_movement_component = character->CharacterMovement;
        character_movement_component->GravityScale = Server::ServerSettings.CharacterMovement.GravityScale;
        character_movement_component->MaxStepHeight = Server::ServerSettings.CharacterMovement.MaxStepHeight;
        character_movement_component->JumpZVelocity = Server::ServerSettings.CharacterMovement.JumpZVelocity;
        character_movement_component->JumpOffJumpZFactor = Server::ServerSettings.CharacterMovement.JumpOffJumpZFactor;
        character_movement_component->WalkableFloorAngle = Server::ServerSettings.CharacterMovement.WalkableFloorAngle;
        character_movement_component->WalkableFloorZ = Server::ServerSettings.CharacterMovement.WalkableFloorZ;
        character_movement_component->GroundFriction = Server::ServerSettings.CharacterMovement.GroundFriction;
        character_movement_component->MaxWalkSpeed = Server::ServerSettings.CharacterMovement.MaxWalkSpeed;
        character_movement_component->MaxWalkSpeedCrouched = Server::ServerSettings.CharacterMovement.MaxWalkSpeedCrouched;
        character_movement_component->MaxSwimSpeed = Server::ServerSettings.CharacterMovement.MaxSwimSpeed;
        character_movement_component->MaxFlySpeed = Server::ServerSettings.CharacterMovement.MaxFlySpeed;
        character_movement_component->MaxCustomMovementSpeed = Server::ServerSettings.CharacterMovement.MaxCustomMovementSpeed;
        character_movement_component->MaxAcceleration = Server::ServerSettings.CharacterMovement.MaxAcceleration;
        character_movement_component->MinAnalogWalkSpeed = Server::ServerSettings.CharacterMovement.MinAnalogWalkSpeed;
        character_movement_component->BrakingFrictionFactor = Server::ServerSettings.CharacterMovement.BrakingFrictionFactor;
        character_movement_component->BrakingFriction = Server::ServerSettings.CharacterMovement.BrakingFriction;
        character_movement_component->BrakingSubStepTime = Server::ServerSettings.CharacterMovement.BrakingSubStepTime;
        character_movement_component->BrakingDecelerationWalking = Server::ServerSettings.CharacterMovement.BrakingDecelerationWalking;
        character_movement_component->BrakingDecelerationFalling = Server::ServerSettings.CharacterMovement.BrakingDecelerationFalling;
        character_movement_component->BrakingDecelerationSwimming = Server::ServerSettings.CharacterMovement.BrakingDecelerationSwimming;
        character_movement_component->BrakingDecelerationFlying = Server::ServerSettings.CharacterMovement.BrakingDecelerationFlying;
        character_movement_component->AirControl = Server::ServerSettings.CharacterMovement.AirControl;
        character_movement_component->AirControlBoostMultiplier = Server::ServerSettings.CharacterMovement.AirControlBoostMultiplier;
        character_movement_component->AirControlBoostVelocityThreshold = Server::ServerSettings.CharacterMovement.AirControlBoostVelocityThreshold;
        character_movement_component->FallingLateralFriction = Server::ServerSettings.CharacterMovement.FallingLateralFriction;
        character_movement_component->CrouchedHalfHeight = Server::ServerSettings.CharacterMovement.CrouchedHalfHeight;
        character_movement_component->Buoyancy = Server::ServerSettings.CharacterMovement.Buoyancy;
        character_movement_component->PerchRadiusThreshold = Server::ServerSettings.CharacterMovement.PerchRadiusThreshold;
        character_movement_component->PerchAdditionalHeight = Server::ServerSettings.CharacterMovement.PerchAdditionalHeight;
        character_movement_component->bUseSeparateBrakingFriction = Server::ServerSettings.CharacterMovement.bUseSeparateBrakingFriction;
        character_movement_component->bApplyGravityWhileJumping = Server::ServerSettings.CharacterMovement.bApplyGravityWhileJumping;
        character_movement_component->bUseControllerDesiredRotation = Server::ServerSettings.CharacterMovement.bUseControllerDesiredRotation;
        character_movement_component->bOrientRotationToMovement = Server::ServerSettings.CharacterMovement.bOrientRotationToMovement;
        character_movement_component->MaxOutOfWaterStepHeight = Server::ServerSettings.CharacterMovement.MaxOutOfWaterStepHeight;
        character_movement_component->OutofWaterZ = Server::ServerSettings.CharacterMovement.OutofWaterZ;
        character_movement_component->Mass = Server::ServerSettings.CharacterMovement.Mass;
        character_movement_component->StandingDownwardForceScale = Server::ServerSettings.CharacterMovement.StandingDownwardForceScale;
        character_movement_component->InitialPushForceFactor = Server::ServerSettings.CharacterMovement.InitialPushForceFactor;
        character_movement_component->PushForceFactor = Server::ServerSettings.CharacterMovement.PushForceFactor;
        character_movement_component->PushForcePointZOffsetFactor = Server::ServerSettings.CharacterMovement.PushForcePointZOffsetFactor;
        character_movement_component->TouchForceFactor = Server::ServerSettings.CharacterMovement.TouchForceFactor;
        character_movement_component->MinTouchForce = Server::ServerSettings.CharacterMovement.MinTouchForce;
        character_movement_component->MaxTouchForce = Server::ServerSettings.CharacterMovement.MaxTouchForce;
        character_movement_component->RepulsionForce = Server::ServerSettings.CharacterMovement.RepulsionForce;

        UMACharacterMovement* uma_character_movement = (UMACharacterMovement*)character->MACharacterMovement;

        uma_character_movement->JetAcceleration = Server::ServerSettings.MACharacterMovement.JetAcceleration;
        uma_character_movement->MaxJetLateralSpeed = Server::ServerSettings.MACharacterMovement.MaxJetLateralSpeed;
        uma_character_movement->MaxJetLateralPercent = Server::ServerSettings.MACharacterMovement.MaxJetLateralPercent;
        uma_character_movement->MaxJetVerticalSpeedStart = Server::ServerSettings.MACharacterMovement.MaxJetVerticalSpeedStart;
        uma_character_movement->MaxJetVerticalSpeedEnd = Server::ServerSettings.MACharacterMovement.MaxJetVerticalSpeedEnd;
        uma_character_movement->UpwardJetBonusMaxSpeed = Server::ServerSettings.MACharacterMovement.UpwardJetBonusMaxSpeed;
        uma_character_movement->MaxUpwardJetBonus = Server::ServerSettings.MACharacterMovement.MaxUpwardJetBonus;
        uma_character_movement->UpwardJetBonusRegenRate = Server::ServerSettings.MACharacterMovement.UpwardJetBonusRegenRate;
        uma_character_movement->UpwardJetBonusWalkingBonusRegenRate = Server::ServerSettings.MACharacterMovement.UpwardJetBonusWalkingBonusRegenRate;
        uma_character_movement->UpwardJetBonusRegenWaitTime = Server::ServerSettings.MACharacterMovement.UpwardJetBonusRegenWaitTime;
        uma_character_movement->UpwardJetBonusBurnRate = Server::ServerSettings.MACharacterMovement.UpwardJetBonusBurnRate;
        uma_character_movement->UpwardJetBurnPower = Server::ServerSettings.MACharacterMovement.UpwardJetBurnPower;
        uma_character_movement->bSeparateSkateJumpVelocity = Server::ServerSettings.MACharacterMovement.bSeparateSkateJumpVelocity;
        uma_character_movement->SkateJumpZVelocity = Server::ServerSettings.MACharacterMovement.SkateJumpZVelocity;
        uma_character_movement->JetModifier = Server::ServerSettings.MACharacterMovement.JetModifier;
        uma_character_movement->MaxFallingLateralSpeed = Server::ServerSettings.MACharacterMovement.MaxFallingLateralSpeed;
        uma_character_movement->UpwardsDamping = Server::ServerSettings.MACharacterMovement.UpwardsDamping;
        uma_character_movement->UpwardsDampingSpeed = Server::ServerSettings.MACharacterMovement.UpwardsDampingSpeed;
        uma_character_movement->MaxUpwardsSpeed = Server::ServerSettings.MACharacterMovement.MaxUpwardsSpeed;
        uma_character_movement->UpwardJetBonusEnergy = Server::ServerSettings.MACharacterMovement.UpwardJetBonusEnergy;
    }

    if (character && character->Vitals) {
        character->Vitals->HealthMax = Server::ServerSettings.Vitals.HealthMax;
        character->Vitals->LowHealthPct = Server::ServerSettings.Vitals.LowHealthPct;
        character->Vitals->bRegenHealth = Server::ServerSettings.Vitals.bRegenHealth;
        character->Vitals->HealthRegenRate = Server::ServerSettings.Vitals.HealthRegenRate;
        character->Vitals->EnergyMax = Server::ServerSettings.Vitals.EnergyMax;
        character->Vitals->EnergyRechargeRate = Server::ServerSettings.Vitals.EnergyRechargeRate;
        character->Vitals->EnergyRegenDisableThreshold = Server::ServerSettings.Vitals.EnergyRegenDisableThreshold;
        character->Vitals->EnergyRegenDisableDuration = Server::ServerSettings.Vitals.EnergyRegenDisableDuration;
        character->Vitals->bEnergyDamageBypassShield = Server::ServerSettings.Vitals.bEnergyDamageBypassShield;

        character->Vitals->Health = character->Vitals->HealthMax;
        character->Vitals->Energy = character->Vitals->EnergyMax;
    }
}

PROCESSEVENT_HOOK_FUNCTION(ServerSay) {
    // static bool absorb = false;
    AMAPlayerControllerBase_ServerSay_Params* p = (AMAPlayerControllerBase_ServerSay_Params*)params;
    AMAPlayerControllerBase* pc_base_ = (AMAPlayerControllerBase*)object;
    ABP_PlayerController_C* pc_base = (ABP_PlayerController_C*)object;

    wstring str = p->Message.ToWString();
    transform(str.begin(), str.end(), str.begin(), towlower);
    if (str.rfind(L"/", 0) == 0) {
        if (str.size() > wcslen(L"/setname ") && /* pc_base->PlayerState->PlayerName.ToWString().rfind(L"Player", 0) == 0 &&*/ str.rfind(L"/setname ", 0) == 0) {
            wstring name(p->Message.cw_str() + wcslen(L"/setname "));
            wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" changed name to ").append(name);
            hooks::original_processevent(object, function, params);
            game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
            pc_base->ServerChangeName(FString(name.c_str()));
        }

        if (str.size() > wcslen(L"/changemap ") && /* pc_base->PlayerState->PlayerName.ToWString().rfind(L"Player", 0) == 0 &&*/ str.rfind(L"/changemap ", 0) == 0) {
            wstring name(p->Message.cw_str() + wcslen(L"/changemap "));
            // wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" changed name to ").append(name);
            hooks::original_processevent(object, function, params);
            game_data::local_player_controller->ChangeMap(FString(name.c_str()));
        }

        if ((str.size() == wcslen(L"/reloadconfig") && str.rfind(L"/reloadconfig", 0) == 0) || (str.size() == wcslen(L"/rc") && str.rfind(L"/rc", 0) == 0)) {
            cout << "Reloading config DUE to force reload of config" << endl;
            Server::ServerSettings.Reload();
            Server::SetIngameSettings();

            static TArray<AActor*> out;
            ue::uobjects::ugameplaystatics->STATIC_GetAllActorsOfClass(game_data::world, AController::StaticClass(), &out);
            for (int i = 0; i < out.Num(); i++) {
                AController* c = (AController*)out[i];
                if (c && c->IsPlayerController()) {
                    AMAPlayerController* player_controller = (AMAPlayerController*)c;
                    if (player_controller->IsA(ABP_PlayerController_Bot_C::StaticClass()) || !player_controller->PlayerState) {
                        out[i] = NULL;
                        continue;
                    }

                    // functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
                    // p->player_controller = player_controller;
                    // functions::RespawnPlayer(p);

                    if (player_controller->ControlledCharacter) {
                        // player_controller->ControlledCharacter->PlayerState->bIsSpectator = 1;
                        // player_controller->ControlledCharacter->PlayerState = NULL; // Prevent OnDied hook from respawning
                        // player_controller->Pawn->K2_DestroyActor();
                        player_controller->ControlledCharacter->OnDied();  // Call on died, respawn through that

                    } else {
                        // All the timers have been cleared, so readd a timer if the players character is dead
                        functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
                        p->player_controller = player_controller;
                        // functions::RespawnPlayer(p); // Instantly respawn here
                        Server::timers.push_back(TimerWithFunctionAndData(Server::ServerSettings.GameMode.MinRespawnDelay*1000, functions::RespawnPlayer, p));
                    }
                    //
                    // Server::timers.push_back(TimerWithFunctionAndData(1, functions::RespawnPlayer, p));
                    //}
                }
            }

            wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" reloaded the server config.");
            game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
        }

        if (str.rfind(L"/help", 0) == 0) {
            hooks::original_processevent(object, function, params);
            wstring message(L" /setname {name} : Set player name");
            game_data::local_player_controller->ServerSay(FString(message.c_str()), false);

            // game_data::local_player_controller->ServerSay(FString(L" /setname {name} : Set player name"), false);
            // game_data::local_player_controller->ServerSay(FString(L" /changemap {map name} : Change to map"), false);
            // game_data::local_player_controller->ServerSay(FString(L" /reloadconfig : Reload server config"), false);
            // game_data::local_player_controller->ServerSay(FString(L" /login : Login as admin"), false);
        }

        if (str.rfind(L"/login", 0) == 0) {
            wstring password(p->Message.cw_str() + wcslen(L"/login "));

            string& admin_password_string = Server::ServerSettings.AdminPassword;
            wstring admin_password_wstring = wstring(admin_password_string.begin(), admin_password_string.end());

            if (password == admin_password_wstring) {
                ((AMAPlayerState*)pc_base->PlayerState)->bIsAdmin = true;
                wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" has logged in as admin.");
                game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
            } else {
                wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" failed to login in as admin (Incorrect password).");
                game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
            }
        }

        if (str.rfind(L"/start", 0) == 0) {
            hooks::original_processevent(object, function, params);
            AMAGameMode* game_mode = ((AMAGameMode*)game_data::world->AuthorityGameMode);
            if (game_mode->bInWarmup || !Server::match_started) {
                game_mode->StartMatch();
                wstring message = pc_base->PlayerState->PlayerName.ToWString().append(L" force started the map.");
                game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
            } else {
                wstring message = wstring(L"Cannot force start match.");
                game_data::local_player_controller->ServerSay(FString(message.c_str()), false);
            }
        }

        if (str.rfind(L"/spectate", 0) == 0) {
            // pc_base->Spectate();
            // pc_base->ServerSpectate();
            pc_base->PlayerState->bIsSpectator = 1;
        }
        //
    } else {
        hooks::original_processevent(object, function, params);
    }

    // if (!absorb)
    //    hooks::original_processevent(object, function, params);
}

PROCESSEVENT_HOOK_FUNCTION(ServerSpectatePre) {
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)object;
    if (player_controller == game_data::local_player_controller) {
        if (player_controller->LastControlledCharacter) {
            // Switch to spectate from live game play
            player_controller->PlayerState->bIsSpectator = 1;  // Set this to 1 so when OnDied is called, it knows not to call respawn
            // player_controller->Spectate();
            player_controller->LastControlledCharacter->OnDied();
            player_controller->LastControlledCharacter = NULL;
        } else {
            player_controller->PlayerState->bIsSpectator = 0;
            functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
            p->player_controller = player_controller;
            functions::RespawnPlayer(p);
            // functions::RespawnPlayer(p);
        }
    }
}

PROCESSEVENT_HOOK_FUNCTION(ServerSpectatePost) {
    ABP_PlayerController_C* player_controller = (ABP_PlayerController_C*)object;
    if (player_controller != game_data::local_player_controller) {
        if (player_controller->LastControlledCharacter) {
            // Switch to spectate from live game play
            player_controller->PlayerState->bIsSpectator = 1;  // Set this to 1 so when OnDied is called, it knows not to call respawn
            // player_controller->Spectate();
            player_controller->LastControlledCharacter->OnDied();
            player_controller->LastControlledCharacter = NULL;
        } else {
            player_controller->PlayerState->bIsSpectator = 0;
            functions::RespawnPlayerParams* p = new functions::RespawnPlayerParams;
            p->player_controller = player_controller;
            functions::RespawnPlayer(p);
            // functions::RespawnPlayer(p);
        }
    }
}

PROCESSEVENT_HOOK_FUNCTION(ReceiveTick) {
    ABP_LightCharacter_C* character = (ABP_LightCharacter_C*)object;
    if (character && character->Weapon) {
        if (character->Weapon->IsA(ABP_PlasmaGun_C::StaticClass())) {
            character->Weapon->ProjectileClass = ABP_Railgun_Bolt_C::StaticClass();
        } else if (character->Weapon->IsA(ABP_RingLauncher_C::StaticClass())) {
            ABP_RingLauncher_C* ring_launcher = (ABP_RingLauncher_C*)character->Weapon;
            static UClass* skin = AWSkin_Tempest_Gold_C::StaticClass();
            ring_launcher->SkinClass = skin;
        }
    }
}

PROCESSEVENT_HOOK_FUNCTION(K2_GivenTo) {
    AMAItem* item = (AMAItem*)object;
    if (item->GetInstigatorController() == game_data::local_player_controller) {
        ((AMAPlayerState*)item->GetInstigatorController()->PlayerState)->bIsAdmin = true;
    }

    if (item->IsA(ABP_RingLauncher_C::StaticClass())) {
        ABP_RingLauncher_C* ring_launcher = (ABP_RingLauncher_C*)item;
        static UClass* skin = AWSkin_Tempest_Gold_C::StaticClass();
        ring_launcher->SkinClass = skin;

        ring_launcher->StoredAmmo = Server::ServerSettings.RingLauncher.StoredAmmo;
        ring_launcher->LoadedAmmo = Server::ServerSettings.RingLauncher.LoadedAmmo;
        ring_launcher->FireSpeed = Server::ServerSettings.RingLauncher.FireSpeed;
        ring_launcher->InitialAmmo = Server::ServerSettings.RingLauncher.InitialAmmo;
        ring_launcher->MaxAmmo = Server::ServerSettings.RingLauncher.MaxAmmo;
        ring_launcher->TakeOutTime = Server::ServerSettings.RingLauncher.TakeOutTime;
        ring_launcher->ProjectilesPerShot = Server::ServerSettings.RingLauncher.ProjectilesPerShot;
        ring_launcher->FireIntervalFrom = Server::ServerSettings.RingLauncher.FireIntervalFrom;
        ring_launcher->FireIntervalTo = Server::ServerSettings.RingLauncher.FireIntervalTo;
        ring_launcher->FireAccelerationTime = Server::ServerSettings.RingLauncher.FireAccelerationTime;
        ring_launcher->FireDecelerationTime = Server::ServerSettings.RingLauncher.FireDecelerationTime;
        ring_launcher->bLockFireInterval = Server::ServerSettings.RingLauncher.bLockFireInterval;
        ring_launcher->BurstSize = Server::ServerSettings.RingLauncher.BurstSize;
        ring_launcher->AmmoCost = Server::ServerSettings.RingLauncher.AmmoCost;
        ring_launcher->EnergyCost = Server::ServerSettings.RingLauncher.EnergyCost;
        ring_launcher->SoftRecoveryWindow = Server::ServerSettings.RingLauncher.SoftRecoveryWindow;
        ring_launcher->DryFireTime = Server::ServerSettings.RingLauncher.DryFireTime;
        ring_launcher->bClearTriggerAfterRecovery = Server::ServerSettings.RingLauncher.bClearTriggerAfterRecovery;
        ring_launcher->bClearTriggerWhenAbsolutelyEmpty = Server::ServerSettings.RingLauncher.bClearTriggerWhenAbsolutelyEmpty;
        ring_launcher->bChargeBeforeFiring = Server::ServerSettings.RingLauncher.bChargeBeforeFiring;
        ring_launcher->BloomAngle = Server::ServerSettings.RingLauncher.BloomAngle;
        ring_launcher->BloomPatternRegions = Server::ServerSettings.RingLauncher.BloomPatternRegions;
        ring_launcher->ChargeTime = Server::ServerSettings.RingLauncher.ChargeTime;
        ring_launcher->ChargeDissipationTime = Server::ServerSettings.RingLauncher.ChargeDissipationTime;
        ring_launcher->OverloadTime = Server::ServerSettings.RingLauncher.OverloadTime;
        ring_launcher->ReloadTime = Server::ServerSettings.RingLauncher.ReloadTime;
        ring_launcher->bReloadEveryRound = Server::ServerSettings.RingLauncher.bReloadEveryRound;
        ring_launcher->bWasteAmmoWhenReloaded = Server::ServerSettings.RingLauncher.bWasteAmmoWhenReloaded;
        ring_launcher->bAutoReloadOnEmpty = Server::ServerSettings.RingLauncher.bAutoReloadOnEmpty;
        ring_launcher->bFireCanInterruptReload = Server::ServerSettings.RingLauncher.bFireCanInterruptReload;
        ring_launcher->HeatPerShot = Server::ServerSettings.RingLauncher.HeatPerShot;
        ring_launcher->OverheatThreshold = Server::ServerSettings.RingLauncher.OverheatThreshold;
        ring_launcher->OverheatRecoveryThreshold = Server::ServerSettings.RingLauncher.OverheatRecoveryThreshold;
        ring_launcher->HeatLossPerSecond = Server::ServerSettings.RingLauncher.HeatLossPerSecond;
        ring_launcher->OverheatedHeatLossPerSecond = Server::ServerSettings.RingLauncher.OverheatedHeatLossPerSecond;

        ring_launcher->FireOffset.X = Server::ServerSettings.RingLauncher.FireOffset.X;
        ring_launcher->FireOffset.Y = Server::ServerSettings.RingLauncher.FireOffset.Y;
        ring_launcher->FireOffset.Z = Server::ServerSettings.RingLauncher.FireOffset.Z;
    } else if (item->IsA(ABP_GrenadeLauncher_C::StaticClass())) {
        ABP_GrenadeLauncher_C* grenade_launcher = (ABP_GrenadeLauncher_C*)item;

        grenade_launcher->StoredAmmo = Server::ServerSettings.GrenadeLauncher.StoredAmmo;
        grenade_launcher->LoadedAmmo = Server::ServerSettings.GrenadeLauncher.LoadedAmmo;
        grenade_launcher->FireSpeed = Server::ServerSettings.GrenadeLauncher.FireSpeed;
        grenade_launcher->InitialAmmo = Server::ServerSettings.GrenadeLauncher.InitialAmmo;
        grenade_launcher->MaxAmmo = Server::ServerSettings.GrenadeLauncher.MaxAmmo;
        grenade_launcher->TakeOutTime = Server::ServerSettings.GrenadeLauncher.TakeOutTime;
        grenade_launcher->ProjectilesPerShot = Server::ServerSettings.GrenadeLauncher.ProjectilesPerShot;
        grenade_launcher->FireIntervalFrom = Server::ServerSettings.GrenadeLauncher.FireIntervalFrom;
        grenade_launcher->FireIntervalTo = Server::ServerSettings.GrenadeLauncher.FireIntervalTo;
        grenade_launcher->FireAccelerationTime = Server::ServerSettings.GrenadeLauncher.FireAccelerationTime;
        grenade_launcher->FireDecelerationTime = Server::ServerSettings.GrenadeLauncher.FireDecelerationTime;
        grenade_launcher->bLockFireInterval = Server::ServerSettings.GrenadeLauncher.bLockFireInterval;
        grenade_launcher->BurstSize = Server::ServerSettings.GrenadeLauncher.BurstSize;
        grenade_launcher->AmmoCost = Server::ServerSettings.GrenadeLauncher.AmmoCost;
        grenade_launcher->EnergyCost = Server::ServerSettings.GrenadeLauncher.EnergyCost;
        grenade_launcher->SoftRecoveryWindow = Server::ServerSettings.GrenadeLauncher.SoftRecoveryWindow;
        grenade_launcher->DryFireTime = Server::ServerSettings.GrenadeLauncher.DryFireTime;
        grenade_launcher->bClearTriggerAfterRecovery = Server::ServerSettings.GrenadeLauncher.bClearTriggerAfterRecovery;
        grenade_launcher->bClearTriggerWhenAbsolutelyEmpty = Server::ServerSettings.GrenadeLauncher.bClearTriggerWhenAbsolutelyEmpty;
        grenade_launcher->bChargeBeforeFiring = Server::ServerSettings.GrenadeLauncher.bChargeBeforeFiring;
        grenade_launcher->BloomAngle = Server::ServerSettings.GrenadeLauncher.BloomAngle;
        grenade_launcher->BloomPatternRegions = Server::ServerSettings.GrenadeLauncher.BloomPatternRegions;
        grenade_launcher->ChargeTime = Server::ServerSettings.GrenadeLauncher.ChargeTime;
        grenade_launcher->ChargeDissipationTime = Server::ServerSettings.GrenadeLauncher.ChargeDissipationTime;
        grenade_launcher->OverloadTime = Server::ServerSettings.GrenadeLauncher.OverloadTime;
        grenade_launcher->ReloadTime = Server::ServerSettings.GrenadeLauncher.ReloadTime;
        grenade_launcher->bReloadEveryRound = Server::ServerSettings.GrenadeLauncher.bReloadEveryRound;
        grenade_launcher->bWasteAmmoWhenReloaded = Server::ServerSettings.GrenadeLauncher.bWasteAmmoWhenReloaded;
        grenade_launcher->bAutoReloadOnEmpty = Server::ServerSettings.GrenadeLauncher.bAutoReloadOnEmpty;
        grenade_launcher->bFireCanInterruptReload = Server::ServerSettings.GrenadeLauncher.bFireCanInterruptReload;
        grenade_launcher->HeatPerShot = Server::ServerSettings.GrenadeLauncher.HeatPerShot;
        grenade_launcher->OverheatThreshold = Server::ServerSettings.GrenadeLauncher.OverheatThreshold;
        grenade_launcher->OverheatRecoveryThreshold = Server::ServerSettings.GrenadeLauncher.OverheatRecoveryThreshold;
        grenade_launcher->HeatLossPerSecond = Server::ServerSettings.GrenadeLauncher.HeatLossPerSecond;
        grenade_launcher->OverheatedHeatLossPerSecond = Server::ServerSettings.GrenadeLauncher.OverheatedHeatLossPerSecond;

        grenade_launcher->FireOffset.X = Server::ServerSettings.GrenadeLauncher.FireOffset.X;
        grenade_launcher->FireOffset.Y = Server::ServerSettings.GrenadeLauncher.FireOffset.Y;
        grenade_launcher->FireOffset.Z = Server::ServerSettings.GrenadeLauncher.FireOffset.Z;
    } else if (item->IsA(ABP_Chaingun_C::StaticClass())) {
        ABP_Chaingun_C* chaingun = (ABP_Chaingun_C*)item;

        chaingun->StoredAmmo = Server::ServerSettings.Chaingun.StoredAmmo;
        chaingun->LoadedAmmo = Server::ServerSettings.Chaingun.LoadedAmmo;
        chaingun->FireSpeed = Server::ServerSettings.Chaingun.FireSpeed;
        chaingun->InitialAmmo = Server::ServerSettings.Chaingun.InitialAmmo;
        chaingun->MaxAmmo = Server::ServerSettings.Chaingun.MaxAmmo;
        chaingun->TakeOutTime = Server::ServerSettings.Chaingun.TakeOutTime;
        chaingun->ProjectilesPerShot = Server::ServerSettings.Chaingun.ProjectilesPerShot;
        chaingun->FireIntervalFrom = Server::ServerSettings.Chaingun.FireIntervalFrom;
        chaingun->FireIntervalTo = Server::ServerSettings.Chaingun.FireIntervalTo;
        chaingun->FireAccelerationTime = Server::ServerSettings.Chaingun.FireAccelerationTime;
        chaingun->FireDecelerationTime = Server::ServerSettings.Chaingun.FireDecelerationTime;
        chaingun->bLockFireInterval = Server::ServerSettings.Chaingun.bLockFireInterval;
        chaingun->BurstSize = Server::ServerSettings.Chaingun.BurstSize;
        chaingun->AmmoCost = Server::ServerSettings.Chaingun.AmmoCost;
        chaingun->EnergyCost = Server::ServerSettings.Chaingun.EnergyCost;
        chaingun->SoftRecoveryWindow = Server::ServerSettings.Chaingun.SoftRecoveryWindow;
        chaingun->DryFireTime = Server::ServerSettings.Chaingun.DryFireTime;
        chaingun->bClearTriggerAfterRecovery = Server::ServerSettings.Chaingun.bClearTriggerAfterRecovery;
        chaingun->bClearTriggerWhenAbsolutelyEmpty = Server::ServerSettings.Chaingun.bClearTriggerWhenAbsolutelyEmpty;
        chaingun->bChargeBeforeFiring = Server::ServerSettings.Chaingun.bChargeBeforeFiring;
        chaingun->BloomAngle = Server::ServerSettings.Chaingun.BloomAngle;
        chaingun->BloomPatternRegions = Server::ServerSettings.Chaingun.BloomPatternRegions;
        chaingun->ChargeTime = Server::ServerSettings.Chaingun.ChargeTime;
        chaingun->ChargeDissipationTime = Server::ServerSettings.Chaingun.ChargeDissipationTime;
        chaingun->OverloadTime = Server::ServerSettings.Chaingun.OverloadTime;
        chaingun->ReloadTime = Server::ServerSettings.Chaingun.ReloadTime;
        chaingun->bReloadEveryRound = Server::ServerSettings.Chaingun.bReloadEveryRound;
        chaingun->bWasteAmmoWhenReloaded = Server::ServerSettings.Chaingun.bWasteAmmoWhenReloaded;
        chaingun->bAutoReloadOnEmpty = Server::ServerSettings.Chaingun.bAutoReloadOnEmpty;
        chaingun->bFireCanInterruptReload = Server::ServerSettings.Chaingun.bFireCanInterruptReload;
        chaingun->HeatPerShot = Server::ServerSettings.Chaingun.HeatPerShot;
        chaingun->OverheatThreshold = Server::ServerSettings.Chaingun.OverheatThreshold;
        chaingun->OverheatRecoveryThreshold = Server::ServerSettings.Chaingun.OverheatRecoveryThreshold;
        chaingun->HeatLossPerSecond = Server::ServerSettings.Chaingun.HeatLossPerSecond;
        chaingun->OverheatedHeatLossPerSecond = Server::ServerSettings.Chaingun.OverheatedHeatLossPerSecond;

        chaingun->FireOffset.X = Server::ServerSettings.Chaingun.FireOffset.X;
        chaingun->FireOffset.Y = Server::ServerSettings.Chaingun.FireOffset.Y;
        chaingun->FireOffset.Z = Server::ServerSettings.Chaingun.FireOffset.Z;
    }
}

void HookUnrealEngine4(void) {
    base_address = (DWORD64)GetModuleHandle(0);

    InitSdk();

    {
        using namespace hooks;

        /*
        Chat Messages
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAPlayerControllerBase.ServerSay"), {ServerSay, HookType::kPre, true});

        // The magic happens here
        /*
        When a player dies, restart the player
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MACharacter.OnDied"), {MACharacterOnDied, HookType::kPre, true});

        /*
         * When warm up ends this gets called. This is used to respawn the players after the switch from warmup to game start.
         */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function Engine.GameMode.ReadyToEndMatch"), {ReadyToEndMatch, HookType::kPost, false});

        /*
        Automatically close the loading screen on map load
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function WBP_LoadingScreen.WBP_LoadingScreen_C.GetVisibility_1"), {GetVisibility_1, HookType::kPost, false});  // Automatically close loading screen

        /*
        Set mesh visibility on the server-client
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAPlayerController.ServerUpdateRTT"), {ServerUpdateRTT, HookType::kPre, false});

        /*
         * When someone joins the server, they need to be manually spawned.
         */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function Engine.GameModeBase.K2_PostLogin"), {K2_PostLogin, HookType::kPost, true});

        /*
        When a projectile is spawned
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAProjectile.OnRep_TeamID"), {OnRep_TeamID, HookType::kPost, false});

        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAProjectile.DamageImpactedActor"), {DamageImpactedActor, HookType::kPre, false});

        /*
        When a player character has respawned
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function BP_LightCharacter.BP_LightCharacter_C.ReceiveBeginPlay"), {ReceiveBeginPlay, HookType::kPost, false});

        /*
        Skin changing
        */
        // AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function BP_LightCharacter.BP_LightCharacter_C.ReceiveTick"), {ReceiveTick, HookType::kPre, false});

        /*
        Skin changer
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAItem.K2_GivenTo"), {K2_GivenTo, HookType::kPre, false});

        /*
        Function MidairCE.MAPlayerController.ServerSpectate
        */
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAPlayerController.ServerSpectate"), {ServerSpectatePre, HookType::kPre, false});
        AddProcessEventHook((UFunction*)UObject::FindObject<UFunction>("Function MidairCE.MAPlayerController.ServerSpectate"), {ServerSpectatePost, HookType::kPost, false});
    }

    {
        using namespace ue::uobjects;
        ugameplaystatics = (UGameplayStatics*)UObject::FindObject<UGameplayStatics>();
    }

    Server::ServerSettings.Reload();

    game_data::world = (UWorld*)(*(DWORD64*)(base_address + uworld_offset));
    game_data::local_player = ((ULocalPlayer*)game_data::world->OwningGameInstance->LocalPlayers[0]);
    game_data::local_player_controller = (AMAPlayerController*)game_data::local_player->PlayerController;

    // If not on a map then don't continue
    if ((!game_data::world->NetDriver || true) && game_data::local_player_controller) {
        static bool loaded_map = false;
        if (!loaded_map && game_data::local_player_controller) {
            loaded_map = true;
            wstring exec_ = wstring(Server::ServerSettings.ExecOnInject.begin(), Server::ServerSettings.ExecOnInject.end());
            ((ABP_PlayerController_C*)game_data::local_player_controller)->SendToConsole(FString(exec_.c_str()));
        }
    }

    DWORD64 processevent_address = base_address + processevent_offset;
    hooks::original_processevent = (hooks::_ProcessEvent)processevent_address;

    DetourTransactionBegin();
    DetourUpdateThread(GetCurrentThread());
    DetourAttach(&(PVOID&)hooks::original_processevent, hooks::ProcessEvent);
    DetourTransactionCommit();
}

}  // namespace ue

namespace hooks {

void ProcessEvent(UObject* object, UFunction* function, void* params) {

    UWorld* old_world = game_data::world;
    game_data::world = (UWorld*)(*(DWORD64*)(base_address + uworld_offset));

    if (!game_data::world) {
        original_processevent(object, function, params);
        game_data::local_player = NULL;
        game_data::local_player_controller = NULL;
        return;
    }

    if (game_data::world != old_world) {
        cout << "New world found" << endl;
         Server::match_started = false;
    }

    game_data::local_player = ((ULocalPlayer*)game_data::world->OwningGameInstance->LocalPlayers[0]);
    game_data::local_player_controller = (AMAPlayerController*)game_data::local_player->PlayerController;


    bool absorb = false;
    vector<_ProcessEvent> post_hooks;
    if (game_data::world && (game_data::world->NavigationSystem || game_data::world->NetDriver) && game_data::local_player_controller) {
        if (processevent_hooks.find(function) != processevent_hooks.end()) {
            vector<Hook<_ProcessEvent>>& hooks = processevent_hooks[function];
            for (vector<Hook<_ProcessEvent>>::iterator hook = hooks.begin(); hook != hooks.end(); hook++) {
                if (hook->hook_type_ == HookType::kPre) {
                    (*hook->hook_)(object, function, params);
                } else if (hook->hook_type_ == HookType::kPost) {
                    post_hooks.push_back(hook->hook_);
                }
                absorb = absorb || hook->absorb_;
            }
        }
    }

    if (!absorb) {
        original_processevent(object, function, params);
    }

    if (game_data::world && (game_data::world->NavigationSystem || game_data::world->NetDriver) && game_data::local_player_controller) {
        for (vector<_ProcessEvent>::iterator hook = post_hooks.begin(); hook != post_hooks.end(); hook++) {
            (*hook)(object, function, params);
        }
    }

    static bool lock = false;
    if (!lock && game_data::world && (game_data::world->NavigationSystem || game_data::world->NetDriver) && game_data::local_player_controller) {
        if (Server::server_timer.Tick()) {
            lock = true;
            for (list<TimerWithFunctionAndData>::iterator i = Server::timers.begin(); i != Server::timers.end();) {
                if (i->Tick())
                    i = Server::timers.erase(i);
                else
                    i++;
            }
            lock = false;
        }
    }
}
}  // namespace hooks