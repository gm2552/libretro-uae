#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <math.h>

#include "libretro.h"

#include "graph.h"
#include "libretro-glue.h"


unsigned short blend(unsigned short fg, unsigned short bg, unsigned int alpha)
{
   // Split foreground into components
   unsigned fg_r = fg >> 11;
   unsigned fg_g = (fg >> 5) & ((1u << 6) - 1);
   unsigned fg_b = fg & ((1u << 5) - 1);

   // Split background into components
   unsigned bg_r = bg >> 11;
   unsigned bg_g = (bg >> 5) & ((1u << 6) - 1);
   unsigned bg_b = bg & ((1u << 5) - 1);

   // Alpha blend components
   unsigned out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
   unsigned out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
   unsigned out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

   // Pack result
   return (unsigned short) ((out_r << 11) | (out_g << 5) | out_b);
}

uint32_t blend32(uint32_t fg, uint32_t bg, unsigned int alpha)
{
   // Split foreground into components
   unsigned fg_r = (fg >> 16) & 0xFF;
   unsigned fg_g = (fg >> 8) & 0xFF;
   unsigned fg_b = (fg >> 0) & 0xFF;

   // Split background into components
   unsigned bg_r = (bg >> 16) & 0xFF;
   unsigned bg_g = (bg >> 8) & 0xFF;
   unsigned bg_b = (bg >> 0) & 0xFF;

   // Alpha blend components
   unsigned out_r = (fg_r * alpha + bg_r * (255 - alpha)) / 255;
   unsigned out_g = (fg_g * alpha + bg_g * (255 - alpha)) / 255;
   unsigned out_b = (fg_b * alpha + bg_b * (255 - alpha)) / 255;

   // Pack result
   return (uint32_t) ((out_r << 16) | (out_g << 8) | out_b);
}

void DrawFBoxBmp(unsigned short *buffer, int x, int y, int dx, int dy, unsigned short color, unsigned int alpha)
{
   int i,j,idx;

   for(i=x;i<x+dx;i++)
   {
      for(j=y;j<y+dy;j++)
      {
         idx=i+j*retrow;
         if (alpha < 255)
            buffer[idx]=blend(color, buffer[idx], alpha);
         else
            buffer[idx]=color;
      }
   }
}

void DrawFBoxBmp32(uint32_t *buffer, int x, int y, int dx, int dy, uint32_t color, unsigned int alpha)
{
   int i,j,idx;

   for(i=x;i<x+dx;i++)
   {
      for(j=y;j<y+dy;j++)
      {
         idx=i+j*retrow;
         if (alpha < 255)
            buffer[idx]=blend32(color, buffer[idx], alpha);
         else
            buffer[idx]=color;
      }
   }
}

void DrawBoxBmp(unsigned short *buffer, int x, int y, int dx, int dy, unsigned short color)
{
   int i,j,idx;

   for(i=x;i<x+dx;i++)
   {
      idx=i+y*retrow;
      buffer[idx]=color;
      idx=i+(y+dy)*retrow;
      buffer[idx]=color;
   }

   for(j=y;j<y+dy;j++)
   {
      idx=x+j*retrow;
      buffer[idx]=color;	
      idx=(x+dx)+j*retrow;
      buffer[idx]=color;	
   }
}

void DrawBoxBmp32(uint32_t *buffer, int x, int y, int dx, int dy, uint32_t color)
{
   int i,j,idx;

   for(i=x;i<x+dx;i++)
   {
      idx=i+y*retrow;
      buffer[idx]=color;
      idx=i+(y+dy)*retrow;
      buffer[idx]=color;
   }

   for(j=y;j<y+dy;j++)
   {
      idx=x+j*retrow;
      buffer[idx]=color;	
      idx=(x+dx)+j*retrow;
      buffer[idx]=color;	
   }
}

void DrawPointBmp(unsigned short *buffer, int x, int y, unsigned short color)
{
   int idx;

   idx = x + y * retrow;
   buffer[idx]=color;
}

void DrawHlineBmp(unsigned short *buffer, int x, int y, int dx, int dy, unsigned short color)
{
   int i,j,idx;

   (void)j;

   for(i=x;i<x+dx;i++)
   {
      idx=i+y*retrow;
      buffer[idx]=color;
   }
}

void DrawHlineBmp32(uint32_t *buffer, int x, int y, int dx, int dy, uint32_t color)
{
   int i,j,idx;

   (void)j;

   for(i=x;i<x+dx;i++)
   {
      idx=i+y*retrow;
      buffer[idx]=color;
   }
}

void DrawHline(unsigned short *buffer, int x, int y, int dx, int dy, unsigned short color)
{
   if (pix_bytes == 4)
      DrawHlineBmp32((uint32_t *)buffer, x, y, dx, dy, (uint32_t)color);
   else
      DrawHlineBmp(buffer, x, y, dx, dy, color);
}

void DrawVlineBmp(unsigned short *buffer, int x, int y, int dx, int dy, unsigned short color)
{
   int i,j,idx;

   (void)i;

   for(j=y;j<y+dy;j++)
   {
      idx=x+j*retrow;
      buffer[idx]=color;		
   }	
}

void DrawlineBmp(unsigned short *buffer, int x1, int y1, int x2, int y2, unsigned short color)
{
   int pixx, pixy, x, y, dx, dy, sx, sy, swaptmp, idx;

   dx = x2 - x1;
   dy = y2 - y1;
   sx = (dx >= 0) ? 1 : -1;
   sy = (dy >= 0) ? 1 : -1;

   if (dx == 0)
   {
      if (dy > 0)
      {
         DrawVlineBmp(buffer, x1, y1, 0, dy, color);
         return;
      }
      else if (dy < 0)
      {
         DrawVlineBmp(buffer, x1, y2, 0, -dy, color);
         return;
      }
      else
      {
         idx=x1+y1*retrow;
         buffer[idx]=color;
         return;
      }
   }

   if (dy == 0)
   {
      if (dx > 0)
      {
         DrawHlineBmp(buffer, x1, y1, dx, 0, color);
         return;
      }
      else if (dx < 0)
      {
         DrawHlineBmp(buffer, x2, y1, -dx,0, color);
         return;
      }
   }

   dx = sx * dx + 1;
   dy = sy * dy + 1;

   pixx = 1;
   pixy = retrow;

   pixx *= sx;
   pixy *= sy;

   if (dx < dy)
   {
      swaptmp = dx;
      dx = dy;
      dy = swaptmp;
      swaptmp = pixx;
      pixx = pixy;
      pixy = swaptmp;
   }

   x = 0;
   y = 0;

   idx=x1+y1*retrow;

   for (; x < dx; x++, idx +=pixx)
   {
      buffer[idx]=color;
      y += dy;
      if (y >= dx)
      {
         y -= dx;
         idx += pixy;
      }
   }
}

#include "font2.c"

void Draw_string(unsigned short *surf, signed short int x, signed short int y,
      const char *string, unsigned short maxstrlen,
      unsigned short xscale, unsigned short yscale,
      unsigned short fg, unsigned short bg, unsigned int alpha)
{
   int k, strlen;
   int xrepeat, yrepeat;
   unsigned char *linesurf;
   signed short int ypixel;
   unsigned short *yptr; 
   int col, bit;
   unsigned char b;

   (void)k;

   if (!string)
      return;

   // Pseudo transparency for now
   if (alpha < 255)
      fg = blend(fg, ((bg == 0) ? 0xFFFF : bg), alpha);
   bg = 0;

   for(strlen = 0; strlen < maxstrlen && string[strlen]; strlen++) {}

   int surfw = strlen * 7 * xscale;
   int surfh = 8 * yscale;

   linesurf = (unsigned char*)malloc(sizeof(unsigned short)*surfw*surfh);
   yptr = (unsigned short *)&linesurf[0];

   for(ypixel = 0; ypixel < 8; ypixel++)
   {
      for(col = 0; col < strlen; col++)
      {
         b = font_array[(((unsigned char)string[col])^0x80)*8 + ypixel];
         for(bit = 0; bit < 7; bit++, yptr++)
         {
            *yptr = (b & (1<<(7-bit))) ? fg : bg;
            for(xrepeat = 1; xrepeat < xscale; xrepeat++, yptr++)
               yptr[1] = *yptr;
         }
      }

      for(yrepeat = 1; yrepeat < yscale; yrepeat++)
         for(xrepeat = 0; xrepeat < surfw; xrepeat++, yptr++)
            *yptr = yptr[-surfw];
   }

   yptr = (unsigned short*)&linesurf[0];

   for(yrepeat = y; yrepeat < y+surfh; yrepeat++)
      for(xrepeat = x; xrepeat < x+surfw; xrepeat++, yptr++)
         if (*yptr != 0) surf[xrepeat+yrepeat*retrow] = *yptr;

   free(linesurf);
}

void Draw_string32(uint32_t *surf, signed short int x, signed short int y,
      const char *string, unsigned short maxstrlen,
      unsigned short xscale, unsigned short yscale,
      uint32_t fg, uint32_t bg, unsigned int alpha)
{
   int k, strlen;
   int xrepeat, yrepeat;
   uint32_t *linesurf;
   signed short int ypixel;
   uint32_t *yptr;
   int col, bit;
   unsigned char b;

   (void)k;

   if (!string)
      return;

   // Pseudo transparency for now
   if (alpha < 255)
      fg = blend32(fg, ((bg == 0) ? 0xFFFFFFFF : bg), alpha);
   bg = 0;

   for(strlen = 0; strlen < maxstrlen && string[strlen]; strlen++) {}

   int surfw = strlen * 7 * xscale;
   int surfh = 8 * yscale;

   linesurf = (uint32_t *)malloc(sizeof(uint32_t)*surfw*surfh);
   yptr = (uint32_t *)&linesurf[0];

   for(ypixel = 0; ypixel < 8; ypixel++)
   {
      for(col = 0; col < strlen; col++)
      {
         b = font_array[(((unsigned char)string[col])^0x80)*8 + ypixel];
         for(bit = 0; bit < 7; bit++, yptr++)
         {
            *yptr = (b & (1<<(7-bit))) ? fg : bg;
            for(xrepeat = 1; xrepeat < xscale; xrepeat++, yptr++)
               yptr[1] = *yptr;
         }
      }

      for(yrepeat = 1; yrepeat < yscale; yrepeat++)
         for(xrepeat = 0; xrepeat < surfw; xrepeat++, yptr++)
            *yptr = yptr[-surfw];
   }

   yptr = (uint32_t *)&linesurf[0];

   for(yrepeat = y; yrepeat < y+surfh; yrepeat++)
      for(xrepeat = x; xrepeat < x+surfw; xrepeat++, yptr++)
         if (*yptr != 0) surf[xrepeat+yrepeat*retrow] = *yptr;

   free(linesurf);
}

void Draw_text(unsigned short *buffer, int x, int y,
      unsigned short fgcol, unsigned short bgcol, unsigned int alpha,
      int scalex, int scaley, int max, char *string, ...)
{
   int boucle=0;
   char text[256];
   va_list ap;

   (void)boucle;

   if (string == NULL)
      return;

   va_start(ap, string);
   vsnprintf(text, sizeof(text), string, ap);
   va_end(ap);

#if 0
   Draw_string(buffer, x, y, text, max, scalex, scaley, fgcol, bgcol, alpha);
#else
   char c[1] = {0};
   char s[2] = {0};
   int charwidth = 6;
   int cmax;
   cmax = strlen(text);
   cmax = (cmax > max) ? max : cmax;
   for (int i = 0; i < cmax; i++)
   {
      c[0] = text[i];
      if (c[0] == 0)
         break;
      snprintf(s, sizeof(s), "%s", c);
      Draw_string(buffer, x+(i*charwidth*scalex), y, s, 1, scalex, scaley, fgcol, bgcol, alpha);
   }
#endif
}

void Draw_text32(uint32_t *buffer, int x, int y,
      uint32_t fgcol, uint32_t bgcol, unsigned int alpha,
      int scalex, int scaley, int max, char *string,...)
{
   int boucle=0;
   char text[256];
   va_list ap;

   (void)boucle;

   if (string == NULL)
      return;

   va_start(ap, string);
   vsnprintf(text, sizeof(text), string, ap);
   va_end(ap);

#if 0
   Draw_string32(buffer, x, y, text, max, scalex, scaley, fgcol, bgcol, alpha);
#else
   char c[2] = {0};
   char s[2] = {0};
   int charwidth = 6;
   int cmax;
   cmax = strlen(text);
   cmax = (cmax > max) ? max : cmax;
   for (int i = 0; i < cmax; i++)
   {
      c[0] = text[i];
      if (c[0] == 0)
         break;
      snprintf(s, sizeof(s), "%s", c);
      Draw_string32(buffer, x+(i*charwidth*scalex), y, s, 1, scalex, scaley, fgcol, bgcol, alpha);
   }
#endif
}
