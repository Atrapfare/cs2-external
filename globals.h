#pragma once

#include "client_dll.hpp"
#include "offsets.hpp"

// Offsets from client_dll.hpp for easier access
namespace game_offsets {
    // C_BaseEntity
    // HACK: Manually defining C_BaseEntity offsets as they are missing from the generated headers.
    // These values are from public sources and might be outdated.
    constexpr std::ptrdiff_t m_iHealth = 0x334;
    constexpr std::ptrdiff_t m_pGameSceneNode = 0x318;

    // CGameSceneNode
    constexpr std::ptrdiff_t m_vecAbsOrigin = cs2_dumper::schemas::client_dll::CGameSceneNode::m_vecAbsOrigin;

    // C_CSPlayerPawn
    // Corrected from C_CSPlayerPawn::m_hPlayerController, which does not exist.
    // Using C_BasePlayerPawn::m_hController instead.
    constexpr std::ptrdiff_t m_hPlayerController = cs2_dumper::schemas::client_dll::C_BasePlayerPawn::m_hController;

    // CBasePlayerController
    constexpr std::ptrdiff_t m_iszPlayerName = cs2_dumper::schemas::client_dll::CBasePlayerController::m_iszPlayerName;
}
