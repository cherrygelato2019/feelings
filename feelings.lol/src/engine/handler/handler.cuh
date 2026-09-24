#pragma once

#include <cstdint>

class handler_c
{
public:
    auto run( ) -> void;

    uintptr_t m_last_datamodel = 0;
};
