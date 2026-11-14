#include <assert.h>
#include <math.h>
#include <stdio.h>

#include "../firmware/src/apps/apriltag/apriltag_distance.h"

static void project(float corners[4][2], float side, float focal,
                    apriltag_vector_t center, float yaw, float pitch)
{
    static const float square[4][2] = {{-0.5f, -0.5f}, {0.5f, -0.5f},
                                       {0.5f, 0.5f}, {-0.5f, 0.5f}};
    for(unsigned i = 0U; i < 4U; i++)
    {
        float x = side * square[i][0], y = side * square[i][1];
        float camera_x = center.x + cosf(yaw) * x + sinf(yaw) * sinf(pitch) * y;
        float camera_y = center.y + cosf(pitch) * y;
        float camera_z = center.z - sinf(yaw) * x + cosf(yaw) * sinf(pitch) * y;
        corners[i][0] = 79.5f + focal * camera_x / camera_z;
        corners[i][1] = 59.5f + focal * camera_y / camera_z;
    }
}

int main(void)
{
    float corners[4][2], focal = 0.0f;
    apriltag_vector_t pose;
    project(corners, 60.0f, 123.4f, (apriltag_vector_t){0.0f, 0.0f, 300.0f}, 0.0f, 0.0f);
    assert(apriltag_distance_calibrate(corners, 60U, 300U, &focal));
    assert(fabsf(focal - 123.4f) < 0.01f);
    assert(!apriltag_distance_pose(corners, 60U, 0.0f, 0U, &pose));

    const apriltag_vector_t expected = {65.0f, -32.0f, 410.0f};
    project(corners, 60.0f, focal, expected, 0.48f, -0.33f);
    assert(apriltag_distance_pose(corners, 60U, focal, 0U, &pose));
    assert(fabsf(pose.x - expected.x) < 0.1f);
    assert(fabsf(pose.y - expected.y) < 0.1f);
    assert(fabsf(pose.z - expected.z) < 0.1f);
    assert(apriltag_distance_pose(corners, 100U, focal, 0U, &pose));
    assert(fabsf(pose.z - 410.0f * 100.0f / 60.0f) < 0.1f);
    corners[0][0] = -1.0f;
    assert(!apriltag_distance_pose(corners, 60U, focal, 0U, &pose));
    project(corners, 60.0f, focal, (apriltag_vector_t){0.0f, 0.0f, 1000.0f}, 0.0f, 0.0f);
    assert(!apriltag_distance_pose(corners, 60U, focal, 0U, &pose));
    project(corners, 100.0f, 154.0f, (apriltag_vector_t){0.0f, 0.0f, 2000.0f}, 0.0f, 0.0f);
    assert(!apriltag_distance_pose(corners, 100U, 154.0f, 0U, &pose));
    assert(apriltag_distance_pose(corners, 100U, 154.0f, 1U, &pose));
    assert(fabsf(pose.z - 2000.0f) < 0.1f);
    assert(!apriltag_distance_calibrate(corners, 100U, 1000U, &focal));
    project(corners, 100.0f, focal, (apriltag_vector_t){0.0f, 0.0f, 300.0f}, 0.7f, 0.0f);
    assert(!apriltag_distance_calibrate(corners, 100U, 300U, &focal));

    const int16_t upright[] = {-1000, 0, 0}, lens_down[] = {0, 1000, 0};
    const int16_t inclined[] = {-800, 600, 0}, moving[] = {-1600, 800, 0};
    float horizontal;
    const apriltag_vector_t forward = {0.0f, 0.0f, 500.0f};
    assert(apriltag_distance_horizontal(forward, upright, lens_down, inclined, &horizontal));
    assert(fabsf(horizontal - 400.0f) < 0.01f);
    assert(!apriltag_distance_horizontal(forward, upright, upright, inclined, &horizontal));
    assert(!apriltag_distance_horizontal(forward, upright, lens_down, moving, &horizontal));
    puts("APRILTAG_DISTANCE_OK");
    return 0;
}
