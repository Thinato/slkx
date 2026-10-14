cmake_minimum_required(VERSION 3.20)
project(slkx VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)

find_package(CURL REQUIRED)

# nlohmann_json: use the system package if present, otherwise fetch it.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
  FetchContent_Declare(nlohmann_json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    GIT_SHALLOW TRUE)
  FetchContent_MakeAvailable(nlohmann_json)
endif()

# IXWebSocket for the RTM socket: the system libcurl has no ws/wss support.
# On Apple, IXWebSocket defaults to SecureTransport, whose header is absent from
# the Command Line Tools SDK here, so force OpenSSL. Configure with
#   -DOPENSSL_ROOT_DIR=$(brew --prefix openssl@3)
set(USE_TLS ON CACHE BOOL "" FORCE)
set(USE_OPEN_SSL ON CACHE BOOL "" FORCE)
set(BUILD_DEMO OFF CACHE BOOL "" FORCE)
FetchContent_Declare(ixwebsocket
  GIT_REPOSITORY https://github.com/machinezone/IXWebSocket.git
  GIT_TAG v12.0.1
  GIT_SHALLOW TRUE)
FetchContent_MakeAvailable(ixwebsocket)

add_executable(slkx
  src/main.cpp
  src/http.cpp
  src/config.cpp
  src/slack.cpp
  src/resolver.cpp
  src/commands.cpp
  src/rtm.cpp
)

target_include_directories(slkx PRIVATE src)
target_link_libraries(slkx PRIVATE CURL::libcurl nlohmann_json::nlohmann_json ixwebsocket)

if(MSVC)
  target_compile_options(slkx PRIVATE /W4)
else()
  target_compile_options(slkx PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Self-checks: assert-based mains, one per pure-logic header. -UNDEBUG because a
# Release build defines NDEBUG, which compiles every assert away -- the binaries
# would then print "all checks passed" whatever the code does.
foreach(t IN ITEMS message_ref block_action rtm filename)
  add_executable(slkx_${t}_test tests/${t}_test.cpp)
  target_include_directories(slkx_${t}_test PRIVATE src)
  target_link_libraries(slkx_${t}_test PRIVATE nlohmann_json::nlohmann_json)
  target_compile_options(slkx_${t}_test PRIVATE -UNDEBUG -Wall -Wextra -Wpedantic)
endforeach()

install(TARGETS slkx RUNTIME DESTINATION bin)
