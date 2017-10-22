static STRICTINLINE void divot_filter(struct ccvg* final, struct ccvg centercolor, struct ccvg leftcolor, struct ccvg rightcolor)
{
    uint32_t leftr, leftg, leftb, rightr, rightg, rightb, centerr, centerg, centerb;

    *final = centercolor;

    if ((centercolor.cvg & leftcolor.cvg & rightcolor.cvg) == 7)
    {
        return;
    }

    leftr = leftcolor.r;
    leftg = leftcolor.g;
    leftb = leftcolor.b;
    rightr = rightcolor.r;
    rightg = rightcolor.g;
    rightb = rightcolor.b;
    centerr = centercolor.r;
    centerg = centercolor.g;
    centerb = centercolor.b;


    if ((leftr >= centerr && rightr >= leftr) || (leftr >= rightr && centerr >= leftr))
        final->r = leftr;
    else if ((rightr >= centerr && leftr >= rightr) || (rightr >= leftr && centerr >= rightr))
        final->r = rightr;

    if ((leftg >= centerg && rightg >= leftg) || (leftg >= rightg && centerg >= leftg))
        final->g = leftg;
    else if ((rightg >= centerg && leftg >= rightg) || (rightg >= leftg && centerg >= rightg))
        final->g = rightg;

    if ((leftb >= centerb && rightb >= leftb) || (leftb >= rightb && centerb >= leftb))
        final->b = leftb;
    else if ((rightb >= centerb && leftb >= rightb) || (rightb >= leftb && centerb >= rightb))
        final->b = rightb;
}
