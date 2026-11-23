#pragma once

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    MC_SFX_NONE = 0,

    /* Block digging / breaking */
    MC_SFX_DIG_WOOD,
    MC_SFX_DIG_STONE,
    MC_SFX_DIG_GRASS,
    MC_SFX_DIG_GRAVEL,
    MC_SFX_DIG_SAND,

    /* Footsteps */
    MC_SFX_STEP_WOOD,
    MC_SFX_STEP_STONE,
    MC_SFX_STEP_GRASS,
    MC_SFX_STEP_GRAVEL,
    MC_SFX_STEP_SAND,

    /* Bow & arrow */
    MC_SFX_BOW_SHOOT,
    MC_SFX_BOW_HIT,
    MC_SFX_HIT_ARROW_DING,

    /* Melee combat & shield */
    MC_SFX_ATTACK_STRONG,
    MC_SFX_ATTACK_SWEEP,
    MC_SFX_ATTACK_CRIT,
    MC_SFX_ATTACK_WEAK,
    MC_SFX_ATTACK_KNOCKBACK,
    MC_SFX_SHIELD_BLOCK,
    MC_SFX_SHIELD_BREAK,

    /* Consumables, XP & UI */
    MC_SFX_EAT,
    MC_SFX_BURP,
    MC_SFX_DRINK,
    MC_SFX_XP_ORB,
    MC_SFX_LEVELUP,
    MC_SFX_UI_CLICK,

    /* Explosives, fire, fireworks & totem */
    MC_SFX_TNT_FUSE,
    MC_SFX_EXPLODE,
    MC_SFX_FIRE_IGNITE,
    MC_SFX_FIREWORK_LAUNCH,
    MC_SFX_FIREWORK_BLAST,
    MC_SFX_FIREWORK_LARGE_BLAST,
    MC_SFX_FIREWORK_TWINKLE,
    MC_SFX_TOTEM_USE,

    /* Player damage & falls */
    MC_SFX_HURT,
    MC_SFX_FALL_SMALL,
    MC_SFX_FALL_BIG,

    /* Mobs */
    MC_SFX_CREEPER_SAY,
    MC_SFX_CREEPER_DEATH,
    MC_SFX_ZOMBIE_SAY,
    MC_SFX_ZOMBIE_HURT,
    MC_SFX_ZOMBIE_DEATH,
    MC_SFX_SKELETON_SAY,
    MC_SFX_SKELETON_HURT,
    MC_SFX_SKELETON_DEATH,

    /* Item pickup, glass & water */
    MC_SFX_ITEM_POP,
    MC_SFX_GLASS_BREAK,

    /* Additional authentic sound groups from mc_assets.h */
    MC_SFX_UI_CLICK_MONO,
    MC_SFX_CLASSIC_HURT,
    MC_SFX_SPLASH,

    MC_SFX_COUNT
} McSfxEvent;

/* Initialize mixer state. Call from main/overlay init (never from CoreAudio pull). */
void mc_audio_init(void);

/* Enqueue an authentic Minecraft SFX event through the lock-free MPSC ring queue.
 * - gain: linear volume multiplier (<= 0 is ignored; clamped to [0, 4]).
 * - pitch: playback speed multiplier (<= 0 defaults to 1.0; clamped to [0.25, 4.0]).
 * - pan: stereo pan in [-1.0 (left), 0.0 (center), +1.0 (right)]. */
void mc_audio_push(McSfxEvent ev, float gain, float pitch, float pan);

/* Enqueue a 3D-spatialized SFX event with distance attenuation and constant-power stereo pan.
 * listener_pos, listener_right (unit right vector), and src_pos are 3D world coordinates. */
void mc_audio_push_3d(McSfxEvent ev,
                      float gain,
                      float pitch,
                      const float listener_pos[3],
                      const float listener_right[3],
                      const float src_pos[3]);

/* Control the continuous real Elytra glide loop (sounds/item/elytra/elytra_loop.wav).
 * Lock-free and safe from any game/UI thread; smoothly fades in/out in mc_audio_mix().
 * - active: non-zero while gliding with Elytra, 0 otherwise.
 * - speed: flight speed in m/s (scales glide volume and pitch smoothly). */
void mc_audio_set_elytra_glide(int active, float speed);

/* Mix queued/active Minecraft SFX voices and Elytra loop into stereo_io (interleaved float32).
 * Safe on the CoreAudio real-time pull() thread: zero malloc, zero locks, zero file I/O.
 * Checks mc_assets_ready() before reading any decoded PCM buffers. */
void mc_audio_mix(float *stereo_io, int frames, int rate);

#ifdef __cplusplus
}
#endif
