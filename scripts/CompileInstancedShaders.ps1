# =======================================================================
# Script: CompileInstancedShaders.ps1
# Purpose: 
#   1. Compile the instanced sprite shaders (vertex and fragment) to SPIR-V using DXC
#   2. Convert the SPIR-V binaries to C++ arrays using xxd
#   3. Update the sprite render pass files to use the new shaders
# =======================================================================

function Compile-Shader {
    param(
        [string]$ShaderFile,
        [string]$Profile,
        [string]$OutputFile
    )
    
    Write-Host "Compiling $ShaderFile with profile $Profile..."
    $dxcArgs = "-T $Profile -E main -spirv -Fo $OutputFile $ShaderFile"
    $dxcProcess = Start-Process -FilePath "dxc" -ArgumentList $dxcArgs -NoNewWindow -Wait -PassThru
    if ($dxcProcess.ExitCode -ne 0) {
        Write-Error "DXC compilation failed for $ShaderFile (exit code $($dxcProcess.ExitCode))"
        return $false
    }
    Write-Host "Successfully compiled $ShaderFile"
    return $true
}

function Convert-ToHeader {
    param(
        [string]$SpvFile,
        [string]$HeaderFile
    )
    
    Write-Host "Converting $SpvFile to header..."
    $xxdArgs = "-i $SpvFile $HeaderFile"
    $xxdProcess = Start-Process -FilePath "xxd" -ArgumentList $xxdArgs -NoNewWindow -Wait -PassThru
    if ($xxdProcess.ExitCode -ne 0) {
        Write-Error "xxd conversion failed for $SpvFile (exit code $($xxdProcess.ExitCode))"
        return $false
    }
    Write-Host "Successfully created $HeaderFile"
    return $true
}

function Extract-ArrayData {
    param(
        [string]$HeaderFile
    )
    
    $headerContent = Get-Content -Path $HeaderFile -Raw
    
    # Extract length
    $lenMatch = [regex]::Match($headerContent, 'unsigned int\s+\S+_len\s*=\s*(\d+);')
    if (-not $lenMatch.Success) {
        Write-Error "Could not extract length from $HeaderFile"
        return $null
    }
    $length = $lenMatch.Groups[1].Value
    
    # Extract array data
    $arrayMatch = [regex]::Match($headerContent, '\{(.*?)\}', [System.Text.RegularExpressions.RegexOptions]::Singleline)
    if (-not $arrayMatch.Success) {
        Write-Error "Could not extract array data from $HeaderFile"
        return $null
    }
    $arrayData = $arrayMatch.Groups[1].Value.Trim()
    
    return @{
        Length = $length
        Data = $arrayData
    }
}

# Change to scripts directory
Set-Location -Path $PSScriptRoot

# Compile vertex shader
if (-not (Compile-Shader -ShaderFile "sprite_instanced.vert.hlsl" -Profile "vs_6_0" -OutputFile "sprite_instanced.vert.spv")) {
    exit 1
}

# Compile fragment shader
if (-not (Compile-Shader -ShaderFile "sprite_instanced.frag.hlsl" -Profile "ps_6_0" -OutputFile "sprite_instanced.frag.spv")) {
    exit 1
}

# Convert to headers
if (-not (Convert-ToHeader -SpvFile "sprite_instanced.vert.spv" -HeaderFile "sprite_instanced.vert.h")) {
    exit 1
}

if (-not (Convert-ToHeader -SpvFile "sprite_instanced.frag.spv" -HeaderFile "sprite_instanced.frag.h")) {
    exit 1
}

# Extract data from headers
$vertData = Extract-ArrayData -HeaderFile "sprite_instanced.vert.h"
$fragData = Extract-ArrayData -HeaderFile "sprite_instanced.frag.h"

if (-not $vertData -or -not $fragData) {
    exit 1
}

# Create new C++ files for the shader binaries
$vertCppPath = "../assethandler/spriteinstancedvert.cpp"
$fragCppPath = "../assethandler/spriteinstancedfrag.cpp"

$vertCppContent = @"
#include "renderer/spriterenderpass.h"

const uint8_t SpriteRenderPass::sprite_instanced_vert_bin[] = {
$($vertData.Data)
};
"@

$fragCppContent = @"
#include "renderer/spriterenderpass.h"

const uint8_t SpriteRenderPass::sprite_instanced_frag_bin[] = {
$($fragData.Data)
};
"@

Write-Host "Creating $vertCppPath..."
Set-Content -Path $vertCppPath -Value $vertCppContent

Write-Host "Creating $fragCppPath..."
Set-Content -Path $fragCppPath -Value $fragCppContent

Write-Host ""
Write-Host "Shader compilation complete!"
Write-Host "Vertex shader length: $($vertData.Length) bytes"
Write-Host "Fragment shader length: $($fragData.Length) bytes"
Write-Host ""
Write-Host "Please update renderer/spriterenderpass.h to add the following declarations:"
Write-Host "    static const uint8_t sprite_instanced_vert_bin[];"
Write-Host "    static const size_t sprite_instanced_vert_bin_len = $($vertData.Length);"
Write-Host ""
Write-Host "    static const uint8_t sprite_instanced_frag_bin[];"
Write-Host "    static const size_t sprite_instanced_frag_bin_len = $($fragData.Length);"
