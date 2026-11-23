#include "mc_audio.h"
#include "mc_assets.h"

#include <math.h>
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

#define MC_AUDIO_QUEUE_CAP 128u
#define MC_AUDIO_MAX_VOICES 32
#define MC_AUDIO_MIX_CHUNK 256
#define MC_AUDIO_ELYTRA_XFADE 2048u
#define MC_AUDIO_PI 3.14159265358979323846f

typedef struct {
    _Atomic uint64_t state;
    uint32_t seed;
    uint16_t event;
    float gain;
    float pitch;
    float pan;
} McAudioQueueSlot;

typedef struct {
    _Atomic uint64_t wr;
    uint64_t rd;
    McAudioQueueSlot slots[MC_AUDIO_QUEUE_CAP];
} McAudioQueue;

typedef struct {
    uint8_t active;
    uint8_t priority;
    uint16_t event;
    McSoundGroupId group;
    int variant;
    uint64_t start_seq;
    double pos;
    float pitch;
    float gain;
    float gain_l;
    float gain_r;
} McAudioVoice;

static const McSoundGroupId k_event_to_group[MC_SFX_COUNT] = {
    [MC_SFX_NONE]                  = MC_SNDGRP_COUNT,

    [MC_SFX_DIG_WOOD]              = MC_SNDGRP_DIG_WOOD,
    [MC_SFX_DIG_STONE]             = MC_SNDGRP_DIG_STONE,
    [MC_SFX_DIG_GRASS]             = MC_SNDGRP_DIG_GRASS,
    [MC_SFX_DIG_GRAVEL]            = MC_SNDGRP_DIG_GRAVEL,
    [MC_SFX_DIG_SAND]              = MC_SNDGRP_DIG_SAND,

    [MC_SFX_STEP_WOOD]             = MC_SNDGRP_STEP_WOOD,
    [MC_SFX_STEP_STONE]            = MC_SNDGRP_STEP_STONE,
    [MC_SFX_STEP_GRASS]            = MC_SNDGRP_STEP_GRASS,
    [MC_SFX_STEP_GRAVEL]           = MC_SNDGRP_STEP_GRAVEL,
    [MC_SFX_STEP_SAND]             = MC_SNDGRP_STEP_SAND,

    [MC_SFX_BOW_SHOOT]             = MC_SNDGRP_BOW_SHOOT,
    [MC_SFX_BOW_HIT]               = MC_SNDGRP_BOW_HIT,
    [MC_SFX_HIT_ARROW_DING]        = MC_SNDGRP_SUCCESSFUL_HIT,

    [MC_SFX_ATTACK_STRONG]         = MC_SNDGRP_ATTACK_STRONG,
    [MC_SFX_ATTACK_SWEEP]          = MC_SNDGRP_ATTACK_SWEEP,
    [MC_SFX_ATTACK_CRIT]           = MC_SNDGRP_ATTACK_CRIT,
    [MC_SFX_ATTACK_WEAK]           = MC_SNDGRP_ATTACK_WEAK,
    [MC_SFX_ATTACK_KNOCKBACK]      = MC_SNDGRP_ATTACK_KNOCKBACK,
    [MC_SFX_SHIELD_BLOCK]          = MC_SNDGRP_SHIELD_BLOCK,
    [MC_SFX_SHIELD_BREAK]          = MC_SNDGRP_SHIELD_BREAK,

    [MC_SFX_EAT]                   = MC_SNDGRP_EAT,
    [MC_SFX_BURP]                  = MC_SNDGRP_BURP,
    [MC_SFX_DRINK]                 = MC_SNDGRP_DRINK,
    [MC_SFX_XP_ORB]                = MC_SNDGRP_ORB,
    [MC_SFX_LEVELUP]               = MC_SNDGRP_LEVELUP,
    [MC_SFX_UI_CLICK]              = MC_SNDGRP_CLICK_STEREO,

    [MC_SFX_TNT_FUSE]              = MC_SNDGRP_FUSE,
    [MC_SFX_EXPLODE]               = MC_SNDGRP_EXPLODE,
    [MC_SFX_FIRE_IGNITE]           = MC_SNDGRP_IGNITE,
    [MC_SFX_FIREWORK_LAUNCH]       = MC_SNDGRP_FIREWORK_LAUNCH,
    [MC_SFX_FIREWORK_BLAST]        = MC_SNDGRP_FIREWORK_BLAST,
    [MC_SFX_FIREWORK_LARGE_BLAST]  = MC_SNDGRP_FIREWORK_LARGE_BLAST,
    [MC_SFX_FIREWORK_TWINKLE]      = MC_SNDGRP_FIREWORK_TWINKLE,
    [MC_SFX_TOTEM_USE]             = MC_SNDGRP_TOTEM_USE,

    [MC_SFX_HURT]                  = MC_SNDGRP_PLAYER_HURT,
    [MC_SFX_FALL_SMALL]            = MC_SNDGRP_FALL_SMALL,
    [MC_SFX_FALL_BIG]              = MC_SNDGRP_FALL_BIG,

    [MC_SFX_CREEPER_SAY]           = MC_SNDGRP_CREEPER_HURT,
    [MC_SFX_CREEPER_DEATH]         = MC_SNDGRP_CREEPER_DEATH,
    [MC_SFX_ZOMBIE_SAY]            = MC_SNDGRP_ZOMBIE_SAY,
    [MC_SFX_ZOMBIE_HURT]           = MC_SNDGRP_ZOMBIE_HURT,
    [MC_SFX_ZOMBIE_DEATH]          = MC_SNDGRP_ZOMBIE_DEATH,
    [MC_SFX_SKELETON_SAY]          = MC_SNDGRP_SKELETON_SAY,
    [MC_SFX_SKELETON_HURT]         = MC_SNDGRP_SKELETON_HURT,
    [MC_SFX_SKELETON_DEATH]        = MC_SNDGRP_SKELETON_DEATH,

    [MC_SFX_ITEM_POP]              = MC_SNDGRP_ITEM_POP,
    [MC_SFX_GLASS_BREAK]           = MC_SNDGRP_GLASS_BREAK,

    [MC_SFX_UI_CLICK_MONO]         = MC_SNDGRP_CLICK,
    [MC_SFX_CLASSIC_HURT]          = MC_SNDGRP_CLASSIC_HURT,
    [MC_SFX_SPLASH]                = MC_SNDGRP_SPLASH,
};

static McAudioQueue g_queue;
static _Atomic uint32_t g_rng_counter = 0x9E3779B9u;
static _Atomic int g_elytra_active = 0;
static _Atomic float g_elytra_speed = 0.0f;

/* Audio-thread-only state (accessed exclusively in mc_audio_mix and mc_audio_init). */
static McAudioVoice g_voices[MC_AUDIO_MAX_VOICES];
static uint8_t g_last_variant[MC_SFX_COUNT];
static uint64_t g_voice_seq;
static double g_elytra_pos;
static float g_elytra_gain;
static float g_elytra_pitch = 1.0f;
static int g_elytra_looped;

static inline float clampf(float v, float lo, float hi) {
    return (v < lo) ? lo : ((v > hi) ? hi : v);
}

static uint32_t next_seed(void) {
    uint32_t x = atomic_fetch_add_explicit(&g_rng_counter, 0x9E3779B9u, memory_order_relaxed);
    x ^= x >> 16;
    x *= 0x85EBCA6Bu;
    x ^= x >> 13;
    x *= 0xC2B2AE35u;
    x ^= x >> 16;
    return x;
}

static uint8_t event_priority(McSfxEvent ev) {
    switch (ev) {
        case MC_SFX_TOTEM_USE:
        case MC_SFX_LEVELUP:
        case MC_SFX_EXPLODE:
            return 3u;
        case MC_SFX_HURT:
        case MC_SFX_CLASSIC_HURT:
        case MC_SFX_SHIELD_BLOCK:
        case MC_SFX_SHIELD_BREAK:
        case MC_SFX_BOW_SHOOT:
        case MC_SFX_BOW_HIT:
        case MC_SFX_HIT_ARROW_DING:
        case MC_SFX_ATTACK_STRONG:
        case MC_SFX_ATTACK_SWEEP:
        case MC_SFX_ATTACK_CRIT:
        case MC_SFX_ATTACK_WEAK:
        case MC_SFX_ATTACK_KNOCKBACK:
        case MC_SFX_CREEPER_SAY:
        case MC_SFX_CREEPER_DEATH:
        case MC_SFX_ZOMBIE_SAY:
        case MC_SFX_ZOMBIE_HURT:
        case MC_SFX_ZOMBIE_DEATH:
        case MC_SFX_SKELETON_SAY:
        case MC_SFX_SKELETON_HURT:
        case MC_SFX_SKELETON_DEATH:
        case MC_SFX_FIREWORK_LAUNCH:
        case MC_SFX_FIREWORK_BLAST:
        case MC_SFX_FIREWORK_LARGE_BLAST:
        case MC_SFX_FIREWORK_TWINKLE:
        case MC_SFX_UI_CLICK:
        case MC_SFX_UI_CLICK_MONO:
        case MC_SFX_XP_ORB:
        case MC_SFX_FALL_BIG:
            return 2u;
        default:
            return 1u;
    }
}

static float event_hearing_radius(McSfxEvent ev) {
    switch (ev) {
        case MC_SFX_EXPLODE:
        case MC_SFX_FIREWORK_LAUNCH:
        case MC_SFX_FIREWORK_BLAST:
        case MC_SFX_FIREWORK_LARGE_BLAST:
        case MC_SFX_FIREWORK_TWINKLE:
            return 48.0f;
        default:
            return 24.0f;
    }
}

void mc_audio_init(void) {
    memset(g_voices, 0, sizeof g_voices);
    memset(g_last_variant, 0xff, sizeof g_last_variant);
    g_voice_seq = 0;
    g_elytra_pos = 0.0;
    g_elytra_gain = 0.0f;
    g_elytra_pitch = 1.0f;
    g_elytra_looped = 0;

    atomic_store_explicit(&g_elytra_active, 0, memory_order_release);
    atomic_store_explicit(&g_elytra_speed, 0.0f, memory_order_release);

    g_queue.rd = 0;
    atomic_store_explicit(&g_queue.wr, 0, memory_order_release);
    for (uint32_t i = 0; i < MC_AUDIO_QUEUE_CAP; i++) {
        atomic_store_explicit(&g_queue.slots[i].state, 0u, memory_order_relaxed);
    }
}

void mc_audio_push(McSfxEvent ev, float gain, float pitch, float pan) {
    if ((unsigned)ev == 0u || (unsigned)ev >= (unsigned)MC_SFX_COUNT) return;
    if (!isfinite(gain) || gain <= 0.0f) return;

    float safe_gain = clampf(gain, 0.0f, 4.0f);
    float safe_pitch = (!isfinite(pitch) || pitch <= 0.0f) ? 1.0f : clampf(pitch, 0.25f, 4.0f);
    float safe_pan = !isfinite(pan) ? 0.0f : clampf(pan, -1.0f, 1.0f);
    uint32_t seed = next_seed();

    uint64_t pos = atomic_load_explicit(&g_queue.wr, memory_order_relaxed);
    for (;;) {
        McAudioQueueSlot *slot = &g_queue.slots[pos & (MC_AUDIO_QUEUE_CAP - 1u)];
        uint64_t st = atomic_load_explicit(&slot->state, memory_order_acquire);
        uint64_t empty_st = (pos / MC_AUDIO_QUEUE_CAP) * 2u;
        if (st == empty_st) {
            if (atomic_compare_exchange_weak_explicit(
                    &g_queue.wr, &pos, pos + 1u,
                    memory_order_relaxed, memory_order_relaxed)) {
                slot->seed = seed;
                slot->event = (uint16_t)ev;
                slot->gain = safe_gain;
                slot->pitch = safe_pitch;
                slot->pan = safe_pan;
                atomic_store_explicit(&slot->state, empty_st + 1u, memory_order_release);
                return;
            }
        } else if (st < empty_st) {
            /* Ring queue is full; drop without blocking or allocating. */
            return;
        } else {
            pos = atomic_load_explicit(&g_queue.wr, memory_order_relaxed);
        }
    }
}

void mc_audio_push_3d(McSfxEvent ev,
                      float gain,
                      float pitch,
                      const float listener_pos[3],
                      const float listener_right[3],
                      const float src_pos[3]) {
    if (!listener_pos || !src_pos) {
        mc_audio_push(ev, gain, pitch, 0.0f);
        return;
    }
    if (!isfinite(listener_pos[0]) || !isfinite(listener_pos[1]) || !isfinite(listener_pos[2]) ||
        !isfinite(src_pos[0]) || !isfinite(src_pos[1]) || !isfinite(src_pos[2])) {
        return;
    }
    if (!isfinite(gain) || gain <= 0.0f) return;

    float dx = src_pos[0] - listener_pos[0];
    float dy = src_pos[1] - listener_pos[1];
    float dz = src_pos[2] - listener_pos[2];
    float dist = sqrtf(dx * dx + dy * dy + dz * dz);
    float max_dist = event_hearing_radius(ev);
    if (dist >= max_dist) return;

    float atten = 1.0f;
    if (dist > 1.5f) {
        float d_adj = dist - 1.5f;
        float norm = d_adj / (max_dist - 1.5f);
        atten = (1.0f / (1.0f + 0.12f * d_adj)) * (1.0f - norm * norm);
    }
    float final_gain = gain * atten;
    if (final_gain < 0.002f) return;

    float pan = 0.0f;
    if (listener_right && dist > 1e-4f &&
        isfinite(listener_right[0]) && isfinite(listener_right[1]) && isfinite(listener_right[2])) {
        float rx = listener_right[0];
        float ry = listener_right[1];
        float rz = listener_right[2];
        float r_len = sqrtf(rx * rx + ry * ry + rz * rz);
        if (r_len > 1e-5f) {
            float dot = (dx * rx + dy * ry + dz * rz) / (dist * r_len);
            dot = clampf(dot, -1.0f, 1.0f);
            float near_blend = dist < 1.5f ? (dist / 1.5f) : 1.0f;
            pan = dot * near_blend;
        }
    }

    mc_audio_push(ev, final_gain, pitch, pan);
}

void mc_audio_set_elytra_glide(int active, float speed) {
    float safe_speed = (!isfinite(speed) || speed < 0.0f) ? 0.0f : clampf(speed, 0.0f, 200.0f);
    atomic_store_explicit(&g_elytra_speed, safe_speed, memory_order_relaxed);
    atomic_store_explicit(&g_elytra_active, active ? 1 : 0, memory_order_release);
}

static void drain_discard_queue(void) {
    uint64_t rd = g_queue.rd;
    for (;;) {
        McAudioQueueSlot *slot = &g_queue.slots[rd & (MC_AUDIO_QUEUE_CAP - 1u)];
        uint64_t st = atomic_load_explicit(&slot->state, memory_order_acquire);
        uint64_t ready_st = (rd / MC_AUDIO_QUEUE_CAP) * 2u + 1u;
        if (st != ready_st) break;
        atomic_store_explicit(&slot->state, ready_st + 1u, memory_order_release);
        rd++;
    }
    g_queue.rd = rd;
}

static void spawn_voice(McSfxEvent ev, float gain, float pitch, float pan, uint32_t seed) {
    if ((unsigned)ev == 0u || (unsigned)ev >= (unsigned)MC_SFX_COUNT) return;
    McSoundGroupId grp = k_event_to_group[ev];
    if ((unsigned)grp >= (unsigned)MC_SNDGRP_COUNT) return;

    int count = mc_asset_sound_group_count(grp);
    if (count <= 0 && ev == MC_SFX_UI_CLICK) {
        grp = MC_SNDGRP_CLICK;
        count = mc_asset_sound_group_count(grp);
    }
    if (count <= 0) return;

    int variant = (int)(seed % (uint32_t)count);
    if (count > 1 && variant == (int)g_last_variant[ev]) {
        variant = (variant + 1) % count;
    }

    const McSound *snd = mc_asset_sound_group_get(grp, variant);
    if (!snd || !snd->loaded || !snd->pcm || snd->frames == 0 || snd->sample_rate == 0) {
        snd = NULL;
        for (int attempt = 0; attempt < count; attempt++) {
            const McSound *cand = mc_asset_sound_group_get(grp, attempt);
            if (cand && cand->loaded && cand->pcm && cand->frames > 0 && cand->sample_rate > 0) {
                variant = attempt;
                snd = cand;
                break;
            }
        }
        if (!snd) return;
    }
    g_last_variant[ev] = (uint8_t)variant;

    int slot_idx = -1;
    for (int i = 0; i < MC_AUDIO_MAX_VOICES; i++) {
        if (!g_voices[i].active) {
            slot_idx = i;
            break;
        }
    }

    uint8_t prio = event_priority(ev);
    if (slot_idx < 0) {
        /* Steal the lowest-priority / furthest-along / oldest active voice. */
        int best_idx = 0;
        double best_score = 1e30;
        for (int i = 0; i < MC_AUDIO_MAX_VOICES; i++) {
            const McAudioVoice *cand = &g_voices[i];
            const McSound *csnd = mc_asset_sound_group_get(cand->group, cand->variant);
            double frac_rem = 0.0;
            if (csnd && csnd->frames > 0 && cand->pos < (double)csnd->frames) {
                frac_rem = ((double)csnd->frames - cand->pos) / (double)csnd->frames;
            }
            double score = (double)cand->priority * 100.0 + frac_rem * 10.0 +
                           (double)cand->gain * 2.0;
            if (score < best_score ||
                (score == best_score && cand->start_seq < g_voices[best_idx].start_seq)) {
                best_score = score;
                best_idx = i;
            }
        }
        if (prio < g_voices[best_idx].priority && g_voices[best_idx].priority >= 3u) {
            return;
        }
        slot_idx = best_idx;
    }

    float gain_l, gain_r;
    if (snd->channels >= 2u) {
        if (pan <= -1.0f) {
            gain_l = gain;
            gain_r = 0.0f;
        } else if (pan >= 1.0f) {
            gain_l = 0.0f;
            gain_r = gain;
        } else {
            gain_l = (pan <= 0.0f) ? gain : gain * (float)cos((double)pan * (0.5 * 3.14159265358979323846));
            gain_r = (pan >= 0.0f) ? gain : gain * (float)cos((double)(-pan) * (0.5 * 3.14159265358979323846));
        }
    } else {
        /* Constant-power stereo pan law: cos^2 + sin^2 == 1.0 across [-1, +1]. */
        if (pan <= -1.0f) {
            gain_l = gain;
            gain_r = 0.0f;
        } else if (pan >= 1.0f) {
            gain_l = 0.0f;
            gain_r = gain;
        } else {
            double theta = ((double)pan + 1.0) * (0.25 * 3.14159265358979323846);
            gain_l = gain * (float)cos(theta);
            gain_r = gain * (float)sin(theta);
        }
    }

    g_voices[slot_idx] = (McAudioVoice){
        .active = 1u,
        .priority = prio,
        .event = (uint16_t)ev,
        .group = grp,
        .variant = variant,
        .start_seq = ++g_voice_seq,
        .pos = 0.0,
        .pitch = pitch,
        .gain = gain,
        .gain_l = gain_l,
        .gain_r = gain_r,
    };
}

static inline void read_stereo_frame(const McSound *snd, uint32_t idx, float *out_l, float *out_r) {
    const float inv = 1.0f / 32768.0f;
    if (snd->channels == 1u) {
        float s = (float)snd->pcm[idx] * inv;
        *out_l = s;
        *out_r = s;
    } else {
        size_t off = (size_t)idx * (size_t)snd->channels;
        *out_l = (float)snd->pcm[off] * inv;
        *out_r = (float)snd->pcm[off + 1u] * inv;
    }
}

static void mix_elytra_chunk(float *mix_buf, int chunk_frames, int rate, int *any_active) {
    int active = atomic_load_explicit(&g_elytra_active, memory_order_acquire);
    float speed = atomic_load_explicit(&g_elytra_speed, memory_order_relaxed);

    if (!active && g_elytra_gain <= 1e-5f) {
        g_elytra_gain = 0.0f;
        g_elytra_pos = 0.0;
        g_elytra_looped = 0;
        return;
    }

    const McSound *snd = mc_asset_sound(MC_SND_ELYTRA_LOOP);
    if (!snd || !snd->loaded || !snd->pcm || snd->frames == 0 || snd->sample_rate == 0) {
        g_elytra_gain = 0.0f;
        g_elytra_pos = 0.0;
        g_elytra_looped = 0;
        return;
    }

    float norm = speed / (speed + 12.0f);
    float target_gain = active ? (0.35f + 0.60f * norm) : 0.0f;
    float target_pitch = active ? (0.85f + 0.45f * norm) : g_elytra_pitch;
    if (active && g_elytra_gain == 0.0f) {
        g_elytra_pitch = target_pitch;
    }

    float attack_alpha = 1.0f - expf(-1.0f / (0.08f * (float)rate));
    float release_alpha = 1.0f - expf(-1.0f / (0.18f * (float)rate));
    float pitch_alpha = 1.0f - expf(-1.0f / (0.10f * (float)rate));
    double base_step = (double)snd->sample_rate / (double)rate;

    uint32_t xfade = (snd->frames > MC_AUDIO_ELYTRA_XFADE * 2u) ? MC_AUDIO_ELYTRA_XFADE : 0u;
    uint32_t loop_len = snd->frames - xfade;

    *any_active = 1;
    for (int f = 0; f < chunk_frames; f++) {
        float g_alpha = (target_gain > g_elytra_gain) ? attack_alpha : release_alpha;
        g_elytra_gain += (target_gain - g_elytra_gain) * g_alpha;
        g_elytra_pitch += (target_pitch - g_elytra_pitch) * pitch_alpha;

        if (!active && g_elytra_gain <= 1e-5f) {
            g_elytra_gain = 0.0f;
            g_elytra_pos = 0.0;
            g_elytra_looped = 0;
            break;
        }

        while (g_elytra_pos >= (double)loop_len) {
            g_elytra_pos -= (double)loop_len;
            if (xfade > 0u) g_elytra_looped = 1;
        }

        uint32_t i0 = (uint32_t)g_elytra_pos;
        uint32_t i1 = (i0 + 1u < snd->frames) ? (i0 + 1u) : 0u;
        float frac = (float)(g_elytra_pos - (double)i0);

        float l0, r0, l1, r1;
        read_stereo_frame(snd, i0, &l0, &r0);
        read_stereo_frame(snd, i1, &l1, &r1);
        float sl = l0 + (l1 - l0) * frac;
        float sr = r0 + (r1 - r0) * frac;

        if (xfade > 0u && g_elytra_looped && i0 < xfade) {
            uint32_t t0 = loop_len + i0;
            uint32_t t1 = (t0 + 1u < snd->frames) ? (t0 + 1u) : t0;
            float tl0, tr0, tl1, tr1;
            read_stereo_frame(snd, t0, &tl0, &tr0);
            read_stereo_frame(snd, t1, &tl1, &tr1);
            float tsl = tl0 + (tl1 - tl0) * frac;
            float tsr = tr0 + (tr1 - tr0) * frac;
            float w = (float)(g_elytra_pos / (double)xfade);
            sl = tsl * (1.0f - w) + sl * w;
            sr = tsr * (1.0f - w) + sr * w;
        }

        mix_buf[2 * f] += sl * g_elytra_gain;
        mix_buf[2 * f + 1] += sr * g_elytra_gain;

        g_elytra_pos += base_step * (double)g_elytra_pitch;
        while (g_elytra_pos >= (double)loop_len) {
            g_elytra_pos -= (double)loop_len;
            if (xfade > 0u) g_elytra_looped = 1;
        }
    }
}

static inline float soft_limit_sample(float x) {
    const float knee = 0.90f;
    const float span = 0.10f;
    float ax = fabsf(x);
    if (ax <= knee) return x;
    float shaped = knee + span * tanhf((ax - knee) / span);
    return (x >= 0.0f) ? shaped : -shaped;
}

void mc_audio_mix(float *stereo_io, int frames, int rate) {
    if (!stereo_io || frames <= 0 || rate <= 0) return;
    int safe_rate = (rate < 8000) ? 8000 : ((rate > 384000) ? 384000 : rate);

    if (!mc_assets_ready()) {
        memset(g_voices, 0, sizeof g_voices);
        g_elytra_gain = 0.0f;
        g_elytra_pos = 0.0;
        g_elytra_looped = 0;
        drain_discard_queue();
        return;
    }

    /* Drain lock-free MPSC queue into polyphonic voices. */
    uint64_t rd = g_queue.rd;
    for (;;) {
        McAudioQueueSlot *slot = &g_queue.slots[rd & (MC_AUDIO_QUEUE_CAP - 1u)];
        uint64_t st = atomic_load_explicit(&slot->state, memory_order_acquire);
        uint64_t ready_st = (rd / MC_AUDIO_QUEUE_CAP) * 2u + 1u;
        if (st != ready_st) break;

        McSfxEvent ev = (McSfxEvent)slot->event;
        float gain = slot->gain;
        float pitch = slot->pitch;
        float pan = slot->pan;
        uint32_t seed = slot->seed;

        atomic_store_explicit(&slot->state, ready_st + 1u, memory_order_release);
        rd++;

        spawn_voice(ev, gain, pitch, pan, seed);
    }
    g_queue.rd = rd;

    int offset = 0;
    while (offset < frames) {
        int chunk = frames - offset;
        if (chunk > MC_AUDIO_MIX_CHUNK) chunk = MC_AUDIO_MIX_CHUNK;

        float mix_buf[MC_AUDIO_MIX_CHUNK * 2];
        memset(mix_buf, 0, (size_t)chunk * 2u * sizeof(float));
        int any_active = 0;

        for (int vi = 0; vi < MC_AUDIO_MAX_VOICES; vi++) {
            McAudioVoice *v = &g_voices[vi];
            if (!v->active) continue;

            const McSound *snd = mc_asset_sound_group_get(v->group, v->variant);
            if (!snd || !snd->loaded || !snd->pcm || snd->frames == 0 || snd->sample_rate == 0) {
                v->active = 0u;
                continue;
            }

            any_active = 1;
            double step = ((double)snd->sample_rate * (double)v->pitch) / (double)safe_rate;
            const int16_t *pcm = snd->pcm;
            const uint32_t total_frames = snd->frames;
            const float inv = 1.0f / 32768.0f;

            if (snd->channels == 1u) {
                for (int f = 0; f < chunk; f++) {
                    uint32_t i0 = (uint32_t)v->pos;
                    if (i0 >= total_frames) {
                        v->active = 0u;
                        break;
                    }
                    float frac = (float)(v->pos - (double)i0);
                    float s0 = (float)pcm[i0] * inv;
                    float s1 = (i0 + 1u < total_frames) ? ((float)pcm[i0 + 1u] * inv) : 0.0f;
                    float s = s0 + (s1 - s0) * frac;

                    mix_buf[2 * f] += s * v->gain_l;
                    mix_buf[2 * f + 1] += s * v->gain_r;
                    v->pos += step;
                }
            } else {
                const uint16_t ch = snd->channels;
                for (int f = 0; f < chunk; f++) {
                    uint32_t i0 = (uint32_t)v->pos;
                    if (i0 >= total_frames) {
                        v->active = 0u;
                        break;
                    }
                    float frac = (float)(v->pos - (double)i0);
                    size_t off0 = (size_t)i0 * (size_t)ch;
                    float l0 = (float)pcm[off0] * inv;
                    float r0 = (float)pcm[off0 + 1u] * inv;
                    float l1 = 0.0f, r1 = 0.0f;
                    if (i0 + 1u < total_frames) {
                        size_t off1 = (size_t)(i0 + 1u) * (size_t)ch;
                        l1 = (float)pcm[off1] * inv;
                        r1 = (float)pcm[off1 + 1u] * inv;
                    }
                    float sl = l0 + (l1 - l0) * frac;
                    float sr = r0 + (r1 - r0) * frac;

                    mix_buf[2 * f] += sl * v->gain_l;
                    mix_buf[2 * f + 1] += sr * v->gain_r;
                    v->pos += step;
                }
            }

            if (v->pos >= (double)total_frames) {
                v->active = 0u;
            }
        }

        mix_elytra_chunk(mix_buf, chunk, safe_rate, &any_active);

        if (any_active) {
            const float bus_headroom = 0.85f;
            float *dst = stereo_io + (size_t)offset * 2u;
            for (int f = 0; f < chunk; f++) {
                float ml = bus_headroom * tanhf(mix_buf[2 * f] / bus_headroom);
                float mr = bus_headroom * tanhf(mix_buf[2 * f + 1] / bus_headroom);
                dst[2 * f] = soft_limit_sample(dst[2 * f] + ml);
                dst[2 * f + 1] = soft_limit_sample(dst[2 * f + 1] + mr);
            }
        }

        offset += chunk;
    }
}
