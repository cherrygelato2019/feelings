#pragma once

#include <cstdint>

namespace sdk
{

enum class friend_status : int
{
    unknown = 0,
    not_friends = 1,
    friends = 2,
    friend_request_sent = 3,
    friend_request_received = 4,
    local_player = 5
};

#pragma pack( push, 1 )
struct friend_node_t
{
    uintptr_t next;
    uintptr_t prev;
    int64_t user_id;
    int64_t other_id;
    int32_t status;
};
#pragma pack( pop )

auto get_friend_status( int64_t local_user_id, int64_t other_user_id ) -> friend_status;
auto is_friend( int64_t local_user_id, int64_t other_user_id ) -> bool;

} // namespace sdk
