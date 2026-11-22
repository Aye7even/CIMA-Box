cmake_minimum_required(VERSION 3.20)
project(CIMABox VERSION 3.1.3 LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release)
endif()

include(FetchContent)
set(FETCHCONTENT_QUIET OFF)

# --- header-only / source-only dependencies (downloaded at configure time)
FetchContent_Declare(imgui     GIT_REPOSITORY https://github.com/ocornut/imgui.git     GIT_TAG v1.92.5 GIT_SHALLOW ON SOURCE_SUBDIR _none)
FetchContent_Declare(miniaudio GIT_REPOSITORY https://github.com/mackron/miniaudio.git GIT_TAG 0.11.21 GIT_SHALLOW ON SOURCE_SUBDIR _none)
FetchContent_Declare(json      GIT_REPOSITORY https://github.com/nlohmann/json.git     GIT_TAG v3.11.3 GIT_SHALLOW ON SOURCE_SUBDIR _none)

set(GLFW_BUILD_DOCS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(GLFW_BUILD_WAYLAND OFF CACHE BOOL "" FORCE)
set(GLFW_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(glfw GIT_REPOSITORY https://github.com/glfw/glfw.git GIT_TAG 3.4 GIT_SHALLOW ON)

FetchContent_MakeAvailable(imgui miniaudio json glfw)

# --- libcurl: use the system one if present (macOS, Linux, vcpkg), otherwise build it
find_package(CURL QUIET)
if(NOT CURL_FOUND)
  message(STATUS "System libcurl not found - building curl from source")
  set(BUILD_CURL_EXE OFF CACHE BOOL "" FORCE)
  set(BUILD_SHARED_LIBS OFF CACHE BOOL "" FORCE)
  set(BUILD_STATIC_LIBS ON CACHE BOOL "" FORCE)
  set(BUILD_TESTING OFF CACHE BOOL "" FORCE)
  set(BUILD_LIBCURL_DOCS OFF CACHE BOOL "" FORCE)
  set(BUILD_MISC_DOCS OFF CACHE BOOL "" FORCE)
  set(ENABLE_CURL_MANUAL OFF CACHE BOOL "" FORCE)
  set(HTTP_ONLY ON CACHE BOOL "" FORCE)
  set(ENABLE_WEBSOCKETS ON CACHE BOOL "" FORCE)  # live transcription (ElevenLabs realtime speech-to-text)
  set(CURL_USE_LIBPSL OFF CACHE BOOL "" FORCE)
  set(CURL_USE_LIBSSH2 OFF CACHE BOOL "" FORCE)
  set(USE_LIBIDN2 OFF CACHE BOOL "" FORCE)
  set(USE_NGHTTP2 OFF CACHE BOOL "" FORCE)
  set(CURL_ZLIB OFF CACHE STRING "" FORCE)
  set(CURL_BROTLI OFF CACHE STRING "" FORCE)
  set(CURL_ZSTD OFF CACHE STRING "" FORCE)
  if(WIN32)
    set(CURL_USE_SCHANNEL ON CACHE BOOL "" FORCE)   # Windows' built-in TLS, no OpenSSL needed
  endif()
  FetchContent_Declare(curl GIT_REPOSITORY https://github.com/curl/curl.git GIT_TAG curl-8_10_1 GIT_SHALLOW ON)
  FetchContent_MakeAvailable(curl)
endif()

find_package(OpenGL REQUIRED)
find_package(Threads REQUIRED)

# ImGui draws text left to right only. A copy of imgui_draw.cpp gets a small hook so Arabic, Persian,
# Urdu and Hebrew are joined and drawn right to left (cmake/imgui_rtl.inl, src/bidi.cpp).
set(IMGUI_DRAW_RTL ${CMAKE_BINARY_DIR}/imgui_draw_rtl.cpp)
execute_process(
  COMMAND ${CMAKE_COMMAND} -DIN=${imgui_SOURCE_DIR}/imgui_draw.cpp -DOUT=${IMGUI_DRAW_RTL}
          -DINL=${CMAKE_CURRENT_SOURCE_DIR}/cmake/imgui_rtl.inl -P ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_imgui.cmake
  RESULT_VARIABLE imgui_patch_result)
if(NOT imgui_patch_result EQUAL 0)
  message(FATAL_ERROR "Couldn't add the right-to-left text hook to ImGui")
endif()
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${imgui_SOURCE_DIR}/imgui_draw.cpp ${CMAKE_CURRENT_SOURCE_DIR}/cmake/patch_imgui.cmake)

add_executable(cimabox
  src/main.cpp
  src/ui.cpp
  src/eleven.cpp
  src/translate.cpp
  src/http.cpp
  src/listen.cpp
  src/appaudio.cpp
  src/osc.cpp
  src/emotion.cpp
  src/micmatch.cpp
  src/voiceout.cpp
  src/captionpanel.cpp
  src/i18n.cpp
  src/updater.cpp
  src/changelog.cpp
  src/servicestatus.cpp
  src/applog.cpp
  src/usage.cpp
  src/guidepics.cpp
  src/vroverlay.cpp
  src/bidi.cpp
  src/extras.cpp
  src/livestt.cpp
  src/localai.cpp
  src/pcspecs.cpp
  src/early.cpp
  src/focus.cpp
  src/voiceid.cpp
  ${imgui_SOURCE_DIR}/imgui.cpp
  ${IMGUI_DRAW_RTL}
  ${imgui_SOURCE_DIR}/imgui_tables.cpp
  ${imgui_SOURCE_DIR}/imgui_widgets.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_glfw.cpp
  ${imgui_SOURCE_DIR}/backends/imgui_impl_opengl3.cpp
  ${imgui_SOURCE_DIR}/misc/cpp/imgui_stdlib.cpp
)
target_include_directories(cimabox PRIVATE
  ${imgui_SOURCE_DIR}
  ${imgui_SOURCE_DIR}/backends
  ${miniaudio_SOURCE_DIR}
  ${json_SOURCE_DIR}/single_include
)
set_target_properties(cimabox PROPERTIES OUTPUT_NAME "CIMABox")
# Shown as "2.4", or "2.4.1" for a patch release; the updater compares the full version.
if(PROJECT_VERSION_PATCH GREATER 0)
  set(CIMA_SHOWN_VERSION "${PROJECT_VERSION}")
else()
  set(CIMA_SHOWN_VERSION "${PROJECT_VERSION_MAJOR}.${PROJECT_VERSION_MINOR}")
endif()
target_compile_definitions(cimabox PRIVATE CIMA_VERSION="${CIMA_SHOWN_VERSION}" CIMA_FULL_VERSION="${PROJECT_VERSION}")

# Updates from GitHub Releases (see DEVELOPING.md: "Publishing updates"). Both are needed:
#   CIMA_UPDATE_REPO  "owner/repo" whose releases hold CIMABox.exe and CIMABox.exe.sig
#   update_key.txt    the public half of your signing key (made by tools/updates/make-update-key.sh)
# Without them the program simply has no updater. The release workflow sets both.
set(CIMA_UPDATE_REPO "" CACHE STRING "GitHub owner/repo the app checks for updates")
if(EXISTS "${CMAKE_SOURCE_DIR}/update_key.txt")
  file(READ "${CMAKE_SOURCE_DIR}/update_key.txt" CIMA_UPDATE_KEY)
  string(STRIP "${CIMA_UPDATE_KEY}" CIMA_UPDATE_KEY)
else()
  set(CIMA_UPDATE_KEY "")
endif()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/update_config.h" CONTENT
"// Generated by CMake: where updates come from.
#define CIMA_UPDATE_REPO \"@CIMA_UPDATE_REPO@\"
#define CIMA_UPDATE_KEY \"@CIMA_UPDATE_KEY@\"
" @ONLY)
target_include_directories(cimabox PRIVATE "${CMAKE_BINARY_DIR}/generated")
target_compile_definitions(cimabox PRIVATE CIMA_HAS_UPDATE_CONFIG)

# CHANGELOG.md, built into the program (Settings > Version history, "What's new" after an update).
# Stored as bytes: MSVC limits the length of a single string literal.
set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS "${CMAKE_SOURCE_DIR}/CHANGELOG.md")
if(EXISTS "${CMAKE_SOURCE_DIR}/CHANGELOG.md")
  file(READ "${CMAKE_SOURCE_DIR}/CHANGELOG.md" CIMA_CHANGELOG_HEX HEX)
  string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1," CIMA_CHANGELOG_BYTES "${CIMA_CHANGELOG_HEX}")
else()
  set(CIMA_CHANGELOG_BYTES "")
endif()
file(CONFIGURE OUTPUT "${CMAKE_BINARY_DIR}/generated/changelog_data.h" CONTENT
"// Generated by CMake from CHANGELOG.md.
static const unsigned char kChangelog[] = {@CIMA_CHANGELOG_BYTES@ 0};
" @ONLY)
target_compile_definitions(cimabox PRIVATE CIMA_HAS_CHANGELOG)
if(CIMA_UPDATE_REPO AND CIMA_UPDATE_KEY)
  message(STATUS "Updates from github.com/${CIMA_UPDATE_REPO}")
endif()
# Screen translation's picture and text-reading code (see the Windows part below).
add_library(cima_screen STATIC src/screen.cpp)
target_include_directories(cima_screen PRIVATE src)
if(MSVC)
  target_compile_options(cima_screen PRIVATE /utf-8 /EHsc)
  target_compile_definitions(cima_screen PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()
target_link_libraries(cimabox PRIVATE cima_screen glfw CURL::libcurl OpenGL::GL Threads::Threads ${CMAKE_DL_LIBS})
if(UNIX AND NOT APPLE)
  target_link_libraries(cimabox PRIVATE m)
endif()
if(WIN32)
  # dark title bar, per-application audio capture, chatbox messages (UDP)
  target_link_libraries(cimabox PRIVATE dwmapi mmdevapi ole32 ws2_32 bcrypt shell32 comdlg32 winmm dxgi)
  # Screen translation reads text with Windows' own recognizer (C++/WinRT, part of the Windows SDK).
  # C++/WinRT wants C++20 with this compiler, so src/screen.cpp is built as its own small library.
  # Without the headers the build still works; only AI reading is offered then.
  include(CheckIncludeFileCXX)
  if(MSVC)
    set(CMAKE_REQUIRED_FLAGS "/std:c++20 /EHsc")
  endif()
  check_include_file_cxx("winrt/base.h" CIMA_HAVE_CPPWINRT)
  unset(CMAKE_REQUIRED_FLAGS)
  if(CIMA_HAVE_CPPWINRT)
    set_target_properties(cima_screen PROPERTIES CXX_STANDARD 20 CXX_STANDARD_REQUIRED ON)
    target_link_libraries(cima_screen PRIVATE windowsapp)
    if(MSVC)
      target_compile_options(cima_screen PRIVATE /bigobj)
    endif()
  else()
    message(STATUS "C++/WinRT headers not found: screen translation will offer AI reading only")
    target_compile_definitions(cima_screen PRIVATE CIMA_NO_WINRT)
  endif()
  target_compile_definitions(cima_screen PRIVATE NOMINMAX)
  # The .exe's icon (Explorer, taskbar, title bar) and its version details.
  configure_file(assets/cimabox.rc.in "${CMAKE_CURRENT_BINARY_DIR}/cimabox.rc" @ONLY)
  target_sources(cimabox PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/cimabox.rc")
  set_source_files_properties("${CMAKE_CURRENT_BINARY_DIR}/cimabox.rc" PROPERTIES
    OBJECT_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/assets/cimabox.ico")
  target_compile_definitions(cimabox PRIVATE NOMINMAX)
endif()
if(MSVC)
  target_compile_options(cimabox PRIVATE /utf-8)
  target_compile_definitions(cimabox PRIVATE _CRT_SECURE_NO_WARNINGS)
  # Start as a normal desktop app, without the black console window.
  target_link_options(cimabox PRIVATE /SUBSYSTEM:WINDOWS /ENTRY:mainCRTStartup)
endif()

# ---------------------------------------------------------------- SteamVR captions (Windows)
# Valve's OpenVR SDK, built right into CIMABox.exe from its source code (no openvr_api.dll to ship).
# The source files are downloaded once into the build folder. OpenVR itself only finds and loads
# SteamVR's own files when VR captions are switched on, so the program runs fine on PCs without
# SteamVR. Turn off with -DCIMA_VR=OFF.
option(CIMA_VR "SteamVR caption overlay" ON)
if(WIN32 AND CIMA_VR)
  set(OPENVR_TAG v2.5.1)
  set(OPENVR_DIR "${CMAKE_BINARY_DIR}/openvr-src-${OPENVR_TAG}")
  set(_openvr_files
    headers/openvr.h
    src/openvr_api_public.cpp src/jsoncpp.cpp src/ivrclientcore.h src/json/json.h src/json/json-forwards.h)
  foreach(_n dirtools envvartools pathtools sharedlibtools hmderrors vrpathregistry strtools)
    list(APPEND _openvr_files src/vrcore/${_n}_public.cpp src/vrcore/${_n}_public.h)
  endforeach()
  set(_openvr_ok TRUE)
  foreach(_f ${_openvr_files})
    if(NOT EXISTS "${OPENVR_DIR}/${_f}")
      message(STATUS "Downloading OpenVR ${OPENVR_TAG}: ${_f}")
      file(DOWNLOAD "https://raw.githubusercontent.com/ValveSoftware/openvr/${OPENVR_TAG}/${_f}" "${OPENVR_DIR}/${_f}"
           STATUS _st TLS_VERIFY ON)
      list(GET _st 0 _code)
      if(NOT _code EQUAL 0)
        file(REMOVE "${OPENVR_DIR}/${_f}")
        message(WARNING "Couldn't download ${_f} (${_st}); building without VR captions.")
        set(_openvr_ok FALSE)
      endif()
    endif()
  endforeach()
  if(_openvr_ok)
    set(_openvr_sources ${_openvr_files})
    list(FILTER _openvr_sources INCLUDE REGEX "\\.cpp$")
    list(TRANSFORM _openvr_sources PREPEND "${OPENVR_DIR}/")
    add_library(openvr_static STATIC ${_openvr_sources})
    target_include_directories(openvr_static PRIVATE "${OPENVR_DIR}/src" "${OPENVR_DIR}/src/vrcore"
                               PUBLIC "${OPENVR_DIR}/headers")
    # The same settings as Valve's own static build. WIN64 makes it look for SteamVR's 64-bit client.
    target_compile_definitions(openvr_static PRIVATE VR_API_PUBLIC VRCORE_NO_PLATFORM WIN64 _CRT_SECURE_NO_WARNINGS
                               PUBLIC OPENVR_BUILD_STATIC)
    target_compile_options(openvr_static PRIVATE /W0 /utf-8)
    target_link_libraries(openvr_static PRIVATE shell32)
    target_link_libraries(cimabox PRIVATE openvr_static)
    target_compile_definitions(cimabox PRIVATE CIMA_HAS_OPENVR)
  endif()
endif()

# The "lang" folder next to the program: drop a translation file in it to add an app language
# (lang/_template.json.txt lists every text). The built-in languages are compiled in.
add_custom_command(TARGET cimabox POST_BUILD
  COMMAND "${CMAKE_COMMAND}" -E make_directory "$<TARGET_FILE_DIR:cimabox>/lang"
  COMMAND "${CMAKE_COMMAND}" -E copy_if_different "${CMAKE_SOURCE_DIR}/lang/_template.json.txt" "$<TARGET_FILE_DIR:cimabox>/lang"
  VERBATIM)
