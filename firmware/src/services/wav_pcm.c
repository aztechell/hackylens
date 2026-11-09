#include "wav_pcm.h"

#include <string.h>

#include "../core/hk_binary.h"

wav_result_t wav_pcm_read(wav_read_at_t read_at, void *context,
                         uint32_t file_size, wav_pcm_t *pcm)
{
    static const uint8_t pcm_guid[16] = {
        1, 0, 0, 0, 0, 0, 0x10, 0, 0x80, 0, 0, 0xaa, 0, 0x38, 0x9b, 0x71,
    };
    uint8_t data[40];
    uint32_t offset = 12U;
    uint32_t end;
    uint16_t align = 0U;
    uint8_t found_format = 0U;
    uint8_t found_data = 0U;

    memset(pcm, 0, sizeof(*pcm));
    if(file_size < offset)
        return HK_WAV_INVALID;
    if(!read_at(context, 0U, data, offset))
        return HK_WAV_IO;
    if(memcmp(data, "RIFF", 4U) || memcmp(data + 8U, "WAVE", 4U))
        return HK_WAV_INVALID;
    if(rd32(data + 4U) > file_size - 8U || rd32(data + 4U) < 4U)
        return HK_WAV_INVALID;
    end = 8U + rd32(data + 4U);

    while(offset < end && (!found_format || !found_data))
    {
        uint32_t size, padded;
        if(end - offset < 8U)
            return HK_WAV_INVALID;
        if(!read_at(context, offset, data, 8U))
            return HK_WAV_IO;
        size = rd32(data + 4U);
        offset += 8U;
        if(size > end - offset || (size & 1U && size == end - offset))
            return HK_WAV_INVALID;
        padded = size + (size & 1U);

        if(!memcmp(data, "fmt ", 4U))
        {
            uint16_t tag;
            uint32_t read_size = size < sizeof(data) ? size : sizeof(data);
            if(found_format || size < 16U)
                return HK_WAV_INVALID;
            if(!read_at(context, offset, data, read_size))
                return HK_WAV_IO;
            tag = rd16(data);
            if(tag == 0xfffeU)
            {
                if(size < 40U || rd16(data + 16U) < 22U ||
                   rd16(data + 16U) > size - 18U)
                    return HK_WAV_INVALID;
                if(rd16(data + 18U) != 16U ||
                   memcmp(data + 24U, pcm_guid, sizeof(pcm_guid)))
                    return HK_WAV_UNSUPPORTED;
            }
            else if(tag != 1U)
                return HK_WAV_UNSUPPORTED;
            pcm->channels = rd16(data + 2U);
            pcm->rate = rd32(data + 4U);
            align = rd16(data + 12U);
            if((pcm->channels != 1U && pcm->channels != 2U) ||
               pcm->rate < 8000U || pcm->rate > 48000U ||
               rd16(data + 14U) != 16U)
                return HK_WAV_UNSUPPORTED;
            if(align != 2U * pcm->channels || rd32(data + 8U) != pcm->rate * align)
                return HK_WAV_INVALID;
            found_format = 1U;
        }
        else if(!memcmp(data, "data", 4U))
        {
            if(found_data)
                return HK_WAV_INVALID;
            pcm->offset = offset;
            pcm->bytes = size;
            found_data = 1U;
        }
        offset += padded;
    }

    if(!found_format || !found_data || !pcm->bytes || pcm->bytes % align)
        return HK_WAV_INVALID;
    return HK_WAV_OK;
}

void wav_pcm_header(uint8_t header[HK_WAV_HEADER_BYTES],
                    uint32_t rate, uint32_t bytes)
{
    memcpy(header, "RIFF", 4U);
    wr32(header + 4U, 36U + bytes);
    memcpy(header + 8U, "WAVEfmt ", 8U);
    wr32(header + 16U, 16U);
    wr16(header + 20U, 1U);
    wr16(header + 22U, 1U);
    wr32(header + 24U, rate);
    wr32(header + 28U, rate * 2U);
    wr16(header + 32U, 2U);
    wr16(header + 34U, 16U);
    memcpy(header + 36U, "data", 4U);
    wr32(header + 40U, bytes);
}

int32_t wav_pcm_sample(const uint8_t *frame, uint16_t channels)
{
    int32_t left = (int16_t)rd16(frame);
    if(channels == 2U)
        return (left + (int16_t)rd16(frame + 2U)) / 2;
    return left;
}
