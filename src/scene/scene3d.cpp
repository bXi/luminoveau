#include "scene3d.h"
#include <stdexcept>

void Scene::_new(const std::string &name) {
    if (_scenes.find(name) != _scenes.end()) {
        LOG_CRITICAL("scene with name {} already exists", name);
    }
    _scenes[name] = SceneData();
}

void Scene::_switch(const std::string &name) {
    if (_scenes.find(name) == _scenes.end()) {
        LOG_CRITICAL("scene with name {} does not exist", name);
    }
    _currentSceneName = name;
}

// Camera
void Scene::_setCamera(vf3d position, vf3d target) {
    auto &scene           = _getCurrentScene();
    scene.camera.position = position;
    scene.camera.target   = target;
}

void Scene::_setCameraFOV(float fov) {
    _getCurrentScene().camera.fov = fov;
}

void Scene::_setCameraClipPlanes(float nearPlane, float farPlane) {
    auto &scene            = _getCurrentScene();
    scene.camera.nearPlane = nearPlane;
    scene.camera.farPlane  = farPlane;
}

Camera3D &Scene::_getCamera() {
    return _getCurrentScene().camera;
}

// Models
ModelInstance &Scene::_addModel(ModelAsset *model, vf3d position, vf3d rotation, vf3d scale) {
    ModelInstance instance;
    instance.model    = model;
    instance.position = position;
    instance.rotation = rotation;
    instance.scale    = scale;
    _getCurrentScene().models.push_back(instance);
    return _getCurrentScene().models.back();
}

std::vector<ModelInstance> &Scene::_getModels() {
    return _getCurrentScene().models;
}

// Lights
Light &Scene::_addPointLight(const Light &light) {
    Light newLight = light;
    newLight.type  = LightType::Point;
    _getCurrentScene().lights.push_back(newLight);
    return _getCurrentScene().lights.back();
}

Light &Scene::_addPointLight(vf3d position, Color color, float intensity) {
    Light light;
    light.type      = LightType::Point;
    light.position  = position;
    light.color     = color;
    light.intensity = intensity;
    _getCurrentScene().lights.push_back(light);
    return _getCurrentScene().lights.back();
}

Light &Scene::_addDirectionalLight(const Light &light) {
    Light newLight     = light;
    newLight.type      = LightType::Directional;
    newLight.direction = newLight.direction.norm();
    _getCurrentScene().lights.push_back(newLight);
    return _getCurrentScene().lights.back();
}

Light &Scene::_addDirectionalLight(vf3d direction, Color color, float intensity) {
    Light light;
    light.type      = LightType::Directional;
    light.direction = direction.norm();
    light.color     = color;
    light.intensity = intensity;
    _getCurrentScene().lights.push_back(light);
    return _getCurrentScene().lights.back();
}

Light &Scene::_addSpotLight(const Light &light) {
    Light newLight     = light;
    newLight.type      = LightType::Spot;
    newLight.direction = newLight.direction.norm();
    _getCurrentScene().lights.push_back(newLight);
    return _getCurrentScene().lights.back();
}

std::vector<Light> &Scene::_getLights() {
    return _getCurrentScene().lights;
}

void Scene::_setAmbientLight(Color color) {
    _getCurrentScene().ambientLight = color;
}

Color Scene::_getAmbientLight() {
    return _getCurrentScene().ambientLight;
}

// Clear
void Scene::_clearModels() {
    _getCurrentScene().models.clear();
}

void Scene::_clearLights() {
    _getCurrentScene().lights.clear();
}

void Scene::_clear() {
    _clearModels();
    _clearLights();
}

void Scene::_delete(const std::string &name) {
    if (name == "defaultScene") {
        LOG_CRITICAL("cannot delete the default scene");
    }
    if (_scenes.find(name) == _scenes.end()) {
        LOG_CRITICAL("scene with name {} does not exist", name);
    }
    if (_currentSceneName == name) {
        _currentSceneName = "defaultScene";
    }
    _scenes.erase(name);
}
