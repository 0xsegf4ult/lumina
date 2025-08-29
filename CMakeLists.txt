cmake_minimum_required(VERSION 3.30.5 FATAL_ERROR)

set(CMAKE_CXX_SCAN_FOR_MODULES ON)
set(CMAKE_INTERPROCEDURAL_OPTIMIZATION TRUE)
set(CMAKE_CXX_STANDARD 23)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_MODULE_STD 1)

project(lumina VERSION 0.1 LANGUAGES CXX)

option(LUMINA_TRACY_INTEGRATION "Enable tracy profiler" OFF)
option(LUMINA_BUILD_TOOLS "Build tools" ON)

# ninja is required for cpp modules
if(NOT ${CMAKE_GENERATOR} MATCHES "Ninja")
	message(FATAL_ERROR "Generator ${CMAKE_GENERATOR} is not supported with C++ modules")
endif()

if(NOT LUMINA_PLATFORM)
	if(${CMAKE_SYSTEM_NAME} MATCHES "Linux")
		set(LUMINA_PLATFORM "LINUX")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_LINUX -DLUMINA_PLATFORM_POSIX)
		message(STATUS "lumina::platform: linux")
	elseif(${CMAKE_SYSTEM_NAME} MATCHES "Windows")
		set(LUMINA_PLATFORM "WIN32")
		set(LUMINA_COMPILE_OPTIONS ${LUMINA_COMPILE_OPTIONS} -DLUMINA_PLATFORM_WIN32)
		message(STATUS "lumina::platform: win32")
	else()
		message(FATAL_ERROR "Platform ${CMAKE_SYSTEM_NAME} not supported")
	endif()
endif()

add_subdirectory("external")

if(LUMINA_TRACY_INTEGRATION)
	message("tracy integration enabled")
	set(TRACY_ENABLE ON)
else()
	set(TRACY_ENABLE OFF)
endif()

add_subdirectory("modules")

if(LUMINA_BUILD_TOOLS)
	add_subdirectory("tools")
endif()
