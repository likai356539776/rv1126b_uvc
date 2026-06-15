#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Unit-test hook: when non-NULL, submit path uses this instead of uvc_video_id_get().
 * Set to NULL for production behavior. Not used by my_uvc_channel_video_id().
 */
void my_uvc_test_set_video_id_hook(int (*fn)(unsigned int channel_id));

/** Resolve channel_id → video_id for my_uvc_submit_* (honours test hook when set). */
int my_uvc_resolve_video_id_for_submit(unsigned int channel_id);

#ifdef __cplusplus
}
#endif
