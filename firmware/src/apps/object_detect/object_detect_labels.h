#ifndef HK_OBJECT_DETECT_LABELS_H
#define HK_OBJECT_DETECT_LABELS_H

#include <stdint.h>

#define OBJECT_DETECT_VOC20_LABELS(X) \
    X(0, "aeroplane")                 \
    X(1, "bicycle")                   \
    X(2, "bird")                      \
    X(3, "boat")                      \
    X(4, "bottle")                    \
    X(5, "bus")                       \
    X(6, "car")                       \
    X(7, "cat")                       \
    X(8, "chair")                     \
    X(9, "cow")                       \
    X(10, "diningtable")              \
    X(11, "dog")                      \
    X(12, "horse")                    \
    X(13, "motorbike")                \
    X(14, "person")                   \
    X(15, "pottedplant")               \
    X(16, "sheep")                    \
    X(17, "sofa")                     \
    X(18, "train")                    \
    X(19, "tvmonitor")

const char *object_detect_label(uint8_t class_id);

#endif
