#include "shaderhandler.h"

#include "rendererhandler.h"

void Shaders::_init() {

}

PhysFSFileData Shaders::_getShader(const std::string &filename) {
    PhysFSFileData filedata;
    if (shaderCache.HasFile(filename)) {
        auto temp = shaderCache.GetFileBuffer(filename);

        filedata.fileDataVector = temp.vMemory;
        filedata.data = temp.vMemory.data();
        filedata.fileSize = temp.vMemory.size();
    } else {
        auto shader = AssetHandler::GetFileFromPhysFS(filename);
        auto formats = SDL_GetGPUShaderFormats(Renderer::GetDevice());

        EShLanguage shaderStage;
        if (SDL_strstr(filename.c_str(), ".vert")) {
            shaderStage = EShLanguage::EShLangVertex;
        } else if (SDL_strstr(filename.c_str(), ".frag")) {
            shaderStage = EShLanguage::EShLangFragment;
        } else {
            throw std::runtime_error("Invalid shader stage!!");
        }

        std::string shaderString(static_cast<char*>(shader.data), shader.fileSize);
        auto spirvBlob = _compileGLSLtoSPIRV(shaderString, shaderStage);

        if (spirvBlob.empty()) {
            throw std::runtime_error(Helpers::TextFormat("%s: failed to compile shader: %s", CURRENT_METHOD(), filename.c_str()));
        }

        if (formats & SDL_GPU_SHADERFORMAT_SPIRV) {

            std::vector<unsigned char> spirvBlobAsChars(reinterpret_cast<uint8_t*>(spirvBlob.data()), reinterpret_cast<uint8_t*>(spirvBlob.data() + spirvBlob.size()));
            shaderCache.AddFile(filename, spirvBlobAsChars);

            shaderCache.SavePack();

            std::vector<uint8_t> spirvBlobAsuint8(reinterpret_cast<uint8_t*>(spirvBlob.data()), reinterpret_cast<uint8_t*>(spirvBlob.data() + spirvBlob.size()));

            filedata.fileDataVector = std::move(spirvBlobAsuint8);
            filedata.data = filedata.fileDataVector.data();
            filedata.fileSize = spirvBlobAsChars.size();

        } else if (formats & SDL_GPU_SHADERFORMAT_DXBC) {
            // DXBC shaders are supported
            std::cout << "DXBC shaders supported" << std::endl;
        } else if (formats & SDL_GPU_SHADERFORMAT_MSL) {
            // Metal shaders are supported
            std::cout << "Metal shaders supported" << std::endl;
        }
    }
    return filedata;
}



std::vector<uint32_t> Shaders::_compileGLSLtoSPIRV(const std::string& source, EShLanguage shaderStage) {
    glslang::InitializeProcess();

    glslang::TShader shader(shaderStage);
    const char* sourceCStr = (const char *) source.data();
    shader.setStrings(&sourceCStr, 1);

    shader.setEnvInput(glslang::EShSourceGlsl, shaderStage, glslang::EShClientVulkan, 450);
    shader.setEnvClient(glslang::EShClientVulkan, glslang::EShTargetVulkan_1_1);
    shader.setEnvTarget(glslang::EShTargetSpv, glslang::EShTargetSpv_1_1);

    TBuiltInResource resources;

    _fillResources(&resources);

    if (!shader.parse(&resources, 450, EProfile::ECoreProfile, false, true, EShMsgDefault)) {
        std::cerr << "GLSL Parsing Failed: " << shader.getInfoLog() << std::endl;
        std::cerr << shader.getInfoDebugLog() << std::endl;
        glslang::FinalizeProcess();
        return {};
    }

    glslang::TProgram program;
    program.addShader(&shader);

    if (!program.link(EShMsgDefault)) {
        std::cerr << "Program Linking Failed: " << program.getInfoLog() << std::endl;
        glslang::FinalizeProcess();
        return {};
    }

    std::vector<uint32_t> spirv;
    glslang::GlslangToSpv(*program.getIntermediate(shaderStage), spirv);
    glslang::FinalizeProcess();
    return spirv;
}

// SPIR-V to HLSL Conversion
std::string Shaders::_convertSPIRVtoHLSL(const std::vector<uint32_t>& spirv) {
    spirv_cross::CompilerHLSL compiler(spirv);
    spirv_cross::CompilerHLSL::Options options;
    options.shader_model = 50; // Use Shader Model 5.0
    compiler.set_hlsl_options(options);

    return compiler.compile();
}

// SPIR-V to Metal Shading Language Conversion
std::string Shaders::_convertSPIRVtoMSL(const std::vector<uint32_t>& spirv) {
    spirv_cross::CompilerMSL compiler(spirv);
    return compiler.compile();
}

void Shaders::_fillResources(TBuiltInResource *resource) {
    resource->maxLights                                 = 32;
    resource->maxClipPlanes                             = 6;
    resource->maxTextureUnits                           = 32;
    resource->maxTextureCoords                          = 32;
    resource->maxVertexAttribs                          = 64;
    resource->maxVertexUniformComponents                = 4096;
    resource->maxVaryingFloats                          = 64;
    resource->maxVertexTextureImageUnits                = 32;
    resource->maxCombinedTextureImageUnits              = 80;
    resource->maxTextureImageUnits                      = 32;
    resource->maxFragmentUniformComponents              = 4096;
    resource->maxDrawBuffers                            = 32;
    resource->maxVertexUniformVectors                   = 128;
    resource->maxVaryingVectors                         = 8;
    resource->maxFragmentUniformVectors                 = 16;
    resource->maxVertexOutputVectors                    = 16;
    resource->maxFragmentInputVectors                   = 15;
    resource->minProgramTexelOffset                     = -8;
    resource->maxProgramTexelOffset                     = 7;
    resource->maxClipDistances                          = 8;
    resource->maxComputeWorkGroupCountX                 = 65535;
    resource->maxComputeWorkGroupCountY                 = 65535;
    resource->maxComputeWorkGroupCountZ                 = 65535;
    resource->maxComputeWorkGroupSizeX                  = 1024;
    resource->maxComputeWorkGroupSizeY                  = 1024;
    resource->maxComputeWorkGroupSizeZ                  = 64;
    resource->maxComputeUniformComponents               = 1024;
    resource->maxComputeTextureImageUnits               = 16;
    resource->maxComputeImageUniforms                   = 8;
    resource->maxComputeAtomicCounters                  = 8;
    resource->maxComputeAtomicCounterBuffers            = 1;
    resource->maxVaryingComponents                      = 60;
    resource->maxVertexOutputComponents                 = 64;
    resource->maxGeometryInputComponents                = 64;
    resource->maxGeometryOutputComponents               = 128;
    resource->maxFragmentInputComponents                = 128;
    resource->maxImageUnits                             = 8;
    resource->maxCombinedImageUnitsAndFragmentOutputs   = 8;
    resource->maxCombinedShaderOutputResources          = 8;
    resource->maxImageSamples                           = 0;
    resource->maxVertexImageUniforms                    = 0;
    resource->maxTessControlImageUniforms               = 0;
    resource->maxTessEvaluationImageUniforms            = 0;
    resource->maxGeometryImageUniforms                  = 0;
    resource->maxFragmentImageUniforms                  = 8;
    resource->maxCombinedImageUniforms                  = 8;
    resource->maxGeometryTextureImageUnits              = 16;
    resource->maxGeometryOutputVertices                 = 256;
    resource->maxGeometryTotalOutputComponents          = 1024;
    resource->maxGeometryUniformComponents              = 1024;
    resource->maxGeometryVaryingComponents              = 64;
    resource->maxTessControlInputComponents             = 128;
    resource->maxTessControlOutputComponents            = 128;
    resource->maxTessControlTextureImageUnits           = 16;
    resource->maxTessControlUniformComponents           = 1024;
    resource->maxTessControlTotalOutputComponents       = 4096;
    resource->maxTessEvaluationInputComponents          = 128;
    resource->maxTessEvaluationOutputComponents         = 128;
    resource->maxTessEvaluationTextureImageUnits        = 16;
    resource->maxTessEvaluationUniformComponents        = 1024;
    resource->maxTessPatchComponents                    = 120;
    resource->maxPatchVertices                          = 32;
    resource->maxTessGenLevel                           = 64;
    resource->maxViewports                              = 16;
    resource->maxVertexAtomicCounters                   = 0;
    resource->maxTessControlAtomicCounters              = 0;
    resource->maxTessEvaluationAtomicCounters           = 0;
    resource->maxGeometryAtomicCounters                 = 0;
    resource->maxFragmentAtomicCounters                 = 8;
    resource->maxCombinedAtomicCounters                 = 8;
    resource->maxAtomicCounterBindings                  = 1;
    resource->maxVertexAtomicCounterBuffers             = 0;
    resource->maxTessControlAtomicCounterBuffers        = 0;
    resource->maxTessEvaluationAtomicCounterBuffers     = 0;
    resource->maxGeometryAtomicCounterBuffers           = 0;
    resource->maxFragmentAtomicCounterBuffers           = 1;
    resource->maxCombinedAtomicCounterBuffers           = 1;
    resource->maxAtomicCounterBufferSize                = 16384;
    resource->maxTransformFeedbackBuffers               = 4;
    resource->maxTransformFeedbackInterleavedComponents = 64;
    resource->maxCullDistances                          = 8;
    resource->maxCombinedClipAndCullDistances           = 8;
    resource->maxSamples                                = 4;
    resource->maxMeshOutputVerticesNV                   = 256;
    resource->maxMeshOutputPrimitivesNV                 = 512;
    resource->maxMeshWorkGroupSizeX_NV                  = 32;
    resource->maxMeshWorkGroupSizeY_NV                  = 1;
    resource->maxMeshWorkGroupSizeZ_NV                  = 1;
    resource->maxTaskWorkGroupSizeX_NV                  = 32;
    resource->maxTaskWorkGroupSizeY_NV                  = 1;
    resource->maxTaskWorkGroupSizeZ_NV                  = 1;
    resource->maxMeshViewCountNV                        = 4;

    resource->limits.nonInductiveForLoops                 = 1;
    resource->limits.whileLoops                           = 1;
    resource->limits.doWhileLoops                         = 1;
    resource->limits.generalUniformIndexing               = 1;
    resource->limits.generalAttributeMatrixVectorIndexing = 1;
    resource->limits.generalVaryingIndexing               = 1;
    resource->limits.generalSamplerIndexing               = 1;
    resource->limits.generalVariableIndexing              = 1;
    resource->limits.generalConstantMatrixVectorIndexing  = 1;
}
