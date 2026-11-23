#pragma once

#include "mc_snapshot.h"
#include "runtime.h"
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_OVERLAY_W 640
#define MC_OVERLAY_H 360

void runtime_mc_save_base(void);
void runtime_mc_init(Module *main_module);
int runtime_mc_options_open(void);
void runtime_mc_apply_settings(float distance, float sensitivity, float fov, int random_mob_spawns,
                               float gui_scale, float mainhand_scale, float offhand_scale);
int runtime_mc_active(void);
int runtime_mc_wants_game_tick(void);
int runtime_mc_mode(void);
int runtime_mc_view(void);
int runtime_mc_should_skip_player_draw(int is_gbuf, int index_count);

void runtime_mc_note_camera(const float *cam84, int count);
void runtime_mc_game_tick(uint64_t frame);

int runtime_mc_key(unsigned code, int down);
int runtime_mc_command(const char *line);

int runtime_mc_wants_mouse_capture(void);
int runtime_mc_allows_sprint(void);
void runtime_mc_mouse_move(float dx, float dy);
void runtime_mc_cursor_pos(float nx, float ny);
void runtime_mc_mouse_button(int button, int down);
void runtime_mc_scroll(float delta_y);

void mc_snapshot_copy(McRenderSnapshot *out);

int runtime_mc_render_frame(uint8_t *rgba_out, int w, int h);
void runtime_mc_audio_mix(float *stereo_io, int frames, int rate);

#ifdef __cplusplus
}
#endif
