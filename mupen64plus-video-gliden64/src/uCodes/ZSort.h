#ifndef ZSORT_H
#define ZSORT_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct zSortVDest zSortVDest;
struct zSortVDest{
	s16 sy;
	s16 sx;
	s32 invw;
	s16 yi;
	s16 xi;
	s16 wi;
	u8 fog;
	u8 cc;
};

typedef f32 M44[4][4];

void gli_ZSort_Init(void);
void gli_ZSort_RDPCMD( u32, u32 _w1 );
int gli_Calc_invw( int _w );

#ifdef __cplusplus
}
#endif

#endif /* ZSORT_H */
