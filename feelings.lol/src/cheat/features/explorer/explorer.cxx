#include <include/global.cuh>

#include <src/cheat/features/explorer/explorer.cuh>
#include <src/cheat/features/explorer/decompiler/decompiler.cuh>
#include <src/cheat/features/explorer/menu/class_icons.hpp>

#include <addons/imgui_addons.h>
#include <imgui.h>
#include <imgui_internal.h>

#include <d3d11.h>

#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <functional>
#include <string>
#include <thread>
#include <vector>

#ifndef STB_IMAGE_IMPLEMENTATION
#define STB_IMAGE_IMPLEMENTATION
#endif
#include <stb_image.h>

namespace
{

static uintptr_t g_selected = 0;
static int g_view_result = -1;
static bool g_view_disasm = false;
static std::atomic<bool> g_decompile_busy{ false };
static char g_status_msg[128] = {};

static std::vector<ID3D11ShaderResourceView*> g_class_icon_srvs;
static bool g_icon_slots_ready = false;

// Exact service order from decompiler-master overlay.cpp
static constexpr std::array<const char*, 20> k_services = {
    "Workspace", "Lighting", "Players", "ReplicatedFirst", "ReplicatedStorage",
    "ServerScriptService", "ServerStorage", "SoundService", "StarterGui",
    "StarterPack", "StarterPlayer", "Teams",
    "TextChatService", "VoiceChatService", "PhysicsService", "InsertService",
    "AnalyticsService", "TestService", "GuiService", "Chat"
};

auto create_srv_from_png( ID3D11Device* device, const std::uint8_t* png_data, std::uint32_t png_size ) -> ID3D11ShaderResourceView*
{
    if ( !device || !png_data || !png_size )
        return nullptr;

    int width = 0, height = 0, channels = 0;
    unsigned char* pixels = stbi_load_from_memory(
        reinterpret_cast<const unsigned char*>( png_data ),
        static_cast<int>( png_size ),
        &width, &height, &channels, 4 );
    if ( !pixels )
        return nullptr;

    D3D11_TEXTURE2D_DESC desc{};
    desc.Width = static_cast<UINT>( width );
    desc.Height = static_cast<UINT>( height );
    desc.MipLevels = 1;
    desc.ArraySize = 1;
    desc.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
    desc.SampleDesc.Count = 1;
    desc.Usage = D3D11_USAGE_DEFAULT;
    desc.BindFlags = D3D11_BIND_SHADER_RESOURCE;

    D3D11_SUBRESOURCE_DATA initial{};
    initial.pSysMem = pixels;
    initial.SysMemPitch = static_cast<UINT>( width * 4 );

    ID3D11Texture2D* texture = nullptr;
    ID3D11ShaderResourceView* view = nullptr;
    if ( SUCCEEDED( device->CreateTexture2D( &desc, &initial, &texture ) ) )
        device->CreateShaderResourceView( texture, nullptr, &view );
    if ( texture )
        texture->Release( );
    stbi_image_free( pixels );
    return view;
}

auto ensure_icon_slots( ) -> void
{
    if ( g_icon_slots_ready )
        return;
    g_class_icon_srvs.assign( k_class_icon_count, nullptr );
    g_icon_slots_ready = true;
}

auto find_class_icon_index( const char* class_name ) -> std::uint16_t
{
    if ( !class_name )
        return k_class_icon_fallback;
    std::uint32_t lo = 0;
    std::uint32_t hi = k_class_name_mapping_count;
    while ( lo < hi )
    {
        const std::uint32_t mid = lo + ( hi - lo ) / 2;
        const int cmp = _stricmp( class_name, k_class_name_to_icon[mid].class_name );
        if ( cmp == 0 )
            return k_class_name_to_icon[mid].icon_index;
        if ( cmp < 0 )
            hi = mid;
        else
            lo = mid + 1;
    }
    return k_class_icon_fallback;
}

// Lazy: only decode the PNGs we actually draw (his loads all 746 — that freezes ours).
auto find_class_icon( const char* class_name ) -> ID3D11ShaderResourceView*
{
    if ( !g || !g->overlay.m_detail || !g->overlay.m_detail->device )
        return nullptr;

    ensure_icon_slots( );
    const std::uint16_t idx = find_class_icon_index( class_name );
    if ( idx >= g_class_icon_srvs.size( ) )
        return nullptr;

    if ( !g_class_icon_srvs[idx] )
    {
        const class_icon_offset_t& off = k_class_icon_offsets[idx];
        if ( off.offset + off.size <= k_class_icon_blob_size )
        {
            g_class_icon_srvs[idx] = create_srv_from_png(
                g->overlay.m_detail->device,
                k_class_icon_blob + off.offset,
                off.size );
        }
    }
    return g_class_icon_srvs[idx];
}

auto resolve_live_datamodel( ) -> sdk::u_datamodel
{
    uintptr_t dm_addr = g->engine.datamodel.load( );
    if ( dm_addr && usermode->is_valid( dm_addr ) )
        return sdk::u_datamodel( dm_addr );

    sdk::u_datamodel resolved = sdk::u_datamodel::resolve( );
    if ( resolved )
    {
        g->engine.datamodel = resolved.address( );
        return resolved;
    }
    return sdk::u_datamodel( );
}

// Faithful port of decompiler-master render_explorer_tab draw_node
auto draw_node( sdk::u_instance node, bool top_level ) -> void
{
    if ( !node || !usermode->is_valid( node.address( ) ) )
        return;

    std::string name = node.name( );
    std::string cls = node.class_name( );
    if ( name == "Ugc" && cls == "DataModel" )
        name = "LuaApp";

    auto children = node.get_children( );
    if ( top_level )
    {
        std::vector<sdk::u_instance> ordered;
        for ( const char* service : k_services )
        {
            for ( auto& c : children )
            {
                if ( c.class_name( ) == service )
                {
                    ordered.push_back( c );
                    break;
                }
            }
        }
        // Append leftovers so we never hide instances his list missed
        for ( auto& c : children )
        {
            bool have = false;
            for ( auto& o : ordered )
            {
                if ( o.address( ) == c.address( ) )
                {
                    have = true;
                    break;
                }
            }
            if ( !have )
                ordered.push_back( c );
        }
        children = std::move( ordered );
    }

    const bool has_children = !children.empty( );
    ImGuiTreeNodeFlags tflags = has_children ? 0 : ImGuiTreeNodeFlags_Leaf;
    if ( top_level )
        tflags |= ImGuiTreeNodeFlags_DefaultOpen;

    ImGui::PushID( static_cast<int>( node.address( ) ) );
    const ImGuiID tree_id = ImGui::GetID( "tree" );
    bool open = ImGui::TreeNodeBehavior( tree_id, tflags, "" );
    if ( ImGui::IsItemClicked( ) )
        g_selected = node.address( );

    ImGui::SameLine( );
    if ( ID3D11ShaderResourceView* icon = find_class_icon( cls.c_str( ) ) )
    {
        ImGui::Image( ( ImTextureID )icon, ImVec2( 16, 16 ) );
        ImGui::SameLine( );
    }

    const std::string label = name + " [" + cls + "]";
    if ( ImGui::Selectable( label.c_str( ), g_selected == node.address( ), 0, ImVec2( 0, 16 ) ) )
    {
        g_selected = node.address( );
        if ( has_children && !open )
        {
            open = true;
            ImGui::TreeNodeSetOpen( tree_id, true );
        }
    }

    if ( open )
    {
        for ( auto& child : children )
            draw_node( child, false );
        ImGui::TreePop( );
    }
    ImGui::PopID( );
}

auto draw_properties_pane( ) -> void
{
    if ( ID3D11ShaderResourceView* prop_icon = find_class_icon( "Properties" ) )
    {
        ImGui::Image( ( ImTextureID )prop_icon, ImVec2( 16, 16 ) );
        ImGui::SameLine( );
    }
    ImGui::TextUnformatted( "Explorer Properties" );
    ImGui::Separator( );

    if ( !g_selected || !usermode->is_valid( g_selected ) )
    {
        ImGui::TextDisabled( "Select an instance to inspect" );
        return;
    }

    sdk::u_instance inst( g_selected );
    std::string name = inst.name( );
    std::string cls = inst.class_name( );

    if ( ID3D11ShaderResourceView* inst_icon = find_class_icon( cls.c_str( ) ) )
    {
        ImGui::Image( ( ImTextureID )inst_icon, ImVec2( 16, 16 ) );
        ImGui::SameLine( );
    }
    ImGui::Text( "Class: %s", cls.c_str( ) );
    ImGui::Text( "Name: %s", name.c_str( ) );
    ImGui::Text( "Address: 0x%llX", ( unsigned long long )inst.address( ) );
    ImGui::Separator( );

    if ( cls == "LocalScript" || cls == "ModuleScript" || cls == "Script" )
    {
        const char* backends[] = { "lunaux", "konstant" };
        ImAdd::Combo( "backend", &config.explorer.decompiler_backend, backends, IM_ARRAYSIZE( backends ) );

        const bool busy = g_decompile_busy.load( );
        if ( busy )
            ImGui::BeginDisabled( );

        if ( ImGui::Button( "Decompile" ) )
        {
            const uintptr_t addr = g_selected;
            const auto st = script_decompiler::type_from_class( cls );
            const auto be = config.explorer.decompiler_backend == 1
                ? script_decompiler::backend::konstant
                : script_decompiler::backend::lunaux;
            g_decompile_busy.store( true );
            std::snprintf( g_status_msg, sizeof( g_status_msg ), "decompiling..." );
            std::thread( [addr, st, be]( )
            {
                bool ok = false;
                try { ok = script_decompiler::decompile( addr, st, be ); }
                catch ( ... ) { ok = false; }
                std::snprintf( g_status_msg, sizeof( g_status_msg ),
                    ok ? "decompile ok" : "no bytecode (check log)" );
                if ( ok )
                {
                    g_view_disasm = false;
                    g_view_result = ( int )script_decompiler::results( ).size( ) - 1;
                }
                g_decompile_busy.store( false );
            } ).detach( );
        }
        ImGui::SameLine( );
        if ( ImGui::Button( "Disassemble" ) )
        {
            const uintptr_t addr = g_selected;
            const auto st = script_decompiler::type_from_class( cls );
            g_decompile_busy.store( true );
            std::snprintf( g_status_msg, sizeof( g_status_msg ), "disassembling..." );
            std::thread( [addr, st]( )
            {
                bool ok = false;
                try { ok = script_decompiler::disassemble( addr, st ); }
                catch ( ... ) { ok = false; }
                std::snprintf( g_status_msg, sizeof( g_status_msg ),
                    ok ? "disasm ok" : "no bytecode (check log)" );
                if ( ok )
                {
                    g_view_disasm = true;
                    g_view_result = ( int )script_decompiler::disasm_results( ).size( ) - 1;
                }
                g_decompile_busy.store( false );
            } ).detach( );
        }

        if ( busy )
            ImGui::EndDisabled( );

        if ( g_status_msg[0] )
            ImGui::TextDisabled( "%s", g_status_msg );

        auto& list = g_view_disasm ? script_decompiler::disasm_results( ) : script_decompiler::results( );
        if ( !list.empty( ) )
        {
            ImGui::Separator( );
            if ( g_view_result < 0 || g_view_result >= ( int )list.size( ) )
                g_view_result = ( int )list.size( ) - 1;

            for ( int i = 0; i < ( int )list.size( ); ++i )
            {
                if ( ImGui::Selectable( list[( size_t )i].title.c_str( ), g_view_result == i ) )
                    g_view_result = i;
            }

            if ( g_view_result >= 0 && g_view_result < ( int )list.size( ) )
            {
                auto& entry = list[( size_t )g_view_result];
                if ( ImGui::Button( "copy source" ) )
                    ImGui::SetClipboardText( entry.code.c_str( ) );
                ImGui::BeginChild( "script_source_view", ImVec2( 0, 0 ), true, ImGuiWindowFlags_HorizontalScrollbar );
                ImGui::PushTextWrapPos( 0.f );
                ImGui::TextUnformatted( entry.code.c_str( ) );
                ImGui::PopTextWrapPos( );
                ImGui::EndChild( );
            }
        }
    }
}

} // namespace

// Dual-pane layout — same shape as decompiler-master render_explorer_tab()
auto explorer_c::draw_tree( ) -> void
{
    ImGui::BeginChild( "instances_pane", ImVec2( ImGui::GetContentRegionAvail( ).x * 0.5f, 0 ), true );
    {
        sdk::u_datamodel dm = resolve_live_datamodel( );
        if ( dm )
        {
            draw_node( dm, true );
        }
        else
        {
            ImGui::TextDisabled( "No game instance" );
            if ( ImGui::Button( "retry resolve" ) )
            {
                g->engine.datamodel = 0;
                ( void )sdk::u_datamodel::resolve( );
            }
        }
    }
    ImGui::EndChild( );

    ImGui::SameLine( );

    ImGui::BeginChild( "properties_pane", ImVec2( 0, 0 ), true );
    {
        draw_properties_pane( );
    }
    ImGui::EndChild( );
}

// Floating side panel still calls this — keep in sync with right pane.
auto explorer_c::draw_properties( ) -> void
{
    draw_properties_pane( );
}
