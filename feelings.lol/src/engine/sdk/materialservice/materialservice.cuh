#pragma once

#include <src/engine/sdk/instance/instance.cuh>

namespace sdk
{

class u_material_service : public u_instance
{
public:
    using u_instance::u_instance;

    static auto resolve( ) -> u_material_service;

    // Best-effort Use2022Materials bool. Offset may need retuning per client.
    auto use_2022_materials( ) const -> bool;
    auto set_use_2022_materials( bool v ) const -> void;
};

}
