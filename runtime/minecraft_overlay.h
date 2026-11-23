#pragma once

#include "host.h"
#include "mc_snapshot.h"
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define MC_OVERLAY_W 640
#define MC_OVERLAY_H 360

void runtime_mc_save_base(void);
void runtime_mc_init(const RtLoaded *main_module);
int runtime_mc_options_open(void);
int runtime_mc_active(void);
int runtime_mc_wants_game_tick(void);
int runtime_mc_mode(void);
int runtime_mc_view(void);
int runtime_mc_should_skip_player_draw(int is_gbuf, int index_count);

void runtime_mc_note_camera(const float *cam84, int count);
void runtime_mc_game_tick(uint64_t frame);
void runtime_mc_on_set_scene_camera(Cpu *c);
void runtime_mc_on_scene_projection(Cpu *c);

int runtime_mc_key(unsigned code, int down);
void runtime_mc_release_input(void);
int runtime_mc_command(const char *line);

int runtime_mc_wants_mouse_capture(void);
int runtime_mc_wants_sprint(void);
int runtime_mc_uses_native_mouse_look(void);
int runtime_mc_ground_move_yaw_delta(float *out_delta_rad);
float runtime_mc_mouse_gain(void);
void runtime_mc_mouse_move(float dx, float dy);
void runtime_mc_cursor_pos(float nx, float ny);
void runtime_mc_mouse_button(int button, int down);
void runtime_mc_scroll(float delta_y);

void mc_snapshot_copy(McRenderSnapshot *out);

int runtime_mc_render_frame(uint8_t *rgba_out, int w, int h);
void runtime_mc_audio_mix(float *stereo_io, int frames, int rate);

/* Input hooks provided by input.c */
void runtime_input_mc_controls(bool *bow_pressed, bool *bow_down, bool *jump, bool *sprint,
                               float *move_right, float *move_forward);
void runtime_input_clear(void);

#ifdef __cplusplus
}
#endif
