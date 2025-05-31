/**
 * =============================================================================
 * CS2Fixes - Timewalker Gamemode Implementation
 * Copyright (C) 2023-2025 Source2ZE
 * =============================================================================
 */

#include "timewalker.h"
#include "adminsystem.h"
#include "commands.h"
#include "ctimer.h"
#include "eventlistener.h"
#include "recipientfilters.h"
#include "tier0/vprof.h"

#include "tier0/memdbgon.h"

extern IVEngineServer2* g_pEngineServer2;
extern CGameEntitySystem* g_pEntitySystem;
extern CGlobalVars* GetGlobals();
extern CPlayerManager* g_playerManager;

// ConVars
CConVar<bool> g_cvarTimewalkerEnable("cs2f_timewalker_enable", FCVAR_NONE, "Enable the timewalker gamemode", false);
CConVar<float> g_cvarTimewalkerInterval("cs2f_timewalker_interval", FCVAR_NONE, "Time between timewalker freezes (seconds)", 45.0f, true, 10.0f, true, 300.0f);
CConVar<float> g_cvarTimewalkerDuration("cs2f_timewalker_duration", FCVAR_NONE, "Duration of timewalker freeze (seconds)", 10.0f, true, 1.0f, true, 60.0f);
CConVar<float> g_cvarTimewalkerWarning("cs2f_timewalker_warning", FCVAR_NONE, "Warning time before timewalker freeze (seconds)", 5.0f, true, 0.0f, true, 15.0f);

CTimewalkerManager* g_pTimewalkerManager = nullptr;

CTimewalkerManager::CTimewalkerManager()
{
    m_bActive = false;
    m_bFreezeActive = false;
    m_bWarningActive = false;
    m_iTimewalkerSlot = -1;

    m_flFreezeInterval = 45.0f;
    m_flFreezeDuration = 10.0f;
    m_flWarningDuration = 5.0f;

    m_pWarningTimer = nullptr;
    m_pFreezeTimer = nullptr;
    m_pUnfreezeTimer = nullptr;
}

CTimewalkerManager::~CTimewalkerManager()
{
    Reset();
}

void CTimewalkerManager::Init()
{
    if (!g_cvarTimewalkerEnable.Get())
        return;

	if (m_bActive) {
		Message("Timewalker already active, skipping init\n");
		return;
	}

    m_bActive = true;
    m_flFreezeInterval = g_cvarTimewalkerInterval.Get();
    m_flFreezeDuration = g_cvarTimewalkerDuration.Get();
    m_flWarningDuration = g_cvarTimewalkerWarning.Get();

    Message("Timewalker gamemode initialized - Interval: %.1fs, Duration: %.1fs, Warning: %.1fs\n",
            m_flFreezeInterval, m_flFreezeDuration, m_flWarningDuration);

    // Start the cycle
    float nextCycleTime = m_flFreezeInterval - m_flWarningDuration;
	m_pWarningTimer = new CTimer(nextCycleTime, false, true, [this]() {
		// Safety checks before each cycle
		if (!g_cvarTimewalkerEnable.Get() || !m_bActive)
		{
			Message("Timewalker disabled during cycle, stopping\n");
			return -1.0f; // Stop timer
		}

		if (!m_bWarningActive && !m_bFreezeActive)
			StartWarning();

		return m_flFreezeInterval;
	});
}

void CTimewalkerManager::Reset()
{
	Message("Timewalker Reset() called\n");

	m_bActive = false;
	m_bFreezeActive = false;
	m_bWarningActive = false;
	m_iTimewalkerSlot = -1;

	// Clean up timers safely - set pointers to null first to prevent race conditions
	CTimer* pWarningTimer = m_pWarningTimer;
	CTimer* pFreezeTimer = m_pFreezeTimer;
	CTimer* pUnfreezeTimer = m_pUnfreezeTimer;

	m_pWarningTimer = nullptr;
	m_pFreezeTimer = nullptr;
	m_pUnfreezeTimer = nullptr;

	// Now safely delete
	if (pWarningTimer) {
		delete pWarningTimer;
	}
	if (pFreezeTimer) {
		delete pFreezeTimer;
	}
	if (pUnfreezeTimer) {
		delete pUnfreezeTimer;
	}

	// Unfreeze everyone if we were in a freeze
	UnfreezeAllPlayers();
}

void CTimewalkerManager::OnPlayerDisconnect(CPlayerSlot slot)
{
    // If the timewalker disconnects during a freeze, select a new one
    if (m_bFreezeActive && slot.Get() == m_iTimewalkerSlot)
    {
        SelectRandomTimewalker();
        Message("Timewalker disconnected, selected new timewalker: %s\n",
                m_iTimewalkerSlot >= 0 ? CCSPlayerController::FromSlot(m_iTimewalkerSlot)->GetPlayerName() : "None");
    }
}

void CTimewalkerManager::StartWarning()
{
	if (!m_bActive || !g_cvarTimewalkerEnable.Get() || !g_pTimewalkerManager) {
		Message("StartWarning() called but conditions not met - ignoring\n");
		return;
	}

    m_bWarningActive = true;
    SendWarningMessage();

    Message("Timewalker warning phase started\n");

    // Schedule freeze start
    m_pFreezeTimer = new CTimer(m_flWarningDuration, false, false, [this]() {
    	if (!m_bActive || !g_cvarTimewalkerEnable.Get() || !g_pTimewalkerManager) {
			Message("Freeze timer: conditions not met, aborting\n");
			return -1.0f;
		}
        StartFreeze();
        return -1.0f;
    });
}

void CTimewalkerManager::StartFreeze()
{
	if (!m_bActive || !g_cvarTimewalkerEnable.Get() || !g_pTimewalkerManager) {
		Message("StartFreeze() called but conditions not met - ignoring\n");
		return;
	}

    m_bWarningActive = false;
    m_bFreezeActive = true;

    SelectRandomTimewalker();
    FreezeAllPlayers();
    SendFreezeMessage();

    Message("Timewalker freeze started - Timewalker: %s\n",
            m_iTimewalkerSlot >= 0 ? CCSPlayerController::FromSlot(m_iTimewalkerSlot)->GetPlayerName() : "None");

    // Schedule freeze end
    m_pUnfreezeTimer = new CTimer(m_flFreezeDuration, false, false, [this]() {
    	if (!g_pTimewalkerManager) {
			Message("Unfreeze timer: manager gone, aborting\n");
			return -1.0f;
		}
        EndFreeze();
        return -1.0f;
    });
}

void CTimewalkerManager::OnRoundStart()
{
	Reset();

	// Small delay before starting to let round settle
	new CTimer(3.0f, false, false, [this]() {
		Init();
		return -1.0f;
	});
}

void CTimewalkerManager::OnRoundEnd()
{
	Message("Timewalker OnRoundEnd() called\n");

	m_bFreezeActive = false;
	m_bWarningActive = false;
	m_iTimewalkerSlot = -1;

	// Clean up temporary timers but keep main cycling timer
	if (m_pFreezeTimer) {
		delete m_pFreezeTimer;
		m_pFreezeTimer = nullptr;
	}
	if (m_pUnfreezeTimer) {
		delete m_pUnfreezeTimer;
		m_pUnfreezeTimer = nullptr;
	}

	UnfreezeAllPlayers();
}

void CTimewalkerManager::EndFreeze()
{
    m_bFreezeActive = false;
    m_iTimewalkerSlot = -1;

    UnfreezeAllPlayers();
    SendUnfreezeMessage();

    Message("Timewalker freeze ended\n");
}

void CTimewalkerManager::SelectRandomTimewalker()
{
    if (!GetGlobals())
        return;

    CUtlVector<int> validPlayers;

    for (int i = 0; i < GetGlobals()->maxClients; i++)
    {
        if (IsValidTimewalker(i))
            validPlayers.AddToTail(i);
    }

    if (validPlayers.Count() == 0)
    {
        m_iTimewalkerSlot = -1;
        return;
    }

    int randomIndex = rand() % validPlayers.Count();
    m_iTimewalkerSlot = validPlayers[randomIndex];
}

void CTimewalkerManager::FreezeAllPlayers()
{
	if (!GetGlobals())
		return;

	for (int i = 0; i < GetGlobals()->maxClients; i++)
	{
		if (i == m_iTimewalkerSlot || !IsValidTimewalker(i))
			continue;

		CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
		if (!pController)
			continue;

		CCSPlayerPawn* pPawn = pController->GetPlayerPawn();
		if (!pPawn)
			continue;

		// Freeze the player using the same method as CS2Fixes
		ZEPlayer* pPlayer = pController->GetZEPlayer();
		if (pPlayer)
		{
			// Store original speed for restoration
			if (pPlayer->GetMaxSpeed() > 0.01f)
				pPlayer->SetStoredMaxSpeed(pPlayer->GetMaxSpeed());
			else
				pPlayer->SetStoredMaxSpeed(1.0f);

			// Freeze movement
			pPlayer->SetMaxSpeed(0.0f);
			pPlayer->SetSpeedMod(0.0f);
		}

		// Disable movement through velocity
		pPawn->m_vecAbsVelocity = Vector(0, 0, 0);
		pPawn->m_vecBaseVelocity = Vector(0, 0, 0);
	}
}

void CTimewalkerManager::UnfreezeAllPlayers()
{
	if (!GetGlobals())
		return;

	for (int i = 0; i < GetGlobals()->maxClients; i++)
	{
		CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
		if (!pController)
			continue;

		CCSPlayerPawn* pPawn = pController->GetPlayerPawn();
		if (!pPawn)
			continue;

		// Restore normal movement
		ZEPlayer* pPlayer = pController->GetZEPlayer();
		if (pPlayer)
		{
			// Restore original speed or default to 1.0
			float originalSpeed = pPlayer->GetStoredMaxSpeed() > 0.01f ? pPlayer->GetStoredMaxSpeed() : 1.0f;
			pPlayer->SetMaxSpeed(originalSpeed);
			pPlayer->SetSpeedMod(originalSpeed);
		}
	}
}

void CTimewalkerManager::SendWarningMessage()
{
    if (!GetGlobals())
        return;

    for (int i = 0; i < GetGlobals()->maxClients; i++)
    {
        CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected())
            continue;

        ClientPrint(pController, HUD_PRINTTALK, " \x04⚠️ TIMEFREEZE WARNING! \x01Freeze in \x05%.1f \x01seconds!", m_flWarningDuration);
        ClientPrint(pController, HUD_PRINTCENTER, "⚠️ TIMEFREEZE WARNING!\nFreeze in %.1f seconds!", m_flWarningDuration);
    }
}

void CTimewalkerManager::SendFreezeMessage()
{
    if (!GetGlobals())
        return;

    CCSPlayerController* pTimewalker = nullptr;
    if (m_iTimewalkerSlot >= 0)
        pTimewalker = CCSPlayerController::FromSlot(m_iTimewalkerSlot);

    for (int i = 0; i < GetGlobals()->maxClients; i++)
    {
        CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected())
            continue;

        if (i == m_iTimewalkerSlot)
        {
            // Special message for timewalker
            ClientPrint(pController, HUD_PRINTTALK, " \x0C🕐 You are the TIMEWALKER! \x01Move freely for \x05%.1f \x01seconds!", m_flFreezeDuration);
            ClientPrint(pController, HUD_PRINTCENTER, "🕐 YOU ARE THE TIMEWALKER!\nMove freely for %.1f seconds!", m_flFreezeDuration);
        }
        else
        {
            // Message for frozen players
            const char* timewalkerName = pTimewalker ? pTimewalker->GetPlayerName() : "Unknown";
            ClientPrint(pController, HUD_PRINTTALK, " \x09❄️ TIME FROZEN! \x01Wait \x05%.1f \x01seconds... \x0CTimewalker: \x03%s", m_flFreezeDuration, timewalkerName);
            ClientPrint(pController, HUD_PRINTCENTER, "❄️ TIME FROZEN!\nWait %.1f seconds...\nTimewalker: %s", m_flFreezeDuration, timewalkerName);
        }
    }
}

void CTimewalkerManager::SendUnfreezeMessage()
{
    if (!GetGlobals())
        return;

    for (int i = 0; i < GetGlobals()->maxClients; i++)
    {
        CCSPlayerController* pController = CCSPlayerController::FromSlot(i);
        if (!pController || !pController->IsConnected())
            continue;

        ClientPrint(pController, HUD_PRINTTALK, " \x0A✅ Time flows again! \x01Next freeze in \x05%.1f \x01seconds.", m_flFreezeInterval);
        ClientPrint(pController, HUD_PRINTCENTER, "✅ Time flows again!\nNext freeze in %.1f seconds.", m_flFreezeInterval);
    }
}

bool CTimewalkerManager::IsValidTimewalker(int slot)
{
    if (slot < 0 || slot >= MAXPLAYERS)
        return false;

    CCSPlayerController* pController = CCSPlayerController::FromSlot(slot);
    if (!pController || !pController->IsConnected() || pController->m_bIsHLTV)
        return false;

    CCSPlayerPawn* pPawn = pController->GetPlayerPawn();
    if (!pPawn || !pPawn->IsAlive())
        return false;

    // Only allow players on actual teams (not spectators)
    if (pController->m_iTeamNum <= CS_TEAM_SPECTATOR)
        return false;

    return true;
}