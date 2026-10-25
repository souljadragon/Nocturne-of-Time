#------------------------------------------------------------------------------------------------------------
#
# Automate-VCPKG by Andre Taulien
# ===============================
#
# Project Repository: https://github.com/REGoth-project/Automate-VCPKG
# License ..........: MIT, see end of file.
#
# Based on: https://github.com/sutambe/cpptruths/blob/vcpkg_cmake_blog/cpp0x/vcpkg_test/CMakeLists.txt
#
# 
# While [Vcpkg](https://github.com/microsoft/vcpkg) on it's own is awesome, it does add 
# a little bit of complexity to getting a project to build. Even more if the one trying 
# to compile your application is not too fond of the commandline. Additionally, CMake 
# commands tend to get rather long with the toolchain path. 
# 
# To keep things simple for new users who just want to get the project to build, this 
# script offers a solution.
# 
# Lets assume your main `CMakelists.txt` looks something like this:
#
#     cmake_minimum_required (VERSION 3.12.0)
#     project (MyProject)
#     
#     add_executable(MyExecutable main.c)
# 
# To integrate Vcpkg into that `CMakelists.txt`, simple put the following lines before the 
# call to `project(MyProject)`:
# 
#     include(cmake/automate-vcpkg.cmake)
#     
#     vcpkg_bootstrap()
#     vcpkg_install_packages(libsquish physfs)
# 
# The call to `vcpkg_bootstrap()` will clone the official Vcpkg repository and bootstrap it. 
# If it detected an existing environment variable defining a valid `VCPKG_ROOT`, it will 
# update the existing installation of Vcpkg.
# 
# Arguments to `vcpkg_install_packages()` are the packages you want to install using Vcpkg.
# 
# If you want to keep the possibility for users to chose their own copy of Vcpkg, you can 
# simply not run the code snippet mentioned above, something like this will work:
# 
#     option(SKIP_AUTOMATE_VCPKG "When ON, you will need to built the packages 
#      required by MyProject on your own or supply your own vcpkg toolchain.")
#     
#     if (NOT SKIP_AUTOMATE_VCPKG)
#       include(cmake/automate-vcpkg.cmake)
#     
#       vcpkg_bootstrap()
#       vcpkg_install_packages(libsquish physfs)
#     endif()
#  
# Then, the user has to supply the packages on their own, be it through Vcpkg or manually 
# specifying their locations.
#------------------------------------------------------------------------------------------------------------

cmake_minimum_required (VERSION 3.12)

option(AUTOMATE_VCPKG_AUTO_UPDATE "git pull the bundled vcpkg on every configure" OFF)

if(WIN32)
    set(VCPKG_FALLBACK_ROOT ${CMAKE_CURRENT_BINARY_DIR}/vcpkg CACHE STRING "vcpkg configuration directory to use if vcpkg was not installed on the system before")
else()
    set(VCPKG_FALLBACK_ROOT ${CMAKE_CURRENT_BINARY_DIR}/.vcpkg CACHE STRING  "vcpkg configuration directory to use if vcpkg was not installed on the system before")
endif()

# On Windows, Vcpkg defaults to x86, even on x64 systems. If we're 
# doing a 64-bit build, we need to fix that.
if (WIN32)

    # Since the compiler checks haven't run yet, we need to figure
    # out the value of CMAKE_SIZEOF_VOID_P ourselfs

    include(CheckTypeSize)
    enable_language(C)
    check_type_size("void*" SIZEOF_VOID_P BUILTIN_TYPES_ONLY)
    
    if (SIZEOF_VOID_P EQUAL 8)
        message(STATUS "Using Vcpkg triplet 'x64-windows'")
        
        set(VCPKG_TRIPLET x64-windows)
    endif()
endif()

if(NOT DEFINED VCPKG_ROOT)
	set(VCPKG_ROOT ${VCPKG_FALLBACK_ROOT})
endif()

# Installs a new copy of Vcpkg or updates an existing one
macro(vcpkg_bootstrap)
    _install_or_update_vcpkg()

    # Find out whether the user supplied their own VCPKG toolchain file
    if(NOT DEFINED ${CMAKE_TOOLCHAIN_FILE})
        # We know this wasn't set before so we need point the toolchain file to the newly found VCPKG_ROOT
        set(CMAKE_TOOLCHAIN_FILE ${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake CACHE STRING "")
    
        # Just setting vcpkg.cmake as toolchain file does not seem to actually pull in the code
        include(${VCPKG_ROOT}/scripts/buildsystems/vcpkg.cmake)
    
        set(AUTOMATE_VCPKG_USE_SYSTEM_VCPKG OFF)
    else()
        # VCPKG_ROOT has been defined by the toolchain file already
        set(AUTOMATE_VCPKG_USE_SYSTEM_VCPKG ON)
    endif()
    
    message(STATUS "Automate VCPKG status:")
    message(STATUS "  VCPKG_ROOT.....: ${VCPKG_ROOT}")
    message(STATUS "  VCPKG_EXEC.....: ${VCPKG_EXEC}")
    message(STATUS "  VCPKG_BOOTSTRAP: ${VCPKG_BOOTSTRAP}")
endmacro()

macro(_install_or_update_vcpkg)
    if(NOT EXISTS ${VCPKG_ROOT})
        message(STATUS "Cloning vcpkg in ${VCPKG_ROOT}")
        execute_process(COMMAND git clone https://github.com/Microsoft/vcpkg.git ${VCPKG_ROOT} --depth 1)

        # If a reproducible build is desired (and potentially old libraries are # ok), uncomment the
        # following line and pin the vcpkg repository to a specific githash.
        # execute_process(COMMAND git checkout 745a0aea597771a580d0b0f4886ea1e3a94dbca6 WORKING_DIRECTORY ${VCPKG_ROOT})
    elseif(AUTOMATE_VCPKG_AUTO_UPDATE)
        # The following command has no effect if the vcpkg repository is in a detached head state.
        message(STATUS "Auto-updating vcpkg in ${VCPKG_ROOT}")
        execute_process(COMMAND git pull WORKING_DIRECTORY ${VCPKG_ROOT})
    else()
        # Nocturne of Time: don't pull a newer vcpkg on every reconfigure. A pull can change
        # port versions, which makes vcpkg rebuild (and sometimes lose) packages such as SDL2
        # when CMake re-runs from inside a build. Pass -DAUTOMATE_VCPKG_AUTO_UPDATE=ON to update.
        message(STATUS "Using existing vcpkg in ${VCPKG_ROOT} (auto-update off)")
    endif()

    if(NOT EXISTS ${VCPKG_ROOT}/README.md)
        message(FATAL_ERROR "***** FATAL ERROR: Could not clone vcpkg *****")
    endif()

    if(WIN32)
        set(VCPKG_EXEC ${VCPKG_ROOT}/vcpkg.exe)
        set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.bat)
    else()
        set(VCPKG_EXEC ${VCPKG_ROOT}/vcpkg)
        set(VCPKG_BOOTSTRAP ${VCPKG_ROOT}/bootstrap-vcpkg.sh)
    endif()

    if(NOT EXISTS ${VCPKG_EXEC})
        message("Bootstrapping vcpkg in ${VCPKG_ROOT}")
        execute_process(COMMAND ${VCPKG_BOOTSTRAP} WORKING_DIRECTORY ${VCPKG_ROOT})
    endif()

    if(NOT EXISTS ${VCPKG_EXEC})
        message(FATAL_ERROR "***** FATAL ERROR: Could not bootstrap vcpkg *****")
    endif()
   
endmacro()

# Installs the list of packages given as parameters using Vcpkg
macro(vcpkg_install_packages)
    
    # Need the given list to be space-separated
    #string (REPLACE ";" " " PACKAGES_LIST_STR "${ARGN}")

    message(STATUS "Installing/Updating the following vcpkg-packages: ${PACKAGES_LIST_STR}")

    if (VCPKG_TRIPLET)
        set(ENV{VCPKG_DEFAULT_TRIPLET} "${VCPKG_TRIPLET}")
    endif()

    # Nocturne of Time: only run `vcpkg install` until it has succeeded once for this package list.
    # Reconfigures triggered from inside a Visual Studio build (ZERO_CHECK) re-ran it in MSBuild's
    # environment, where vcpkg decided to rebuild packages, failed ("batch label ... VCEnd"), and left
    # SDL2 missing. Pass -DAUTOMATE_VCPKG_FORCE_INSTALL=ON to run it again anyway.
    string(JOIN " " _automate_vcpkg_pkgs ${ARGN})
    set(_automate_vcpkg_stamp "${VCPKG_ROOT}/installed/.automate-vcpkg-${VCPKG_TRIPLET}.stamp")
    set(_automate_vcpkg_prev "")
    if(EXISTS "${_automate_vcpkg_stamp}")
        file(READ "${_automate_vcpkg_stamp}" _automate_vcpkg_prev)
    endif()

    if(NOT AUTOMATE_VCPKG_FORCE_INSTALL AND "${_automate_vcpkg_prev}" STREQUAL "${_automate_vcpkg_pkgs}")
        message(STATUS "vcpkg packages already installed, skipping `vcpkg install`")
    else()
        execute_process(
            COMMAND ${VCPKG_EXEC} install ${ARGN}
            WORKING_DIRECTORY ${VCPKG_ROOT}
            RESULT_VARIABLE _automate_vcpkg_result
            )
        if(_automate_vcpkg_result EQUAL 0)
            file(WRITE "${_automate_vcpkg_stamp}" "${_automate_vcpkg_pkgs}")
        else()
            message(WARNING "vcpkg install failed (${_automate_vcpkg_result}); it will be retried on the next configure")
        endif()
    endif()
endmacro()
    
# MIT License
# 
# Copyright (c) 2019 REGoth-project
# 
# Permission is hereby granted, free of charge, to any person obtaining a copy
# of this software and associated documentation files (the "Software"), to deal
# in the Software without restriction, including without limitation the rights
# to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
# copies of the Software, and to permit persons to whom the Software is
# furnished to do so, subject to the following conditions:
# 
# The above copyright notice and this permission notice shall be included in all
# copies or substantial portions of the Software.
# 
# THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
# IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
# FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
# AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
# LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
# OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
# SOFTWARE.
