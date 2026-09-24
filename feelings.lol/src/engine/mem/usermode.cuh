#pragma once

#include <atomic>
#include <cstdint>
#include <string>
#include <windows.h>
#include <winternl.h>

typedef NTSTATUS ( NTAPI* pNtAllocateVirtualMemory )( HANDLE, PVOID*, ULONG_PTR, PSIZE_T, ULONG, ULONG );
typedef NTSTATUS ( NTAPI* pNtProtectVirtualMemory )( HANDLE, PVOID*, PSIZE_T, ULONG, PULONG );

extern "C" NTSTATUS Luck_ReadVirtualMemory( HANDLE proc, PVOID base, PVOID buffer, SIZE_T nread, PSIZE_T nread_out );
extern "C" NTSTATUS Luck_WriteVirtualMemory( HANDLE proc, PVOID base, PVOID buffer, SIZE_T nwrite, PSIZE_T nwrite_out );

class usermode_c
{
public:
    usermode_c( );
    ~usermode_c( );

    auto attach( const wchar_t* target = L"RobloxPlayerBeta.exe" ) -> bool;
    auto detach( ) -> void;

    auto set_internal( DWORD pid, uintptr_t base ) -> void
    {
        m_pid = pid;
        m_base = base;
        m_handle = nullptr;
        m_gen.fetch_add( 1, std::memory_order_acq_rel );
    }

    auto handle( ) const -> HANDLE { return m_handle; }
    auto pid( ) const -> DWORD { return m_pid; }
    auto base( ) const -> uintptr_t { return m_base; }
    auto image_size( ) const -> size_t { return m_image_size; }
    auto version( ) const -> const std::string& { return m_version; }
    auto generation( ) const -> uint64_t { return m_gen.load( std::memory_order_acquire ); }
    auto attached( ) const -> bool { return m_handle != nullptr && m_base != 0; }

    // True only when RPM is safe (external process with open handle)
    auto can_rpm( ) const -> bool
    {
        return m_handle != nullptr && m_pid != 0 && m_pid != GetCurrentProcessId( );
    }

    template<typename t>
    auto read( uintptr_t address ) -> t
    {
        t out{};
        if ( !address )
            return out;

        // Internal / same-process mapping
        if ( m_pid == GetCurrentProcessId( ) )
        {
            __try
            {
                out = *( t* )address;
            }
            __except ( EXCEPTION_EXECUTE_HANDLER )
            {
            }
            return out;
        }

        // External: NEVER treat Roblox VAs as local pointers (crashes on rescan/detach)
        if ( !m_handle )
            return out;

        Luck_ReadVirtualMemory( m_handle, ( PVOID )address, &out, sizeof( t ), nullptr );
        return out;
    }

    template<typename t>
    auto write( uintptr_t address, const t& value ) -> bool
    {
        if ( !address )
            return false;

        if ( m_pid == GetCurrentProcessId( ) )
        {
            __try
            {
                *( t* )address = value;
                return true;
            }
            __except ( EXCEPTION_EXECUTE_HANDLER )
            {
                return false;
            }
        }

        if ( !m_handle )
            return false;

        SIZE_T out = 0;
        return Luck_WriteVirtualMemory( m_handle, ( PVOID )address, ( PVOID )&value, sizeof( t ), &out ) == 0;
    }

    template<typename t>
    auto read_array( uintptr_t address, t* buffer, size_t count ) -> void
    {
        if ( !address || !buffer || !count )
            return;

        if ( m_pid == GetCurrentProcessId( ) )
        {
            __try
            {
                memcpy( buffer, ( void* )address, count * sizeof( t ) );
            }
            __except ( EXCEPTION_EXECUTE_HANDLER )
            {
            }
            return;
        }

        if ( !m_handle )
            return;

        Luck_ReadVirtualMemory( m_handle, ( PVOID )address, buffer, count * sizeof( t ), nullptr );
    }

    template<typename t>
    auto write_array( uintptr_t address, const t* buffer, size_t count ) -> void
    {
        if ( !address || !buffer || !count )
            return;

        if ( m_pid == GetCurrentProcessId( ) )
        {
            __try
            {
                memcpy( ( void* )address, buffer, count * sizeof( t ) );
            }
            __except ( EXCEPTION_EXECUTE_HANDLER )
            {
            }
            return;
        }

        if ( !m_handle )
            return;

        Luck_WriteVirtualMemory( m_handle, ( PVOID )address, ( PVOID )buffer, count * sizeof( t ), nullptr );
    }

    auto read_buf( uintptr_t address, void* buffer, size_t size ) -> bool;
    auto write_buf( uintptr_t address, const void* buffer, size_t size ) -> bool;

    auto read_string( uintptr_t address ) -> std::string;
    auto write_string( uintptr_t address, const std::string& value ) -> bool;

    auto allocate( size_t size ) -> uintptr_t;
    auto free( uintptr_t address ) -> bool;
    auto protect( uintptr_t address, size_t size, ULONG new_protect, ULONG* old_protect ) -> bool;
    auto module( const wchar_t* name, size_t* size = nullptr ) -> uintptr_t;

    auto is_valid( uintptr_t address ) -> bool;

private:
    struct rbx_string
    {
        union
        {
            char buffer[16];
            uintptr_t pointer;
        } data;
        uintptr_t length;
        uintptr_t capacity;
    };

    HANDLE m_handle{ nullptr };
    DWORD m_pid{ 0 };
    uintptr_t m_base{ 0 };
    size_t m_image_size{ 0 };
    std::string m_version;
    std::atomic<uint64_t> m_gen{ 1 };

    pNtAllocateVirtualMemory NtAllocateVirtualMemory{ nullptr };
    pNtProtectVirtualMemory NtProtectVirtualMemory{ nullptr };
};

extern usermode_c* usermode;
