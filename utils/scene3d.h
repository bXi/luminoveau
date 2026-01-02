#pragma once

#include <vector>
#include <unordered_map>
#include <string>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "utils/vectors.h"
#include "utils/colors.h"
#include "utils/camera3d.h"
#include "assettypes/model.h"

#include "log/loghandler.h"
/**
 * @brief Instance of a 3D model with transform
 */
struct ModelInstance {
    ModelAsset* model = nullptr;
    
    vf3d position = {0.0f, 0.0f, 0.0f};
    vf3d rotation = {0.0f, 0.0f, 0.0f};  // Euler angles in degrees
    vf3d scale = {1.0f, 1.0f, 1.0f};
    
    Color tint = WHITE;
    TextureAsset textureOverride;  // Optional: overrides model's default texture (use model.texture if not set)
    
    /**
     * @brief Gets the model matrix for this instance
     */
    glm::mat4 GetModelMatrix() const {
        glm::mat4 model = glm::mat4(1.0f);
        
        // Translation
        model = glm::translate(model, glm::vec3(position.x, position.y, position.z));
        
        // Rotation (Z, Y, X order - typical for Euler angles)
        model = glm::rotate(model, glm::radians(rotation.z), glm::vec3(0.0f, 0.0f, 1.0f));
        model = glm::rotate(model, glm::radians(rotation.y), glm::vec3(0.0f, 1.0f, 0.0f));
        model = glm::rotate(model, glm::radians(rotation.x), glm::vec3(1.0f, 0.0f, 0.0f));
        
        // Scale
        model = glm::scale(model, glm::vec3(scale.x, scale.y, scale.z));
        
        return model;
    }
};

/**
 * @brief Types of lights in the scene
 */
enum class LightType {
    Point,
    Directional,
    Spot
};

/**
 * @brief Light in 3D space
 */
struct Light {
    LightType type = LightType::Point;
    
    vf3d position = {0.0f, 0.0f, 0.0f};      // For point/spot lights
    vf3d direction = {0.0f, -1.0f, 0.0f};    // For directional/spot lights
    
    Color color = WHITE;
    float intensity = 1.0f;
    
    // Point light attenuation
    float constant = 1.0f;
    float linear = 0.09f;
    float quadratic = 0.032f;
    
    // Spot light properties
    float cutoffAngle = 12.5f;     // Inner cone angle in degrees
    float outerCutoffAngle = 17.5f; // Outer cone angle in degrees
};

/**
 * @brief Internal 3D scene data
 */
struct SceneData {
    Camera3D camera;
    std::vector<ModelInstance> models;
    std::vector<Light> lights;
    Color ambientLight = {50, 50, 50, 255};
    
    SceneData() {
        // Default camera setup
        camera.position = {0.0f, 0.0f, 5.0f};
        camera.target = {0.0f, 0.0f, 0.0f};
    }
};

/**
 * @brief Singleton manager for 3D scenes
 */
class Scene {
public:
    /**
     * @brief Creates a new named scene
     * @param name Name of the scene
     */
    static void New(const std::string& name) { get()._new(name); }
    
    /**
     * @brief Switches to a different scene
     * @param name Name of the scene to switch to
     */
    static void Switch(const std::string& name) { get()._switch(name); }
    
    /**
     * @brief Gets the name of the current scene
     */
    static std::string GetCurrentSceneName() { return get()._getCurrentSceneName(); }
    
    // Camera methods
    
    /**
     * @brief Sets the camera position and target
     * @param position Camera position
     * @param target Camera target (what it's looking at)
     */
    static void SetCamera(vf3d position, vf3d target) { get()._setCamera(position, target); }
    
    /**
     * @brief Sets the camera field of view
     * @param fov Field of view in degrees
     */
    static void SetCameraFOV(float fov) { get()._setCameraFOV(fov); }
    
    /**
     * @brief Sets the camera near and far planes
     * @param nearPlane Near clipping plane
     * @param farPlane Far clipping plane
     */
    static void SetCameraClipPlanes(float nearPlane, float farPlane) { get()._setCameraClipPlanes(nearPlane, farPlane); }
    
    /**
     * @brief Gets the current camera
     */
    static Camera3D& GetCamera() { return get()._getCamera(); }
    
    // Model methods
    
    /**
     * @brief Adds a model instance to the current scene
     * @param model Pointer to the ModelAsset
     * @param position Position in 3D space
     * @param rotation Rotation in degrees (Euler angles)
     * @param scale Scale factors
     * @return Reference to the created ModelInstance
     */
    static ModelInstance& AddModel(ModelAsset* model, vf3d position = {0, 0, 0}, 
                                   vf3d rotation = {0, 0, 0}, vf3d scale = {1, 1, 1}) {
        return get()._addModel(model, position, rotation, scale);
    }
    
    /**
     * @brief Gets all models in the current scene
     */
    static std::vector<ModelInstance>& GetModels() { return get()._getModels(); }
    
    // Light methods
    
    /**
     * @brief Adds a point light to the current scene
     * @param light Light configuration
     * @return Reference to the created Light
     */
    static Light& AddPointLight(const Light& light) { return get()._addPointLight(light); }
    
    /**
     * @brief Adds a point light to the current scene
     * @param position Light position
     * @param color Light color
     * @param intensity Light intensity
     * @return Reference to the created Light
     */
    static Light& AddPointLight(vf3d position, Color color = WHITE, float intensity = 1.0f) {
        return get()._addPointLight(position, color, intensity);
    }
    
    /**
     * @brief Adds a directional light to the current scene
     * @param light Light configuration
     * @return Reference to the created Light
     */
    static Light& AddDirectionalLight(const Light& light) { return get()._addDirectionalLight(light); }
    
    /**
     * @brief Adds a directional light to the current scene
     * @param direction Light direction
     * @param color Light color
     * @param intensity Light intensity
     * @return Reference to the created Light
     */
    static Light& AddDirectionalLight(vf3d direction, Color color = WHITE, float intensity = 1.0f) {
        return get()._addDirectionalLight(direction, color, intensity);
    }
    
    /**
     * @brief Adds a spot light to the current scene
     * @param light Light configuration
     * @return Reference to the created Light
     */
    static Light& AddSpotLight(const Light& light) { return get()._addSpotLight(light); }
    
    /**
     * @brief Gets all lights in the current scene
     */
    static std::vector<Light>& GetLights() { return get()._getLights(); }
    
    /**
     * @brief Sets the ambient light color for the current scene
     * @param color Ambient light color
     */
    static void SetAmbientLight(Color color) { get()._setAmbientLight(color); }
    
    /**
     * @brief Gets the ambient light color for the current scene
     */
    static Color GetAmbientLight() { return get()._getAmbientLight(); }
    
    // Clear methods
    
    /**
     * @brief Clears all models from the current scene
     */
    static void ClearModels() { get()._clearModels(); }
    
    /**
     * @brief Clears all lights from the current scene
     */
    static void ClearLights() { get()._clearLights(); }
    
    /**
     * @brief Clears everything from the current scene
     */
    static void Clear() { get()._clear(); }
    
    /**
     * @brief Deletes a named scene
     * @param name Name of the scene to delete (cannot delete default scene)
     */
    static void Delete(const std::string& name) { get()._delete(name); }

private:
    std::unordered_map<std::string, SceneData> scenes;
    std::string currentSceneName = "defaultScene";
    
    void _new(const std::string& name);
    void _switch(const std::string& name);
    std::string _getCurrentSceneName() { return currentSceneName; }
    
    SceneData& getCurrentScene() {
        return scenes[currentSceneName];
    }
    
    // Camera
    void _setCamera(vf3d position, vf3d target);
    void _setCameraFOV(float fov);
    void _setCameraClipPlanes(float nearPlane, float farPlane);
    Camera3D& _getCamera();
    
    // Models
    ModelInstance& _addModel(ModelAsset* model, vf3d position, vf3d rotation, vf3d scale);
    std::vector<ModelInstance>& _getModels();
    
    // Lights
    Light& _addPointLight(const Light& light);
    Light& _addPointLight(vf3d position, Color color, float intensity);
    Light& _addDirectionalLight(const Light& light);
    Light& _addDirectionalLight(vf3d direction, Color color, float intensity);
    Light& _addSpotLight(const Light& light);
    std::vector<Light>& _getLights();
    void _setAmbientLight(Color color);
    Color _getAmbientLight();
    
    // Clear
    void _clearModels();
    void _clearLights();
    void _clear();
    void _delete(const std::string& name);
    
public:
    Scene(const Scene&) = delete;
    Scene& operator=(const Scene&) = delete;
    
    static Scene& get() {
        static Scene instance;
        return instance;
    }
    
private:
    Scene() {
        // Create default scene
        scenes["defaultScene"] = SceneData();
    }
};
