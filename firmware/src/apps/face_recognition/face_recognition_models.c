#include "face_recognition_models.h"

#define LANDMARK_BYTES (3U * FACE_LANDMARK_SIDE * FACE_LANDMARK_SIDE)
#define EMBEDDING_BYTES (3U * FACE_EMBEDDING_SIDE * FACE_EMBEDDING_SIDE)

const ai_model_descriptor_t face_landmark_model = {
    .id = "canmv-ld5",
    .directory_path = FACE_GALLERY_DIRECTORY,
    .model_path = FACE_GALLERY_DIRECTORY "/ld5.kmodel",
    .max_model_bytes = 159768U,
    .expected_model_crc32 = UINT32_C(0xe198a4d4),
    .scratch_bytes = LANDMARK_BYTES,
    .kmodel = {
        .version = 3U, .flags = 1U, .layers_length = 11U,
        .main_mem_usage = 5120U, .output_count = 1U, .input_bytes = LANDMARK_BYTES,
    },
    .input = {
        .name = "face", .bytes = LANDMARK_BYTES,
        .shape = {1U, 3U, FACE_LANDMARK_SIDE, FACE_LANDMARK_SIDE},
        .rank = 4U, .element = AI_MODEL_ELEMENT_U8, .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "landmarks", .bytes = 2U * FACE_LANDMARK_COUNT * sizeof(float),
        .shape = {2U * FACE_LANDMARK_COUNT}, .rank = 1U,
        .element = AI_MODEL_ELEMENT_F32, .layout = AI_MODEL_LAYOUT_FLAT,
    }},
    .output_count = 1U,
};

const ai_model_descriptor_t face_embedding_model = {
    .id = "canmv-face-embedding",
    .directory_path = FACE_GALLERY_DIRECTORY,
    .model_path = FACE_GALLERY_DIRECTORY "/feature_extraction.kmodel",
    .max_model_bytes = 1108368U,
    .expected_model_crc32 = FACE_EMBEDDING_MODEL_CRC,
    .kmodel = {
        .version = 4U, .arch = 1U, .layers_length = 80U,
        .main_mem_usage = 49152U, .output_count = 1U, .input_bytes = EMBEDDING_BYTES,
    },
    .input = {
        .name = "aligned_face", .bytes = EMBEDDING_BYTES,
        .shape = {1U, 3U, FACE_EMBEDDING_SIDE, FACE_EMBEDDING_SIDE},
        .rank = 4U, .element = AI_MODEL_ELEMENT_U8, .layout = AI_MODEL_LAYOUT_CHW,
    },
    .outputs = {{
        .name = "embedding", .bytes = FACE_GALLERY_DIM * sizeof(float),
        .shape = {FACE_GALLERY_DIM}, .rank = 1U,
        .element = AI_MODEL_ELEMENT_F32, .layout = AI_MODEL_LAYOUT_FLAT,
    }},
    .postprocess = AI_MODEL_POSTPROCESS_EMBEDDING,
    .output_count = 1U,
};
