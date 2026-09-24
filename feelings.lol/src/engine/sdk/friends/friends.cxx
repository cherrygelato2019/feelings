#include <include/global.cuh>

#include <src/engine/sdk/friends/friends.cuh>

namespace sdk
{

auto get_friend_status( int64_t local_user_id, int64_t other_user_id ) -> friend_status
{
    if ( local_user_id == other_user_id )
        return friend_status::local_player;

    if ( local_user_id <= 0 || other_user_id <= 0 )
        return friend_status::not_friends;

    const uintptr_t dm = g->engine.datamodel.load( );
    if ( !dm )
        return friend_status::not_friends;

    const sdk::u_instance friend_svc = sdk::u_instance( dm ).find_first_child_of_class( "FriendService" );
    if ( !friend_svc )
        return friend_status::not_friends;

    const uintptr_t fs = friend_svc.address( );
    const uint64_t mask = usermode->read<uint64_t>( fs + offsets::friend_service::mask );
    const uintptr_t bucket_base = usermode->read<uintptr_t>( fs + offsets::friend_service::bucket_base );
    const uintptr_t sentinel = usermode->read<uintptr_t>( fs + offsets::friend_service::sentinel );

    if ( !bucket_base || !mask )
        return friend_status::not_friends;

    uint64_t hash = ( uint64_t )( local_user_id + 0x9E3779B9LL );
    hash ^= ( uint64_t )other_user_id + ( ( hash << 6 ) + ( hash >> 2 ) + 0x9E3779B9ULL );
    const size_t index = ( size_t )( mask & hash );
    const uintptr_t bucket = bucket_base + ( index * 0x10 );

    const uintptr_t first_node = usermode->read<uintptr_t>( bucket + 0x8 );
    if ( !first_node || first_node == sentinel )
        return friend_status::not_friends;

    const uintptr_t end = usermode->read<uintptr_t>( bucket + 0x0 );
    uintptr_t current = first_node;

    for ( int guard = 0; current && current != sentinel && guard < 256; ++guard )
    {
        const friend_node_t node = usermode->read<friend_node_t>( current );
        if ( node.user_id == local_user_id && node.other_id == other_user_id )
            return static_cast<friend_status>( node.status );

        if ( current == end )
            break;

        current = node.next;
    }

    return friend_status::not_friends;
}

auto is_friend( int64_t local_user_id, int64_t other_user_id ) -> bool
{
    return get_friend_status( local_user_id, other_user_id ) == friend_status::friends;
}

} // namespace sdk
