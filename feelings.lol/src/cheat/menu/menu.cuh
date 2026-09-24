#pragma once

struct ID3D11Device;

class menu_c
{
public:
    auto init( ) -> void;
    auto init_esp_preview( ID3D11Device* device ) -> bool;
    auto shutdown_esp_preview( ) -> void;
    auto draw( ) -> void;
};
