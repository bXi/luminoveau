// Sprite Vertex Shader (HLSL)
// Instanced sprite rendering with vertex buffers and half-precision data packing

struct SpriteData
{
    uint pos_xy;      // x in low 16 bits, y in high 16 bits
    uint pos_z_rot;   // z in low 16 bits, rotation in high 16 bits
    uint tex_uv;      // u in low 16 bits, v in high 16 bits
    uint tex_wh;      // w in low 16 bits, h in high 16 bits
    uint color_rg;    // r in low 16 bits, g in high 16 bits
    uint color_ba;    // b in low 16 bits, a in high 16 bits
    uint size_wh;     // w in low 16 bits, h in high 16 bits
    uint pivot_xy;    // pivot_x in low 16 bits, pivot_y in high 16 bits
};

struct VertexInput
{
    uint pos_xy : TEXCOORD0;  // Vertex position from vertex buffer (half-float packed)
    uint uv : TEXCOORD1;      // UV coordinates from vertex buffer (half-float packed)
};

struct Output
{
    float2 Texcoord : TEXCOORD0;
    float4 Color : TEXCOORD1;
    float4 Position : SV_Position;
};

// Storage buffer for sprite instance data
StructuredBuffer<SpriteData> SpriteInstances : register(t0, space0);

cbuffer UniformBlock : register(b0, space1)
{
    float4x4 ViewProjectionMatrix : packoffset(c0);
};

cbuffer InstanceOffset : register(b1, space1)
{
    uint baseInstance : packoffset(c0);
};

// Float16 to Float32 conversion using HLSL built-in
float unpackHalf(uint h16)
{
    return f16tof32(h16);
}

float2 unpackHalf2(uint packed)
{
    uint h0 = packed & 0xFFFF;
    uint h1 = (packed >> 16) & 0xFFFF;
    return float2(unpackHalf(h0), unpackHalf(h1));
}

Output main(VertexInput input, uint instanceID : SV_InstanceID)
{
    // Read sprite data from storage buffer
    SpriteData sprite = SpriteInstances[instanceID + baseInstance];
    
    // Unpack instance data
    float x = unpackHalf(sprite.pos_xy & 0xFFFF);
    float y = unpackHalf((sprite.pos_xy >> 16) & 0xFFFF);
    float rotation = unpackHalf((sprite.pos_z_rot >> 16) & 0xFFFF);
    float2 texUV = unpackHalf2(sprite.tex_uv);
    float2 texWH = unpackHalf2(sprite.tex_wh);
    float4 color = float4(
        unpackHalf(sprite.color_rg & 0xFFFF),
        unpackHalf((sprite.color_rg >> 16) & 0xFFFF),
        unpackHalf(sprite.color_ba & 0xFFFF),
        unpackHalf((sprite.color_ba >> 16) & 0xFFFF)
    );
    float2 scale = unpackHalf2(sprite.size_wh);
    float2 pivot = unpackHalf2(sprite.pivot_xy);

    // Unpack vertex data from vertex buffer
    // Vertex position is in local space (0,0 to 1,1 for quad, -1 to 1 for circle, etc.)
    float2 vertexPos = unpackHalf2(input.pos_xy);
    float2 vertexUV = unpackHalf2(input.uv);
    
    // Start with vertex position in local space
    float2 coord = vertexPos;
    
    // Calculate texture coordinates (vertex UV mapped through instance UV rect)
    float2 texcoord = float2(
        texUV.x + vertexUV.x * texWH.x,
        texUV.y + vertexUV.y * texWH.y
    );
    
    // Apply pivot offset before rotation
    if (rotation != 0.0)
    {
        coord -= pivot;
    }
    
    // Apply scale
    coord *= scale;
    
    // Apply rotation
    if (rotation != 0.0)
    {
        float c = cos(rotation);
        float s = sin(rotation);
        float2x2 rotMatrix = float2x2(c, s, -s, c);
        coord = mul(coord, rotMatrix);
        coord += (pivot * scale);
    }

    // Add sprite position (NO Z-DEPTH FOR NOW)
    float3 worldPos = float3(coord + float2(x, y), 0.0f);
    
    Output output;
    output.Position = mul(ViewProjectionMatrix, float4(worldPos, 1.0f));
    output.Texcoord = texcoord;
    output.Color = color;
    return output;
}
