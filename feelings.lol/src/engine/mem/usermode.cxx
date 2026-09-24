#include <include/global.cuh>

#include <TlHelp32.h>
#include <algorithm>
#include <cstring>
#include <vector>

usermode_c::usermode_c( )
{
    HMODULE ntdll = GetModuleHandleA( "ntdll.dll" );
    if ( ntdll )
    {
        NtAllocateVirtualMemory = ( pNtAllocateVirtualMemory )GetProcAddress( ntdll, "NtAllocateVirtualMemory" );
        NtProtectVirtualMemory = ( pNtProtectVirtualMemory )GetProcAddress( ntdll, "NtProtectVirtualMemory" );
    }
}

usermode_c::~usermode_c( )
{
    this->detach( );
}

auto usermode_c::detach( ) -> void
{
    // Bump first so other threads see the fence before handle disappears
    m_gen.fetch_add( 1, std::memory_order_acq_rel );
    HANDLE h = m_handle;
    m_handle = nullptr;
    m_pid = 0;
    m_base = 0;
    if ( h )
        CloseHandle( h );
    m_image_size = 0;
    m_version.clear( );
}

namespace
{

auto is_roblox_exe( const wchar_t* name ) -> bool
{
    return name && ( !_wcsicmp( name, L"RobloxPlayerBeta.exe" )
        || !_wcsicmp( name, L"Windows10Universal.exe" ) );
}

auto version_from_path( const wchar_t* path ) -> std::string
{
    if ( !path )
        return {};
    const wchar_t* v = wcsstr( path, L"version-" );
    if ( !v )
        return {};
    char buf[48]{};
    int i = 0;
    while ( v[i] && v[i] != L'\\' && v[i] != L'/' && i < 47 )
    {
        buf[i] = ( char )v[i];
        ++i;
    }
    return buf;
}

auto path_of( HANDLE h ) -> std::wstring
{
    wchar_t path[MAX_PATH]{};
    DWORD n = MAX_PATH;
    if ( !QueryFullProcessImageNameW( h, 0, path, &n ) )
        return {};
    return path;
}

struct pe_hit
{
    uintptr_t base{};
    uint32_t size{};
};

auto largest_pe( HANDLE h ) -> pe_hit
{
    pe_hit best{};
    MEMORY_BASIC_INFORMATION mbi{};
    uintptr_t addr = 0;

    while ( addr < 0x7FFFFFFEFFFF )
    {
        if ( VirtualQueryEx( h, ( LPCVOID )addr, &mbi, sizeof( mbi ) ) != sizeof( mbi ) )
            break;

        const size_t region = mbi.RegionSize ? mbi.RegionSize : 0x1000;
        const uintptr_t va = ( uintptr_t )mbi.BaseAddress;
        if ( mbi.State == MEM_COMMIT )
        {
            uint16_t mz = 0;
            SIZE_T got = 0;
            if ( Luck_ReadVirtualMemory( h, ( PVOID )va, &mz, 2, &got ) == 0 && mz == 0x5A4D )
            {
                uint32_t lfanew = 0;
                if ( Luck_ReadVirtualMemory( h, ( PVOID )( va + 0x3C ), &lfanew, 4, &got ) == 0
                    && lfanew >= 0x40 && lfanew < 0x400 )
                {
                    uint32_t sig = 0;
                    uint16_t magic = 0;
                    uint32_t so = 0;
                    if ( Luck_ReadVirtualMemory( h, ( PVOID )( va + lfanew ), &sig, 4, &got ) == 0
                        && sig == 0x4550
                        && Luck_ReadVirtualMemory( h, ( PVOID )( va + lfanew + 0x18 ), &magic, 2, &got ) == 0
                        && magic == 0x20B
                        && Luck_ReadVirtualMemory( h, ( PVOID )( va + lfanew + 0x50 ), &so, 4, &got ) == 0
                        && so >= 40u * 1024u * 1024u
                        && so > best.size )
                    {
                        best.base = va;
                        best.size = so;
                    }
                }
            }
        }

        const uintptr_t next = va + region;
        if ( next <= addr )
            break;
        addr = next;
    }
    return best;
}

auto named_module( DWORD pid, const wchar_t* name, size_t* out_size ) -> uintptr_t
{
    if ( out_size )
        *out_size = 0;
    HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, pid );
    if ( snap == INVALID_HANDLE_VALUE )
        return 0;

    MODULEENTRY32W me{};
    me.dwSize = sizeof( me );
    uintptr_t base = 0;
    if ( Module32FirstW( snap, &me ) )
    {
        do
        {
            if ( !_wcsicmp( me.szModule, name ) || is_roblox_exe( me.szModule ) )
            {
                base = ( uintptr_t )me.modBaseAddr;
                if ( out_size )
                    *out_size = ( size_t )me.modBaseSize;
                if ( is_roblox_exe( me.szModule ) )
                    break;
            }
        } while ( Module32NextW( snap, &me ) );
    }
    CloseHandle( snap );
    return base;
}

struct hwnd_ctx
{
    std::vector<DWORD> pids;
};

auto CALLBACK enum_roblox_hwnd( HWND hwnd, LPARAM lp ) -> BOOL
{
    if ( !IsWindowVisible( hwnd ) )
        return TRUE;

    wchar_t title[256]{};
    if ( !GetWindowTextW( hwnd, title, 256 ) )
        return TRUE;
    if ( _wcsicmp( title, L"Roblox" ) != 0 )
        return TRUE;

    DWORD pid = 0;
    GetWindowThreadProcessId( hwnd, &pid );
    if ( pid )
        reinterpret_cast<hwnd_ctx*>( lp )->pids.push_back( pid );
    return TRUE;
}

}

auto usermode_c::attach( const wchar_t* target ) -> bool
{
    this->detach( );

    std::vector<DWORD> pids;
    hwnd_ctx windows{};
    EnumWindows( enum_roblox_hwnd, reinterpret_cast<LPARAM>( &windows ) );

    HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPPROCESS, 0 );
    if ( snap != INVALID_HANDLE_VALUE )
    {
        PROCESSENTRY32W pe{};
        pe.dwSize = sizeof( pe );
        if ( Process32FirstW( snap, &pe ) )
        {
            do
            {
                if ( is_roblox_exe( pe.szExeFile )
                    || ( target && !_wcsicmp( pe.szExeFile, target ) ) )
                    pids.push_back( pe.th32ProcessID );
            } while ( Process32NextW( snap, &pe ) );
        }
        CloseHandle( snap );
    }

    for ( DWORD pid : windows.pids )
        pids.push_back( pid );

    std::sort( pids.begin( ), pids.end( ) );
    pids.erase( std::unique( pids.begin( ), pids.end( ) ), pids.end( ) );
    if ( pids.empty( ) )
        return false;

    HANDLE best_h = nullptr;
    DWORD best_pid = 0;
    uintptr_t best_base = 0;
    size_t best_size = 0;
    int best_score = -1;
    std::string best_ver;

    for ( DWORD pid : pids )
    {
        HANDLE h = OpenProcess( PROCESS_ALL_ACCESS, FALSE, pid );
        if ( !h )
            continue;

        const pe_hit pe = largest_pe( h );
        size_t exe_size = 0;
        const uintptr_t exe_base = named_module( pid, target ? target : L"RobloxPlayerBeta.exe", &exe_size );
        const uintptr_t base = exe_base ? exe_base : pe.base;
        const size_t size = exe_size ? exe_size : ( size_t )pe.size;
        if ( !base )
        {
            CloseHandle( h );
            continue;
        }

        const bool has_hwnd = std::find( windows.pids.begin( ), windows.pids.end( ), pid ) != windows.pids.end( );
        if ( !has_hwnd )
        {
            CloseHandle( h );
            continue;
        }
        int score = 0;
        if ( pe.size >= 40u * 1024u * 1024u )
            score += 100;
        if ( has_hwnd )
            score += 50;
        if ( size > best_size )
            score += 10;

        const std::wstring path = path_of( h );
        const std::string ver = version_from_path( path.c_str( ) );

        if ( g )
        {
            g->logger.print( "attach candidate pid=%lu ver=%s exe=0x%llx mapped=0x%llx size=0x%zx hwnd=%d",
                pid,
                ver.empty( ) ? "?" : ver.c_str( ),
                ( unsigned long long )exe_base,
                ( unsigned long long )pe.base,
                size,
                has_hwnd ? 1 : 0 );
        }

        if ( score > best_score || ( score == best_score && size > best_size ) )
        {
            if ( best_h )
                CloseHandle( best_h );
            best_h = h;
            best_pid = pid;
            best_base = base;
            best_size = size;
            best_score = score;
            best_ver = ver;
        }
        else
        {
            CloseHandle( h );
        }
    }

    if ( !best_h || !best_base )
        return false;

    m_handle = best_h;
    m_pid = best_pid;
    m_base = best_base;
    m_image_size = best_size;
    m_version = best_ver;
    m_gen.fetch_add( 1, std::memory_order_acq_rel );
    return true;
}

auto usermode_c::read_buf( uintptr_t address, void* buffer, size_t size ) -> bool
{
    if ( !address || !buffer || !size || !m_handle )
        return false;

    SIZE_T out = 0;
    return Luck_ReadVirtualMemory( m_handle, ( PVOID )address, buffer, size, &out ) == 0 && out == size;
}

auto usermode_c::write_buf( uintptr_t address, const void* buffer, size_t size ) -> bool
{
    if ( !address || !buffer || !size || !m_handle )
        return false;

    SIZE_T out = 0;
    return Luck_WriteVirtualMemory( m_handle, ( PVOID )address, ( PVOID )buffer, size, &out ) == 0 && out == size;
}

auto usermode_c::read_string( uintptr_t address ) -> std::string
{
    if ( !address || !this->is_valid( address ) )
        return { };

    int size = this->read<int>( address + 0x10 );
    if ( size <= 0 || size > 255 )
        return { };

    uintptr_t ptr = ( size >= 16 ) ? this->read<uintptr_t>( address ) : address;
    if ( !ptr || !this->is_valid( ptr ) )
        return { };

    char buf[256] = { };
    Luck_ReadVirtualMemory( m_handle, ( PVOID )ptr, buf, ( SIZE_T )size, nullptr );
    return std::string( buf, size );
}

auto usermode_c::write_string( uintptr_t address, const std::string& value ) -> bool
{
    if ( !address || !m_handle )
        return false;

    auto str = this->read<rbx_string>( address );
    str.length = ( uintptr_t )value.length( );

    if ( str.length > 15 )
    {
        if ( !str.data.pointer || !this->is_valid( str.data.pointer ) || value.length( ) > str.capacity )
            return false;
        this->write<rbx_string>( address, str );
        Luck_WriteVirtualMemory( m_handle, ( PVOID )str.data.pointer, ( PVOID )value.c_str( ), value.length( ), nullptr );
    }
    else
    {
        str.capacity = 15;
        std::memcpy( str.data.buffer, value.c_str( ), value.length( ) );
        if ( value.length( ) < 16 )
            str.data.buffer[value.length( )] = '\0';
        this->write<rbx_string>( address, str );
    }
    return true;
}

auto usermode_c::allocate( size_t /*size*/ ) -> uintptr_t
{
    // Hyperion kills the client on remote NtAllocateVirtualMemory.
    return 0;
}

auto usermode_c::free( uintptr_t address ) -> bool
{
    if ( !address || !m_handle )
        return false;
    return VirtualFreeEx( m_handle, ( LPVOID )address, 0, MEM_RELEASE ) != 0;
}

auto usermode_c::protect( uintptr_t address, size_t size, ULONG new_protect, ULONG* old_protect ) -> bool
{
    if ( !address || !m_handle || !NtProtectVirtualMemory )
        return false;

    PVOID region = ( PVOID )address;
    SIZE_T region_size = size;
    ULONG old_prot = 0;

    NTSTATUS status = NtProtectVirtualMemory( m_handle, &region, &region_size, new_protect, &old_prot );

    if ( old_protect )
        *old_protect = old_prot;

    return status == 0;
}

auto usermode_c::module( const wchar_t* name, size_t* size ) -> uintptr_t
{
    if ( size )
        *size = 0;
    if ( !m_pid || !name )
        return 0;

    HANDLE snap = CreateToolhelp32Snapshot( TH32CS_SNAPMODULE | TH32CS_SNAPMODULE32, m_pid );
    if ( snap == INVALID_HANDLE_VALUE )
        return 0;

    MODULEENTRY32W me{};
    me.dwSize = sizeof( me );
    uintptr_t base = 0;
    if ( Module32FirstW( snap, &me ) )
    {
        do
        {
            if ( !_wcsicmp( me.szModule, name ) )
            {
                base = ( uintptr_t )me.modBaseAddr;
                if ( size )
                    *size = ( size_t )me.modBaseSize;
                break;
            }
        } while ( Module32NextW( snap, &me ) );
    }
    CloseHandle( snap );
    return base;
}

auto usermode_c::is_valid( uintptr_t address ) -> bool
{
    if ( !address || address < 0x10000 || address > 0x7FFFFFFEFFFF )
        return false;
    return true;
}
