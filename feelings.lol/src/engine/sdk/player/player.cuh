#pragma once

#include <src/engine/sdk/instance/instance.cuh>
#include <src/engine/sdk/model/model.cuh>

#include <cstdint>
#include <string>

namespace sdk
{

class u_player : public u_instance
{
public:
    using u_instance::u_instance;

    auto character( ) const -> u_model;
    auto team( ) const -> u_instance;

    auto display_name( ) const -> std::string;
    // unique login name (Instance.Name) — same as name(), explicit for clarity
    auto username( ) const -> std::string;
    auto user_id( ) const -> int64_t;
    auto platform( ) const -> std::string;
};

} 
