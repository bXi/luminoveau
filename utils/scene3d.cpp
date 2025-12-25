#include "scene3d.h"
#include <stdexcept>

void Scene::_new(const std::string& name) {
    if (scenes.find(name) != scenes.end()) {
        throw std::runtime_error("Scene with name '" + name + "' already exists");
    }
    scenes[name] = SceneData();
}

void Scene::_switch(const std::string& name) {
    if (scenes.find(name) == scenes.end()) {
        throw std::runtime_error("Scene with name '" + name + "' does not exist");
    }
    currentSceneName = name;
}

// Camera
void Scene::_setCamera(vf3d position, vf3d target) {
    auto& scene = getCurrentScene();
    scene.camera.position = position;
    scene.camera.target = target;
}

void Scene::_setCameraFOV(float fov) {
    getCurrentScene().camera.fov = fov;
}

void Scene::_setCameraClipPlanes(float nearPlane, float farPlane) {
    auto& scene = getCurrentScene();
    scene.camera.nearPlane = nearPlane;
    scene.camera.farPlane = farPlane;
}

Camera3D& Scene::_getCamera() {
    return getCurrentScene().camera;
}

// Models
ModelInstance& Scene::_addModel(ModelAsset* model, vf3d position, vf3d rotation, vf3d scale) {
    ModelInstance instance;
    instance.model = model;
    instance.position = position;
    instance.rotation = rotation;
    instance.scale = scale;
    getCurrentScene().models.push_back(instance);
    return getCurrentScene().models.back();
}

std::vector<ModelInstance>& Scene::_getModels() {
    return getCurrentScene().models;
}

// Lights
Light& Scene::_addPointLight(const Light& light) {
    Light newLight = light;
    newLight.type = LightType::Point;
    getCurrentScene().lights.push_back(newLight);
    return getCurrentScene().lights.back();
}

Light& Scene::_addPointLight(vf3d position, Color color, float intensity) {
    Light light;
    light.type = LightType::Point;
    light.position = position;
    light.color = color;
    light.intensity = intensity;
    getCurrentScene().lights.push_back(light);
    return getCurrentScene().lights.back();
}

Light& Scene::_addDirectionalLight(const Light& light) {
    Light newLight = light;
    newLight.type = LightType::Directional;
    newLight.direction = newLight.direction.norm();
    getCurrentScene().lights.push_back(newLight);
    return getCurrentScene().lights.back();
}

Light& Scene::_addDirectionalLight(vf3d direction, Color color, float intensity) {
    Light light;
    light.type = LightType::Directional;
    light.direction = direction.norm();
    light.color = color;
    light.intensity = intensity;
    getCurrentScene().lights.push_back(light);
    return getCurrentScene().lights.back();
}

Light& Scene::_addSpotLight(const Light& light) {
    Light newLight = light;
    newLight.type = LightType::Spot;
    newLight.direction = newLight.direction.norm();
    getCurrentScene().lights.push_back(newLight);
    return getCurrentScene().lights.back();
}

std::vector<Light>& Scene::_getLights() {
    return getCurrentScene().lights;
}

void Scene::_setAmbientLight(Color color) {
    getCurrentScene().ambientLight = color;
}

Color Scene::_getAmbientLight() {
    return getCurrentScene().ambientLight;
}

// Clear
void Scene::_clearModels() {
    getCurrentScene().models.clear();
}

void Scene::_clearLights() {
    getCurrentScene().lights.clear();
}

void Scene::_clear() {
    _clearModels();
    _clearLights();
}

void Scene::_delete(const std::string& name) {
    if (name == "defaultScene") {
        throw std::runtime_error("Cannot delete the default scene");
    }
    if (scenes.find(name) == scenes.end()) {
        throw std::runtime_error("Scene with name '" + name + "' does not exist");
    }
    if (currentSceneName == name) {
        currentSceneName = "defaultScene";
    }
    scenes.erase(name);
}
