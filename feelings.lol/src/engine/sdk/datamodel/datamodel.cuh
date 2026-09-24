#pragma once

#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/sdk/players/players.cuh>
#include <src/engine/sdk/workspace/workspace.cuh>

namespace sdk
{

class u_lighting;

class u_datamodel : public u_instance
{
public:
    using u_instance::u_instance;

    static auto resolve( ) -> u_datamodel;

    auto workspace( ) const -> u_workspace;
    auto players( ) const -> u_players;
    auto lighting( ) const -> u_lighting;

    auto job_id( ) const -> std::string;
    auto place_id( ) const -> int64_t;
    auto loaded( ) const -> bool;
};

} // namespace sdk
