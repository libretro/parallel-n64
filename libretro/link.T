/* Only the libretro entry points belong in the dynamic symbol table.
 *
 * The glob this replaces was "retro_*", which also matched everything
 * libretro-common names that way - the whole retro_vfs_*_impl family
 * above all.  Exporting those has two costs.  Every internal call to one
 * goes through the PLT rather than direct, which was 75 call sites, most
 * of them the VFS hot path.  Worse, a global definition can be
 * interposed: the frontend links the same libretro-common sources, and
 * where it exports them too the dynamic linker binds our calls to its
 * copies, so this core's VFS - the mapped ECM reads, the atomic save
 * rename, the cached SMB dirent walk - would silently not be the code
 * that runs.
 *
 * Listing the twenty-five entry points from libretro.h leaves no room
 * for either.  A new entry point has to be added here as well, which is
 * the point: the export list should be a decision, not a wildcard.
 */
{
   global:
      retro_api_version;
      retro_cheat_reset;
      retro_cheat_set;
      retro_deinit;
      retro_get_memory_data;
      retro_get_memory_size;
      retro_get_region;
      retro_get_system_av_info;
      retro_get_system_info;
      retro_init;
      retro_load_game;
      retro_load_game_special;
      retro_reset;
      retro_run;
      retro_serialize;
      retro_serialize_size;
      retro_set_audio_sample;
      retro_set_audio_sample_batch;
      retro_set_controller_port_device;
      retro_set_environment;
      retro_set_input_poll;
      retro_set_input_state;
      retro_set_video_refresh;
      retro_unload_game;
      retro_unserialize;
   local: *;
};
