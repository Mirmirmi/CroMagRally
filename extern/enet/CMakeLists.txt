cmake_minimum_required(VERSION 3.16)
project(enet C)

include(CheckFunctionExists)
include(CheckStructHasMember)
include(CheckTypeSize)

set(ENET_SOURCES
    callbacks.c
    compress.c
    host.c
    list.c
    packet.c
    peer.c
    protocol.c
    unix.c
    win32.c
)

add_library(enet STATIC ${ENET_SOURCES})

target_include_directories(enet PUBLIC
    "${CMAKE_CURRENT_SOURCE_DIR}/include"
)

if(WIN32)
    target_link_libraries(enet PUBLIC ws2_32 winmm)
else()
    check_function_exists(fcntl HAS_FCNTL)
    check_function_exists(poll HAS_POLL)
    check_function_exists(getaddrinfo HAS_GETADDRINFO)
    check_function_exists(getnameinfo HAS_GETNAMEINFO)
    check_function_exists(gethostbyname_r HAS_GETHOSTBYNAME_R)
    check_function_exists(gethostbyaddr_r HAS_GETHOSTBYADDR_R)
    check_function_exists(inet_pton HAS_INET_PTON)
    check_function_exists(inet_ntop HAS_INET_NTOP)
    check_struct_has_member("struct msghdr" msg_flags "sys/types.h;sys/socket.h" HAS_MSGHDR_FLAGS)
    set(CMAKE_EXTRA_INCLUDE_FILES "sys/types.h" "sys/socket.h")
    check_type_size("socklen_t" HAS_SOCKLEN_T)
    unset(CMAKE_EXTRA_INCLUDE_FILES)

    if(HAS_FCNTL)
        target_compile_definitions(enet PRIVATE HAS_FCNTL=1)
    endif()
    if(HAS_POLL AND NOT APPLE)
        target_compile_definitions(enet PRIVATE HAS_POLL=1)
    endif()
    if(HAS_GETADDRINFO)
        target_compile_definitions(enet PRIVATE HAS_GETADDRINFO=1)
    endif()
    if(HAS_GETNAMEINFO)
        target_compile_definitions(enet PRIVATE HAS_GETNAMEINFO=1)
    endif()
    if(HAS_GETHOSTBYNAME_R)
        target_compile_definitions(enet PRIVATE HAS_GETHOSTBYNAME_R=1)
    endif()
    if(HAS_GETHOSTBYADDR_R)
        target_compile_definitions(enet PRIVATE HAS_GETHOSTBYADDR_R=1)
    endif()
    if(HAS_INET_PTON)
        target_compile_definitions(enet PRIVATE HAS_INET_PTON=1)
    endif()
    if(HAS_INET_NTOP)
        target_compile_definitions(enet PRIVATE HAS_INET_NTOP=1)
    endif()
    if(HAS_MSGHDR_FLAGS)
        target_compile_definitions(enet PRIVATE HAS_MSGHDR_FLAGS=1)
    endif()
    if(HAVE_HAS_SOCKLEN_T)
        target_compile_definitions(enet PRIVATE HAS_SOCKLEN_T=1)
    endif()
endif()
