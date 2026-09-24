#pragma once

#include <src/engine/sdk/basepart/basepart.cuh>
#include <src/engine/sdk/instance/instance.cuh>

namespace sdk
{

class u_model : public u_instance
{
public:
    using u_instance::u_instance;

    auto primary_part( ) const -> u_base_part;
    auto scale( ) const -> float;
};

} // namespace sdk
