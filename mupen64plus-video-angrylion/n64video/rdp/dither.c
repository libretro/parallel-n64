static const uint8_t bayer_matrix[16] =
{
     0,  4,  1, 5,
     4,  0,  5, 1,
     3,  7,  2, 6,
     7,  3,  6, 2
};


static const uint8_t magic_matrix[16] =
{
     0,  6,  1, 7,
     4,  2,  5, 3,
     3,  5,  2, 4,
     7,  1,  6, 0
};

static STRICTINLINE void rgb_dither(int rgb_dither_sel, int* r, int* g, int* b, int dith)
{
   int32_t ditherdiff[3];
   int32_t replacesign[3];
   int32_t newcol[3];
   int32_t comp[3];

   newcol[0]      = (*r > 247) ? 255 : ((*r & 0xf8) + 8);
   newcol[1]      = (*g > 247) ? 255 : ((*g & 0xf8) + 8);
   newcol[2]      = (*b > 247) ? 255 : ((*b & 0xf8) + 8);

   comp[0]        = dith;
   comp[1]        = (rgb_dither_sel != 2) ? dith : ((dith + 3) & 7);
   comp[2]        = (rgb_dither_sel != 2) ? dith : ((dith + 5) & 7);

   replacesign[0] = (comp[0] - (*r & 7)) >> 31;
   replacesign[1] = (comp[1] - (*g & 7)) >> 31;
   replacesign[2] = (comp[2] - (*b & 7)) >> 31;
   ditherdiff[0]  = newcol[0] - *r;
   ditherdiff[1]  = newcol[1] - *g;
   ditherdiff[2]  = newcol[2] - *b;

   *r            += (ditherdiff[0] & replacesign[0]);
   *g            += (ditherdiff[1] & replacesign[1]);
   *b            += (ditherdiff[2] & replacesign[2]);
}

static STRICTINLINE void get_dither_noise(uint32_t wid, int x, int y, int* cdith, int* adith)
{
    if (!state[wid].other_modes.f.getditherlevel)
        state[wid].noise = ((irand(&state[wid].rseed) & 7) << 6) | 0x20;

    y >>= state[wid].scfield;

    int dithindex;
    switch(state[wid].other_modes.f.rgb_alpha_dither)
    {
    case 0:
        dithindex = ((y & 3) << 2) | (x & 3);
        *adith = *cdith = magic_matrix[dithindex];
        break;
    case 1:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = magic_matrix[dithindex];
        *adith = (~(*cdith)) & 7;
        break;
    case 2:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = magic_matrix[dithindex];
        *adith = (state[wid].noise >> 6) & 7;
        break;
    case 3:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = magic_matrix[dithindex];
        *adith = 0;
        break;
    case 4:
        dithindex = ((y & 3) << 2) | (x & 3);
        *adith = *cdith = bayer_matrix[dithindex];
        break;
    case 5:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = bayer_matrix[dithindex];
        *adith = (~(*cdith)) & 7;
        break;
    case 6:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = bayer_matrix[dithindex];
        *adith = (state[wid].noise >> 6) & 7;
        break;
    case 7:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = bayer_matrix[dithindex];
        *adith = 0;
        break;
    case 8:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = irand(&state[wid].rseed);
        *adith = magic_matrix[dithindex];
        break;
    case 9:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = irand(&state[wid].rseed);
        *adith = (~magic_matrix[dithindex]) & 7;
        break;
    case 10:
        *cdith = irand(&state[wid].rseed);
        *adith = (state[wid].noise >> 6) & 7;
        break;
    case 11:
        *cdith = irand(&state[wid].rseed);
        *adith = 0;
        break;
    case 12:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = 7;
        *adith = bayer_matrix[dithindex];
        break;
    case 13:
        dithindex = ((y & 3) << 2) | (x & 3);
        *cdith = 7;
        *adith = (~bayer_matrix[dithindex]) & 7;
        break;
    case 14:
        *cdith = 7;
        *adith = (state[wid].noise >> 6) & 7;
        break;
    case 15:
        *cdith = 7;
        *adith = 0;
        break;
    }
}
