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
    [ValidateSet("spirv", "dxil", "metallib", "wgsl", "all")]
    [string]$Backend = "all",  # Compile all backends by default
    
    [string]$Shader = "",  # Empty = all shaders
    
    [switch]$Clean
)

$ErrorActionPreference = "Stop"

# ============================================================================
# Configuration
# ============================================================================

# Shader profiles (HLSL Shader Model 6.0)
$VertexProfile = "vs_6_0"
$FragmentProfile = "ps_6_0"
$ComputeProfile = "cs_6_0"

# Metal Shading Language version (2.1 = macOS 10.14+, covers M1 and later)
$MetalLanguageVersion = "2.1"
# macOS deployment target for metal compiler
$MetalDeploymentTarget = "11.0"

$ScriptDir = $PSScriptRoot
$ProjectRoot = Split-Path $ScriptDir -Parent
$ShaderSourceDir = $ScriptDir
$OutputDir = Join-Path $ProjectRoot "src" "assets" "shaders"
$HeaderOutputPath = Join-Path $ProjectRoot "src" "assets" "shaders_generated.h"
$CMakeOutputPath = Join-Path $ProjectRoot "cmake" "Sources.Shaders.cmake"

# Platform detection
#$IsMacOS = $IsMacOS -or ($PSVersionTable.OS -and $PSVersionTable.OS.Contains("Darwin")) -or (Test-Path "/usr/bin/xcrun")
#$IsWindows = $IsWindows -or ($PSVersionTable.OS -and $PSVersionTable.OS.Contains("Windows")) -or ($env:OS -eq "Windows_NT")

# ============================================================================
# Helper Functions
# ============================================================================

function Write-Header
{
    param([string]$Message)
    Write-Host "`n============================================================================" -ForegroundColor Cyan
    Write-Host $Message -ForegroundColor Cyan
    Write-Host "============================================================================`n" -ForegroundColor Cyan
}

function Write-Success
{
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Green
}

function Write-Error-Custom
{
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Red
}

function Write-Info
{
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor Yellow
}

function Write-Skip
{
    param([string]$Message)
    Write-Host "  $Message" -ForegroundColor DarkGray
}

function Clean-GeneratedFiles
{
    Write-Header "Cleaning Generated Files"

    if (Test-Path $OutputDir)
    {
        Get-ChildItem -Path $OutputDir -File | Remove-Item -Force
        Write-Success "Cleaned $OutputDir"
    }

    if (Test-Path $HeaderOutputPath)
    {
        Remove-Item -Path $HeaderOutputPath -Force
        Write-Success "Removed $HeaderOutputPath"
    }

    if (Test-Path $CMakeOutputPath)
    {
        Remove-Item -Path $CMakeOutputPath -Force
        Write-Success "Removed $CMakeOutputPath"
    }
}

function Ensure-Directory
{
    param([string]$Path)
    if (-not (Test-Path $Path))
    {
        New-Item -ItemType Directory -Path $Path -Force | Out-Null
    }
}

function Run-Process
{
    param(
        [string]$FilePath,
        [string[]]$Arguments,
        [string]$Description
    )

    $process = Start-Process -FilePath $FilePath -ArgumentList $Arguments -NoNewWindow -Wait -PassThru -RedirectStandardError (Join-Path ([System.IO.Path]::GetTempPath()) "shader_stderr.txt")

    if ($process.ExitCode -ne 0)
    {
        $stderr = Get-Content (Join-Path ([System.IO.Path]::GetTempPath()) "shader_stderr.txt") -Raw -ErrorAction SilentlyContinue
        Write-Error-Custom "$Description failed (exit code $($process.ExitCode))"
        if ($stderr)
        { Write-Error-Custom $stderr 
        }
        return $false
    }
    return $true
}

function Discover-Shaders
{
    Write-Header "Discovering Shader Pairs"

    $shaderPairs = @()

    # Find all .vert.hlsl files
    $vertFiles = Get-ChildItem -Path $ShaderSourceDir -Filter "*.vert.hlsl"

    foreach ($vertFile in $vertFiles)
    {
        # Extract base name (everything before .vert.hlsl)
        $baseName = $vertFile.Name -replace '\.vert\.hlsl$', ''

        # Check if corresponding .frag.hlsl exists
        $fragFile = Join-Path $ShaderSourceDir "$baseName.frag.hlsl"

        if (Test-Path $fragFile)
        {
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

            Write-Success "Found shader pair: $baseName -> $symbolName"
        } else
        {
            Write-Info "Skipping $($vertFile.Name) - no matching fragment shader"
        }
    }

    if ($shaderPairs.Count -eq 0)
    {
        Write-Error-Custom "No shader pairs found in $ShaderSourceDir"
        exit 1
    }

    Write-Host "`nDiscovered $($shaderPairs.Count) shader pair(s)`n" -ForegroundColor White

    return $shaderPairs
}

function Discover-ComputeShaders
{
    Write-Header "Discovering Compute Shaders"

    $computeShaders = @()

    $compFiles = Get-ChildItem -Path $ShaderSourceDir -Filter "*.comp.hlsl"

    foreach ($compFile in $compFiles)
    {
        $baseName = $compFile.Name -replace '\.comp\.hlsl$', ''

        $symbolName = ($baseName -split '_' | ForEach-Object {
                $_.Substring(0,1).ToUpper() + $_.Substring(1).ToLower()
            }) -join ''

        $computeShaders += @{
            Name = $symbolName
            BaseName = $baseName
            CompFile = $compFile.Name
            CompSymbol = "${symbolName}_Comp"
        }

        Write-Success "Found compute shader: $baseName -> ${symbolName}_Comp"
    }

    Write-Host "`nDiscovered $($computeShaders.Count) compute shader(s)`n" -ForegroundColor White

    return $computeShaders
}

# ============================================================================
# Shader Compilation Functions
# ============================================================================

function Compile-SPIRV
{
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$Profile,
        [string]$ShaderName
    )

    Write-Info "Compiling $ShaderName to SPIR-V..."

    $dxcArgs = @("-T", $Profile, "-E", "main", "-spirv", "-Fo", $OutputFile, $SourceFile)
    $success = Run-Process -FilePath "dxc" -Arguments $dxcArgs -Description "DXC SPIR-V compilation of $ShaderName"

    if ($success)
    { Write-Success "Compiled $ShaderName" 
    }
    return $success
}

function Compile-DXIL
{
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$Profile,
        [string]$ShaderName
    )

    Write-Info "Compiling $ShaderName to DXIL (SM6.0)..."

    $dxcArgs = @("-T", $Profile, "-E", "main", "-Fo", $OutputFile, $SourceFile)
    $success = Run-Process -FilePath "dxc" -Arguments $dxcArgs -Description "DXC DXIL compilation of $ShaderName"

    if ($success)
    { Write-Success "Compiled $ShaderName" 
    }
    return $success
}

function Get-SDLBindingMap
{
    param(
        [string]$SpvFile
    )

    # Use spirv-cross --reflect to get resource layout from SPIRV
    $reflectOutput = & spirv-cross $SpvFile --reflect 2>&1
    if ($LASTEXITCODE -ne 0)
    {
        Write-Error-Custom "spirv-cross --reflect failed: $reflectOutput"
        return $null
    }

    $reflect = $reflectOutput | ConvertFrom-Json

    # ==========================================================================
    # Replicate SDL_shadercross binding algorithm (SDL_shadercross.c lines 955-1221)
    # ==========================================================================

    # Collect texture bindings (from sets 0 or 2)
    $textureResources = @()   # { name, desc_set, binding }
    $numSeparateSamplers = 0

    # Combined image-samplers ("textures" in spirv-cross reflect)
    $combinedTextures = @()
    if ($reflect.textures) { $combinedTextures = @($reflect.textures) }

    if ($combinedTextures.Count -gt 0)
    {
        foreach ($tex in $combinedTextures)
        {
            $textureResources += @{ name = $tex.name; desc_set = [int]$tex.set; binding = [int]$tex.binding }
        }
    }
    else
    {
        # HLSL produces separate images + separate samplers
        $separateSamplers = @()
        if ($reflect.separate_samplers) { $separateSamplers = @($reflect.separate_samplers) }
        $numSeparateSamplers = $separateSamplers.Count

        foreach ($samp in $separateSamplers)
        {
            $textureResources += @{ name = $samp.name; desc_set = [int]$samp.set; binding = [int]$samp.binding }
        }
    }

    # Storage images
    if ($reflect.images)
    {
        foreach ($img in @($reflect.images))
        {
            $textureResources += @{ name = $img.name; desc_set = [int]$img.set; binding = [int]$img.binding }
        }
    }

    # Separate images that don't have matching samplers (excess)
    $separateImages = @()
    if ($reflect.separate_images) { $separateImages = @($reflect.separate_images) }
    for ($i = $numSeparateSamplers; $i -lt $separateImages.Count; $i++)
    {
        $img = $separateImages[$i]
        $textureResources += @{ name = $img.name; desc_set = [int]$img.set; binding = [int]$img.binding }
    }

    $numTextureBindings = $textureResources.Count

    # Collect buffer bindings
    $uniformBufferResources = @()   # { name, desc_set, binding }
    $storageBufferResources = @()   # { name, desc_set, binding }

    if ($reflect.ubos)
    {
        foreach ($ubo in @($reflect.ubos))
        {
            $uniformBufferResources += @{ name = $ubo.name; desc_set = [int]$ubo.set; binding = [int]$ubo.binding }
        }
    }

    if ($reflect.ssbos)
    {
        foreach ($ssbo in @($reflect.ssbos))
        {
            $storageBufferResources += @{ name = $ssbo.name; desc_set = [int]$ssbo.set; binding = [int]$ssbo.binding }
        }
    }

    $uniformBufferCount = $uniformBufferResources.Count

    # Build binding map: resource_name -> { type, msl_index }
    $bindingMap = @{}

    # Textures: msl_texture = binding, msl_sampler = binding
    foreach ($tex in $textureResources)
    {
        $bindingMap[$tex.name] = @{ type = "texture"; msl_index = $tex.binding }
    }

    # Also map the matching separate_images (textures paired with samplers)
    # The first N separate_images match the N separate_samplers
    if ($combinedTextures.Count -eq 0 -and $separateImages.Count -gt 0)
    {
        for ($i = 0; $i -lt [Math]::Min($numSeparateSamplers, $separateImages.Count); $i++)
        {
            $img = $separateImages[$i]
            $bindingMap[$img.name] = @{ type = "texture"; msl_index = [int]$img.binding }
        }
    }

    # Uniform buffers: msl_buffer = binding
    foreach ($ubo in $uniformBufferResources)
    {
        $bindingMap[$ubo.name] = @{ type = "buffer"; msl_index = $ubo.binding }
    }

    # Storage buffers: msl_buffer = uniformBufferCount + (binding - numTextureBindings)
    foreach ($ssbo in $storageBufferResources)
    {
        $mslBuffer = $uniformBufferCount + ($ssbo.binding - $numTextureBindings)
        $bindingMap[$ssbo.name] = @{ type = "buffer"; msl_index = $mslBuffer }
    }

    # Clean up names: HLSL cbuffers get 'type.' prefix in SPIRV reflection
    # but MSL uses just the base name (e.g., 'type.UniformBlock' -> 'UniformBlock')
    $cleanedMap = @{}
    foreach ($key in $bindingMap.Keys)
    {
        $cleanKey = $key -replace '^type\.', ''
        $cleanedMap[$cleanKey] = $bindingMap[$key]
    }

    Write-Info "    SDL binding map: $uniformBufferCount UBO, $($storageBufferResources.Count) SSBO, $numTextureBindings tex"
    foreach ($key in $cleanedMap.Keys)
    {
        $entry = $cleanedMap[$key]
        Write-Info "      $key -> $($entry.type)($($entry.msl_index))"
    }

    return $cleanedMap
}

function Fix-MSLBindings
{
    param(
        [string]$MslFile,
        [hashtable]$BindingMap
    )

    $content = [System.IO.File]::ReadAllText($MslFile)
    $modified = $false

    # Buffers: rewrite every '<name> [[buffer(N)]]' in a single evaluator pass, keyed on the
    # parameter identifier that immediately precedes each attribute. Looking up the real MSL
    # name (rather than searching outward from a reflection name) is order-independent and
    # collision-proof. The old per-resource lookbehind mis-fired when two StructuredBuffers
    # sit adjacent in the signature: spirv-cross emits them at buffer(0)/buffer(1), and the
    # second remap never landed -- leaving e.g. both 'particles' and 'systems' at buffer(1),
    # which xcrun metal rejects ("cannot reserve 'buffer' resource location at index 1").
    $bufferRegex = '(?<name>\b\w+)(?<pre>\s+\[\[buffer\()\d+(?<post>\)\]\])'
    $newContent = [regex]::Replace($content, $bufferRegex, {
        param($m)
        $entry = $BindingMap[$m.Groups['name'].Value]
        if ($entry -and $entry.type -eq 'buffer')
        {
            # Re-emit the captured name -- it's part of the match, so dropping it would
            # delete the parameter identifier and leave a nameless '& [[buffer(N)]]'.
            return $m.Groups['name'].Value + $m.Groups['pre'].Value + $entry.msl_index.ToString() + $m.Groups['post'].Value
        }
        return $m.Value
    })
    if ($newContent -ne $content)
    {
        $content = $newContent
        $modified = $true
    }

    # Textures / samplers: handled per-resource. Unlike adjacent buffers these don't collide,
    # and a sampler is remapped to match its paired texture's index.
    foreach ($resourceName in $BindingMap.Keys)
    {
        $entry = $BindingMap[$resourceName]
        if ($entry.type -ne "texture") { continue }
        $targetIndex = $entry.msl_index

        # Fix texture binding: [[texture(N)]]
        $pattern = "(?<=" + [regex]::Escape($resourceName) + "[^\[]*\[\[texture\()\d+(?=\)\]\])"
        if ($content -match $pattern)
        {
            $content = $content -replace $pattern, $targetIndex.ToString()
            $modified = $true
        }

        # Fix matching sampler binding: [[sampler(N)]]
        # Samplers derived from HLSL separate samplers get their own name
        # but SDL expects msl_sampler = binding (same as texture)
        $samplerPattern = "(?<=" + [regex]::Escape($resourceName) + "[^\[]*\[\[sampler\()\d+(?=\)\]\])"
        if ($content -match $samplerPattern)
        {
            $content = $content -replace $samplerPattern, $targetIndex.ToString()
            $modified = $true
        }
    }

    if ($modified)
    {
        [System.IO.File]::WriteAllText($MslFile, $content)
        Write-Success "    Fixed MSL resource bindings"
    }
    else
    {
        Write-Info "    MSL bindings already correct (no changes needed)"
    }
}

function Compile-MetalLib
{
    param(
        [string]$SourceFile,
        [string]$OutputFile,
        [string]$Profile,
        [string]$ShaderName
    )

    # Pipeline: HLSL -> SPIR-V -> MSL (+ binding fix) -> AIR -> metallib

    $tempDir = Join-Path ([System.IO.Path]::GetTempPath()) "luminoveau_metal"
    Ensure-Directory $tempDir

    $baseTempName = [System.IO.Path]::GetFileNameWithoutExtension($OutputFile)
    $tempSpv   = Join-Path $tempDir "$baseTempName.spv"
    $tempMsl   = Join-Path $tempDir "$baseTempName.metal"
    $tempAir   = Join-Path $tempDir "$baseTempName.air"

    # Step 1: HLSL -> SPIR-V
    Write-Info "  [1/5] HLSL -> SPIR-V ($ShaderName)..."
    $dxcArgs = @("-T", $Profile, "-E", "main", "-spirv", "-Fo", $tempSpv, $SourceFile)
    $success = Run-Process -FilePath "dxc" -Arguments $dxcArgs -Description "DXC SPIR-V for Metal pipeline"
    if (-not $success)
    { return $false 
    }

    # Step 2: SPIR-V -> MSL (with default bindings from spirv-cross)
    Write-Info "  [2/5] SPIR-V -> MSL ($ShaderName)..."
    $crossArgs = @(
        $tempSpv,
        "--msl",
        "--msl-version", ($MetalLanguageVersion -replace '\.', '0'),   # 2.1 -> 20100
        "--output", $tempMsl
    )
    $success = Run-Process -FilePath "spirv-cross" -Arguments $crossArgs -Description "spirv-cross MSL conversion"
    if (-not $success)
    { return $false 
    }

    # Step 3: Compute SDL_shadercross-compatible bindings and fix MSL source
    Write-Info "  [3/5] Fixing MSL resource bindings ($ShaderName)..."
    $bindingMap = Get-SDLBindingMap -SpvFile $tempSpv
    if ($bindingMap -and $bindingMap.Count -gt 0)
    {
        Fix-MSLBindings -MslFile $tempMsl -BindingMap $bindingMap
    }

    # Step 4: MSL -> AIR (Apple Intermediate Representation)
    Write-Info "  [4/5] MSL -> AIR ($ShaderName)..."
    $metalArgs = @(
        "-sdk", "macosx",
        "metal",
        "-c", $tempMsl,
        "-o", $tempAir,
        "-std=macos-metal$MetalLanguageVersion",
        "-mmacosx-version-min=$MetalDeploymentTarget"
    )
    $success = Run-Process -FilePath "xcrun" -Arguments $metalArgs -Description "xcrun metal compilation"
    if (-not $success)
    { return $false 
    }

    # Step 5: AIR -> metallib
    Write-Info "  [5/5] AIR -> metallib ($ShaderName)..."
    $metallibArgs = @("-sdk", "macosx", "metallib", $tempAir, "-o", $OutputFile)
    $success = Run-Process -FilePath "xcrun" -Arguments $metallibArgs -Description "xcrun metallib linking"
    if (-not $success)
    { return $false 
    }

    # Clean up temp files
    Remove-Item -Path $tempSpv, $tempMsl, $tempAir -Force -ErrorAction SilentlyContinue

    Write-Success "Compiled $ShaderName"
    return $true
}

# ============================================================================
# Code Generation Functions
# ============================================================================

function Generate-CppFile
{
    param(
        [string]$BinaryFile,
        [string]$OutputFile,
        [string]$SymbolName,
        [string]$Backend,
        [switch]$NullTerminate
    )

    # Read binary data
    $bytes = [System.IO.File]::ReadAllBytes($BinaryFile)
    if ($NullTerminate) { $bytes = $bytes + [byte]0 }
    $length = $bytes.Length

    # Make the source path relative to the repo root's parent so it's machine-independent
    $repoParent = Split-Path $ProjectRoot -Parent
    $relativeSource = [System.IO.Path]::GetRelativePath($repoParent, $BinaryFile)

    # Format bytes as C++ array
    $byteArray = ""
    for ($i = 0; $i -lt $bytes.Length; $i += 12)
    {
        $byteArray += "  "
        $end = [Math]::Min($i + 12, $bytes.Length)
        for ($j = $i; $j -lt $end; $j++)
        {
            $byteArray += "0x{0:x2}" -f $bytes[$j]
            if ($j -lt $bytes.Length - 1)
            {
                $byteArray += ", "
            }
        }
        $byteArray += "`n"
    }

    # Generate C++ file with extern linkage
    # Symbol names are the SAME across backends - CMake selects which .cpp to compile
    $cppContent = @"
// Auto-generated shader binary - DO NOT EDIT
// Source: $relativeSource
// Backend: $Backend

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

    # Normalise to LF so line-ending differences (autocrlf, editors) never cause spurious rewrites
    $cppContent = $cppContent -replace "`r`n", "`n" -replace "`r", "`n"

    $shouldWrite = $true
    if (Test-Path $OutputFile)
    {
        $existingContent = ([System.IO.File]::ReadAllText($OutputFile)) -replace "`r`n", "`n" -replace "`r", "`n"
        if ($existingContent -eq $cppContent)
        {
            $shouldWrite = $false
            Write-Skip "  Unchanged $([System.IO.Path]::GetFileName($OutputFile))"
        }
    }

    if ($shouldWrite)
    {
        [System.IO.File]::WriteAllText($OutputFile, $cppContent, [System.Text.UTF8Encoding]::new($false))
        Write-Success "Generated $OutputFile ($length bytes)"
    }
}

function Generate-HeaderFile
{
    param(
        [array]$CompiledShaders,
        [array]$CompiledCompute
    )

    Write-Header "Generating Unified Header"

    $headerContent = @"
// Auto-generated shader header - DO NOT EDIT
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

    foreach ($shader in $CompiledShaders)
    {
        $headerContent += @"
    // $($shader.Name) Shaders
    extern const uint8_t $($shader.VertSymbol)[];
    extern const size_t $($shader.VertSymbol)_Size;
    extern const uint8_t $($shader.FragSymbol)[];
    extern const size_t $($shader.FragSymbol)_Size;

"@
    }

    foreach ($compute in $CompiledCompute)
    {
        $headerContent += @"
    // $($compute.Name) Compute Shader (always SPIR-V - SDL_ShaderCross handles cross-compilation)
    extern const uint8_t $($compute.CompSymbol)[];
    extern const size_t $($compute.CompSymbol)_Size;

"@
    }

    $headerContent += @"
} // namespace Shaders
} // namespace Luminoveau
"@

    $headerContent = $headerContent -replace "`r`n", "`n" -replace "`r", "`n"

    $shouldWrite = $true
    if (Test-Path $HeaderOutputPath)
    {
        $existingContent = ([System.IO.File]::ReadAllText($HeaderOutputPath)) -replace "`r`n", "`n" -replace "`r", "`n"
        if ($existingContent -eq $headerContent)
        {
            $shouldWrite = $false
            Write-Skip "  Unchanged $([System.IO.Path]::GetFileName($HeaderOutputPath))"
        }
    }

    if ($shouldWrite)
    {
        [System.IO.File]::WriteAllText($HeaderOutputPath, $headerContent, [System.Text.UTF8Encoding]::new($false))
        Write-Success "Generated $HeaderOutputPath"
    }
}

function Generate-CMakeFile
{
    param(
        [array]$CompiledShaders,
        [array]$CompiledCompute,
        [string]$Backend
    )

    Write-Header "Generating CMake Sources File"

    # Generate conditional CMake file that selects backend at build time
    $cmakeContent = @"
# Auto-generated shader sources - DO NOT EDIT
# Available backends: $Backend

# Set default GPU backend if not specified
if(NOT DEFINED LUMINOVEAU_GPU_BACKEND)
    set(LUMINOVEAU_GPU_BACKEND "SPIRV" CACHE STRING "GPU shader backend (SPIRV, DXIL, METALLIB, WGSL)")
endif()

# Validate backend selection
if(NOT LUMINOVEAU_GPU_BACKEND MATCHES "^(SPIRV|DXIL|METALLIB|WGSL)$")
    message(FATAL_ERROR "Invalid LUMINOVEAU_GPU_BACKEND: `${LUMINOVEAU_GPU_BACKEND}. Must be SPIRV, DXIL, METALLIB, or WGSL")
endif()

lumi_done("GPU Backend: `${LUMINOVEAU_GPU_BACKEND}")

# Select shader files based on backend
if(LUMINOVEAU_GPU_BACKEND STREQUAL "SPIRV")
    set(LUMINOVEAU_SHADER_SOURCES
"@

    foreach ($shader in $CompiledShaders)
    {
        if ($shader.VertCppSPIRV)
        {
            $cmakeContent += "`n        src/assets/shaders/$($shader.VertCppSPIRV)"
            $cmakeContent += "`n        src/assets/shaders/$($shader.FragCppSPIRV)"
        }
    }

    $cmakeContent += "`n    )`n"

    # DXIL section
    $cmakeContent += "elseif(LUMINOVEAU_GPU_BACKEND STREQUAL `"DXIL`")`n"
    $cmakeContent += "    set(LUMINOVEAU_SHADER_SOURCES`n"

    foreach ($shader in $CompiledShaders)
    {
        if ($shader.VertCppDXIL)
        {
            $cmakeContent += "`n        src/assets/shaders/$($shader.VertCppDXIL)"
            $cmakeContent += "`n        src/assets/shaders/$($shader.FragCppDXIL)"
        }
    }

    $cmakeContent += "`n    )`n"

    # METALLIB section
    $cmakeContent += "elseif(LUMINOVEAU_GPU_BACKEND STREQUAL `"METALLIB`")`n"
    $cmakeContent += "    set(LUMINOVEAU_SHADER_SOURCES`n"

    foreach ($shader in $CompiledShaders)
    {
        if ($shader.VertCppMETALLIB)
        {
            $cmakeContent += "`n        src/assets/shaders/$($shader.VertCppMETALLIB)"
            $cmakeContent += "`n        src/assets/shaders/$($shader.FragCppMETALLIB)"
        }
    }

    $cmakeContent += "`n    )`n"
    $cmakeContent += "elseif(LUMINOVEAU_GPU_BACKEND STREQUAL `"WGSL`")`n"
    $cmakeContent += "    set(LUMINOVEAU_SHADER_SOURCES`n"

    foreach ($shader in $CompiledShaders)
    {
        if ($shader.VertCppWGSL)
        {
            $cmakeContent += "`n        src/assets/shaders/$($shader.VertCppWGSL)"
            $cmakeContent += "`n        src/assets/shaders/$($shader.FragCppWGSL)"
        }
    }

    $cmakeContent += @"

    )
else()
    message(FATAL_ERROR "No shader files available for backend: `${LUMINOVEAU_GPU_BACKEND}")
endif()

# Compute shaders are always SPIR-V: SDL_ShaderCross handles cross-compilation at runtime
set(LUMINOVEAU_COMPUTE_SHADER_SOURCES
"@

    foreach ($compute in $CompiledCompute)
    {
        if ($compute.CompCppSPIRV)
        {
            $cmakeContent += "`n    src/assets/shaders/$($compute.CompCppSPIRV)"
        }
    }

    $cmakeContent += @"

)

list(APPEND LUMINOVEAU_SHADER_SOURCES `${LUMINOVEAU_COMPUTE_SHADER_SOURCES})

# Group shader files in IDE
source_group("Generated\\Shaders" FILES `${LUMINOVEAU_SHADER_SOURCES})

# Define shader backend for C++ code
add_compile_definitions(LUMINOVEAU_SHADER_BACKEND_`${LUMINOVEAU_GPU_BACKEND})
"@

    $cmakeContent = $cmakeContent -replace "`r`n", "`n" -replace "`r", "`n"

    $shouldWrite = $true
    if (Test-Path $CMakeOutputPath)
    {
        $existingContent = ([System.IO.File]::ReadAllText($CMakeOutputPath)) -replace "`r`n", "`n" -replace "`r", "`n"
        if ($existingContent -eq $cmakeContent)
        {
            $shouldWrite = $false
            Write-Skip "  Unchanged $([System.IO.Path]::GetFileName($CMakeOutputPath))"
        }
    }

    if ($shouldWrite)
    {
        [System.IO.File]::WriteAllText($CMakeOutputPath, $cmakeContent, [System.Text.UTF8Encoding]::new($false))
        Write-Success "Generated $CMakeOutputPath"
    }
}

# ============================================================================
# Main Compilation Logic
# ============================================================================

function Get-AvailableBackends
{
    # Determine which backends can be compiled on this platform
    $available = @()

    # WGSL: no tool needed — source files are embedded as-is
    $available += "wgsl"

    # SPIR-V: needs dxc (available everywhere)
    if (Get-Command dxc -ErrorAction SilentlyContinue)
    {
        $available += "spirv"
    }

    # DXIL: needs dxc, but only produces valid signed output on Windows
    if ($IsWindows -and (Get-Command dxc -ErrorAction SilentlyContinue))
    {
        $available += "dxil"
    }

    # METALLIB: needs dxc + spirv-cross + xcrun (macOS only)
    if ($IsMacOS)
    {
        $hasSpirvCross = Get-Command spirv-cross -ErrorAction SilentlyContinue
        $hasXcrun = Get-Command xcrun -ErrorAction SilentlyContinue

        if ($hasSpirvCross -and $hasXcrun)
        {
            $available += "metallib"
        } else
        {
            if (-not $hasSpirvCross)
            { Write-Info "spirv-cross not found - install via: brew install spirv-cross" 
            }
            if (-not $hasXcrun)
            { Write-Info "xcrun not found - install Xcode Command Line Tools" 
            }
        }
    }

    return $available
}

function Compile-AllShaders
{
    Write-Header "Luminoveau Shader Compilation"

    # Determine which backends to compile
    $availableBackends = Get-AvailableBackends

    if ($Backend -eq "all")
    {
        $backendsToCompile = $availableBackends
        Write-Host "Platform: $(if ($IsMacOS) { 'macOS' } elseif ($IsWindows) { 'Windows' } else { 'Linux' })" -ForegroundColor White
        Write-Host "Available backends: $($availableBackends -join ', ')" -ForegroundColor White
    } else
    {
        if ($Backend -notin $availableBackends)
        {
            Write-Error-Custom "Backend '$Backend' is not available on this platform."
            Write-Host "Available: $($availableBackends -join ', ')" -ForegroundColor Yellow
            exit 1
        }
        $backendsToCompile = @($Backend)
    }

    Write-Host "Compiling: $($backendsToCompile -join ', ')" -ForegroundColor White
    Write-Host "Output Directory: $OutputDir`n" -ForegroundColor White

    # Discover shader pairs from directory
    $shaderDefs = Discover-Shaders

    # Ensure output directory exists
    Ensure-Directory $OutputDir

    # Track compiled shaders for header/cmake generation
    $compiledShaders = @()

    foreach ($shaderDef in $shaderDefs)
    {
        # Skip if specific shader requested and this isn't it
        if ($Shader -and $shaderDef.Name -ne $Shader -and $shaderDef.BaseName -ne $Shader)
        {
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
            VertCppMETALLIB = ""
            FragCppMETALLIB = ""
            VertCppWGSL = ""
            FragCppWGSL = ""
        }

        # Compile to SPIR-V
        if ("spirv" -in $backendsToCompile)
        {
            $vertSpv = Join-Path $OutputDir "$($shaderDef.BaseName)_vert.spv"
            $fragSpv = Join-Path $OutputDir "$($shaderDef.BaseName)_frag.spv"

            $vertSuccess = Compile-SPIRV -SourceFile $vertSource -OutputFile $vertSpv -Profile $VertexProfile -ShaderName "$($shaderDef.Name) Vertex"
            $fragSuccess = Compile-SPIRV -SourceFile $fragSource -OutputFile $fragSpv -Profile $FragmentProfile -ShaderName "$($shaderDef.Name) Fragment"

            if ($vertSuccess -and $fragSuccess)
            {
                $shaderInfo.VertCppSPIRV = "$($shaderDef.BaseName)_vert.spirv.cpp"
                $shaderInfo.FragCppSPIRV = "$($shaderDef.BaseName)_frag.spirv.cpp"

                Generate-CppFile -BinaryFile $vertSpv -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppSPIRV) -SymbolName $shaderInfo.VertSymbol -Backend "SPIR-V"
                Generate-CppFile -BinaryFile $fragSpv -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppSPIRV) -SymbolName $shaderInfo.FragSymbol -Backend "SPIR-V"
            }
        }

        # Compile to DXIL
        if ("dxil" -in $backendsToCompile)
        {
            $vertDxil = Join-Path $OutputDir "$($shaderDef.BaseName)_vert.dxil"
            $fragDxil = Join-Path $OutputDir "$($shaderDef.BaseName)_frag.dxil"

            $vertSuccess = Compile-DXIL -SourceFile $vertSource -OutputFile $vertDxil -Profile $VertexProfile -ShaderName "$($shaderDef.Name) Vertex"
            $fragSuccess = Compile-DXIL -SourceFile $fragSource -OutputFile $fragDxil -Profile $FragmentProfile -ShaderName "$($shaderDef.Name) Fragment"

            if ($vertSuccess -and $fragSuccess)
            {
                $shaderInfo.VertCppDXIL = "$($shaderDef.BaseName)_vert.dxil.cpp"
                $shaderInfo.FragCppDXIL = "$($shaderDef.BaseName)_frag.dxil.cpp"

                Generate-CppFile -BinaryFile $vertDxil -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppDXIL) -SymbolName $shaderInfo.VertSymbol -Backend "DXIL"
                Generate-CppFile -BinaryFile $fragDxil -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppDXIL) -SymbolName $shaderInfo.FragSymbol -Backend "DXIL"
            }
        }

        # Compile to Metal
        if ("metallib" -in $backendsToCompile)
        {
            $vertMetallib = Join-Path $OutputDir "$($shaderDef.BaseName)_vert.metallib"
            $fragMetallib = Join-Path $OutputDir "$($shaderDef.BaseName)_frag.metallib"

            $vertSuccess = Compile-MetalLib -SourceFile $vertSource -OutputFile $vertMetallib -Profile $VertexProfile -ShaderName "$($shaderDef.Name) Vertex"
            $fragSuccess = Compile-MetalLib -SourceFile $fragSource -OutputFile $fragMetallib -Profile $FragmentProfile -ShaderName "$($shaderDef.Name) Fragment"

            if ($vertSuccess -and $fragSuccess)
            {
                $shaderInfo.VertCppMETALLIB = "$($shaderDef.BaseName)_vert.metallib.cpp"
                $shaderInfo.FragCppMETALLIB = "$($shaderDef.BaseName)_frag.metallib.cpp"

                Generate-CppFile -BinaryFile $vertMetallib -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppMETALLIB) -SymbolName $shaderInfo.VertSymbol -Backend "METALLIB"
                Generate-CppFile -BinaryFile $fragMetallib -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppMETALLIB) -SymbolName $shaderInfo.FragSymbol -Backend "METALLIB"
            }
        }

        # Compile to WGSL (embed source text as null-terminated bytes — no tool required)
        if ("wgsl" -in $backendsToCompile)
        {
            $vertWgsl = Join-Path $ShaderSourceDir "$($shaderDef.BaseName).vert.wgsl"
            $fragWgsl = Join-Path $ShaderSourceDir "$($shaderDef.BaseName).frag.wgsl"

            if ((Test-Path $vertWgsl) -and (Test-Path $fragWgsl))
            {
                $shaderInfo.VertCppWGSL = "$($shaderDef.BaseName)_vert.wgsl.cpp"
                $shaderInfo.FragCppWGSL = "$($shaderDef.BaseName)_frag.wgsl.cpp"

                Generate-CppFile -BinaryFile $vertWgsl -OutputFile (Join-Path $OutputDir $shaderInfo.VertCppWGSL) -SymbolName $shaderInfo.VertSymbol -Backend "WGSL" -NullTerminate
                Generate-CppFile -BinaryFile $fragWgsl -OutputFile (Join-Path $OutputDir $shaderInfo.FragCppWGSL) -SymbolName $shaderInfo.FragSymbol -Backend "WGSL" -NullTerminate
            }
            else
            {
                Write-Info "No .wgsl source files found for $($shaderDef.Name) - skipping WGSL"
            }
        }

        # Fill in any backend fields from existing .cpp files on disk
        # This preserves backends compiled on other platforms (e.g., SPIRV/DXIL from Windows)
        $backendSuffixes = @{
            VertCppSPIRV    = "$($shaderDef.BaseName)_vert.spirv.cpp"
            FragCppSPIRV    = "$($shaderDef.BaseName)_frag.spirv.cpp"
            VertCppDXIL     = "$($shaderDef.BaseName)_vert.dxil.cpp"
            FragCppDXIL     = "$($shaderDef.BaseName)_frag.dxil.cpp"
            VertCppMETALLIB = "$($shaderDef.BaseName)_vert.metallib.cpp"
            FragCppMETALLIB = "$($shaderDef.BaseName)_frag.metallib.cpp"
            VertCppWGSL     = "$($shaderDef.BaseName)_vert.wgsl.cpp"
            FragCppWGSL     = "$($shaderDef.BaseName)_frag.wgsl.cpp"
        }

        foreach ($field in $backendSuffixes.Keys)
        {
            if (-not $shaderInfo[$field])
            {
                $existingFile = Join-Path $OutputDir $backendSuffixes[$field]
                if (Test-Path $existingFile)
                {
                    $shaderInfo[$field] = $backendSuffixes[$field]
                    Write-Skip "  Keeping existing $($backendSuffixes[$field])"
                }
            }
        }

        # Add to compiled shaders if at least one backend exists
        if ($shaderInfo.VertCppSPIRV -or $shaderInfo.VertCppDXIL -or $shaderInfo.VertCppMETALLIB -or $shaderInfo.VertCppWGSL)
        {
            $compiledShaders += $shaderInfo
        }
    }

    # ── Compute shaders (always compiled to SPIR-V; SDL_ShaderCross cross-compiles at runtime) ──

    $computeDefs = Discover-ComputeShaders
    $compiledCompute = @()

    foreach ($computeDef in $computeDefs)
    {
        if ($Shader -and $computeDef.Name -ne $Shader -and $computeDef.BaseName -ne $Shader)
        {
            continue
        }

        Write-Header "Compiling $($computeDef.Name) Compute Shader"

        $compSource = Join-Path $ShaderSourceDir $computeDef.CompFile

        $computeInfo = @{
            Name        = $computeDef.Name
            BaseName    = $computeDef.BaseName
            CompSymbol  = $computeDef.CompSymbol
            CompCppSPIRV = ""
        }

        # Compute shaders are always compiled to SPIR-V
        $compSpv = Join-Path $OutputDir "$($computeDef.BaseName)_comp.spv"
        $compSuccess = Compile-SPIRV -SourceFile $compSource -OutputFile $compSpv -Profile $ComputeProfile -ShaderName "$($computeDef.Name) Compute"

        if ($compSuccess)
        {
            $computeInfo.CompCppSPIRV = "$($computeDef.BaseName)_comp.spirv.cpp"
            Generate-CppFile -BinaryFile $compSpv -OutputFile (Join-Path $OutputDir $computeInfo.CompCppSPIRV) -SymbolName $computeInfo.CompSymbol -Backend "SPIR-V"
        }

        # Keep existing .cpp if we didn't compile (e.g. -Shader filter skipped this)
        if (-not $computeInfo.CompCppSPIRV)
        {
            $existingFile = Join-Path $OutputDir "$($computeDef.BaseName)_comp.spirv.cpp"
            if (Test-Path $existingFile)
            {
                $computeInfo.CompCppSPIRV = "$($computeDef.BaseName)_comp.spirv.cpp"
                Write-Skip "  Keeping existing $($computeInfo.CompCppSPIRV)"
            }
        }

        if ($computeInfo.CompCppSPIRV)
        {
            $compiledCompute += $computeInfo
        }
    }

    # Generate header and cmake files
    if ($compiledShaders.Count -gt 0 -or $compiledCompute.Count -gt 0)
    {
        Generate-HeaderFile -CompiledShaders $compiledShaders -CompiledCompute $compiledCompute
        Generate-CMakeFile -CompiledShaders $compiledShaders -CompiledCompute $compiledCompute -Backend ($backendsToCompile -join ", ")

        # Clean up intermediate binary files
        Write-Info "Cleaning up intermediate binary files..."
        $binaryFiles = @()
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.spv" -File -ErrorAction SilentlyContinue
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.dxil" -File -ErrorAction SilentlyContinue
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.dxbc" -File -ErrorAction SilentlyContinue
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.metallib" -File -ErrorAction SilentlyContinue
        $binaryFiles += Get-ChildItem -Path $OutputDir -Filter "*.air" -File -ErrorAction SilentlyContinue

        foreach ($binaryFile in $binaryFiles)
        {
            Remove-Item -Path $binaryFile.FullName -Force
        }
        if ($binaryFiles.Count -gt 0)
        {
            Write-Success "Removed $($binaryFiles.Count) intermediate binary file(s)"
        }

        Write-Header "Compilation Complete"
        Write-Success "Compiled $($compiledShaders.Count) shader pair(s) and $($compiledCompute.Count) compute shader(s)"
        Write-Success "Backends: $($backendsToCompile -join ', ')"
    } else
    {
        Write-Error-Custom "No shaders were compiled successfully"
        exit 1
    }
}

# ============================================================================
# Entry Point
# ============================================================================

try
{
    if ($Clean)
    {
        Clean-GeneratedFiles
        Write-Success "Clean complete"
        exit 0
    }

    # Check for DXC compiler (required for all backends as the first step)
    $dxc = Get-Command dxc -ErrorAction SilentlyContinue
    if (-not $dxc)
    {
        Write-Error-Custom "DXC compiler not found."
        if ($IsMacOS)
        {
            Write-Host "Install via Homebrew: brew install microsoft/directx/dxc" -ForegroundColor Yellow
            Write-Host "Or install the Vulkan SDK which includes DXC" -ForegroundColor Yellow
        } else
        {
            Write-Host "Download from: https://github.com/microsoft/DirectXShaderCompiler/releases" -ForegroundColor Yellow
        }
        exit 1
    }

    Compile-AllShaders

} catch
{
    Write-Error-Custom "An error occurred: $_"
    Write-Host $_.ScriptStackTrace -ForegroundColor Red
    exit 1
}
