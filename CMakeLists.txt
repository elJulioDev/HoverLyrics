cmake_minimum_required(VERSION 3.21)
project(hoverlyrics VERSION 0.2.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Widgets Network)

# config.json se guarda con nlohmann::ordered_json: QJsonObject ordena las
# claves alfabéticamente y el clic derecho rota los perfiles en el orden en que
# están escritos.
find_package(nlohmann_json 3.11 QUIET)
if(NOT nlohmann_json_FOUND)
    include(FetchContent)
    FetchContent_Declare(json
        URL https://github.com/nlohmann/json/releases/download/v3.11.3/json.tar.xz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
    FetchContent_MakeAvailable(json)
endif()

add_executable(hoverlyrics
    src/main.cpp
    src/config.cpp
    src/system.cpp
    src/lyrics.cpp
    src/images.cpp
    src/spotify.cpp
    src/window.cpp
)

target_link_libraries(hoverlyrics PRIVATE Qt6::Widgets Qt6::Network nlohmann_json::nlohmann_json)

# X11: para marcar la ventana como "skip taskbar" en Linux (ver system.cpp).
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
    target_link_libraries(hoverlyrics PRIVATE X11::X11)
endif()
