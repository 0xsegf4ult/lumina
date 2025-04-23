cmake_minimum_required(VERSION 4.0.1 FATAL_ERROR)

set(CMAKE_EXPERIMENTAL_CXX_IMPORT_STD
    "a9e1cf81-9932-4810-974b-6eccaf14e457")

project(lumina VERSION 0.1 LANGUAGES CXX)

option(LUMINA_TRACY_INTEGRATION "Enable tracy profiler" ON)

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

# import std with clang requires linking against libc++
set(LUMINA_COMPILE_OPTIONS -Wno-deprecated-declarations -stdlib=libc++)

# ninja is required for cpp modules
# colorize ninja output
if(${CMAKE_GENERATOR} MATCHES "Ninja")
	set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -fdiagnostics-color=always)
else()
	message(FATAL_ERROR "Generator ${CMAKE_GENERATOR} is not supported with C++ modules")
endif()

if(NOT LUMINA_PLATFORM)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		set(LUMINA_PLATFORM "LINUX")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_LINUX -DLUMINA_PLATFORM_POSIX)
		message(status "lumina::platform: linux")
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		set(LUMINA_PLATFORM "WIN32")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_WIN32)
		message(STATUS "lumina::platform: win32")
	else()
		message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_NAME} not supported")
	endif()
endif()

if(LUMINA_TRACY_INTEGRATION)
	message("tracy integration enabled")
	set(TRACY_ENABLE ON)
else()
	set(TRACY_ENABLE OFF)
	remove_definitions(-DTRACY_ENABLE)
endif()

add_subdirectory("external")

add_subdirectory("modules")
