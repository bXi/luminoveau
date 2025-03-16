# =======================================================================
# Script: UpdateSpriteShader.ps1
# Purpose: 
#   1. Compile the shader HLSL file to SPIR-V using DXC.
#   2. Convert the resulting SPIR-V binary to a C header using xxd.
#   3. Update renderer/spriterenderpass.h to use the new binary length.
#   4. Update assethandler/spritebatchvert.cpp to embed the new array data.
#
# Adjust file paths as needed.
# =======================================================================

# ----- STEP 1: Compile HLSL using DXC -----
Write-Host "Running DXC command to compile shader..."
$dxcArgs = "-T vs_6_0 -E main -spirv -Fo batchsprite.vert.spv batchsprite.vert.hlsl"
# Using Start-Process for a controlled execution.
$dxcProcess = Start-Process -FilePath "dxc" -ArgumentList $dxcArgs -NoNewWindow -Wait -PassThru
if ($dxcProcess.ExitCode -ne 0) {
    Write-Error "DXC compilation failed (exit code $($dxcProcess.ExitCode)). Aborting."
    exit $dxcProcess.ExitCode
}

# ----- STEP 2: Generate a C header from the SPIR-V binary -----
Write-Host "Converting SPIR-V binary using xxd..."
$xxdArgs = "-i .\batchsprite.vert.spv sprite.vert.h"
$xxdProcess = Start-Process -FilePath "xxd" -ArgumentList $xxdArgs -NoNewWindow -Wait -PassThru
if ($xxdProcess.ExitCode -ne 0) {
    Write-Error "xxd conversion failed (exit code $($xxdProcess.ExitCode)). Aborting."
    exit $xxdProcess.ExitCode
}

# ----- STEP 3: Update renderer/spriterenderpass.h with new binary length -----
# The generated header (sprite.vert.h) contains a line like:
#   unsigned int batchsprite_vert_spv_len = 2928;
# We extract that new length:

$spriteHeaderPath = ".\sprite.vert.h"
if (-not (Test-Path $spriteHeaderPath)) {
    Write-Error "Cannot find file: $spriteHeaderPath"
    exit 1
}

$headerLines = Get-Content -Path $spriteHeaderPath
$lenLineMatch = ($headerLines | Select-String -Pattern 'unsigned int\s+\S+_len\s*=\s*(\d+);').Matches
if (-not $lenLineMatch.Count) {
    Write-Error "Could not extract binary length from $spriteHeaderPath"
    exit 1
}
$newLength = $lenLineMatch[0].Groups[1].Value.Trim()
Write-Host "New binary length identified as $newLength"

# Update the file "../renderer/spriterenderpass.h":
$renderPassPath = "../renderer/spriterenderpass.h"
if (-not (Test-Path $renderPassPath)) {
    Write-Error "Cannot find file: $renderPassPath"
    exit 1
}

$renderPassContent = Get-Content -Path $renderPassPath
# Replace the matching line (using a regex matching 'static const size_t sprite_batch_vert_bin_len = <number>;')
$newRenderPassContent = $renderPassContent | ForEach-Object {
    if ($_ -match 'static\s+const\s+size_t\s+sprite_batch_vert_bin_len\s*=\s*\d+\s*;') {
        "    static const size_t sprite_batch_vert_bin_len = $newLength;"
    }
    else {
        $_
    }
}
Write-Host "Updating $renderPassPath with the new binary length..."
Set-Content -Path $renderPassPath -Value $newRenderPassContent

# ----- STEP 4: Update assethandler/spritebatchvert.cpp with the new array data -----
# Our sprite.vert.h (generated in step 2) looks like:
#
#   unsigned char batchsprite_vert_spv[] = {
#     0x03, 0x02, 0x23, ... 
#   };
#   unsigned int batchsprite_vert_spv_len = 2928;
#
# We need only the array initializer (everything between '{' and '}').
$spriteHRaw = Get-Content -Path $spriteHeaderPath -Raw
$arrayDataPattern = '{(.*?)}'
$arrayMatch = [regex]::Match($spriteHRaw, $arrayDataPattern, [System.Text.RegularExpressions.RegexOptions]::Singleline)
if (-not $arrayMatch.Success) {
    Write-Error "Could not extract array data from $spriteHeaderPath"
    exit 1
}
# Clean up the extracted content; you may adjust indentation if desired.
$newArrayContent = $arrayMatch.Groups[1].Value.Trim()

# Now update assethandler/spritebatchvert.cpp:
$cppPath = "../assethandler/spritebatchvert.cpp"
if (-not (Test-Path $cppPath)) {
    Write-Error "Cannot find file: $cppPath"
    exit 1
}

$cppContent = Get-Content -Path $cppPath -Raw

# We assume the file has a structure similar to:
#    #include "renderer/spriterenderpass.h"
#
#    const uint8_t SpriteRenderPass::sprite_batch_vert_bin[] = {
#        ... old array data ...
#    };
#
# We replace the content between the opening '{' and the closing '};'
$pattern = '(const\s+uint8_t\s+SpriteRenderPass::sprite_batch_vert_bin\[\]\s*=\s*\{)(.*?)(\};)'
$replacement = "`$1`n$newArrayContent`n`$3"
$newCppContent = [regex]::Replace($cppContent, $pattern, $replacement, [System.Text.RegularExpressions.RegexOptions]::Singleline)
Write-Host "Updating $cppPath with new array data..."
Set-Content -Path $cppPath -Value $newCppContent

Write-Host "All updates complete successfully!"


