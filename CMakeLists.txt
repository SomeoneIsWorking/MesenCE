cmake_minimum_required(VERSION 3.24)

project(MesenCECore LANGUAGES C CXX)

option(MESENCE_BUILD_PORT_TESTS "Build the direct NES port execution proof" ON)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE MESENCE_CORE_SOURCES CONFIGURE_DEPENDS
	"Core/*.cpp"
	"Lua/*.c"
	"Sdl/*.cpp"
	"SevenZip/*.c"
	"Utilities/*.c"
	"Utilities/*.cpp"
)

if(WIN32)
	file(GLOB_RECURSE MESENCE_PLATFORM_SOURCES CONFIGURE_DEPENDS "Windows/*.cpp")
	set(MESENCE_POSIX_LUA_SOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/Lua/serial.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/Lua/unix.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/Lua/unixdgram.c"
		"${CMAKE_CURRENT_SOURCE_DIR}/Lua/unixstream.c"
	)
	list(REMOVE_ITEM MESENCE_CORE_SOURCES ${MESENCE_POSIX_LUA_SOURCES})
elseif(APPLE)
	enable_language(OBJCXX)
	file(GLOB_RECURSE MESENCE_PLATFORM_SOURCES CONFIGURE_DEPENDS "MacOS/*.mm")
elseif(UNIX AND NOT ANDROID)
	file(GLOB_RECURSE MESENCE_PLATFORM_SOURCES CONFIGURE_DEPENDS "Linux/*.c" "Linux/*.cpp")
else()
	message(FATAL_ERROR "The complete MesenCE runtime has no maintained platform target for this host")
endif()

list(APPEND MESENCE_CORE_SOURCES ${MESENCE_PLATFORM_SOURCES})

add_library(mesence_core STATIC ${MESENCE_CORE_SOURCES})
target_compile_features(mesence_core PUBLIC cxx_std_17)
target_include_directories(mesence_core PUBLIC
	${CMAKE_CURRENT_SOURCE_DIR}
	${CMAKE_CURRENT_SOURCE_DIR}/Core
	${CMAKE_CURRENT_SOURCE_DIR}/Linux
	${CMAKE_CURRENT_SOURCE_DIR}/Sdl
	${CMAKE_CURRENT_SOURCE_DIR}/Utilities
)
target_link_libraries(mesence_core PUBLIC SDL2::SDL2 Threads::Threads ${CMAKE_DL_LIBS})
if(WIN32)
	target_compile_definitions(mesence_core PRIVATE NOMINMAX WIN32_LEAN_AND_MEAN UNICODE _UNICODE)
	target_link_libraries(mesence_core PUBLIC
		comctl32
		d3d11
		d3dcompiler
		dinput8
		dsound
		dxgi
		dxguid
		iphlpapi
		setupapi
		shlwapi
		winmm
		ws2_32
		Xinput9_1_0
	)
elseif(APPLE)
	target_link_libraries(mesence_core PUBLIC
		"-framework Cocoa"
		"-framework CoreHaptics"
		"-framework Foundation"
		"-framework GameController"
	)
endif()
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(mesence_core PRIVATE -Wno-return-type-c-linkage -Wno-undefined-inline)
endif()

if(MESENCE_BUILD_PORT_TESTS)
	enable_testing()
	add_executable(mesence_nes_port_test tests/NesPortExecutionTest.cpp)
	target_link_libraries(mesence_nes_port_test PRIVATE mesence_core)
	add_test(NAME mesence_nes_port_test COMMAND mesence_nes_port_test)

	add_executable(mesence_snes_port_test tests/SnesPortExecutionTest.cpp)
	target_link_libraries(mesence_snes_port_test PRIVATE mesence_core)
	add_test(NAME mesence_snes_port_test COMMAND mesence_snes_port_test)
endif()

option(MESENCE_BUILD_SNES_ORACLE "Build the headless SNES oracle for port differential checks" OFF)
if(MESENCE_BUILD_SNES_ORACLE)
	add_executable(snes_oracle tools/snes_oracle/snes_oracle.cpp)
	target_link_libraries(snes_oracle PRIVATE mesence_core)
endif()
