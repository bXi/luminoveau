struct SpriteData
{
    float3 Position;
    float Rotation;
    float TexU, TexV, TexW, TexH;
    float4 Color;
    float2 Scale;
    float2 Pivot;
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

// Vertex positions for a quad (indexed)
static const float2 quadVertices[4] = {
    {0.0f, 0.0f},  // bottom-left
    {1.0f, 0.0f},  // bottom-right
    {0.0f, 1.0f},  // top-left
    {1.0f, 1.0f}   // top-right
};

Output main(uint vertexID : SV_VertexID, uint instanceID : SV_InstanceID)
{
    // Get sprite data for this instance
    SpriteData sprite = SpriteInstances[instanceID];
    
    // Get vertex position for this corner of the quad
    float2 coord = quadVertices[vertexID];
    
    // Calculate texture coordinates
    float2 texcoord = float2(
        sprite.TexU + coord.x * sprite.TexW,
        sprite.TexV + coord.y * sprite.TexH
    );
    
    // Apply pivot offset before rotation
    if (sprite.Rotation != 0.0)
    {
        coord -= sprite.Pivot;
    }
    
    // Apply scale
    coord *= sprite.Scale;
    
    // Apply rotation
    if (sprite.Rotation != 0.0)
    {
        float c = cos(sprite.Rotation);
        float s = sin(sprite.Rotation);
        float2x2 rotation = float2x2(c, s, -s, c);
        coord = mul(coord, rotation);
        coord += (sprite.Pivot * sprite.Scale);
    }
    
    // Add sprite position and create 3D coordinate with depth
    float3 worldPos = float3(coord + sprite.Position.xy, sprite.Position.z);
    
    Output output;
    output.Position = mul(ViewProjectionMatrix, float4(worldPos, 1.0f));
    output.Texcoord = texcoord;
    output.Color = sprite.Color;
    
    return output;
}
