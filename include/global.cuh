#pragma once

#include <src/util/logger.cuh>

#include <src/engine/mem/usermode.cuh>
#include <src/engine/math/math.cuh>
#include <src/engine/offsets/offsets.cuh>
#include <src/engine/sdk/sdk.cuh>
#include <src/engine/cache/cache.cuh>
#include <src/engine/handler/handler.cuh>
#include <src/engine/engine.cuh>

#include <src/cheat/config/config.cuh>
#include <src/cheat/overlay/overlay.cuh>
#include <src/cheat/menu/menu.cuh>
#include <src/cheat/features/esp/esp.cuh>
#include <src/cheat/features/aimbot/aimbot.cuh>
#include <src/cheat/features/combat/combat_state.cuh>
#include <src/cheat/features/silentaim/silentaim.cuh>
#include <src/cheat/features/triggerbot/triggerbot.cuh>
#include <src/cheat/features/notifications/notifications.cuh>
#include <src/cheat/features/player/player_mods.cuh>
#include <src/cheat/features/player/teleport.cuh>
#include <src/cheat/features/player/animation_changer.cuh>
#include <src/cheat/features/player/avatar_changer.cuh>
#include <src/cheat/features/explorer/explorer.cuh>
#include <src/cheat/features/players/players_list.cuh>
#include <src/cheat/features/music/feelings_fm.cuh>
#include <src/cheat/features/world/world_lighting.cuh>

struct global_t
{
    logger_c logger;
    engine_c engine;
    cache_c cache;
    handler_c handler;
    overlay_c overlay;
    menu_c menu;
    esp_c esp;
    aimbot_c aimbot;
    explorer_c explorer;
};

extern global_t* g;
