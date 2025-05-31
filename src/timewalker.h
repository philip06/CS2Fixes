/**
 * =============================================================================
 * CS2Fixes - Timewalker Gamemode
 * Copyright (C) 2023-2025 Source2ZE
 * =============================================================================
 */

#pragma once

#include "common.h"
#include "ctimer.h"
#include "entity/ccsplayercontroller.h"
#include "playermanager.h"

// Timewalker gamemode class
class CTimewalkerManager
{
public:
    CTimewalkerManager();
    ~CTimewalkerManager();

    void Init();
    void Reset();
    void OnRoundStart();
    void OnRoundEnd();
	void StartWarning();
	void EndFreeze();
    void OnPlayerDisconnect(CPlayerSlot slot);

    // Configuration
    void SetFreezeInterval(float interval) { m_flFreezeInterval = interval; }
    void SetFreezeDuration(float duration) { m_flFreezeDuration = duration; }
    void SetWarningDuration(float warning) { m_flWarningDuration = warning; }

    float GetFreezeInterval() const { return m_flFreezeInterval; }
    float GetFreezeDuration() const { return m_flFreezeDuration; }
    float GetWarningDuration() const { return m_flWarningDuration; }

    // State queries
    bool IsActive() const { return m_bActive; }
    bool IsFreezeActive() const { return m_bFreezeActive; }
    bool IsWarningActive() const { return m_bWarningActive; }
    int GetTimewalkerSlot() const { return m_iTimewalkerSlot; }

private:
    void StartFreeze();
    void SelectRandomTimewalker();
    void FreezeAllPlayers();
    void UnfreezeAllPlayers();
    void SendWarningMessage();
    void SendFreezeMessage();
    void SendUnfreezeMessage();
    bool IsValidTimewalker(int slot);

    // State variables
    bool m_bActive;
    bool m_bFreezeActive;
    bool m_bWarningActive;
    int m_iTimewalkerSlot;

    // Configuration
    float m_flFreezeInterval;    // Time between freezes
    float m_flFreezeDuration;    // How long freeze lasts
    float m_flWarningDuration;   // Warning time before freeze

    // Timers
    CTimer* m_pWarningTimer;
    CTimer* m_pFreezeTimer;
    CTimer* m_pUnfreezeTimer;
};

extern CTimewalkerManager* g_pTimewalkerManager;

// ConVars
extern CConVar<bool> g_cvarTimewalkerEnable;
extern CConVar<float> g_cvarTimewalkerInterval;
extern CConVar<float> g_cvarTimewalkerDuration;
extern CConVar<float> g_cvarTimewalkerWarning;