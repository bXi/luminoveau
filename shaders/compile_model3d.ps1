#!/usr/bin/env pwsh
# Shader compilation script for Luminoveau
# Compiles GLSL shaders to SPIRV and generates C++ binary arrays

$ErrorActionPreference = "Stop"

# Check for glslc (comes with Vulkan SDK)
$glslc = Get-Command glslc -ErrorAction SilentlyContinue
if (-not $glslc) {
    Write-Error "glslc not found. Please install the Vulkan SDK: https://vulkan.lunarg.com/"
    exit 1
}

Write-Host "=== Compiling 3D Model Shaders ===" -ForegroundColor Cyan

$shaderDir = $PSScriptRoot
$outputDir = Join-Path $PSScriptRoot "..\assethandler"

# Compile model3d.vert
Write-Host "Compiling model3d.vert..." -ForegroundColor Yellow
$vertInput = Join-Path $shaderDir "model3d.vert"
$vertOutput = Join-Path $shaderDir "model3d.vert.spv"
& glslc -fshader-stage=vertex $vertInput -o $vertOutput

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile model3d.vert"
    exit 1
}

# Compile model3d.frag
Write-Host "Compiling model3d.frag..." -ForegroundColor Yellow
$fragInput = Join-Path $shaderDir "model3d.frag"
$fragOutput = Join-Path $shaderDir "model3d.frag.spv"
& glslc -fshader-stage=fragment $fragInput -o $fragOutput

if ($LASTEXITCODE -ne 0) {
    Write-Error "Failed to compile model3d.frag"
    exit 1
}

# Function to convert binary to C++ array
function Convert-BinaryToCpp {
    param(
        [string]$InputFile,
        [string]$OutputFile,
        [string]$ArrayName,
        [string]$ClassName
    )
    
    $bytes = [System.IO.File]::ReadAllBytes($InputFile)
    $length = $bytes.Length
    
    $cpp = "#include `"renderer/model3drenderpass.h`"`n`n"
    $cpp += "const uint8_t ${ClassName}::${ArrayName}_bin[] = {`n"
    
    for ($i = 0; $i -lt $bytes.Length; $i += 12) {
        $cpp += "  "
        $end = [Math]::Min($i + 12, $bytes.Length)
        for ($j = $i; $j -lt $end; $j++) {
            $cpp += "0x{0:x2}" -f $bytes[$j]
            if ($j -lt $bytes.Length - 1) {
                $cpp += ", "
            }
        }
        $cpp += "`n"
    }
    
    $cpp += "};`n"
    # Don't add size here - we'll print it for the user to copy
    
    [System.IO.File]::WriteAllText($OutputFile, $cpp)
    Write-Host "  Generated $OutputFile ($length bytes)" -ForegroundColor Green
    
    return @{ Name = $ArrayName; Length = $length }
}

# Generate C++ files
Write-Host "`nGenerating C++ arrays..." -ForegroundColor Yellow

$vertInfo = Convert-BinaryToCpp -InputFile $vertOutput `
                    -OutputFile (Join-Path $outputDir "model3dvert.cpp") `
                    -ArrayName "model3d_vert" `
                    -ClassName "Model3DRenderPass"

$fragInfo = Convert-BinaryToCpp -InputFile $fragOutput `
                    -OutputFile (Join-Path $outputDir "model3dfrag.cpp") `
                    -ArrayName "model3d_frag" `
                    -ClassName "Model3DRenderPass"

Write-Host "`n=== Compilation Complete ===" -ForegroundColor Green
Write-Host "Generated files:" -ForegroundColor Cyan
Write-Host "  - assethandler/model3dvert.cpp" -ForegroundColor White
Write-Host "  - assethandler/model3dfrag.cpp" -ForegroundColor White

Write-Host "`n=== Copy this to model3drenderpass.h ===" -ForegroundColor Cyan
Write-Host @"
// Shader binaries
static const uint8_t model3d_vert_bin[];
static const size_t model3d_vert_bin_len = $($vertInfo.Length);

static const uint8_t model3d_frag_bin[];
static const size_t model3d_frag_bin_len = $($fragInfo.Length);
"@ -ForegroundColor Yellow
