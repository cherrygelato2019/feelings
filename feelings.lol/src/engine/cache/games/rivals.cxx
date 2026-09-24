#include <src/engine/cache/games/rivals.hxx>
#include <src/engine/cache/games/common.hxx>

namespace games
{
namespace rivals
{

auto cache_players( std::vector<cache::player>& /*out*/, const cache_ctx& /*ctx*/ ) -> void
{
    // njx stub — empty cache; default Players path + TeammateLabel teammate check.
}

auto is_teammate( uintptr_t character ) -> bool
{
    if ( !character )
        return false;
    sdk::u_instance model( character );
    sdk::u_instance hrp = model.find_first_child( "HumanoidRootPart" );
    if ( !hrp )
        return false;
    return ( bool )hrp.find_first_child( "TeammateLabel" );
}

}
}
