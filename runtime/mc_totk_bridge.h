#pragma once

#include "host.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/*
 * Safe runtime bridge between the Minecraft overlay engine and TOTK v1.2.1
 * guest memory / functions.
 *
 * THREAD-SAFETY CONTRACT:
 *   All guest state readers, mutators, phive::RayCast queries, ActorMgr scans,
 *   and visibility toggles MUST be called ONLY on the guest frame thread from
 *   inside runtime_mc_game_tick(). NEVER call them from the audio thread or
 *   Vulkan replay/present threads.
 *
 *   Only mc_totk_set_camera_look(), mc_totk_clear_camera_look(), and
 *   mc_totk_sample_camera_stick() are cross-thread safe (atomic/mutex).
 */

typedef struct {
    int   valid;              /* 1 when SCENE_SLOT and live Player actor are active */
    void *actor;              /* Live Player Actor* (guest address) */
    float pos[3];             /* World XYZ position (Link's feet) */
    float rot[9];             /* Row-major 3x3 world basis */
    float fwd[3];             /* Forward facing unit vector (col 2 of rot) */
    float yaw;                /* Radians; yaw=0 faces +Z, matches mc_snapshot.h */
    float vel[3];             /* World linear velocity (m/s-ish guest units) */
    float vert_vel;           /* CctGetVertVel vertical velocity */
    int   grounded;           /* Non-zero when character controller is on ground */
    int   falling;            /* Non-zero when falling */
    int   gliding_paraglider; /* Non-zero when Parasail is open */
    int   blocking;           /* Non-zero when guarding with shield */
    int   attacking;          /* Non-zero during Link attack state */
    int   hp_quarters;        /* Current HP in quarter-hearts (4 = 1 Zelda heart) */
    int   max_hp_quarters;    /* Max HP in quarter-hearts */
    int   extra_hp_quarters;  /* Yellow bonus HP in quarter-hearts */
    int   gloom_quarters;     /* Gloom-locked HP in quarter-hearts */
    float stamina;            /* Current stamina (1000.0f = 1 wheel) */
    float max_stamina;        /* Stamina capacity estimate */
    int   stamina_exhausted;  /* Non-zero when stamina wheel is exhausted */
} McTotkPlayerState;

typedef enum {
    MC_TOTK_ACTOR_OTHER = 0,
    MC_TOTK_ACTOR_ENEMY = 1,
    MC_TOTK_ACTOR_NPC   = 2,
    MC_TOTK_ACTOR_ITEM  = 3,
    MC_TOTK_ACTOR_BOMB_TARGET = 4
} McTotkActorKind;

typedef struct {
    void           *actor;       /* Guest Actor* (NULL-checked & live-verified) */
    char            name[64];    /* Actor name (e.g. Enemy_Bokoblin_Junior) */
    float           pos[3];      /* World XYZ */
    float           aabb_min[3]; /* World AABB min (synthesized from pos/radius if unset) */
    float           aabb_max[3]; /* World AABB max */
    float           radius;      /* Approximate horizontal collision radius */
    int             hp;          /* Current HP (-1 if actor has no LifeComp) */
    int             max_hp;      /* Max HP (-1 if actor has no LifeComp) */
    McTotkActorKind kind;
} McTotkActorInfo;

typedef struct {
    int   took_damage;          /* 1 if damage is waiting to be applied */
    int   raw_damage_quarters;  /* Incoming quarter-hearts this tick */
    int   hp_before;            /* Shared health before this damage */
    void *attacker_actor;       /* Nearest plausible enemy Actor* if found, else NULL */
    char  attacker_name[64];    /* Attacker actor name (empty if unknown) */
    float attacker_pos[3];      /* Attacker XYZ (for directional shield blocking) */
    int   has_attacker_pos;     /* 1 when attacker_pos is valid */
} McTotkIncomingDamage;

/* Lifecycle (safe no-op on non-v1.2.1 builds) */
void mc_totk_bridge_init(const RtLoaded *main_module);
void mc_totk_bridge_cleanup(void);

/* Returns 1 ONLY during active, interactive Hyrule gameplay; 0 on Title, Save/Loading,
 * Scene Transition/Fade, Cutscene/Event, Game Over, or TOTK Fullscreen Menus. */
int mc_totk_options_open(void);
int mc_totk_is_gameplay_active(void);

/* Player state, pose, velocity, HP (quarter-hearts), stamina */
int mc_totk_read_player(McTotkPlayerState *out);
int mc_totk_set_player_pos(const float pos[3], const float *opt_yaw_rad);
int mc_totk_set_player_yaw(float yaw_rad);
int mc_totk_set_player_vel(const float vel[3]);
int mc_totk_set_player_hp(int hp_quarters);
int mc_totk_add_player_hp(int delta_quarters);
int mc_totk_add_player_stamina(float delta);
void mc_totk_refill_player_stamina(void *player);

/* Link 3D body + Zelda HUD visibility (tracks & restores saved state on visible=1 / cleanup) */
void mc_totk_set_player_visible(int visible);
void mc_totk_set_hud_visible(int visible);

/* Mouse-look -> TOTK camera right-stick bridge (cross-thread safe) */
void mc_totk_set_camera_look(float dx, float dy);
void mc_totk_clear_camera_look(void);
void mc_totk_sample_camera_stick(int32_t *out_rx, int32_t *out_ry);

/* Hyrule terrain/physics raycast, ground probe & wall-sweep helpers (guest frame thread only) */
int mc_totk_raycast_from_eye(const float origin[3], const float dir[3], float max_dist,
                             float out_hit[3], float out_normal[3], float *out_dist);
int mc_totk_raycast(const float origin[3], const float dir[3], float max_dist,
                    float out_hit[3], float out_normal[3], float *out_dist);
float mc_totk_camera_clearance(const float origin[3], const float dir[3], float distance);
int mc_totk_ground_height(float x, float y_hint, float z, float *out_y);
int mc_totk_ground_height_with_normal(float x, float y_hint, float z,
                                      float *out_y, float *out_norm_y);
/* Sweep horizontal step delta_xz for an entity cylinder (pos, radius, height) against
 * Hyrule walls/cliffs/obstacles, sliding along wall normals when blocked.
 * Returns bitmask: 0 = free movement, bit 0 (1) = hit/slid against wall (out_wall_norm_xz),
 * bit 1 (2) = walkable step-up ledge (<= 1.15m) detected (*out_step_y set). */
int mc_totk_check_wall_step(const float pos[3], float radius, float height,
                            const float delta_xz[2], float out_pos_xz[2],
                            float out_wall_norm_xz[2], float *out_step_y);

/* Active actor / enemy enumeration & damage dealing (guest frame thread only) */
int mc_totk_enumerate_actors(const float center[3], float max_radius,
                             McTotkActorInfo *out, int max_out);
/* Resolve the live actor attached to a Hyrule physics hit. */
void *mc_totk_trace_actor(const float origin[3], const float direction[3], float distance,
                          float hit[3]);
int mc_totk_damage_actor(void *actor, int damage, const float impulse_dir[3]);
int mc_totk_blast_object(void *actor, const float center[3], const float impulse_dir[3]);

/* Incoming damage delta monitor on Link (ignores bridge's own HP edits) */
int mc_totk_poll_incoming_damage(int minecraft_active, McTotkIncomingDamage *out);

#ifdef __cplusplus
}
#endif
