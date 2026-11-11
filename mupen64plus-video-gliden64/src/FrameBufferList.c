#include <string.h>

#include "Types.h"
#include "gli_fb_list.h"

void gli_fb_list_init(gli_fb_list *_l)
{
	memset(_l, 0, sizeof(*_l));
}

u32 gli_fb_list_free_slot(const gli_fb_list *_l)
{
	u32 i;
	for (i = 0; i < GLI_FB_MAX; ++i)
		if (_l->slot[i] == NULL)
			return i;
	return GLI_FB_MAX;
}

void gli_fb_list_push_front(gli_fb_list *_l, u32 _slot, gli_fb *_fb)
{
	memmove(_l->order + 1, _l->order, _l->count);
	_l->order[0] = (u8)_slot;
	_l->slot[_slot] = _fb;
	++_l->count;
}

u32 gli_fb_list_remove_at(gli_fb_list *_l, u32 _pos)
{
	const u32 slot = _l->order[_pos];
	memmove(_l->order + _pos, _l->order + _pos + 1, _l->count - _pos - 1);
	--_l->count;
	_l->slot[slot] = NULL;
	return slot;
}

u32 gli_fb_list_find(const gli_fb_list *_l, u32 _address)
{
	u32 i;
	for (i = 0; i < _l->count; ++i) {
		const gli_fb *fb = _l->slot[_l->order[i]];
		if (fb->m_startAddress <= _address && fb->m_endAddress >= _address)
			return _l->order[i];
	}
	return GLI_FB_MAX;
}

u32 gli_fb_list_get(const gli_fb_list *_l, u32 _address)
{
	u32 i;
	for (i = 0; i < _l->count; ++i)
		if (_l->slot[_l->order[i]]->m_startAddress == _address)
			return _l->order[i];
	return GLI_FB_MAX;
}

u32 gli_fb_list_find_other(const gli_fb_list *_l, u32 _address)
{
	u32 i;
	for (i = 0; i < _l->count; ++i) {
		const gli_fb *fb = _l->slot[_l->order[i]];
		if (fb->m_startAddress > _address || fb->m_endAddress < _address)
			return _l->order[i];
	}
	return GLI_FB_MAX;
}

u32 gli_fb_list_remove_intersections(gli_fb_list *_l, gli_fb *_current, u8 *_removed)
{
	u32 n = 0;
	u32 pos = _l->count;
	while (pos-- > 0) {
		gli_fb *fb = _l->slot[_l->order[pos]];
		if (fb == _current)
			continue;
		if (fb->m_startAddress <= _current->m_startAddress && fb->m_endAddress >= _current->m_startAddress) {
			/* [  {  ] */
			if (gli_fb_overlapping(fb, _current)) {
				fb->m_endAddress = _current->m_startAddress - 1;
				continue;
			}
			_removed[n++] = (u8)gli_fb_list_remove_at(_l, pos);
		} else if (_current->m_startAddress <= fb->m_startAddress && _current->m_endAddress >= fb->m_startAddress) {
			/* {  [  } */
			if (gli_fb_overlapping(_current, fb)) {
				_current->m_endAddress = fb->m_startAddress - 1;
				continue;
			}
			_removed[n++] = (u8)gli_fb_list_remove_at(_l, pos);
		}
	}
	return n;
}

u32 gli_fb_list_remove_aux(gli_fb_list *_l, u8 *_removed)
{
	u32 n = 0, pos = 0;
	while (pos < _l->count) {
		if (gli_fb_is_auxiliary(_l->slot[_l->order[pos]]))
			_removed[n++] = (u8)gli_fb_list_remove_at(_l, pos);
		else
			++pos;
	}
	return n;
}

u32 gli_fb_list_remove_width(gli_fb_list *_l, u32 _width, u8 *_removed)
{
	u32 n = 0, pos = 0;
	while (pos < _l->count) {
		if (_l->slot[_l->order[pos]]->m_width == _width)
			_removed[n++] = (u8)gli_fb_list_remove_at(_l, pos);
		else
			++pos;
	}
	return n;
}
