// WebGPU-backend implementation of AssetHandler::_loadShaderFromDisk.
// Runtime path: load GLSL → SPIR-V via glslang → reflect via SPIRV-Cross →
// load pre-transpiled WGSL (build-time Tint) → fix-ups → createShader.

#include "assets/assethandler.h"
#include "core/log/log.h"
#include "renderer/renderer.h"
#include "file/filehandler.h"

#include <glslang/Public/ShaderLang.h>
#include <SPIRV/GlslangToSpv.h>
#include <spirv_cross.hpp>

#include <regex>
#include <string>
#include <vector>

ShaderAsset AssetHandler::_loadShaderFromDisk(const std::string &fileName) {
    // Determine stage from filename
    EShLanguage    glslStage;
    GpuShaderStage gpuStage;
    if (fileName.find(".vert") != std::string::npos) {
        glslStage = EShLangVertex;
        gpuStage  = GpuShaderStage::Vertex;
    } else if (fileName.find(".frag") != std::string::npos) {
        glslStage = EShLangFragment;
        gpuStage  = GpuShaderStage::Fragment;
    } else {
        LOG_CRITICAL("AssetHandler::_loadShaderFromDisk: cannot determine stage from filename '{}'", fileName.c_str());
        return {};
    }

    // Load GLSL source
    auto sourceFile = FileHandler::GetFileFromPhysFS(fileName);
    std::string glslSource(static_cast<const char*>(sourceFile.data), sourceFile.fileSize);

    // Compile GLSL → SPIR-V via glslang
    glslang::InitializeProcess();
    glslang::TShader glslShader(glslStage);
    const char* srcPtr = glslSource.c_str();
    glslShader.setStrings(&srcPtr, 1);
    glslShader.setEnvInput(glslang::EShSourceGlsl, glslStage, glslang::EShClientVulkan, 450);
    glslShader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    glslShader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

    TBuiltInResource glslRes = {};
    glslRes.maxLights = 32; glslRes.maxClipPlanes = 6; glslRes.maxTextureUnits = 32;
    glslRes.maxTextureCoords = 32; glslRes.maxVertexAttribs = 64;
    glslRes.maxVertexUniformComponents = 4096; glslRes.maxVaryingFloats = 64;
    glslRes.maxVertexTextureImageUnits = 32; glslRes.maxCombinedTextureImageUnits = 80;
    glslRes.maxTextureImageUnits = 32; glslRes.maxFragmentUniformComponents = 4096;
    glslRes.maxDrawBuffers = 32; glslRes.maxVertexUniformVectors = 128;
    glslRes.maxVaryingVectors = 8; glslRes.maxFragmentUniformVectors = 16;
    glslRes.maxVertexOutputVectors = 16; glslRes.maxFragmentInputVectors = 15;
    glslRes.minProgramTexelOffset = -8; glslRes.maxProgramTexelOffset = 7;
    glslRes.maxClipDistances = 8;
    glslRes.maxComputeWorkGroupCountX = 65535; glslRes.maxComputeWorkGroupCountY = 65535;
    glslRes.maxComputeWorkGroupCountZ = 65535; glslRes.maxComputeWorkGroupSizeX = 1024;
    glslRes.maxComputeWorkGroupSizeY = 1024; glslRes.maxComputeWorkGroupSizeZ = 64;
    glslRes.maxComputeUniformComponents = 1024; glslRes.maxComputeTextureImageUnits = 16;
    glslRes.maxComputeImageUniforms = 8; glslRes.maxComputeAtomicCounters = 8;
    glslRes.maxComputeAtomicCounterBuffers = 1; glslRes.maxVaryingComponents = 60;
    glslRes.maxVertexOutputComponents = 64; glslRes.maxGeometryInputComponents = 64;
    glslRes.maxGeometryOutputComponents = 128; glslRes.maxFragmentInputComponents = 128;
    glslRes.maxImageUnits = 8; glslRes.maxCombinedImageUnitsAndFragmentOutputs = 8;
    glslRes.maxCombinedShaderOutputResources = 8; glslRes.maxImageSamples = 0;
    glslRes.maxVertexImageUniforms = 0; glslRes.maxTessControlImageUniforms = 0;
    glslRes.maxTessEvaluationImageUniforms = 0; glslRes.maxGeometryImageUniforms = 0;
    glslRes.maxFragmentImageUniforms = 8; glslRes.maxCombinedImageUniforms = 8;
    glslRes.maxGeometryTextureImageUnits = 16; glslRes.maxGeometryOutputVertices = 256;
    glslRes.maxGeometryTotalOutputComponents = 1024; glslRes.maxGeometryUniformComponents = 1024;
    glslRes.maxGeometryVaryingComponents = 64; glslRes.maxTessControlInputComponents = 128;
    glslRes.maxTessControlOutputComponents = 128; glslRes.maxTessControlTextureImageUnits = 16;
    glslRes.maxTessControlUniformComponents = 1024; glslRes.maxTessControlTotalOutputComponents = 4096;
    glslRes.maxTessEvaluationInputComponents = 128; glslRes.maxTessEvaluationOutputComponents = 128;
    glslRes.maxTessEvaluationTextureImageUnits = 16; glslRes.maxTessEvaluationUniformComponents = 1024;
    glslRes.maxTessPatchComponents = 120; glslRes.maxPatchVertices = 32; glslRes.maxTessGenLevel = 64;
    glslRes.maxViewports = 16; glslRes.maxVertexAtomicCounters = 0;
    glslRes.maxTessControlAtomicCounters = 0; glslRes.maxTessEvaluationAtomicCounters = 0;
    glslRes.maxGeometryAtomicCounters = 0; glslRes.maxFragmentAtomicCounters = 8;
    glslRes.maxCombinedAtomicCounters = 8; glslRes.maxAtomicCounterBindings = 1;
    glslRes.maxVertexAtomicCounterBuffers = 0; glslRes.maxTessControlAtomicCounterBuffers = 0;
    glslRes.maxTessEvaluationAtomicCounterBuffers = 0; glslRes.maxGeometryAtomicCounterBuffers = 0;
    glslRes.maxFragmentAtomicCounterBuffers = 1; glslRes.maxCombinedAtomicCounterBuffers = 1;
    glslRes.maxAtomicCounterBufferSize = 16384; glslRes.maxTransformFeedbackBuffers = 4;
    glslRes.maxTransformFeedbackInterleavedComponents = 64; glslRes.maxCullDistances = 8;
    glslRes.maxCombinedClipAndCullDistances = 8; glslRes.maxSamples = 4;
    glslRes.limits.nonInductiveForLoops = 1; glslRes.limits.whileLoops = 1;
    glslRes.limits.doWhileLoops = 1; glslRes.limits.generalUniformIndexing = 1;
    glslRes.limits.generalAttributeMatrixVectorIndexing = 1;
    glslRes.limits.generalVaryingIndexing = 1; glslRes.limits.generalSamplerIndexing = 1;
    glslRes.limits.generalVariableIndexing = 1;
    glslRes.limits.generalConstantMatrixVectorIndexing = 1;

    std::vector<uint32_t> spirv;
    if (!glslShader.parse(&glslRes, 450, EProfile::ECoreProfile, false, true, EShMsgDefault)) {
        LOG_ERROR("WebGPU shader GLSL parse failed ({}): {}", fileName.c_str(), glslShader.getInfoLog());
        glslang::FinalizeProcess();
        return {};
    }
    glslang::TProgram glslProg;
    glslProg.addShader(&glslShader);
    if (!glslProg.link(EShMsgDefault)) {
        LOG_ERROR("WebGPU shader link failed ({}): {}", fileName.c_str(), glslProg.getInfoLog());
        glslang::FinalizeProcess();
        return {};
    }
    glslang::GlslangToSpv(*glslProg.getIntermediate(glslStage), spirv);
    glslang::FinalizeProcess();

    // Reflect SPIR-V → uniform offsets/sizes, resource counts
    ShaderAsset _shader;
    _shader.shaderFilename = fileName;
    try {
        spirv_cross::Compiler spvComp(spirv);
        auto spvRes = spvComp.get_shader_resources();

        _shader.samplerCount        = static_cast<uint32_t>(spvRes.sampled_images.size());
        _shader.uniformBufferCount  = static_cast<uint32_t>(spvRes.uniform_buffers.size());
        _shader.storageBufferCount  = static_cast<uint32_t>(spvRes.storage_buffers.size());
        _shader.storageTextureCount = static_cast<uint32_t>(spvRes.storage_images.size());

        for (const auto& ub : spvRes.uniform_buffers) {
            auto& bufType = spvComp.get_type(ub.base_type_id);
            for (size_t i = 0; i < bufType.member_types.size(); ++i) {
                const std::string& name = spvComp.get_member_name(ub.base_type_id, i);
                _shader.uniformOffsets[name] = spvComp.type_struct_member_offset(bufType, i);
                _shader.uniformSizes[name]   = spvComp.get_declared_struct_member_size(bufType, i);
            }
        }
    } catch (const std::exception& e) {
        LOG_ERROR("WebGPU SPIRV reflection failed ({}): {}", fileName.c_str(), e.what());
    }

    // Load pre-transpiled WGSL file (generated at build time by Tint).
    std::string wgslPath = "/_transpiled/" + fileName + ".wgsl";
    std::string wgsl = FileHandler::ReadTextFile(wgslPath);

    if (wgsl.empty()) {
        LOG_ERROR("WebGPU: pre-transpiled WGSL not found for '{}' (looked at '{}')", fileName.c_str(), wgslPath.c_str());
        return {};
    }

    LOG_INFO("Loaded WGSL ({} bytes) for '{}'", wgsl.size(), fileName.c_str());

    // Post-process: fix Tint's group(2) binding pair order.
    // Tint emits each GLSL combined sampler2D as binding(N*2)=texture, binding(N*2+1)=sampler.
    // IGpu expects binding(N*2)=sampler, binding(N*2+1)=texture. Swap within every pair.
    {
        std::regex re(R"(@group\(2u?\)\s*@binding\((\d+)u?\))");
        std::string out;
        out.reserve(wgsl.size());
        auto it  = std::sregex_iterator(wgsl.begin(), wgsl.end(), re);
        auto end = std::sregex_iterator();
        size_t last = 0;
        for (; it != end; ++it) {
            auto& m = *it;
            out.append(wgsl, last, m.position() - last);
            uint32_t b = (uint32_t)std::stoul(m[1].str());
            uint32_t swapped = (b ^ 1u);   // flip low bit: 0<->1, 2<->3, 4<->5, ...
            out += "@group(2u) @binding(" + std::to_string(swapped) + "u)";
            last = m.position() + m.length();
        }
        out.append(wgsl, last, std::string::npos);
        wgsl = std::move(out);
    }

    // Fragment shaders authored against SDL_GPU place uniforms at set=3. The WebGPU
    // backend's graphics pipeline layout expects fragment uniforms at group(1). Remap
    // @group(3u) → @group(1u) so the same GLSL works in both backends without source forks.
    if (gpuStage == GpuShaderStage::Fragment) {
        std::regex re(R"(@group\(3u?\))");
        wgsl = std::regex_replace(wgsl, re, "@group(1u)");
    }

    // Vertex shaders authored against SDL_GPU place uniforms at set=1. The WebGPU
    // backend's graphics pipeline layout expects vertex uniforms at group(0). Remap
    // @group(1u) → @group(0u) so the same GLSL works in both backends without source forks.
    if (gpuStage == GpuShaderStage::Vertex) {
        std::regex re(R"(@group\(1u?\))");
        wgsl = std::regex_replace(wgsl, re, "@group(0u)");
    }

    GpuShaderCreateInfo shaderCI;
    shaderCI.code                = reinterpret_cast<const uint8_t*>(wgsl.c_str());
    shaderCI.codeSize            = wgsl.size();
    shaderCI.entrypoint          = "main";
    shaderCI.stage               = gpuStage;
    shaderCI.samplerCount        = _shader.samplerCount;
    shaderCI.uniformBufferCount  = _shader.uniformBufferCount;
    shaderCI.storageBufferCount  = _shader.storageBufferCount;
    shaderCI.storageTextureCount = _shader.storageTextureCount;

    _shader.gpuShader = Renderer::GetGpu().createShader(shaderCI);
    return _shader;
}
