#ifndef F3DBETA_H
#define F3DBETA_H

#ifdef __cplusplus
extern "C" {
#endif

#define F3DBETA_PERSPNORM		0xB4
#define F3DBETA_RDPHALF_1		0xB3
#define F3DBETA_RDPHALF_2		0xB2
#define F3DBETA_TRI2			0xB1

void gli_F3DBETA_Init(void);

void gli_F3DBETA_Perpnorm(u32 w0, u32 w1);

#ifdef __cplusplus
}
#endif

#endif

