#!/usr/bin/env pwsh
# ============================================================================
# Luminoveau Shader Compilation Script
# ============================================================================
# Compiles all shaders to their target formats and generates:
# - Backend-specific .cpp files (e.g., shader_name.spirv.cpp)
# - Unified header file (shaders_generated.h)
# - CMake sources file (Sources.Shaders.cmake)
#
# Auto-discovers shader pairs (.vert.hlsl + .frag.hlsl) in the current directory
#
# Usage:
#   .\compile_shaders.ps1 [-Backend <spirv|dxil|metallib|all>] [-Shader <n>] [-Clean]
#
# Examples:
#   .\compile_shaders.ps1                    # Compile all shaders, all backends
#   .\compile_shaders.ps1 -Backend spirv     # Compile only SPIR-V
#   .\compile_shaders.ps1 -Shader sprite     # Compile only sprite shaders
#   .\compile_shaders.ps1 -Clean             # Clean generated files
# ============================================================================

param(
    [ValidateSet("spirv", "dxil", "metallib", "all")]
    [string]$Backend = "all",  # Compile all backends by default
    
    [string]$Shader = "",  # Empty = all shaders
    
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# ============================================================================
# Configuration
# ============================================================================

# Shader profiles
# DXIL SM6.0 for modern DirectX 12 (works on PC and Xbox One S)
$VertexProfile = "vs_6_0"
$FragmentProfile = "ps_6_0"

$ScriptDir = $PSScriptRoot
$ProjectRoot = Split-Path $ScriptDir -Parent
$ShaderSourceDir = $ScriptDir
$OutputDir = Join-Path $ProjectRoot "assethandler\shaders"
$HeaderOutputPath = Join-Path $ProjectRoot "assethandler\shaders_generated.h"
$CMakeOutputPath = Join-Path $ProjectRoot "cmake\Sources.Shaders.cmake"

# ============================================================================
# Helper Functions
# ============================================================================

function Write-Header {
    param([string]$Message)
    Write-Host "`n============================================================================" -ForegroundColor Cyan
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "============================================================================`n" -ForegroundColor Cyan
}

function Write-Success {
    param([string]$Message)
    Write-Host "✓ $Message" -ForegroundColor Green
}

function Write-Error-Custom {
    param([string]$Message)
    Write-Host "✗ $Message" -ForegroundColor Red
}

function Write-Info {
    param([string]$Message)
    Write-Host "→ $Message" -ForegroundColor Yellow
}

function Clean-GeneratedFiles {
    Write-Header "Cleaning Generated Files"
    
    if (Test-Path $OutputDir) {
        Remove-Item -Path "$OutputDir\*" -Recurse -Force
        Write-Success "Cleaned $OutputDir"
    }
    
    if (Test-Path $HeaderOutputPath) {
        Remove-Item -Path $HeaderOutputPath -Force
        Write-Success "Removed $HeaderOutputPath"
    }
    
    if (Test-Path $CMakeOutputPath) {
        Remove-Item -Path $CMakeOutputPath -Force
        Write-Success "Removed $CMakeOutputPath"
    }
}

function Ensure-Directory {
    param([string]$Path)
    if (-not (Test-Path $Path)) {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Discover-Shaders {
    Write-Header "Discovering Shader Pairs"
    
    $shaderPairs = @()
    
    # Find all .vert.hlsl files
    $vertFiles = Get-ChildItem -Path $ShaderSourceDir -Filter "*.vert.hlsl"
    
    foreach ($vertFile in $vertFiles) {
        # Extract base name (everything before .vert.hlsl)
        $baseName = $vertFile.Name -replace '\.vert\.hlsl$', ''
        
        # Check if corresponding .frag.hlsl exists
        $fragFile = Join-Path $ShaderSourceDir "$baseName.frag.hlsl"
        
        if (Test-Path $fragFile) {
            # Convert base name to PascalCase for symbol name
            # e.g., fullscreen_quad -> FullscreenQuad
            $symbolName = ($baseName -split '_' | ForEach-Object { 
                $_.Substring(0,1).ToUpper() + $_.Substring(1).ToLower() 
            }) -join ''
            
            $shaderPairs += @{
                Name = $symbolName
                BaseName = $baseName
                VertFile = $vertFile.Name
                FragFile = "$baseName.frag.hlsl"
            }
            
            Write-Success "Found shader pair: $baseName → $symbolName"
        } else {
            Write-Info "Skipping $($vertFile.Name) - no matching fragment shader"
        }
    }
    
    if ($shaderPairs.Count -eq 0) {
        Write-Error-Custom "No shader pairs found in $ShaderSourceDir"
        exit 1
    }
    
    Write-Host "`nDiscovered $($shaderPairs.Count) shader pair(s)`n" -ForegroundColor White
    
    return $shaderPairs
}

function Compile-SPIRV {
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$Profile,
        [string]$ShaderName
    )
    
    Write-Info "Compiling $ShaderName to SPIR-V..."
    
    # Compile using DXC to SPIR-V
    $dxcArgs = @(
        "-T", $Profile,
        "-E", "main",
        "-spirv",
        "-Fo", $OutputFile,
        $SourceFile
    )
    
    $dxcProcess = Start-Process -FilePath "dxc" -ArgumentList $dxcArgs -NoNewWindow -Wait -PassThru
    
    if ($dxcProcess.ExitCode -ne 0) {
        Write-Error-Custom "DXC compilation failed for $ShaderName (exit code $($dxcProcess.ExitCode))"
        return $false
    }
    
    Write-Success "Compiled $ShaderName"
    return $true
}

function Compile-DXIL {
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$Profile,
        [string]$ShaderName
    )
    
    Write-Info "Compiling $ShaderName to DXIL (SM6.0)..."
    
    # Compile using DXC to DXIL (Shader Model 6.0)
    $dxcArgs = @(
        "-T", $Profile,
        "-E", "main",
        "-Fo", $OutputFile,
        $SourceFile
    )
    
    $dxcProcess = Start-Process -FilePath "dxc" -ArgumentList $dxcArgs -NoNewWindow -Wait -PassThru
    
    if ($dxcProcess.ExitCode -ne 0) {
        Write-Error-Custom "DXC compilation failed for $ShaderName (exit code $($dxcProcess.ExitCode))"
        return $false
    }
    
    Write-Success "Compiled $ShaderName"
    return $true
}

function Compile-Metal {
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$ShaderName
    )
    
    # TODO: Implement Metal compilation
    # Steps: HLSL → SPIR-V → Metal (using spirv-cross)
    Write-Info "Metal compilation not yet implemented for $ShaderName"
    return $false
}

function Generate-CppFile {
    param(
        [string]$BinaryFile,
        [string]$OutputFile,
        [string]$SymbolName,
        [string]$Backend
    )
    
    # Read binary data
    $bytes = [System.IO.File]::ReadAllBytes($BinaryFile)
    $length = $bytes.Length
    
    # Format bytes as C++ array
    $byteArray = ""
    for ($i = 0; $i -lt $bytes.Length; $i += 12) {
        $byteArray += "  "
        $end = [Math]::Min($i + 12, $bytes.Length)
        for ($j = $i; $j -lt $end; $j++) {
            $byteArray += "0x{0:x2}" -f $bytes[$j]
            if ($j -lt $bytes.Length - 1) {
                $byteArray += ", "
            }
        }
        $byteArray += "`n"
    }
    
    # Generate C++ file with extern linkage
    # Symbol names are the SAME across backends - CMake selects which .cpp to compile
    $cppContent = @"
// Auto-generated shader binary - DO NOT EDIT
// Source: $BinaryFile
// Backend: $Backend
// Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")

#include <cstdint>
#include <cstddef>

namespace Luminoveau {
namespace Shaders {

extern const uint8_t ${SymbolName}[] = {
$byteArray};

extern const size_t ${SymbolName}_Size = $length;

} // namespace Shaders
} // namespace Luminoveau
"@
    
    [System.IO.File]::WriteAllText($OutputFile, $cppContent)
    Write-Success "Generated $OutputFile ($length bytes)"
}

function Generate-HeaderFile {
    param([array]$CompiledShaders)
    
    Write-Header "Generating Unified Header"
    
    $headerContent = @"
// Auto-generated shader header - DO NOT EDIT
// Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
//
// This file provides a unified interface to all compiled shaders.
// The actual backend (SPIR-V, DXIL, Metal) is determined at compile time
// by which .cpp files are included in the build.

#pragma once

#include <cstdint>
#include <cstddef>

namespace Luminoveau {
namespace Shaders {

"@
    
    foreach ($shader in $CompiledShaders) {
        $headerContent += @"
    // $($shader.Name) Shaders
    extern const uint8_t $($shader.VertSymbol)[];
    extern const size_t $($shader.VertSymbol)_Size;
    extern const uint8_t $($shader.FragSymbol)[];
    extern const size_t $($shader.FragSymbol)_Size;
    
"@
    }
    
    $headerContent += @"
} // namespace Shaders
} // namespace Luminoveau
"@
    
    [System.IO.File]::WriteAllText($HeaderOutputPath, $headerContent)
    Write-Success "Generated $HeaderOutputPath"
}

function Generate-CMakeFile {
    param(
        [array]$CompiledShaders,
        [string]$Backend
    )
    
    Write-Header "Generating CMake Sources File"
    
    # Generate conditional CMake file that selects backend at build time
    $cmakeContent = @"
# Auto-generated shader sources - DO NOT EDIT
# Generated: $(Get-Date -Format "yyyy-MM-dd HH:mm:ss")
# Available backends: $Backend

# Set default GPU backend if not specified
if(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB)")
endif()

# Validate backend selection
if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB)$")
    message(FATAL_ERROR "Invalid LUMINOVEAU_GPU_BACKEND: `${LUMINOVEAU_GPU_BACKEND}. Must be SPIRV, DXIL, or METALLIB")
endif()

message(STATUS "Luminoveau GPU Backend: `${LUMINOVEAU_GPU_BACKEND}")

# Select shader files based on backend
if(LUMINOVEAU_GPU_BACKEND STREQUAL "SPIRV")
    set(LUMINOVEAU_SHADER_SOURCES
"@
    
    foreach ($shader in $CompiledShaders) {
        if ($shader.VertCppSPIRV) {
            $cmakeContent += "`n        assethandler/shaders/$($shader.VertCppSPIRV)"
            $cmakeContent += "`n        assethandler/shaders/$($shader.FragCppSPIRV)"
        }
    }
    
    $cmakeContent += "`n    )`n"
    
    # DXIL section
    $cmakeContent += "elseif(LUMINOVEAU_GPU_BACKEND STREQUAL `"DXIL`")`n"
    $cmakeContent += "    set(LUMINOVEAU_SHADER_SOURCES`n"
    
    foreach ($shader in $CompiledShaders) {
        if ($shader.VertCppDXIL) {
            $cmakeContent += "`n        assethandler/shaders/$($shader.VertCppDXIL)"
            $cmakeContent += "`n        assethandler/shaders/$($shader.FragCppDXIL)"
        }
    }
    
    $cmakeContent += @"

    )
else()
    message(FATAL_ERROR "No shader files available for backend: `${LUMINOVEAU_GPU_BACKEND}")
endif()

# Group shader files in IDE
source_group("Generated\\Shaders" FILES `${LUMINOVEAU_SHADER_SOURCES})

# Define shader backend for C++ code
add_compile_definitions(LUMINOVEAU_SHADER_BACKEND_`${LUMINOVEAU_GPU_BACKEND})
"@
    
    [System.IO.File]::WriteAllText($CMakeOutputPath, $cmakeContent)
    Write-Success "Generated $CMakeOutputPath"
}

# ============================================================================
# Main Compilation Logic
# ============================================================================

function Compile-AllShaders {
    Write-Header "Luminoveau Shader Compilation"
    Write-Host "Backend: $Backend" -ForegroundColor White
    Write-Host "Output Directory: $OutputDir`n" -ForegroundColor White
    
    # Discover shader pairs from directory
    $shaderDefs = Discover-Shaders
    
    # Ensure output directory exists
    Ensure-Directory $OutputDir
    
    # Track compiled shaders for header/cmake generation
    $compiledShaders = @()
    
    foreach ($shaderDef in $shaderDefs) {
        # Skip if specific shader requested and this isn't it
        if ($Shader -and $shaderDef.Name -ne $Shader -and $shaderDef.BaseName -ne $Shader) {
            continue
        }
        
        Write-Header "Compiling $($shaderDef.Name) Shaders"
        
        $vertSource = Join-Path $ShaderSourceDir $shaderDef.VertFile
        $fragSource = Join-Path $ShaderSourceDir $shaderDef.FragFile
        
        $shaderInfo = @{
            Name = $shaderDef.Name
            VertSymbol = "$($shaderDef.Name)_Vert"
            FragSymbol = "$($shaderDef.Name)_Frag"
            VertCppSPIRV = ""
            FragCppSPIRV = ""
            VertCppDXIL = ""
            FragCppDXIL = ""
        }
        
        # Compile to SPIR-V
        if ($Backend -eq "spirv" -or $Backend -eq "all") {
            $vertSpv = Join-Path $OutputDir "$($shaderDef.BaseName)_vert.spv"
            $fragSpv = Join-Path $OutputDir "$($shaderDef.BaseName)_frag.spv"
            
            $vertSuccess = Compile-SPIRV -SourceFile $vertSource -OutputFile $vertSpv -Profile $VertexProfile -ShaderName "$($shaderDef.Name) Vertex"
            $fragSuccess = Compile-SPIRV -SourceFile $fragSource -OutputFile $fragSpv -Profile $FragmentProfile -ShaderName "$($shaderDef.Name) Fragment"
            
            if ($vertSuccess -and $fragSuccess) {
                # Generate C++ files
                $shaderInfo.VertCppSPIRV = "$($shaderDef.BaseName)_vert.spirv.cpp"
                $shaderInfo.FragCppSPIRV = "$($shaderDef.BaseName)_frag.spirv.cpp"
                
                Generate-CppFile -BinaryFile $vertSpv -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppSPIRV) -SymbolName $shaderInfo.VertSymbol -Backend "SPIR-V"
                Generate-CppFile -BinaryFile $fragSpv -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppSPIRV) -SymbolName $shaderInfo.FragSymbol -Backend "SPIR-V"
            }
        }
        
        # Compile to DXIL
        if ($Backend -eq "dxil" -or $Backend -eq "all") {
            $vertDxil = Join-Path $OutputDir "$($shaderDef.BaseName)_vert.dxil"
            $fragDxil = Join-Path $OutputDir "$($shaderDef.BaseName)_frag.dxil"
            
            $vertSuccess = Compile-DXIL -SourceFile $vertSource -OutputFile $vertDxil -Profile $VertexProfile -ShaderName "$($shaderDef.Name) Vertex"
            $fragSuccess = Compile-DXIL -SourceFile $fragSource -OutputFile $fragDxil -Profile $FragmentProfile -ShaderName "$($shaderDef.Name) Fragment"
            
            if ($vertSuccess -and $fragSuccess) {
                # Generate C++ files
                $shaderInfo.VertCppDXIL = "$($shaderDef.BaseName)_vert.dxil.cpp"
                $shaderInfo.FragCppDXIL = "$($shaderDef.BaseName)_frag.dxil.cpp"
                
                Generate-CppFile -BinaryFile $vertDxil -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppDXIL) -SymbolName $shaderInfo.VertSymbol -Backend "DXIL"
                Generate-CppFile -BinaryFile $fragDxil -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppDXIL) -SymbolName $shaderInfo.FragSymbol -Backend "DXIL"
            }
        }
        
        # Add to compiled shaders if at least one backend succeeded
        if ($shaderInfo.VertCppSPIRV -or $shaderInfo.VertCppDXIL) {
            $compiledShaders += $shaderInfo
        }
    }
    
    # Generate header and cmake files
    if ($compiledShaders.Count -gt 0) {
        Generate-HeaderFile -CompiledShaders $compiledShaders
        Generate-CMakeFile -CompiledShaders $compiledShaders -Backend $Backend
        
        # Clean up intermediate binary files
        Write-Info "Cleaning up intermediate binary files..."
        $binaryFiles = @()
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.spv" -File
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.dxil" -File
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.dxbc" -File
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.metallib" -File
        
        foreach ($binaryFile in $binaryFiles) {
            Remove-Item -Path $binaryFile.FullName -Force
        }
        if ($binaryFiles.Count -gt 0) {
            Write-Success "Removed $($binaryFiles.Count) intermediate binary file(s)"
        }
        
        Write-Header "Compilation Complete"
        Write-Success "Compiled $($compiledShaders.Count) shader(s)"
    } else {
        Write-Error-Custom "No shaders were compiled successfully"
        exit 1
    }
}

# ============================================================================
# Entry Point
# ============================================================================

try {
    if ($Clean) {
        Clean-GeneratedFiles
        Write-Success "Clean complete"
        exit 0
    }
    
    # Check for DXC compiler
    $dxc = Get-Command dxc -ErrorAction SilentlyContinue
    if (-not $dxc) {
        Write-Error-Custom "DXC compiler not found. Please install the DirectX Shader Compiler."
        Write-Host "Download from: https://github.com/microsoft/DirectXShaderCompiler/releases" -ForegroundColor Yellow
        exit 1
    }
    
    Compile-AllShaders
    
} catch {
    Write-Error-Custom "An error occurred: $_"
    Write-Host $_.ScriptStackTrace -ForegroundColor Red
    exit 1
}
