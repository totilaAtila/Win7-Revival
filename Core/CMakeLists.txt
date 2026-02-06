cmake_minimum_required(VERSION 3.20)
project(CrystalFrame.Core)

# C++20 required
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Windows subsystem
set(CMAKE_WIN32_EXECUTABLE ON)

# Source files
set(SOURCES
    main.cpp
    Core.cpp
    Diagnostics.cpp
    ConfigManager.cpp
    ShellTargetLocator.cpp
    OverlayHost.cpp
    Renderer.cpp
    IpcBridge.cpp
)

# Header files
set(HEADERS
    Core.h
    Diagnostics.h
    ConfigManager.h
    ShellTargetLocator.h
    OverlayHost.h
    Renderer.h
    IpcBridge.h
)

# Create executable
add_executable(CrystalFrame.Core WIN32 ${SOURCES} ${HEADERS})

# Link libraries
target_link_libraries(CrystalFrame.Core
    # Windows libraries
    user32.lib
    gdi32.lib
    shell32.lib
    advapi32.lib
    
    # DirectComposition
    dcomp.lib
    
    # Direct2D
    d2d1.lib
    dwrite.lib
)

# Compiler options for MSVC
if(MSVC)
    target_compile_options(CrystalFrame.Core PRIVATE
        /W4                 # Warning level 4
        /WX                 # Treat warnings as errors
        /permissive-        # Standards conformance
        /MP                 # Multi-processor compilation
    )
    
    # Unicode support
    target_compile_definitions(CrystalFrame.Core PRIVATE
        UNICODE
        _UNICODE
    )
endif()

# Set output directory
set_target_properties(CrystalFrame.Core PROPERTIES
    RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/bin"
)

# Installation
install(TARGETS CrystalFrame.Core
    RUNTIME DESTINATION bin
)
