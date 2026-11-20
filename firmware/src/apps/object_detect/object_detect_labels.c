#include "object_detect_labels.h"

#include "../../services/object_detect_config.h"

static const char *const g_labels[OBJECT_DETECT_CLASS_COUNT] = {
#define CLASS_LABEL(class_id, label) [class_id] = label,
    OBJECT_DETECT_VOC20_LABELS(CLASS_LABEL)
#undef CLASS_LABEL
};

const char *object_detect_label(uint8_t class_id)
{
    return class_id < OBJECT_DETECT_CLASS_COUNT ? g_labels[class_id] : "unknown";
}
