// Simple 3D rendering test
// Add this to your game initialization

#include "luminoveau.h"

void Setup3DTest() {
    // Setup camera - looking at origin from a distance
    Scene::SetCamera(
        {0.0f, 3.0f, 5.0f},  // Camera position (above and back)
        {0.0f, 0.0f, 0.0f}   // Looking at origin
    );
    Scene::SetCameraFOV(60.0f);
    
    // Set a visible ambient light so we can see something even without lights
    Scene::SetAmbientLight({50, 50, 50, 255});
    
    // Add a bright directional light from above
    Scene::AddDirectionalLight(
        {-0.3f, -1.0f, -0.3f},  // Direction (down and slightly angled)
        {255, 255, 255, 255},   // White light
        1.0f                     // Full intensity
    );
    
    // Add a point light to the side
    Light pointLight;
    pointLight.type = LightType::Point;
    pointLight.position = {3.0f, 2.0f, 3.0f};
    pointLight.color = {255, 200, 150, 255};  // Warm white
    pointLight.intensity = 1.0f;
    pointLight.constant = 1.0f;
    pointLight.linear = 0.09f;
    pointLight.quadratic = 0.032f;
    Scene::AddPointLight(pointLight);
    
    // Create a cube model
    ModelAsset* cube = new ModelAsset();
    *cube = AssetHandler::CreateCube(1.0f);
    
    // Add cube at origin with red tint
    ModelInstance& inst1 = Scene::AddModel(
        cube,
        {0.0f, 0.0f, 0.0f},    // Position at origin
        {0.0f, 0.0f, 0.0f},    // No rotation
        {1.0f, 1.0f, 1.0f}     // Normal scale
    );
    inst1.tint = {255, 100, 100, 255};  // Red tint
    
    // Add another cube to the side with green tint
    ModelInstance& inst2 = Scene::AddModel(
        cube,
        {-2.0f, 0.0f, 0.0f},   // To the left
        {0.0f, 45.0f, 0.0f},   // Rotated 45 degrees on Y
        {0.8f, 0.8f, 0.8f}     // Slightly smaller
    );
    inst2.tint = {100, 255, 100, 255};  // Green tint
    
    // Add one more cube with blue tint
    ModelInstance& inst3 = Scene::AddModel(
        cube,
        {2.0f, 0.0f, 0.0f},    // To the right
        {45.0f, 0.0f, 0.0f},   // Rotated 45 degrees on X
        {0.8f, 0.8f, 0.8f}     // Slightly smaller
    );
    inst3.tint = {100, 100, 255, 255};  // Blue tint
    
    SDL_Log("3D Test Scene Setup Complete");
    SDL_Log("- Camera at (0, 3, 5) looking at origin");
    SDL_Log("- 3 cubes added (red center, green left, blue right)");
    SDL_Log("- 1 directional light + 1 point light + ambient");
}

// In your game loop, you can rotate the cubes:
void Update3DTest(float deltaTime) {
    static float rotation = 0.0f;
    rotation += deltaTime * 30.0f;  // 30 degrees per second
    
    std::vector<ModelInstance>& models = Scene::GetModels();
    if (models.size() >= 3) {
        // Rotate all cubes on Y axis
        models[0].rotation.y = rotation;
        models[1].rotation.y = rotation + 120.0f;
        models[2].rotation.y = rotation + 240.0f;
    }
}
