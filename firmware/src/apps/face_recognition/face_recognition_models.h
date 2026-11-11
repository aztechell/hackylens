#ifndef HK_FACE_RECOGNITION_MODELS_H
#define HK_FACE_RECOGNITION_MODELS_H

#include "../../services/ai_model_runtime.h"
#include "../../storage/face_gallery.h"

#define FACE_LANDMARK_SIDE 128U
#define FACE_EMBEDDING_SIDE 64U
#define FACE_LANDMARK_COUNT 5U
#define FACE_EMBEDDING_MODEL_CRC UINT32_C(0xd168d336)

extern const ai_model_descriptor_t face_landmark_model;
extern const ai_model_descriptor_t face_embedding_model;

#endif
