#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

#include "face_recognition_image.h"
#include "image_decode_png_inflate.h"
#include "media_workspace.h"
#include "ai_model_storage.h"
#include "file_path.h"
#include "hk_binary.h"
#include "fat32_directory.h"

#define MODEL_TEST_BYTES (33U * 1024U + 17U)
static uint8_t model_fixture[MODEL_TEST_BYTES];
static uint8_t model_card = 1U;
static uint8_t gallery_sector[512];
static uint32_t gallery_size;
static uint8_t gallery_allocated;

uint8_t hk_sd_present(void) { return model_card; }
uint8_t hk_fat_mounted(void) { return model_card; }
uint8_t file_mount_if_needed(void) { return model_card; }

file_path_result_t file_path_find(const char *path, fat_file_entry_t *entry)
{
    memset(entry, 0, sizeof(*entry));
    if(strcmp(path, FACE_GALLERY_DIRECTORY) == 0)
    {
        entry->attr = 0x10U;
        entry->cluster = 2U;
        return FILE_PATH_OK;
    }
    if(strcmp(path, FACE_GALLERY_DIRECTORY "/FR01.DAT") == 0 && gallery_size)
    {
        entry->size = gallery_size;
        entry->cluster = 3U;
        return FILE_PATH_OK;
    }
    if(strcmp(path, "/model") != 0)
        return FILE_PATH_NOT_FOUND;
    entry->size = sizeof(model_fixture);
    return FILE_PATH_OK;
}

uint8_t fat_file_read_at(const fat_file_entry_t *entry, uint32_t offset,
                        uint8_t *destination, uint32_t bytes)
{
    const uint8_t *source = entry->cluster == 3U ? gallery_sector : model_fixture;
    assert(offset <= entry->size && bytes <= entry->size - offset);
    memcpy(destination, source + offset, bytes);
    return 1U;
}

uint8_t fat_stream_open(fat_stream_t *stream, const fat_file_entry_t *entry, uint32_t offset)
{
    stream->entry = entry;
    stream->file_offset = offset;
    return 1U;
}

uint8_t fat_stream_read(fat_stream_t *stream, uint8_t *destination, uint32_t bytes)
{
    uint8_t result = fat_file_read_at(stream->entry, stream->file_offset, destination, bytes);
    stream->file_offset += bytes;
    return result;
}

uint8_t fat_dir_find_slots(uint32_t directory, uint8_t count, fat_dir_slot_t *slot)
{
    assert(directory == 2U && count == 1U);
    *slot = (fat_dir_slot_t){.cluster = directory};
    return !gallery_size;
}

uint8_t fat_dir_write_slots(const fat_dir_slot_t *slot, const uint8_t *entries, uint8_t count)
{
    assert(slot->cluster == 2U && count == 1U && gallery_allocated);
    gallery_size = rd32(entries + 28U);
    return 1U;
}

uint8_t fat_dir_mark_deleted(uint32_t directory, uint32_t ordinal, uint8_t lfn_count)
{
    assert(directory == 2U && ordinal == 0U && lfn_count == 0U);
    gallery_size = 0U;
    return 1U;
}

uint8_t fat_alloc_chain(uint32_t count, uint32_t *cluster)
{
    assert(count == 1U && !gallery_allocated);
    *cluster = 3U;
    gallery_allocated = 1U;
    return 1U;
}

uint8_t fat_free_chain(uint32_t cluster)
{
    assert(cluster == 3U && gallery_allocated);
    gallery_allocated = 0U;
    return 1U;
}

uint32_t fat_cluster_lba(uint32_t cluster) { return cluster; }

uint8_t sd_card_write_block(uint32_t lba, const uint8_t *source)
{
    assert(lba == 3U && gallery_allocated);
    memcpy(gallery_sector, source, sizeof(gallery_sector));
    return 1U;
}

void fat_make_short_name(uint8_t out[11], const char *base, const char *ext)
{
    assert(strcmp(base, "FR01") == 0 && strcmp(ext, "DAT") == 0);
    memcpy(out, "FR01    DAT", 11U);
}

void fat_make_short_entry(uint8_t *entry, const uint8_t short_name[11],
                          uint8_t attr, uint32_t cluster, uint32_t size)
{
    assert(attr == 0x20U && cluster == 3U);
    memset(entry, 0, 32U);
    memcpy(entry, short_name, 11U);
    wr32(entry + 28U, size);
}

#define FRAME_PIXELS (FACE_DETECT_INPUT_WIDTH * FACE_DETECT_INPUT_HEIGHT)

static uint8_t frame[3U * FRAME_PIXELS];
static uint8_t crop[3U * FACE_LANDMARK_SIDE * FACE_LANDMARK_SIDE];

static void test_crop_and_alignment(void)
{
    const face_detect_box_t box = {70, 40, 64, 64};
    const float canonical[FACE_LANDMARK_COUNT][2] = {
        {21.88f, 29.54f}, {42.02f, 29.43f}, {32.01f, 41.00f},
        {23.74f, 52.78f}, {40.42f, 52.69f},
    };
    float landmarks[2U * FACE_LANDMARK_COUNT];
    for(uint16_t y = 0; y < FACE_DETECT_INPUT_HEIGHT; y++)
    {
        for(uint16_t x = 0; x < FACE_DETECT_INPUT_WIDTH; x++)
        {
            size_t i = (size_t)y * FACE_DETECT_INPUT_WIDTH + x;
            frame[i] = (uint8_t)x;
            frame[i + FRAME_PIXELS] = (uint8_t)y;
            frame[i + 2U * FRAME_PIXELS] = 220U;
        }
    }
    face_recognition_crop(frame, &box, crop);
    const size_t plane = FACE_LANDMARK_SIDE * FACE_LANDMARK_SIDE;
    assert(crop[0] == 70U && crop[plane] == 40U && crop[2U * plane] == 220U);
    assert(crop[plane - 1U] == 133U && crop[2U * plane - 1U] == 103U);
    for(uint8_t i = 0; i < FACE_LANDMARK_COUNT; i++)
    {
        for(uint8_t axis = 0; axis < 2; axis++)
        {
            float p = canonical[i][axis] / 64.0f;
            landmarks[i * 2U + axis] = logf(p / (1.0f - p));
        }
    }
    assert(face_recognition_align(frame, &box, landmarks, crop));
    size_t corner = FACE_EMBEDDING_SIDE * FACE_EMBEDDING_SIDE - 1U;
    assert(crop[0] == 70U && crop[corner] == 133U);
    assert(crop[corner + 1U] == 40U && crop[2U * corner + 1U] == 103U);
    landmarks[0] = NAN;
    assert(!face_recognition_align(frame, &box, landmarks, crop));
    memset(landmarks, 0, sizeof(landmarks));
    assert(!face_recognition_align(frame, &box, landmarks, crop));
}

static void test_features(void)
{
    float embedding[FACE_GALLERY_DIM] = {0};
    int8_t feature[FACE_GALLERY_DIM], scaled[FACE_GALLERY_DIM], different[FACE_GALLERY_DIM];
    assert(!face_recognition_encode(embedding, feature));
    embedding[0] = NAN;
    assert(!face_recognition_encode(embedding, feature));
    embedding[0] = 0.5f;
    embedding[1] = -1.0f;
    assert(face_recognition_encode(embedding, feature));
    embedding[0] = 25.0f;
    embedding[1] = -50.0f;
    assert(face_recognition_encode(embedding, scaled));
    assert(memcmp(feature, scaled, sizeof(feature)) == 0);
    assert(face_recognition_compare(feature, scaled) > 0.999f);
    embedding[0] = 0.0f;
    embedding[1] = 0.0f;
    embedding[2] = 1.0f;
    assert(face_recognition_encode(embedding, different));
    assert(face_recognition_compare(feature, different) == 0.0f);
}

static void test_png_workspace(const char *compressed_path)
{
    const char recognition = 0;
    const char png = 0;
    const ai_model_workspace_t weights = media_model_workspace_claim(&recognition);
    assert(weights.data && !media_workspace_claim(&png));
    media_workspace_release(&png);
    assert(!media_workspace_claim(&png));
    media_workspace_release(&recognition);

    media_workspace_t *media = media_workspace_claim(&png);
    assert(media);
    FILE *compressed = fopen(compressed_path, "rb");
    assert(compressed);
    size_t size = fread(media->png.compressed, 1U, sizeof(media->png.compressed), compressed);
    assert(fclose(compressed) == 0);
    uint8_t pixels[4096];
    assert(png_zlib_inflate_to_buffer(media->png.compressed, (uint32_t)size,
        pixels, sizeof(pixels), media->png.literals, media->png.distances));
    for(size_t i = 0U; i < sizeof(pixels); i++)
        assert(pixels[i] == (uint8_t)(i / 7U));
    media_workspace_release(&png);
}

static void test_incremental_model_read(const ai_model_workspace_t *workspace)
{
    ai_model_reader_t reader = {0};
    uint8_t *buffer;
    uint32_t size, crc;
    model_card = 1U;
    for(uint32_t i = 0U; i < sizeof(model_fixture); i++)
        model_fixture[i] = (uint8_t)(i ^ (i >> 3U));
    assert(ai_model_storage_load(NULL, "/model", sizeof(model_fixture), 256U, 0U,
        workspace, &buffer, &size, &crc, &reader) == AI_MODEL_STORAGE_PENDING);
    if(workspace)
        assert(buffer == workspace->data);
    assert(reader.stream.entry == &reader.entry && reader.entry.size == size && crc == 0U);
    assert(ai_model_storage_read_step(&reader, buffer, size, &crc) == AI_MODEL_STORAGE_PENDING);
    assert(reader.position == 16384U);
    assert(ai_model_storage_read_step(&reader, buffer, size, &crc) == AI_MODEL_STORAGE_PENDING);
    assert(ai_model_storage_read_step(&reader, buffer, size, &crc) == AI_MODEL_STORAGE_OK);
    assert(memcmp(buffer, model_fixture, size) == 0);
    assert(crc == crc32_update(0U, model_fixture, sizeof(model_fixture)));
    model_card = 0U;
    assert(ai_model_storage_read_step(&reader, buffer, size, &crc) == AI_MODEL_STORAGE_READ);
    if(!workspace)
        ai_model_storage_free(buffer);
}

static void test_gallery_records(void)
{
    face_gallery_t gallery;
    int8_t feature[FACE_GALLERY_DIM];
    for(uint16_t i = 0U; i < FACE_GALLERY_DIM; i++)
        feature[i] = (int8_t)(i % 127U);
    const uint32_t model_crc = FACE_EMBEDDING_MODEL_CRC;
    assert(face_gallery_load(&gallery, model_crc) == FACE_GALLERY_OK && !gallery.occupied);
    assert(face_gallery_store(&gallery, 1U, model_crc, feature) == FACE_GALLERY_OK);
    assert(gallery.occupied == 1U && gallery_allocated);
    assert(face_gallery_store(&gallery, 1U, model_crc, feature) == FACE_GALLERY_OCCUPIED);

    assert(face_gallery_load(&gallery, model_crc) == FACE_GALLERY_OK);
    assert(gallery.occupied == 1U && memcmp(gallery.features[0], feature, sizeof(feature)) == 0);
    assert(face_gallery_load(&gallery, model_crc ^ 1U) == FACE_GALLERY_CORRUPT);
    assert(!gallery.occupied && gallery.invalid == 1U);
    gallery_sector[sizeof(feature)] ^= 1U;
    assert(face_gallery_load(&gallery, model_crc) == FACE_GALLERY_CORRUPT);
    assert(face_gallery_forget(&gallery, 1U) == FACE_GALLERY_OK);
    assert(!gallery.occupied && !gallery.invalid && !gallery_allocated && !gallery_size);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    test_crop_and_alignment();
    test_features();
    test_png_workspace(argv[1]);
    test_gallery_records();
    test_incremental_model_read(NULL);
    const char model = 0;
    const ai_model_workspace_t weights = media_model_workspace_claim(&model);
    assert(weights.data);
    test_incremental_model_read(&weights);
    media_workspace_release(&model);
    puts("FACE_RECOGNITION_OK");
    return 0;
}
