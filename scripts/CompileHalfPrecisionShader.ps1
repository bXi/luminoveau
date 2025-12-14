# Compile the updated half-precision vertex shader

Write-Host "Compiling half-precision vertex shader..."
$dxcArgs = "-T vs_6_0 -E main -spirv -Fo sprite_instanced.vert.spv sprite_instanced.vert.hlsl"
$dxcProcess = Start-Process -FilePath "dxc" -ArgumentList $dxcArgs -NoNewWindow -Wait -PassThru

if ($dxcProcess.ExitCode -ne 0) {
    Write-Error "DXC compilation failed"
    exit 1
}

Write-Host "Converting to C++ array..."
$xxdArgs = "-i sprite_instanced.vert.spv sprite_instanced.vert.h"
$xxdProcess = Start-Process -FilePath "xxd" -ArgumentList $xxdArgs -NoNewWindow -Wait -PassThru

if ($xxdProcess.ExitCode -ne 0) {
    Write-Error "xxd conversion failed"
    exit 1
}

# Extract array data and length
$headerContent = Get-Content -Path "sprite_instanced.vert.h" -Raw

$lenMatch = [regex]::Match($headerContent, 'unsigned int\s+\S+_len\s*=\s*(\d+);')
if (-not $lenMatch.Success) {
    Write-Error "Could not extract length"
    exit 1
}
$newLength = $lenMatch.Groups[1].Value

$arrayMatch = [regex]::Match($headerContent, '\{(.*?)\}', [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $arrayMatch.Success) {
    Write-Error "Could not extract array data"
    exit 1
}
$arrayData = $arrayMatch.Groups[1].Value.Trim()

# Update the C++ file
$cppPath = "../assethandler/spriteinstancedvert.cpp"
$cppContent = @"
#include "renderer/spriterenderpass.h"

const uint8_t SpriteRenderPass::sprite_instanced_vert_bin[] = {
$arrayData
};
"@

Set-Content -Path $cppPath -Value $cppContent

Write-Host ""
Write-Host "Success! New shader length: $newLength bytes"
Write-Host "Updated: $cppPath"
Write-Host ""
Write-Host "Now update spriterenderpass.h and change:"
Write-Host "    static const size_t sprite_instanced_vert_bin_len = $newLength;"
