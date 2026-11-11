#ifndef GLI_FB_LIST_H
#define GLI_FB_LIST_H

#include "Types.h"
#include "gli_fb_rdram.h"

#ifdef __cplusplus
extern "C" {
#endif

/* The most frame buffers alive at once; the oldest makes way past it */
#define GLI_FB_MAX 128

/* The frame buffers in a fixed set of slots, newest first, as the old
 * std::list kept them: nothing is allocated when a buffer comes or goes */
typedef struct gli_fb_list
{
	gli_fb *slot[GLI_FB_MAX]; /* the buffer living in each slot, or NULL */
	u8 order[GLI_FB_MAX];     /* the slots in use, newest first */
	u32 count;
} gli_fb_list;

void gli_fb_list_init(gli_fb_list *_l);
/* A free slot, or GLI_FB_MAX when every slot is in use */
u32 gli_fb_list_free_slot(const gli_fb_list *_l);
/* Puts the buffer @_fb living in @_slot first */
void gli_fb_list_push_front(gli_fb_list *_l, u32 _slot, gli_fb *_fb);
/* Takes the buffer at position @_pos out; returns its slot */
u32 gli_fb_list_remove_at(gli_fb_list *_l, u32 _pos);
/* The slot at position @_pos, newest first */
#define gli_fb_list_at(_l, _pos) ((u32)(_l)->order[_pos])

/* The first buffer holding @_address, its start @_address, or not
 * holding @_address; GLI_FB_MAX for none */
u32 gli_fb_list_find(const gli_fb_list *_l, u32 _address);
u32 gli_fb_list_get(const gli_fb_list *_l, u32 _address);
u32 gli_fb_list_find_other(const gli_fb_list *_l, u32 _address);

/* Cuts or takes out the buffers overlapping @_current, last to first;
 * the slots taken out go to @_removed, their count returned */
u32 gli_fb_list_remove_intersections(gli_fb_list *_l, gli_fb *_current, u8 *_removed);
/* Takes out the auxiliary buffers, or those @_width wide */
u32 gli_fb_list_remove_aux(gli_fb_list *_l, u8 *_removed);
u32 gli_fb_list_remove_width(gli_fb_list *_l, u32 _width, u8 *_removed);

#ifdef __cplusplus
}
#endif

#endif
