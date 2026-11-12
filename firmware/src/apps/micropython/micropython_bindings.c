#include <stddef.h>
#include <stdint.h>

#include <hackylens/capability/time.h>
#include <hackylens/capability/vision.h>

#include "py/runtime.h"

#include "../../config/input_config.h"
#include "../../adapters/micropython/micropython_capability_bridge.h"
#include "../../adapters/micropython/micropython_camera_codes.h"
#include "../../services/audio_service.h"
#include "../../services/micropython_runtime.h"

static void binding_time_raise(hk_result_t result)
{
    if(result == HK_ERR_LIMIT || result == HK_ERR_INVALID_ARGUMENT)
        mp_raise_ValueError(MP_ERROR_TEXT("time limit invalid"));
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("time capability error"));
}

static uint8_t binding_time_cancel(const void *context)
{
    (void)context;
    return micropython_runtime_interrupt_pending();
}

static void binding_raise(micropython_binding_result_t result)
{
    if(result == MICROPYTHON_BINDING_ERROR_INVALID_ARGUMENT ||
       result == MICROPYTHON_BINDING_ERROR_LIMIT)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid binding argument"));
    if(result == MICROPYTHON_BINDING_ERROR_TIMEOUT)
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding timeout"));
    if(result == MICROPYTHON_BINDING_ERROR_BUSY)
        mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding busy"));
    if(result == MICROPYTHON_BINDING_ERROR_IO)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("binding I/O error"));
    if(result == MICROPYTHON_BINDING_ERROR_UNAVAILABLE)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("capability unavailable"));
    if(result == MICROPYTHON_BINDING_ERROR_MODEL_FORMAT)
        mp_raise_ValueError(MP_ERROR_TEXT("incompatible model or manifest"));
    if(result == MICROPYTHON_BINDING_ERROR_SD_NAME)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid SD filename"));
    if(result == MICROPYTHON_BINDING_ERROR_SD_EXISTS)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("SD file already exists"));
    if(result == MICROPYTHON_BINDING_ERROR_SD_NO_MEDIA)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("FAT32 microSD unavailable"));
    if(result == MICROPYTHON_BINDING_ERROR_SD_FULL)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("microSD full"));
    if(result == MICROPYTHON_BINDING_ERROR_SD_IO)
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("microSD write failed"));
    mp_raise_msg(&mp_type_RuntimeError, MP_ERROR_TEXT("binding inactive"));
}

static size_t binding_call(micropython_binding_op_t operation,
                           const uint32_t arguments[6],
                           const uint8_t *input, size_t input_length,
                           uint8_t *output, size_t output_capacity)
{
    size_t output_length = 0U;
    micropython_binding_result_t result = micropython_binding_call(
        operation, arguments, input, input_length,
        output, output_capacity, &output_length);

    if(result != MICROPYTHON_BINDING_OK)
        binding_raise(result);
    return output_length;
}

static uint32_t binding_uint(mp_obj_t object, uint32_t maximum)
{
    mp_int_t value = mp_obj_get_int(object);

    if(value < 0 || (mp_uint_t)value > maximum)
        mp_raise_ValueError(MP_ERROR_TEXT("integer out of range"));
    return (uint32_t)value;
}

static mp_obj_t binding_buttons(void)
{
    uint8_t output[4];
    uint32_t arguments[6] = {0};
    uint32_t value;

    (void)binding_call(MICROPYTHON_BINDING_OP_BUTTONS, arguments,
                       NULL, 0U, output, sizeof(output));
    value = (uint32_t)output[0] |
            ((uint32_t)output[1] << 8) |
            ((uint32_t)output[2] << 16) |
            ((uint32_t)output[3] << 24);
    return mp_obj_new_int_from_uint(value);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_buttons_obj, binding_buttons);

static mp_obj_t binding_button(mp_obj_t mask_object)
{
    uint32_t mask = binding_uint(mask_object, BUTTON_ALL);
    mp_obj_t state = binding_buttons();

    return mp_obj_new_bool((mp_obj_get_int(state) & mask) != 0);
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_button_obj, binding_button);

static mp_obj_t binding_ticks_ms(void)
{
    uint64_t now = 0U;
    hk_result_t result = hk_time_now_us(hk_time_service(), &now);
    if(result != HK_OK)
        binding_time_raise(result);
    return mp_obj_new_int_from_uint((mp_uint_t)(now / 1000ULL));
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_ticks_ms_obj, binding_ticks_ms);

static mp_obj_t binding_sleep_ms(mp_obj_t duration_object)
{
    uint32_t duration = binding_uint(
        duration_object, MICROPYTHON_RUNTIME_MAX_LIMIT_MS);
    hk_deadline_t wake;
    const hk_cancel_t cancel = {binding_time_cancel, NULL};
    hk_result_t result = hk_time_deadline_after_us(
            hk_time_service(), (uint64_t)duration * 1000ULL,
            &wake);
    if(result == HK_OK)
        result = hk_time_sleep_until(
            hk_time_service(), wake, wake, &cancel);
    if(result == HK_ERR_CANCELLED)
        micropython_runtime_vm_hook();
    if(result != HK_OK)
        binding_time_raise(result);
    micropython_runtime_vm_hook();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_sleep_ms_obj, binding_sleep_ms);

static mp_obj_t binding_display_clear(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    if(count)
        arguments[0] = binding_uint(args[0], 0xFFFFU);
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_CLEAR, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_clear_obj, 0, 1, binding_display_clear);

static mp_obj_t binding_display_text(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};
    size_t text_length;
    const char *text;

    arguments[0] = binding_uint(args[0], 319U);
    arguments[1] = binding_uint(args[1], 239U);
    arguments[2] = count >= 4U ? binding_uint(args[3], 0xFFFFU) : 0xFFFFU;
    arguments[3] = count >= 5U ? binding_uint(args[4], 0xFFFFU) : 0U;
    text = (const char *)mp_obj_str_get_data(args[2], &text_length);
    if(text_length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("text too long"));
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_TEXT, arguments,
                       (const uint8_t *)text, text_length, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_text_obj, 3, 5, binding_display_text);

static mp_obj_t binding_display_rect(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(args[0], 319U);
    arguments[1] = binding_uint(args[1], 239U);
    arguments[2] = binding_uint(args[2], 320U);
    arguments[3] = binding_uint(args[3], 240U);
    arguments[4] = binding_uint(args[4], 0xFFFFU);
    arguments[5] = count >= 6U && mp_obj_is_true(args[5]);
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_RECT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_rect_obj, 5, 6, binding_display_rect);

static mp_obj_t binding_display_size(void)
{
    uint32_t arguments[6] = {0};
    uint8_t output[4];
    mp_obj_t dimensions[2];

    if(binding_call(MICROPYTHON_BINDING_OP_DISPLAY_SIZE, arguments,
                    NULL, 0U, output, sizeof(output)) != sizeof(output))
        mp_raise_msg(&mp_type_OSError, MP_ERROR_TEXT("invalid display size"));
    dimensions[0] = MP_OBJ_NEW_SMALL_INT(output[0] | (output[1] << 8));
    dimensions[1] = MP_OBJ_NEW_SMALL_INT(output[2] | (output[3] << 8));
    return mp_obj_new_tuple(2U, dimensions);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_display_size_obj, binding_display_size);

static mp_obj_t binding_display_rects(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};
    mp_buffer_info_t buffer;
    size_t length;

    mp_get_buffer_raise(args[0], &buffer, MP_BUFFER_READ);
    length = buffer.len;
    if(count == 2U)
        length = binding_uint(args[1], MICROPYTHON_BINDING_RECTS_MAX) *
            MICROPYTHON_BINDING_RECT_BYTES;
    if(length > buffer.len || length > MICROPYTHON_BINDING_RECTS_BYTES ||
       length % MICROPYTHON_BINDING_RECT_BYTES != 0U)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid rectangle buffer length"));
    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_RECTS, arguments,
                       buffer.buf, length, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_display_rects_obj, 1, 2, binding_display_rects);

static mp_obj_t binding_display_present(void)
{
    uint32_t arguments[6] = {0};

    (void)binding_call(MICROPYTHON_BINDING_OP_DISPLAY_PRESENT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_display_present_obj,
                                 binding_display_present);

static mp_obj_t binding_led(mp_obj_t brightness_object)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(brightness_object, 100U);
    (void)binding_call(MICROPYTHON_BINDING_OP_LED, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_led_obj, binding_led);

static mp_obj_t binding_rgb(mp_obj_t red_object, mp_obj_t green_object,
                            mp_obj_t blue_object)
{
    uint32_t arguments[6] = {0};

    arguments[0] = binding_uint(red_object, 255U);
    arguments[1] = binding_uint(green_object, 255U);
    arguments[2] = binding_uint(blue_object, 255U);
    (void)binding_call(MICROPYTHON_BINDING_OP_RGB, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_3(binding_rgb_obj, binding_rgb);

static mp_obj_t binding_uart_init(size_t count, const mp_obj_t *args)
{
    uint32_t arguments[6] = {0};

    arguments[0] = count ? binding_uint(args[0], 2000000U) : 115200U;
    if(arguments[0] < 1200U)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid baud"));
    (void)binding_call(MICROPYTHON_BINDING_OP_UART_INIT, arguments,
                       NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_uart_init_obj, 0, 1, binding_uart_init);

static mp_obj_t binding_uart_write(mp_obj_t data_object)
{
    size_t length;
    const uint8_t *data = (const uint8_t *)mp_obj_str_get_data(
        data_object, &length);
    uint32_t arguments[6] = {0};
    size_t position = 0U;

    while(position < length)
    {
        size_t chunk = length - position;
        if(chunk > MICROPYTHON_BINDING_DATA_MAX)
            chunk = MICROPYTHON_BINDING_DATA_MAX;
        (void)binding_call(MICROPYTHON_BINDING_OP_UART_WRITE, arguments,
                           data + position, chunk, NULL, 0U);
        position += chunk;
    }
    return mp_obj_new_int_from_uint(length);
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_uart_write_obj, binding_uart_write);

static mp_obj_t binding_uart_read(size_t count, const mp_obj_t *args)
{
    uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
    uint32_t arguments[6] = {0};
    size_t length;

    arguments[0] = count ? binding_uint(
        args[0], MICROPYTHON_BINDING_DATA_MAX) : 64U;
    length = binding_call(MICROPYTHON_BINDING_OP_UART_READ, arguments,
                          NULL, 0U, data, arguments[0]);
    return mp_obj_new_bytes(data, length);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_uart_read_obj, 0, 1, binding_uart_read);

static mp_obj_t binding_i2c_write(mp_obj_t address_object,
                                  mp_obj_t data_object)
{
    uint32_t arguments[6] = {0};
    size_t length;
    const uint8_t *data = (const uint8_t *)mp_obj_str_get_data(
        data_object, &length);

    arguments[0] = binding_uint(address_object, 0x7FU);
    if(arguments[0] == 0U || length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid I2C write"));
    (void)binding_call(MICROPYTHON_BINDING_OP_I2C_WRITE, arguments,
                       data, length, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(binding_i2c_write_obj, binding_i2c_write);

static mp_obj_t binding_i2c_read(size_t count, const mp_obj_t *args)
{
    uint8_t data[MICROPYTHON_BINDING_DATA_MAX];
    uint32_t arguments[6] = {0};
    const uint8_t *prefix = NULL;
    size_t prefix_length = 0U;
    size_t length;

    arguments[0] = binding_uint(args[0], 0x7FU);
    arguments[1] = binding_uint(args[1], MICROPYTHON_BINDING_DATA_MAX);
    if(arguments[0] == 0U)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid I2C address"));
    if(count >= 3U)
        prefix = (const uint8_t *)mp_obj_str_get_data(args[2], &prefix_length);
    if(prefix_length > MICROPYTHON_BINDING_DATA_MAX)
        mp_raise_ValueError(MP_ERROR_TEXT("I2C prefix too long"));
    length = binding_call(MICROPYTHON_BINDING_OP_I2C_READ, arguments,
                          prefix, prefix_length, data, arguments[1]);
    return mp_obj_new_bytes(data, length);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(
    binding_i2c_read_obj, 2, 3, binding_i2c_read);

static mp_obj_t binding_audio_available(void)
{
    uint8_t available = 0U;
    (void)binding_call(MICROPYTHON_BINDING_OP_AUDIO_AVAILABLE,
                       NULL, NULL, 0U, &available, sizeof(available));
    return mp_obj_new_bool(available);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_audio_available_obj, binding_audio_available);

static mp_obj_t binding_audio_play(mp_obj_t pcm_object)
{
    mp_buffer_info_t pcm;
    mp_get_buffer_raise(pcm_object, &pcm, MP_BUFFER_READ);
    if(!pcm.len || pcm.len % 2U || pcm.len > HK_AUDIO_PCM_MAX_BYTES)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid audio buffer size"));
    (void)binding_call(MICROPYTHON_BINDING_OP_AUDIO_PLAY,
                       NULL, pcm.buf, pcm.len, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_audio_play_obj, binding_audio_play);

static mp_obj_t binding_audio_record_into(mp_obj_t pcm_object)
{
    mp_buffer_info_t pcm;
    uint32_t arguments[6] = {0};
    mp_get_buffer_raise(pcm_object, &pcm, MP_BUFFER_WRITE);
    if(!pcm.len || pcm.len % 2U || pcm.len > HK_AUDIO_PCM_MAX_BYTES)
        mp_raise_ValueError(MP_ERROR_TEXT("invalid audio buffer size"));
    arguments[0] = (uint32_t)pcm.len;
    return mp_obj_new_int_from_uint(binding_call(
        MICROPYTHON_BINDING_OP_AUDIO_RECORD, arguments,
        NULL, 0U, pcm.buf, pcm.len));
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_audio_record_into_obj, binding_audio_record_into);

static mp_obj_t binding_accelerometer_available(void)
{
    uint8_t available = 0U;
    (void)binding_call(MICROPYTHON_BINDING_OP_ACCELEROMETER_AVAILABLE,
                       NULL, NULL, 0U, &available, sizeof(available));
    return mp_obj_new_bool(available);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_accelerometer_available_obj, binding_accelerometer_available);

static mp_obj_t binding_acceleration_mg(void)
{
    int16_t axes[3];
    size_t bytes = binding_call(MICROPYTHON_BINDING_OP_ACCELEROMETER_READ,
                                NULL, NULL, 0U, (uint8_t *)axes, sizeof(axes));
    if(bytes == 0U)
        return mp_const_none;
    if(bytes != sizeof(axes))
        binding_raise(MICROPYTHON_BINDING_ERROR_IO);
    mp_obj_t values[] = {mp_obj_new_int(axes[0]), mp_obj_new_int(axes[1]), mp_obj_new_int(axes[2])};
    return mp_obj_new_tuple(3U, values);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_acceleration_mg_obj, binding_acceleration_mg);

static mp_obj_t binding_vision_object(const vision_result_snapshot_t *snapshot)
{
    if(snapshot->count > VISION_RESULT_MAX_ITEMS ||
       snapshot->qr_length > VISION_RESULT_MAX_QR_BYTES)
        binding_raise(MICROPYTHON_BINDING_ERROR_IO);

    mp_obj_t items[VISION_RESULT_MAX_ITEMS];
    for(uint8_t i = 0U; i < snapshot->count; i++)
    {
        const vision_result_item_t *item = &snapshot->items[i];
        mp_obj_t fields[] = {
            MP_OBJ_NEW_SMALL_INT(item->kind), MP_OBJ_NEW_SMALL_INT(item->flags),
            MP_OBJ_NEW_SMALL_INT(item->id), MP_OBJ_NEW_SMALL_INT(item->x0),
            MP_OBJ_NEW_SMALL_INT(item->y0), MP_OBJ_NEW_SMALL_INT(item->x1),
            MP_OBJ_NEW_SMALL_INT(item->y1), MP_OBJ_NEW_SMALL_INT(item->confidence),
        };
        items[i] = mp_obj_new_tuple(MP_ARRAY_SIZE(fields), fields);
    }

    mp_obj_t result = mp_obj_new_dict(6U);
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_frame_id),
                      mp_obj_new_int_from_uint(snapshot->frame_id));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_source),
                      MP_OBJ_NEW_SMALL_INT(snapshot->source));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_width),
                      MP_OBJ_NEW_SMALL_INT(snapshot->width));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_height),
                      MP_OBJ_NEW_SMALL_INT(snapshot->height));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_items),
                      mp_obj_new_tuple(snapshot->count, items));
    mp_obj_dict_store(result, MP_OBJ_NEW_QSTR(MP_QSTR_qr_payload),
                      mp_obj_new_bytes(snapshot->qr_payload, snapshot->qr_length));
    return result;
}

static mp_obj_t binding_vision_results(void)
{
    vision_result_snapshot_t snapshot;
    size_t length = binding_call(MICROPYTHON_BINDING_OP_VISION_RESULTS,
                                 NULL, NULL, 0U, (uint8_t *)&snapshot, sizeof(snapshot));
    if(length != sizeof(snapshot))
        binding_raise(MICROPYTHON_BINDING_ERROR_IO);
    return binding_vision_object(&snapshot);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_vision_results_obj, binding_vision_results);

static mp_obj_t binding_camera_open(void)
{
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_OPEN, NULL, NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_camera_open_obj, binding_camera_open);

static mp_obj_t binding_camera_close(void)
{
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_CLOSE, NULL, NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_camera_close_obj, binding_camera_close);

static mp_obj_t binding_camera_tuple(micropython_binding_op_t operation,
                                     const uint32_t arguments[6],
                                     const uint8_t *input, size_t input_length,
                                     size_t count)
{
    uint32_t output[5];
    mp_obj_t values[5];
    size_t length = binding_call(operation, arguments, input, input_length,
                                 (uint8_t *)output, sizeof(output));
    if(!length)
        return mp_const_none;
    if(length != count * sizeof(output[0]))
        binding_raise(MICROPYTHON_BINDING_ERROR_IO);
    for(size_t i = 0U; i < count; i++)
        values[i] = mp_obj_new_int_from_uint(output[i]);
    return mp_obj_new_tuple(count, values);
}

static mp_obj_t binding_camera_frame(void)
{
    return binding_camera_tuple(MICROPYTHON_BINDING_OP_CAMERA_FRAME, NULL, NULL, 0U, 4U);
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_camera_frame_obj, binding_camera_frame);

static mp_obj_t binding_camera_release(mp_obj_t lease)
{
    uint32_t arguments[6] = {binding_uint(lease, UINT32_MAX)};
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_RELEASE, arguments, NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_camera_release_obj, binding_camera_release);

static void binding_camera_rect(mp_obj_t rect, uint32_t arguments[6])
{
    if(rect == mp_const_none)
        return;
    mp_obj_t *values;
    mp_obj_get_array_fixed_n(rect, 4U, &values);
    for(uint8_t i = 0U; i < 4U; i++)
        arguments[i + 1U] = binding_uint(values[i], UINT16_MAX);
    if(!arguments[3] || !arguments[4])
        mp_raise_ValueError(MP_ERROR_TEXT("camera rectangle is empty"));
}

static mp_obj_t binding_camera_find_color(size_t count, const mp_obj_t *objects)
{
    static const uint8_t maximum[] = {31U, 31U, 63U, 63U, 31U, 31U};
    uint32_t arguments[6] = {binding_uint(objects[0], UINT32_MAX)};
    mp_obj_t *values;
    uint8_t threshold[MP_ARRAY_SIZE(maximum)];
    mp_obj_get_array_fixed_n(objects[1], MP_ARRAY_SIZE(threshold), &values);
    for(uint8_t i = 0U; i < MP_ARRAY_SIZE(threshold); i++)
        threshold[i] = binding_uint(values[i], maximum[i]);
    if(count >= 3U)
        binding_camera_rect(objects[2], arguments);
    arguments[5] = count == 4U ? binding_uint(objects[3], UINT32_MAX) : 8U;
    return binding_camera_tuple(MICROPYTHON_BINDING_OP_CAMERA_FIND_COLOR,
                                 arguments, threshold, sizeof(threshold), 5U);
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(binding_camera_find_color_obj, 2, 4,
                                          binding_camera_find_color);

static mp_obj_t binding_camera_present(size_t count, const mp_obj_t *objects)
{
    uint32_t arguments[6] = {binding_uint(objects[0], UINT32_MAX)};
    if(count >= 2U)
        binding_camera_rect(objects[1], arguments);
    arguments[5] = count == 3U ? binding_uint(objects[2], UINT16_MAX) : 0x07E0U;
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_PRESENT, arguments, NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(binding_camera_present_obj, 1, 3,
                                          binding_camera_present);

static mp_obj_t binding_camera_model_open(size_t count, const mp_obj_t *objects)
{
    uint32_t arguments[6] = {binding_uint(objects[0], UINT8_MAX)};
    size_t package_length = 0U;
    const uint8_t *package = NULL;
    uint8_t ready;
    if(count == 2U)
        package = (const uint8_t *)mp_obj_str_get_data(objects[1], &package_length);
    while(!binding_call(MICROPYTHON_BINDING_OP_CAMERA_MODEL_OPEN, arguments,
                         package, package_length, &ready, sizeof(ready)))
        micropython_runtime_vm_hook();
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_VAR_BETWEEN(binding_camera_model_open_obj, 1, 2,
                                          binding_camera_model_open);

static mp_obj_t binding_camera_model_close(void)
{
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_MODEL_CLOSE, NULL, NULL, 0U, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_0(binding_camera_model_close_obj, binding_camera_model_close);

static mp_obj_t binding_camera_detect(mp_obj_t lease)
{
    const uint32_t arguments[6] = {binding_uint(lease, UINT32_MAX)};
    vision_result_snapshot_t snapshot;
    size_t length;
    (void)binding_call(MICROPYTHON_BINDING_OP_CAMERA_DETECT_SUBMIT, arguments,
                       NULL, 0U, NULL, 0U);
    do
    {
        micropython_runtime_vm_hook();
        length = binding_call(MICROPYTHON_BINDING_OP_CAMERA_DETECT_POLL, NULL,
                              NULL, 0U, (uint8_t *)&snapshot, sizeof(snapshot));
    } while(!length);
    if(length != sizeof(snapshot))
        binding_raise(MICROPYTHON_BINDING_ERROR_IO);
    return binding_vision_object(&snapshot);
}
static MP_DEFINE_CONST_FUN_OBJ_1(binding_camera_detect_obj, binding_camera_detect);

static mp_obj_t binding_camera_scan(mp_obj_t lease, mp_obj_t source_obj)
{
    uint32_t lease_id = binding_uint(lease, UINT32_MAX);
    uint32_t source = binding_uint(source_obj, UINT8_MAX);
    if(source != VISION_SOURCE_QR && source != VISION_SOURCE_APRILTAG)
        mp_raise_ValueError(MP_ERROR_TEXT("select VISION_QR or VISION_APRILTAG"));
    vision_result_snapshot_t snapshot;
    micropython_binding_result_t result = micropython_camera_codes_scan(
        lease_id, (vision_source_t)source, &snapshot);
    micropython_runtime_vm_hook();
    if(result != MICROPYTHON_BINDING_OK)
        binding_raise(result);
    return binding_vision_object(&snapshot);
}
static MP_DEFINE_CONST_FUN_OBJ_2(binding_camera_scan_obj, binding_camera_scan);

static void binding_storage_wait(micropython_binding_op_t op, const uint32_t *args,
                                 const uint8_t *input, size_t length)
{
    uint8_t done;
    do
    {
        if(binding_call(op, args, input, length, &done, sizeof(done)) != sizeof(done))
            binding_raise(MICROPYTHON_BINDING_ERROR_SD_IO);
    } while(!done);
}

static mp_obj_t binding_sd_write(mp_obj_t name_obj, mp_obj_t data_obj)
{
    size_t name_length;
    const uint8_t *name = (const uint8_t *)mp_obj_str_get_data(name_obj, &name_length);
    mp_buffer_info_t input;
    mp_get_buffer_raise(data_obj, &input, MP_BUFFER_READ);
    const uint8_t *data = input.buf;

    binding_storage_wait(MICROPYTHON_BINDING_OP_SD_BEGIN, NULL, name, name_length);
    size_t offset = 0U;
    while(offset < input.len)
    {
        uint8_t written[2];
        size_t take = input.len - offset;
        if(take > MICROPYTHON_BINDING_DATA_MAX)
            take = MICROPYTHON_BINDING_DATA_MAX;
        if(binding_call(MICROPYTHON_BINDING_OP_SD_WRITE, NULL, data + offset, take,
                        written, sizeof(written)) != sizeof(written))
            binding_raise(MICROPYTHON_BINDING_ERROR_SD_IO);
        offset += written[0] | ((uint32_t)written[1] << 8U);
    }
    binding_storage_wait(MICROPYTHON_BINDING_OP_SD_FINISH, NULL, NULL, 0U);
    return mp_obj_new_int_from_uint(offset);
}
static MP_DEFINE_CONST_FUN_OBJ_2(binding_sd_write_obj, binding_sd_write);

static mp_obj_t binding_camera_save(mp_obj_t lease_obj, mp_obj_t name_obj)
{
    size_t length;
    const uint8_t *name = (const uint8_t *)mp_obj_str_get_data(name_obj, &length);
    const uint32_t args[6] = {binding_uint(lease_obj, UINT32_MAX)};
    binding_storage_wait(MICROPYTHON_BINDING_OP_CAMERA_SAVE_BEGIN, args, name, length);
    binding_storage_wait(MICROPYTHON_BINDING_OP_CAMERA_SAVE_STEP, NULL, NULL, 0U);
    return mp_const_none;
}
static MP_DEFINE_CONST_FUN_OBJ_2(binding_camera_save_obj, binding_camera_save);

static const mp_rom_map_elem_t hackylens_module_globals_table[] = {
    { MP_ROM_QSTR(MP_QSTR___name__), MP_ROM_QSTR(MP_QSTR_hackylens) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_LEFT), MP_ROM_INT(BUTTON_LEFT) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_OK), MP_ROM_INT(BUTTON_OK) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_RIGHT), MP_ROM_INT(BUTTON_RIGHT) },
    { MP_ROM_QSTR(MP_QSTR_BUTTON_BACK), MP_ROM_INT(BUTTON_BACK) },
    { MP_ROM_QSTR(MP_QSTR_buttons), MP_ROM_PTR(&binding_buttons_obj) },
    { MP_ROM_QSTR(MP_QSTR_button), MP_ROM_PTR(&binding_button_obj) },
    { MP_ROM_QSTR(MP_QSTR_ticks_ms), MP_ROM_PTR(&binding_ticks_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_sleep_ms), MP_ROM_PTR(&binding_sleep_ms_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_clear), MP_ROM_PTR(&binding_display_clear_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_text), MP_ROM_PTR(&binding_display_text_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rect), MP_ROM_PTR(&binding_display_rect_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_size), MP_ROM_PTR(&binding_display_size_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_rects), MP_ROM_PTR(&binding_display_rects_obj) },
    { MP_ROM_QSTR(MP_QSTR_display_present), MP_ROM_PTR(&binding_display_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_led), MP_ROM_PTR(&binding_led_obj) },
    { MP_ROM_QSTR(MP_QSTR_rgb), MP_ROM_PTR(&binding_rgb_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_init), MP_ROM_PTR(&binding_uart_init_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_write), MP_ROM_PTR(&binding_uart_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_uart_read), MP_ROM_PTR(&binding_uart_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_write), MP_ROM_PTR(&binding_i2c_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_i2c_read), MP_ROM_PTR(&binding_i2c_read_obj) },
    { MP_ROM_QSTR(MP_QSTR_AUDIO_RATE_HZ), MP_ROM_INT(HK_AUDIO_RATE_HZ) },
    { MP_ROM_QSTR(MP_QSTR_AUDIO_MAX_BYTES), MP_ROM_INT(HK_AUDIO_PCM_MAX_BYTES) },
    { MP_ROM_QSTR(MP_QSTR_audio_available), MP_ROM_PTR(&binding_audio_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_audio_play), MP_ROM_PTR(&binding_audio_play_obj) },
    { MP_ROM_QSTR(MP_QSTR_audio_record_into), MP_ROM_PTR(&binding_audio_record_into_obj) },
    { MP_ROM_QSTR(MP_QSTR_accelerometer_available), MP_ROM_PTR(&binding_accelerometer_available_obj) },
    { MP_ROM_QSTR(MP_QSTR_acceleration_mg), MP_ROM_PTR(&binding_acceleration_mg_obj) },
    { MP_ROM_QSTR(MP_QSTR_vision_results), MP_ROM_PTR(&binding_vision_results_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_open), MP_ROM_PTR(&binding_camera_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_close), MP_ROM_PTR(&binding_camera_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_frame), MP_ROM_PTR(&binding_camera_frame_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_release), MP_ROM_PTR(&binding_camera_release_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_find_color), MP_ROM_PTR(&binding_camera_find_color_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_present), MP_ROM_PTR(&binding_camera_present_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_model_open), MP_ROM_PTR(&binding_camera_model_open_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_model_close), MP_ROM_PTR(&binding_camera_model_close_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_detect), MP_ROM_PTR(&binding_camera_detect_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_scan), MP_ROM_PTR(&binding_camera_scan_obj) },
    { MP_ROM_QSTR(MP_QSTR_sd_write), MP_ROM_PTR(&binding_sd_write_obj) },
    { MP_ROM_QSTR(MP_QSTR_camera_save), MP_ROM_PTR(&binding_camera_save_obj) },
    { MP_ROM_QSTR(MP_QSTR_VISION_NONE), MP_ROM_INT(VISION_SOURCE_NONE) },
    { MP_ROM_QSTR(MP_QSTR_VISION_FACE), MP_ROM_INT(VISION_SOURCE_FACE) },
    { MP_ROM_QSTR(MP_QSTR_VISION_QR), MP_ROM_INT(VISION_SOURCE_QR) },
    { MP_ROM_QSTR(MP_QSTR_VISION_APRILTAG), MP_ROM_INT(VISION_SOURCE_APRILTAG) },
    { MP_ROM_QSTR(MP_QSTR_VISION_OBJECT_DETECT), MP_ROM_INT(VISION_SOURCE_OBJECT_DETECT) },
    { MP_ROM_QSTR(MP_QSTR_VISION_FACE_RECOGNITION), MP_ROM_INT(VISION_SOURCE_FACE_RECOGNITION) },
    { MP_ROM_QSTR(MP_QSTR_VISION_USER), MP_ROM_INT(VISION_SOURCE_USER) },
    { MP_ROM_QSTR(MP_QSTR_VISION_BLOCK), MP_ROM_INT(VISION_ITEM_BLOCK) },
    { MP_ROM_QSTR(MP_QSTR_VISION_ARROW), MP_ROM_INT(VISION_ITEM_ARROW) },
};
static MP_DEFINE_CONST_DICT(hackylens_module_globals,
                            hackylens_module_globals_table);

const mp_obj_module_t hackylens_user_module = {
    .base = {&mp_type_module},
    .globals = (mp_obj_dict_t *)&hackylens_module_globals,
};

MP_REGISTER_MODULE(MP_QSTR_hackylens, hackylens_user_module);
