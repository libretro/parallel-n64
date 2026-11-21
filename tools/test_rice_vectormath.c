/* Rice's matrices and vectors (mupen64plus-video-rice/src/VectorMath.c):
 *
 *   * MatrixMultiply: row i of the product the sum over k of A's (i, k)
 *     times B's (k, j), A on the left (a vertex, a row, goes through A
 *     first);
 *   * MatrixTranspose: (i, j) of the result (j, i) of the matrix, the
 *     result given back;
 *   * Vec3Transform: (x, y, z, 1) times the matrix, w its fourth column. */
#include <stdio.h>
#include <string.h>

#include "VectorMath.h"

static int failures;
#define CHECK(c) do { if (!(c)) { printf("FAILED %s:%d: %s\n", __FILE__, __LINE__, #c); failures++; } } while (0)

static void fill(XMATRIX *m, int seed)
{
   int i, j;
   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
         m->m[i][j] = (float)((i * 4 + j + seed) % 7 - 3) + (float)(i + 1) * 0.5f + 0.125f;   /* none 0 */
}

static void multiply(void)
{
   XMATRIX a, b, r, ab;
   int i, j, k;

   fill(&a, 1);
   fill(&b, 5);
   memset(&r, 0xFF, sizeof(r));
   MatrixMultiply(&r, &a, &b);
   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
      {
         float s = 0.0f;
         for (k = 0; k < 4; k++)
            s += a.m[i][k] * b.m[k][j];
         CHECK(r.m[i][j] == s);
      }
   /* not the other way round */
   MatrixMultiply(&ab, &b, &a);
   CHECK(memcmp(&ab, &r, sizeof(r)) != 0);
   /* the named elements are the same storage, row by row */
   CHECK(r._11 == r.m[0][0] && r._14 == r.m[0][3] && r._21 == r.m[1][0]
         && r._34 == r.m[2][3] && r._41 == r.m[3][0] && r._44 == r.m[3][3]);
   /* an identity changes nothing */
   memset(&b, 0, sizeof(b));
   b._11 = b._22 = b._33 = b._44 = 1.0f;
   MatrixMultiply(&r, &a, &b);
   CHECK(memcmp(&r, &a, sizeof(r)) == 0);
   MatrixMultiply(&r, &b, &a);
   CHECK(memcmp(&r, &a, sizeof(r)) == 0);
}

static void transpose(void)
{
   XMATRIX a, t;
   int i, j;

   fill(&a, 3);
   CHECK(MatrixTranspose(&t, &a) == &t);
   for (i = 0; i < 4; i++)
      for (j = 0; j < 4; j++)
         CHECK(t.m[i][j] == a.m[j][i]);
}

static void transform(void)
{
   XMATRIX a;
   XVECTOR3 v;
   XVECTOR4 o;
   int j;

   fill(&a, 2);
   v.x = 2.0f; v.y = -3.0f; v.z = 0.25f;
   Vec3Transform(&o, &v, &a);
   for (j = 0; j < 4; j++)
   {
      float e = v.x * a.m[0][j] + v.y * a.m[1][j] + v.z * a.m[2][j] + a.m[3][j];
      float g = j == 0 ? o.x : j == 1 ? o.y : j == 2 ? o.z : o.w;
      CHECK(g == e);
   }
   /* the origin: the fourth row */
   v.x = v.y = v.z = 0.0f;
   Vec3Transform(&o, &v, &a);
   CHECK(o.x == a._41 && o.y == a._42 && o.z == a._43 && o.w == a._44);
}

int main(void)
{
   multiply();
   transpose();
   transform();
   printf("rice vector maths: %s\n", failures ? "FAILED" : "ok");
   return failures ? 1 : 0;
}
