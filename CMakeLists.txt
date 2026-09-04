cmake_minimum_required(VERSION 3.24)

project(MesenCECore LANGUAGES C CXX)

option(MESENCE_BUILD_PORT_TESTS "Build the direct NES port execution proof" ON)

find_package(SDL2 REQUIRED)
find_package(Threads REQUIRED)

file(GLOB_RECURSE MESENCE_CORE_SOURCES CONFIGURE_DEPENDS
	"Core/*.cpp"
	"Linux/*.c"
	"Linux/*.cpp"
	"Lua/*.c"
	"Sdl/*.cpp"
	"SevenZip/*.c"
	"Utilities/*.c"
	"Utilities/*.cpp"
)

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
if(CMAKE_CXX_COMPILER_ID MATCHES "Clang")
	target_compile_options(mesence_core PRIVATE -Wno-return-type-c-linkage -Wno-undefined-inline)
endif()

if(MESENCE_BUILD_PORT_TESTS)
	enable_testing()
	add_executable(mesence_nes_port_test tests/NesPortExecutionTest.cpp)
	target_link_libraries(mesence_nes_port_test PRIVATE mesence_core)
	add_test(NAME mesence_nes_port_test COMMAND mesence_nes_port_test)
endif()
