#pragma once

#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/sdk/player/player.cuh>

namespace sdk
{

class u_players : public u_instance
{
public:
    using u_instance::u_instance;

    auto local_player( ) const -> u_player;
    auto get_players( ) const -> std::vector<u_player>;
};

} // namespace sdk
