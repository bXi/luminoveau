#pragma once

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include "math/vectors.h"

/**
 * @brief 3D camera with position, rotation, and projection settings
 */
struct Camera3D {
    vf3d position = {0.0f, 0.0f, 5.0f};  ///< World-space camera position.
    vf3d target = {0.0f, 0.0f, 0.0f};    ///< World-space point the camera looks at.
    vf3d up = {0.0f, 1.0f, 0.0f};        ///< Up direction used to orient the camera.
    
    float fov = 60.0f;        ///< Vertical field of view in degrees.
    float nearPlane = 0.1f;   ///< Near clip plane distance.
    float farPlane = 100.0f;  ///< Far clip plane distance.
    
    /**
     * @brief Gets the view matrix for this camera
     */
    glm::mat4 GetViewMatrix() const {
        // Left-handed: X+ = right, Y+ = up, Z+ = into the screen (forward/north).
        return glm::lookAtLH(
            glm::vec3(position.x, position.y, position.z),
            glm::vec3(target.x, target.y, target.z),
            glm::vec3(up.x, up.y, up.z)
        );
    }

    /**
     * @brief Gets the projection matrix for this camera
     * @param aspectRatio Width/height ratio of the viewport
     */
    glm::mat4 GetProjectionMatrix(float aspectRatio) const {
        return glm::perspectiveLH_ZO(glm::radians(fov), aspectRatio, nearPlane, farPlane);
    }
    
    /**
     * @brief Gets the combined view-projection matrix
     * @param aspectRatio Width/height ratio of the viewport
     */
    glm::mat4 GetViewProjectionMatrix(float aspectRatio) const {
        return GetProjectionMatrix(aspectRatio) * GetViewMatrix();
    }
};
