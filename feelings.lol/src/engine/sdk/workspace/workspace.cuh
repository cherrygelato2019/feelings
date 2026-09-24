#pragma once

#include <src/engine/sdk/camera/camera.cuh>
#include <src/engine/sdk/model/model.cuh>

namespace sdk
{

class u_workspace : public u_model
{
public:
    using u_model::u_model;

    auto current_camera( ) const -> u_camera;
    auto distributed_game_time( ) const -> float;
    auto gravity( ) const -> float;
    auto set_gravity( float v ) const -> void;
    auto world( ) const -> uintptr_t;
};

} 
