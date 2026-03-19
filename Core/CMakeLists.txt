cmake_minimum_required(VERSION 3.20)
project(GlassBar.Core)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Source files (removed main.cpp, IpcBridge.cpp - no longer needed)
set(SOURCES
    Core.cpp
    CoreApi.cpp
    Diagnostics.cpp
    ConfigManager.cpp
    ShellTargetLocator.cpp
    Renderer.cpp
    StartMenuHook.cpp
    StartMenuWindow.cpp
    AllProgramsEnumerator.cpp
)

# Header files (removed IpcBridge.h, added CoreApi.h)
set(HEADERS
    Core.h
    CoreApi.h
    Diagnostics.h
    ConfigManager.h
    ShellTargetLocator.h
    Renderer.h
    StartMenuHook.h
    StartMenuWindow.h
    AllProgramsEnumerator.h
)

# Create shared library (DLL)
add_library(GlassBar.Core SHARED ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(GlassBar.Core
    # Windows libraries
    user32.lib
    gdi32.lib
    shell32.lib
    ole32.lib
    advapi32.lib
    dwmapi.lib
)

# Compiler options for MSVC
if(MSVC)
    target_compile_options(GlassBar.Core PRIVATE
        /W4                 # Warning level 4
        /permissive-        # Standards conformance
        # /MP removed - causes heap errors on some systems
        /Zm300              # Increase compiler heap size to 300%
        /bigobj             # Support large object files
        /d2ReducedOptimizeHugeFunctions  # Reduce optimization for huge functions
    )

    # Static CRT: embed runtime into the DLL so VCRUNTIME140.dll / MSVCP140.dll
    # are not required in the publish folder or on the target machine.
    set_property(TARGET GlassBar.Core PROPERTY
        MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")

    # Definitions
    target_compile_definitions(GlassBar.Core PRIVATE
        UNICODE
        _UNICODE
        CRYSTALFRAME_EXPORTS  # Export symbols for DLL
    )
endif()

# Set output directory
set_target_properties(GlassBar.Core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    LIBRARY_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
    ARCHIVE_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/lib"
)

# Installation
install(TARGETS GlassBar.Core
    RUNTIME DESTINATION bin
    LIBRARY DESTINATION bin
    ARCHIVE DESTINATION lib
)

# Diagnostic tool (standalone executable)
add_executable(WindowDiagnostic WindowDiagnostic.cpp)

target_link_libraries(WindowDiagnostic
    user32.lib
    psapi.lib
)

if(MSVC)
    target_compile_definitions(WindowDiagnostic PRIVATE
        UNICODE
        _UNICODE
    )
endif()

set_target_properties(WindowDiagnostic PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)
