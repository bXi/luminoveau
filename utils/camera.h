#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "vectors.h"
#include "constants.h"
struct Camera {
    vf2d offset;         // Camera offset (displacement from target)
    vf2d target;         // Camera target (rotation and zoom origin)
    float rotation;         // Camera rotation in degrees
    float zoom;             // Camera zoom (scaling), should be 1.0f by default

    static glm::mat4 GetCameraMatrix2D(Camera camera) {
        glm::mat4 matTransform(1.0f); // Initialize as the identity matrix

        glm::mat4 matOrigin = glm::translate(glm::mat4(1.0f), glm::vec3(-camera.target.x, -camera.target.y, 0.0f));
        glm::mat4 matRotation = glm::rotate(glm::mat4(1.0f), camera.rotation * (PI / 180.f), glm::vec3(0.0f, 0.0f, 1.0f));
        glm::mat4 matScale = glm::scale(glm::mat4(1.0f), glm::vec3(camera.zoom, camera.zoom, 1.0f));
        glm::mat4 matTranslation = glm::translate(glm::mat4(1.0f), glm::vec3(camera.offset.x, camera.offset.y, 0.0f));

        matTransform = matTranslation * matRotation * matScale * matOrigin;

        return matTransform;
    }


    // Get the screen space position for a 2d camera world space position
    static vf2d GetWorldToScreen2D(vf2d position, Camera camera) {
        glm::mat4 matCamera = GetCameraMatrix2D(camera);
        glm::vec4 transformedPosition = glm::vec4(position.x, position.y, 0.0f, 1.0f);
        transformedPosition = matCamera * transformedPosition;

        return (vf2d) {transformedPosition.x, transformedPosition.y};
    }

// Get the world space position for a 2d camera screen space position
    static vf2d GetScreenToWorld2D(vf2d position, Camera camera) {
        glm::mat4 invMatCamera = glm::inverse(GetCameraMatrix2D(camera));
        glm::vec4 transformedPosition = glm::vec4(position.x, position.y, 0.0f, 1.0f);

        transformedPosition = invMatCamera * transformedPosition;

        return (vf2d) {transformedPosition.x, transformedPosition.y};
    }
};