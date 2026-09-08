#include <hackylens/capability/lights.h>

#include <stddef.h>
#include "lights_provider.h"

const hk_lights_service_t *hk_lights_service(void)
{
    return hk_lights_binding.get_info ? &hk_lights_binding : NULL;
}

static hk_result_t validate_service(const hk_lights_service_t *service)
{
    if(!service)
        return HK_ERR_CAPABILITY_ABSENT;
    if(!service->state || !service->prepare || !service->close_channels ||
       !service->get_info || !service->set_level || !service->set_rgb)
        return HK_ERR_INTERNAL;
    return HK_OK;
}

static hk_result_t validate_session(const hk_lights_t *session)
{
    hk_result_t result;
    if(!session)
        return HK_ERR_INVALID_ARGUMENT;
    if(!session->service || !session->channels)
        return HK_ERR_STALE_HANDLE;
    result = validate_service(session->service);
    if(result != HK_OK)
        return result;
    if(session->channels & ~HK_LIGHTS_CHANNEL_ALL)
        return HK_ERR_INVALID_ARGUMENT;
    for(unsigned i = 0U; i < 3U; ++i)
        if((session->channels & (1U << i)) &&
           session->service->state->claimants[i] != session)
            return HK_ERR_WRONG_OWNER;
    return HK_OK;
}

hk_result_t hk_lights_get_info(const hk_lights_service_t *service,
    hk_lights_info_t *info)
{
    hk_result_t result;
    if(!info)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_service(service);
    return result == HK_OK ? service->get_info(service->context, info) : result;
}

hk_result_t hk_lights_open(const hk_lights_service_t *service,
    uint32_t channels, hk_lights_t *session)
{
    hk_lights_info_t info;
    hk_result_t result;
    if(!session || !channels || (channels & ~HK_LIGHTS_CHANNEL_ALL))
        return HK_ERR_INVALID_ARGUMENT;
    if(session->service || session->channels)
        return HK_ERR_INVALID_STATE;
    result = hk_lights_get_info(service, &info);
    if(result != HK_OK)
        return result;
    if((info.supported_channels & channels) != channels)
        return HK_ERR_FEATURE_UNAVAILABLE;
    if(service->state->quarantined & channels)
        return HK_ERR_INVALID_STATE;
    for(unsigned i = 0U; i < 3U; ++i)
        if((channels & (1U << i)) && service->state->claimants[i])
            return HK_ERR_BUSY;
    if(!service->state->prepared)
    {
        result = service->prepare(service->context);
        if(result != HK_OK)
        {
            if(result == HK_ERR_INTERNAL)
                service->state->quarantined |= channels;
            return result;
        }
        service->state->prepared = 1U;
    }
    session->service = service;
    session->channels = channels;
    for(unsigned i = 0U; i < 3U; ++i)
        if(channels & (1U << i))
            service->state->claimants[i] = session;
    return HK_OK;
}

static hk_result_t finish(hk_lights_t *session, hk_deadline_t deadline,
    uint8_t retire)
{
    hk_result_t first = HK_OK;
    const hk_lights_service_t *service;
    if(!session)
        return HK_ERR_INVALID_ARGUMENT;
    if(deadline.at_us == UINT64_MAX && !retire)
        return HK_ERR_INVALID_ARGUMENT;
    if(!session->service && !session->channels)
        return HK_OK;
    first = validate_session(session);
    if(first != HK_OK)
    {
        if(retire)
            *session = (hk_lights_t){0};
        return first;
    }
    service = session->service;
    if(deadline.at_us == UINT64_MAX && !retire)
        return HK_ERR_INVALID_ARGUMENT;
    for(unsigned i = 0U; i < 3U; ++i)
    {
        uint32_t channel = 1U << i;
        hk_result_t result;
        if(!(session->channels & channel))
            continue;
        result = deadline.at_us == UINT64_MAX ? HK_ERR_INVALID_ARGUMENT :
            service->close_channels(service->context, channel, deadline);
        if(first == HK_OK && result != HK_OK)
            first = result;
        if(result != HK_OK && (retire || result == HK_ERR_INTERNAL))
            service->state->quarantined |= channel;
    }
    if(first == HK_OK || retire)
    {
        for(unsigned i = 0U; i < 3U; ++i)
            if(service->state->claimants[i] == session)
                service->state->claimants[i] = NULL;
        *session = (hk_lights_t){0};
    }
    return first;
}

hk_result_t hk_lights_close(hk_lights_t *session, hk_deadline_t deadline)
{
    return finish(session, deadline, 0U);
}

hk_result_t hk_lights_retire(hk_lights_t *session, hk_deadline_t deadline)
{
    return finish(session, deadline, 1U);
}

static hk_result_t validate_write(const hk_lights_t *session,
    uint32_t channels)
{
    hk_result_t result = validate_session(session);
    if(result != HK_OK)
        return result;
    if((session->channels & channels) != channels)
        return HK_ERR_WRONG_OWNER;
    if(session->service->state->quarantined & channels)
        return HK_ERR_INVALID_STATE;
    return HK_OK;
}

static hk_result_t write_result(const hk_lights_t *session,
    uint32_t channels, hk_result_t result)
{
    if(result == HK_ERR_INTERNAL)
        session->service->state->quarantined |= channels;
    return result;
}

hk_result_t hk_lights_set_level(const hk_lights_t *session,
    uint32_t channel, uint16_t level, hk_deadline_t deadline,
    const hk_cancel_t *cancel)
{
    hk_result_t result;
    if((channel != HK_LIGHTS_CHANNEL_BACKLIGHT &&
        channel != HK_LIGHTS_CHANNEL_ILLUMINATION) ||
       level > HK_LIGHTS_LEVEL_MAX || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_write(session, channel);
    if(result != HK_OK)
        return result;
    return write_result(session, channel, session->service->set_level(
        session->service->context, channel, level, deadline, cancel));
}

hk_result_t hk_lights_set_rgb(const hk_lights_t *session,
    uint16_t red, uint16_t green, uint16_t blue,
    hk_deadline_t deadline, const hk_cancel_t *cancel)
{
    hk_result_t result;
    if(red > HK_LIGHTS_LEVEL_MAX || green > HK_LIGHTS_LEVEL_MAX ||
       blue > HK_LIGHTS_LEVEL_MAX || deadline.at_us == UINT64_MAX)
        return HK_ERR_INVALID_ARGUMENT;
    result = validate_write(session, HK_LIGHTS_CHANNEL_RGB);
    if(result != HK_OK)
        return result;
    return write_result(session, HK_LIGHTS_CHANNEL_RGB,
        session->service->set_rgb(session->service->context,
            red, green, blue, deadline, cancel));
}
