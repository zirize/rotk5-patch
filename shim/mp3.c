/*
 * MP3 decoding for the CD audio tracks, with minimp3 (CC0, vendored in
 * minimp3/). Positions are in CD frames: 588 stereo samples, 1/75 s.
 */
#define MINIMP3_IMPLEMENTATION
#define MINIMP3_ONLY_MP3
#include "shim.h"
#include "minimp3/minimp3_ex.h"

#define FRAME_SAMPLES (588 * 2)

struct mp3 {
    mp3dec_ex_t dec;
};

mp3_t *mp3_open(const char *path, DWORD *frames)
{
    mp3_t *m = calloc(1, sizeof(*m));

    if (!m)
        return NULL;
    if (mp3dec_ex_open(&m->dec, path, MP3D_SEEK_TO_SAMPLE)) {
        free(m);
        return NULL;
    }
    if (m->dec.info.hz != 44100 || m->dec.info.channels != 2) {
        shim_log("cdaudio: %s is %d Hz, %d channels; need 44100 Hz stereo", path, m->dec.info.hz,
            m->dec.info.channels);
        mp3_close(m);
        return NULL;
    }
    if (frames)
        *frames = (DWORD)(m->dec.samples / FRAME_SAMPLES);
    return m;
}

BOOL mp3_seek(mp3_t *m, DWORD frame)
{
    return !mp3dec_ex_seek(&m->dec, (uint64_t)frame * FRAME_SAMPLES);
}

DWORD mp3_read(mp3_t *m, BYTE *buf, DWORD frames)
{
    return (DWORD)mp3dec_ex_read(&m->dec, (mp3d_sample_t *)buf, frames * FRAME_SAMPLES) * sizeof(mp3d_sample_t);
}

void mp3_close(mp3_t *m)
{
    if (!m)
        return;
    mp3dec_ex_close(&m->dec);
    free(m);
}
