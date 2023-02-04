#pragma once

// This file is a C++ port of BlitSort code: https://github.com/scandum/blitsort

/*
    Copyright (C) 2014-2022 Igor van den Hoven ivdhoven@gmail.com
*/

/*
    Permission is hereby granted, free of charge, to any person obtaining
    a copy of this software and associated documentation files (the
    "Software"), to deal in the Software without restriction, including
    without limitation the rights to use, copy, modify, merge, publish,
    distribute, sublicense, and/or sell copies of the Software, and to
    permit persons to whom the Software is furnished to do so, subject to
    the following conditions:

    The above copyright notice and this permission notice shall be
    included in all copies or substantial portions of the Software.

    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
    EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
    MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.
    IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY
    CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT,
    TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN CONNECTION WITH THE
    SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.
    */

/*
    blitsort 1.1.5.3
*/


#include "Base.h"

// _Cmp = Function: [](const _T& a, const _T& b)->int
// Result is like memcmp: <0: a is lower than b, =0: equal, >0 a is higher than b
// With these preassumptions, it sorts with ascending order
template <typename _T, typename _Cmp> void BlitSort(_T* array, size_t count, _Cmp cmp);

//------------------------------------------------------------------------
namespace _private 
{
    constexpr uint32 kBlitSortOut = 24;

    template <typename _T, typename _Cmp> 
    void blit_partition(_T *array, _T *swap, size_t swap_size, size_t nmemb, _Cmp cmp);

    template <typename _T, typename _Cmp> 
    void quadsort_swap(_T *array, _T *swap, size_t swap_size, size_t nmemb, _Cmp cmp);

    template <typename _T, typename _Cmp> 
    void blit_merge_block(_T *array, _T *swap, size_t swap_size, size_t lblock, size_t right, _Cmp cmp);

    template <typename _T, typename _Cmp>
    void trinity_rotation(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t left);


    template <typename _T, typename _Cmp> 
    void parity_merge_two(_T *array,  _T *swap, size_t x, size_t y, _T *ptl, _T *ptr, _T *pts, _Cmp cmp)  
    {  
        ptl = array + 0; ptr = array + 2; pts = swap + 0;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts[x] = *ptr; ptr += y; pts[y] = *ptl; ptl += x; pts++;  
        *pts = cmp(*ptl, *ptr) <= 0 ? *ptl : *ptr;  
        
        ptl = array + 1; ptr = array + 3; pts = swap + 3;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts--; pts[x] = *ptr; ptr -= x; pts[y] = *ptl; ptl -= y;  
        *pts = cmp(*ptl, *ptr)  > 0 ? *ptl : *ptr;  
    }

    template <typename _T, typename _Cmp> 
    void parity_merge_four(_T *array, _T *swap, size_t x, size_t y, _T *ptl, _T *ptr, _T *pts, _Cmp cmp)  
    {  
        ptl = array + 0; ptr = array + 4; pts = swap;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts[x] = *ptr; ptr += y; pts[y] = *ptl; ptl += x; pts++;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts[x] = *ptr; ptr += y; pts[y] = *ptl; ptl += x; pts++;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts[x] = *ptr; ptr += y; pts[y] = *ptl; ptl += x; pts++;  
        *pts = cmp(*ptl, *ptr) <= 0 ? *ptl : *ptr;  
        
        ptl = array + 3; ptr = array + 7; pts = swap + 7;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts--; pts[x] = *ptr; ptr -= x; pts[y] = *ptl; ptl -= y;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts--; pts[x] = *ptr; ptr -= x; pts[y] = *ptl; ptl -= y;  
        x = cmp(*ptl, *ptr) <= 0; y = !x; pts--; pts[x] = *ptr; ptr -= x; pts[y] = *ptl; ptl -= y;  
        *pts = cmp(*ptl, *ptr)  > 0 ? *ptl : *ptr;  
    }

    template <typename _T, typename _Cmp> 
    void blit_analyze(_T* array, _T* swap, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        char loop, asum, zsum;
        size_t cnt, abalance = 0, zbalance = 0, astreaks = 0, zstreaks = 0;
        _T *pta, *ptz, tmp;
    
        pta = array;
        ptz = array + nmemb - 2;
    
        for (cnt = nmemb ; cnt > 64 ; cnt -= 64)
        {
            for (asum = zsum = 0, loop = 32 ; loop ; loop--)
            {
                asum += cmp(*pta, *(pta + 1)) > 0; pta++;
                zsum += cmp(*ptz, *(ptz + 1)) > 0; ptz--;
            }
            astreaks += (asum == 0) | (asum == 32);
            zstreaks += (zsum == 0) | (zsum == 32);
            abalance += asum;
            zbalance += zsum;
        }
    
        while (--cnt)
        {
            zbalance += cmp(*ptz, *(ptz + 1)) > 0; ptz--;
        }
    
        if (abalance + zbalance == 0)
        {
            return;
        }
    
        if (abalance + zbalance == nmemb - 1)
        {
            ptz = array + nmemb;
            pta = array;
    
            cnt = nmemb / 2;
    
            do
            {
                tmp = *pta; *pta++ = *--ptz; *ptz = tmp;
            }
            while (--cnt);
    
            return;
        }
    
        if (astreaks + zstreaks > nmemb / 80)
        {
            if (nmemb >= 1024)
            {
                size_t block = pta - array;
    
                if (astreaks < nmemb / 128)
                {
                    blit_partition<_T, _Cmp>(array, swap, swap_size, block, cmp);
                }
                else if (abalance)
                {
                    quadsort_swap<_T, _Cmp>(array, swap, swap_size, block, cmp);
                }
    
                if (zstreaks < nmemb / 128)
                {
                    blit_partition<_T, _Cmp>(array + block, swap, swap_size, nmemb - block, cmp);
                }
                else if (zbalance)
                {
                    quadsort_swap<_T, _Cmp>(array + block, swap, swap_size, nmemb - block, cmp);
                }
                blit_merge_block<_T, _Cmp>(array, swap, swap_size, block, nmemb - block, cmp);
            }
            else
            {
                quadsort_swap<_T, _Cmp>(array, swap, swap_size, nmemb, cmp);
            }
            return;
        }
        blit_partition<_T, _Cmp>(array, swap, swap_size, nmemb, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    _T blit_median_of_sqrt(_T *array, _T *swap, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        UNUSED(swap_size);
        _T *pta, *pts;
        size_t cnt, sqrt, div;
    
        sqrt = nmemb > 262144 ? 256 : 128;
    
        div = nmemb / sqrt;
    
        pta = array + randomNewUint() % sqrt;
        pts = swap;
    
        for (cnt = 0 ; cnt < sqrt ; cnt++)
        {
            pts[cnt] = pta[0];
    
            pta += div;
        }
        quadsort_swap<_T, _Cmp>(pts, pts + sqrt, sqrt, sqrt, cmp);
    
        return pts[sqrt / 2];
    }
    
    template <typename _T, typename _Cmp> 
    _T blit_median_of_five(_T *array, size_t v0, size_t v1, size_t v2, size_t v3, size_t v4, _Cmp cmp)
    {
        _T swap[6], *pta;
        size_t x, y, z;
    
        swap[2] = array[v0];
        swap[3] = array[v1];
        swap[4] = array[v2];
        swap[5] = array[v3];
    
        pta = swap + 2;
    
        x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap[0] = pta[y]; pta[0] = pta[x]; pta[1] = swap[0]; pta += 2;
        x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap[0] = pta[y]; pta[0] = pta[x]; pta[1] = swap[0]; pta -= 2;
        x = cmp(*pta, *(pta + 2)) > 0; y = !x; swap[0] = pta[0]; swap[1] = pta[2]; pta[0] = swap[x]; pta[2] = swap[y]; pta++;
        x = cmp(*pta, *(pta + 2)) > 0; y = !x; swap[0] = pta[0]; swap[1] = pta[2]; pta[0] = swap[x]; pta[2] = swap[y];
    
        pta[2] = array[v4];
    
        x = cmp(*pta, *(pta + 1)) > 0;
        y = cmp(*pta, *(pta + 2)) > 0;
        z = cmp(*(pta + 1), *(pta + 2)) > 0;
    
        return pta[(x == y) + (y ^ z)];
    }
    
    template <typename _T, typename _Cmp> 
    _T blit_median_of_twentyfive(_T *array, size_t nmemb, _Cmp cmp)
    {
        _T swap[5];
        size_t div = nmemb / 64;
    
        swap[0] = blit_median_of_five<_T, _Cmp>(array, div *  4, div *  1, div *  2, div *  8, div * 10, cmp);
        swap[1] = blit_median_of_five<_T, _Cmp>(array, div * 16, div * 12, div * 14, div * 18, div * 20, cmp);
        swap[2] = blit_median_of_five<_T, _Cmp>(array, div * 32, div * 24, div * 30, div * 34, div * 38, cmp);
        swap[3] = blit_median_of_five<_T, _Cmp>(array, div * 48, div * 42, div * 44, div * 50, div * 52, cmp);
        swap[4] = blit_median_of_five<_T, _Cmp>(array, div * 60, div * 54, div * 56, div * 62, div * 63, cmp);
    
        return blit_median_of_five<_T, _Cmp>(swap, 0, 1, 2, 3, 4, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    size_t blit_median_of_three(_T *array, size_t v0, size_t v1, size_t v2, _Cmp cmp)
    {
        size_t v[3] = {v0, v1, v2};
        char x, y, z;
    
        x = cmp(*(array + v0), *(array + v1)) > 0;
        y = cmp(*(array + v0), *(array + v2)) > 0;
        z = cmp(*(array + v1), *(array + v2)) > 0;
    
        return v[(x == y) + (y ^ z)];
    }
    
    template <typename _T, typename _Cmp> 
    _T blit_median_of_nine(_T *array, size_t nmemb, _Cmp cmp)
    {
        size_t x, y, z, div = nmemb / 16;
    
        x = blit_median_of_three<_T, _Cmp>(array, div * 2, div * 1, div * 4, cmp);
        y = blit_median_of_three<_T, _Cmp>(array, div * 8, div * 6, div * 10, cmp);
        z = blit_median_of_three<_T, _Cmp>(array, div * 14, div * 12, div * 15, cmp);
    
        return array[blit_median_of_three<_T, _Cmp>(array, x, y, z, cmp)];
    }
    
    // As per suggestion by Marshall Lochbaum to improve generic data handling
    template <typename _T, typename _Cmp> 
    size_t blit_reverse_partition(_T *array, _T *swap, _T *piv, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        if (nmemb > swap_size)
        {
            size_t l, r, h = nmemb / 2;
    
            l = blit_reverse_partition<_T, _Cmp>(array + 0, swap, piv, swap_size, h, cmp);
            r = blit_reverse_partition<_T, _Cmp>(array + h, swap, piv, swap_size, nmemb - h, cmp);
    
            trinity_rotation<_T, _Cmp>(array + l, swap, swap_size, h - l + r, h - l);
    
            return l + r;
        }
        size_t cnt, val, m;
        _T *pta = array;
    
        for (m = 0, cnt = nmemb / 4 ; cnt ; cnt--)
        {
            val = cmp(*piv, *pta) > 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*piv, *pta) > 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*piv, *pta) > 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*piv, *pta) > 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
        }
    
        for (cnt = nmemb % 4 ; cnt ; cnt--)
        {
            val = cmp(*piv, *pta) > 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
        }
    
        memcpy(array + m, swap - nmemb, (nmemb - m) * sizeof(_T));
    
        return m;
    }

    template <typename _T, typename _Cmp> 
    size_t blit_default_partition(_T *array, _T *swap, _T *piv, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        if (nmemb > swap_size)
        {
            size_t l, r, h = nmemb / 2;
    
            l = blit_default_partition<_T, _Cmp>(array + 0, swap, piv, swap_size, h, cmp);
            r = blit_default_partition<_T, _Cmp>(array + h, swap, piv, swap_size, nmemb - h, cmp);
    
            trinity_rotation<_T, _Cmp>(array + l, swap, swap_size, h - l + r, h - l);
    
            return l + r;
        }
        size_t cnt, val, m = 0;
        _T *pta = array;
    
        for (cnt = nmemb / 4 ; cnt ; cnt--)
        {
            val = cmp(*pta, *piv) <= 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*pta, *piv) <= 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*pta, *piv) <= 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
            val = cmp(*pta, *piv) <= 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
        }
    
        for (cnt = nmemb % 4 ; cnt ; cnt--)
        {
            val = cmp(*pta, *piv) <= 0; swap[-(int64)m] = array[m] = *pta++; m += val; swap++;
        }
    
        memcpy(array + m, swap - nmemb, sizeof(_T) * (nmemb - m));
    
        return m;
    }
    
    template <typename _T, typename _Cmp> 
    void blit_partition(_T *array, _T *swap, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        size_t a_size = 0, s_size;
        _T piv, max {};
    
        while (1)
        {
            if (nmemb <= 2048)
            {
                piv = blit_median_of_nine<_T, _Cmp>(array, nmemb, cmp);
            }
            else if (nmemb <= 65536 || swap_size < 512)
            {
                piv = blit_median_of_twentyfive<_T, _Cmp>(array, nmemb, cmp);
            }
            else
            {
                piv = blit_median_of_sqrt<_T, _Cmp>(array, swap, swap_size, nmemb, cmp);
            }
    
            if (a_size && cmp(max, piv) <= 0)
            {
                a_size = blit_reverse_partition<_T, _Cmp>(array, swap, &piv, swap_size, nmemb, cmp);
                s_size = nmemb - a_size;
    
                if (s_size <= a_size / 16 || a_size <= kBlitSortOut)
                {
                    return quadsort_swap<_T, _Cmp>(array, swap, swap_size, a_size, cmp);
                }
                nmemb = a_size; a_size = 0;
                continue;
            }
    
            a_size = blit_default_partition<_T, _Cmp>(array, swap, &piv, swap_size, nmemb, cmp);
            s_size = nmemb - a_size;
    
            if (a_size <= s_size / 16 || s_size <= kBlitSortOut)
            {
                if (s_size == 0)
                {
                    a_size = blit_reverse_partition<_T, _Cmp>(array, swap, &piv, swap_size, a_size, cmp);
                    s_size = nmemb - a_size;
    
                    if (s_size <= a_size / 16 || a_size <= kBlitSortOut)
                    {
                        return quadsort_swap<_T, _Cmp>(array, swap, swap_size, a_size, cmp);
                    }
                    nmemb = a_size; a_size = 0;
                    continue;
                }
                quadsort_swap<_T, _Cmp>(array + a_size, swap, swap_size, s_size, cmp);
            }
            else
            {
                blit_partition<_T, _Cmp>(array + a_size, swap, swap_size, s_size, cmp);
            }
    
            if (s_size <= a_size / 16 || a_size <= kBlitSortOut)
            {
                return quadsort_swap<_T, _Cmp>(array, swap, swap_size, a_size, cmp);
            }
            nmemb = a_size;
            max = piv;
        }
    }
    
    template <typename _T, typename _Cmp>     
    void unguarded_insert(_T *array, size_t offset, size_t nmemb, _Cmp cmp)
    {
        _T key, *pta, *end;
        size_t i, top, x, y;
    
        for (i = offset ; i < nmemb ; i++)
        {
            pta = end = array + i;
    
            if (cmp(*(--pta), *end) <= 0)
            {
                continue;
            }
    
            key = *end;
    
            if (cmp(*(array + 1), key) > 0)
            {
                top = i - 1;
    
                do
                {
                    *end-- = *pta--;
                }
                while (--top);
    
                *end-- = key;
            }
            else
            {
                do
                {
                    *end-- = *pta--;
                    *end-- = *pta--;
                }
                while (cmp(*pta, key) > 0);
    
                end[0] = end[1];
                end[1] = key;
            }
            x = cmp(*end, *(end + 1)) > 0; y = !x; key = end[y]; end[0] = end[x]; end[1] = key;
        }
    }
    
    template <typename _T, typename _Cmp> 
    void bubble_sort(_T *array, size_t nmemb, _Cmp cmp)
    {
        _T swap, *pta;
        size_t x, y;
    
        if (nmemb > 1)
        {
            pta = array;
    
            if (nmemb > 2)
            {
                x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta++;
                x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta--;
            }
            x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap;
        }
    }
    
    template <typename _T, typename _Cmp> 
    void quad_swap_four(_T *array, _Cmp cmp)
    {
        _T *pta, swap;
        size_t x, y;
    
        pta = array;
        x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta += 2;
        x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta--;
    
        if (cmp(*pta, *(pta + 1)) > 0)
        {
            swap = pta[0]; pta[0] = pta[1]; pta[1] = swap; pta--;
    
            x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta += 2;
            x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap; pta--;
            x = cmp(*pta, *(pta + 1)) > 0; y = !x; swap = pta[y]; pta[0] = pta[x]; pta[1] = swap;
        }
    }
    
    template <typename _T, typename _Cmp> 
    void parity_swap_eight(_T *array, _Cmp cmp)
    {
        _T swap[8], *ptl = nullptr, *ptr = nullptr, *pts = nullptr;
        unsigned char x, y;
    
        ptl = array;
        x = cmp(*ptl, *(ptl + 1)) > 0; y = !x; swap[0] = ptl[y]; ptl[0] = ptl[x]; ptl[1] = swap[0]; ptl += 2;
        x = cmp(*ptl, *(ptl + 1)) > 0; y = !x; swap[0] = ptl[y]; ptl[0] = ptl[x]; ptl[1] = swap[0]; ptl += 2;
        x = cmp(*ptl, *(ptl + 1)) > 0; y = !x; swap[0] = ptl[y]; ptl[0] = ptl[x]; ptl[1] = swap[0]; ptl += 2;
        x = cmp(*ptl, *(ptl + 1)) > 0; y = !x; swap[0] = ptl[y]; ptl[0] = ptl[x]; ptl[1] = swap[0];
    
        if (cmp(*(array + 1), *(array + 2)) <= 0 && cmp(*(array + 3), *(array + 4)) <= 0 && cmp(*(array + 5), *(array + 6)) <= 0)
        {
            return;
        }
        parity_merge_two<_T, _Cmp>(array + 0, swap + 0, x, y, ptl, ptr, pts, cmp);
        parity_merge_two<_T, _Cmp>(array + 4, swap + 4, x, y, ptl, ptr, pts, cmp);
    
        parity_merge_four<_T, _Cmp>(swap, array, x, y, ptl, ptr, pts, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    void parity_merge(_T *dest, _T *from, size_t block, size_t nmemb, _Cmp cmp)
    {
        _T *ptl, *ptr, *tpl, *tpr, *tpd, *ptd;
        unsigned char x, y;
    
        ptl = from;
        ptr = from + block;
        ptd = dest;
        tpl = from + block - 1;
        tpr = from + nmemb - 1;
        tpd = dest + nmemb - 1;
    
        while (--block)
        {
            x = cmp(*ptl, *ptr) <= 0; y = !x; ptd[x] = *ptr; ptr += y; ptd[y] = *ptl; ptl += x; ptd++;
            x = cmp(*tpl, *tpr) <= 0; y = !x; tpd--; tpd[x] = *tpr; tpr -= x; tpd[y] = *tpl; tpl -= y;
        }
        *ptd = cmp(*ptl, *ptr) <= 0 ? *ptl : *ptr;
        *tpd = cmp(*tpl, *tpr)  > 0 ? *tpl : *tpr;
    }

    template <typename _T, typename _Cmp> 
    void parity_swap_sixteen(_T *array, _Cmp cmp)
    {
        _T swap[16], *ptl = nullptr, *ptr = nullptr, *pts = nullptr;
        unsigned char x = 0, y = 0;
    
        quad_swap_four<_T, _Cmp>(array +  0, cmp);
        quad_swap_four<_T, _Cmp>(array +  4, cmp);
        quad_swap_four<_T, _Cmp>(array +  8, cmp);
        quad_swap_four<_T, _Cmp>(array + 12, cmp);
    
        if (cmp(*(array + 3), *(array + 4)) <= 0 && cmp(*(array + 7), *(array + 8)) <= 0 && cmp(*(array + 11), *(array + 12)) <= 0)
        {
            return;
        }
        parity_merge_four<_T, _Cmp>(array + 0, swap + 0, x, y, ptl, ptr, pts, cmp);
        parity_merge_four<_T, _Cmp>(array + 8, swap + 8, x, y, ptl, ptr, pts, cmp);
    
        parity_merge<_T, _Cmp>(array, swap, 8, 16, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    void tail_swap(_T *array, size_t nmemb, _Cmp cmp)
    {
        if (nmemb < 4)
        {
            bubble_sort<_T, _Cmp>(array, nmemb, cmp);
            return;
        }
        if (nmemb < 8)
        {
            quad_swap_four<_T, _Cmp>(array, cmp);
            unguarded_insert<_T, _Cmp>(array, 4, nmemb, cmp);
            return;
        }
        if (nmemb < 16)
        {
            parity_swap_eight<_T, _Cmp>(array, cmp);
            unguarded_insert<_T, _Cmp>(array, 8, nmemb, cmp);
            return;
        }
        parity_swap_sixteen<_T, _Cmp>(array, cmp);
        unguarded_insert<_T, _Cmp>(array, 16, nmemb, cmp);
    }
    
    // the next three functions create sorted blocks of 32 elements
    template <typename _T, typename _Cmp> 
    void parity_tail_swap_eight(_T *array, _Cmp cmp)
    {
        _T swap[8], *ptl = nullptr, *ptr = nullptr, *pts = nullptr;
        unsigned char x = 0, y = 0;
    
        if (cmp(*(array + 4), *(array + 5)) > 0) { swap[5] = array[4]; array[4] = array[5]; array[5] = swap[5]; }
        if (cmp(*(array + 6), *(array + 7)) > 0) { swap[7] = array[6]; array[6] = array[7]; array[7] = swap[7]; } else
    
            if (cmp(*(array + 3), *(array + 4)) <= 0 && cmp(*(array + 5), *(array + 6)) <= 0)
            {
                return;
            }
            swap[0] = array[0]; swap[1] = array[1]; swap[2] = array[2]; swap[3] = array[3];
    
            parity_merge_two<_T, _Cmp>(array + 4, swap + 4, x, y, ptl, ptr, pts, cmp);
    
            parity_merge_four<_T, _Cmp>(swap, array, x, y, ptl, ptr, pts, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    void parity_tail_flip_eight(_T *array, _Cmp cmp)
    {
        _T swap[8], *ptl = nullptr, *ptr = nullptr, *pts = nullptr;
        unsigned char x = 0, y = 0;
    
        if (cmp(*(array + 3), *(array + 4)) <= 0)
        {
            return;
        }
        swap[0] = array[0]; swap[1] = array[1]; swap[2] = array[2]; swap[3] = array[3];
        swap[4] = array[4]; swap[5] = array[5]; swap[6] = array[6]; swap[7] = array[7];
    
        parity_merge_four<_T, _Cmp>(swap, array, x, y, ptl, ptr, pts, cmp);
    }
    
    template <typename _T, typename _Cmp> 
    void tail_merge(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t block, _Cmp cmp);
    
    template <typename _T, typename _Cmp> 
    size_t quad_swap(_T *array, size_t nmemb, _Cmp cmp)
    {
        _T swap[32];
        size_t count, reverse, x, y;
        _T *pta, *pts, *pte, tmp;
    
        pta = array;
    
        count = nmemb / 8 * 2;
    
        while (count--)
        {
            switch ((cmp(*pta, *(pta + 1)) > 0) | (cmp(*(pta + 1), *(pta + 2)) > 0) * 2 | (cmp(*(pta + 2), *(pta + 3)) > 0) * 4)
            {
            case 0:
                break;
            case 1:
                tmp = pta[0]; pta[0] = pta[1]; pta[1] = tmp;
                pta += 1; x = cmp(*pta, *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta += 1; x = cmp(*pta, *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 2;
                break;
            case 2:
                tmp = pta[1]; pta[1] = pta[2]; pta[2] = tmp;
                x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta += 2; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1;
                break;
            case 3:
                tmp = pta[0]; pta[0] = pta[2]; pta[2] = tmp;
                pta += 2; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1;
                break;
            case 4:
                tmp = pta[2]; pta[2] = pta[3]; pta[3] = tmp;
                pta += 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                break;
            case 5:
                tmp = pta[0]; pta[0] = pta[1]; pta[1] = tmp;
                tmp = pta[2]; pta[2] = pta[3]; pta[3] = tmp;
                pta += 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta += 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 2; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                break;
            case 6:
                tmp = pta[1]; pta[1] = pta[3]; pta[3] = tmp;
                x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta += 1; x = cmp(*(pta), *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp;
                pta -= 1;
                break;
            case 7:
                pts = pta;
                goto swapper;
            }
            count--;
    
            parity_tail_swap_eight<_T, _Cmp>(pta, cmp);
    
            pta += 8;
    
            continue;
    
            swapper:
    
                pta += 4;
    
            if (count--)
            {
                if (cmp(pta[0], pta[1]) > 0)
                {
                    if (cmp(pta[2], pta[3]) > 0)
                    {
                        if (cmp(pta[1], pta[2]) > 0)
                        {
                            if (cmp(pta[-1], pta[0]) > 0)
                            {
                                goto swapper;
                            }
                        }
                        tmp = pta[2]; pta[2] = pta[3]; pta[3] = tmp;
                    }
                    tmp = pta[0]; pta[0] = pta[1]; pta[1] = tmp;
                }
                else if (cmp(pta[2], pta[3]) > 0)
                {
                    tmp = pta[2]; pta[2] = pta[3]; pta[3] = tmp;
                }
    
                if (cmp(pta[1], pta[2]) > 0)
                {
                    tmp = pta[1]; pta[1] = pta[2]; pta[2] = tmp;
    
                    x = cmp(*pta, *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp; pta += 2;
                    x = cmp(*pta, *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp; pta -= 1;
                    x = cmp(*pta, *(pta + 1)) > 0; y = !x; tmp = pta[y]; pta[0] = pta[x]; pta[1] = tmp; pta -= 1;
                }
                pte = pta - 1;
    
                reverse = (pte - pts) / 2;
    
                do
                {
                    tmp = *pts; *pts++ = *pte; *pte-- = tmp;
                }
                while (reverse--);
    
                if (count % 2 == 0)
                {
                    pta -= 4;
    
                    parity_tail_flip_eight<_T, _Cmp>(pta, cmp);
                }
                else
                {
                    count--;
    
                    parity_tail_swap_eight<_T, _Cmp>(pta, cmp);
                }
                pta += 8;
    
                continue;
            }
    
            if (pts == array)
            {
                switch (nmemb % 8)
                {
                case 7: if (cmp(*(pta + 5), *(pta + 6)) <= 0) break;
                case 6: if (cmp(*(pta + 4), *(pta + 5)) <= 0) break;
                case 5: if (cmp(*(pta + 3), *(pta + 4)) <= 0) break;
                case 4: if (cmp(*(pta + 2), *(pta + 3)) <= 0) break;
                case 3: if (cmp(*(pta + 1), *(pta + 2)) <= 0) break;
                case 2: if (cmp(*(pta + 0), *(pta + 1)) <= 0) break;
                case 1: if (cmp(*(pta - 1), *(pta + 0)) <= 0) break;
                case 0:
                    pte = pts + nmemb - 1;
    
                    reverse = (pte - pts) / 2;
    
                    do
                    {
                        tmp = *pts; *pts++ = *pte; *pte-- = tmp;
                    }
                    while (reverse--);
    
                    return 1;
                }
            }
            pte = pta - 1;
    
            reverse = (pte - pts) / 2;
    
            do
            {
                tmp = *pts; *pts++ = *pte; *pte-- = tmp;
            }
            while (reverse--);
    
            break;
        }
    
        tail_swap<_T, _Cmp>(pta, nmemb % 8, cmp);
    
        pta = array;
    
        for (count = nmemb / 32 ; count-- ; pta += 32)
        {
            if (cmp(*(pta + 7), *(pta + 8)) <= 0 && cmp(*(pta + 15), *(pta + 16)) <= 0 && cmp(*(pta + 23), *(pta + 24)) <= 0)
            {
                continue;
            }
            parity_merge<_T, _Cmp>(swap, pta, 8, 16, cmp);
            parity_merge<_T, _Cmp>(swap + 16, pta + 16, 8, 16, cmp);
            parity_merge<_T, _Cmp>(pta, swap, 16, 32, cmp);
        }
    
        if (nmemb % 32 > 8)
        {
            tail_merge<_T, _Cmp>(pta, swap, 32, nmemb % 32, 8, cmp);
        }
        return 0;
    }
    
    // quad merge support routines
    template <typename _T, typename _Cmp> 
    void forward_merge(_T *dest, _T *from, size_t block, _Cmp cmp)
    {
        _T *ptl, *ptr, *m, *e; // left, right, middle, end
        size_t x, y;
    
        ptl = from;
        ptr = from + block;
        m = ptr - 1;
        e = ptr + block - 1;
    
        if (cmp(*m, *(e - block / 4)) <= 0)
        {
            while (ptl < m - 1)
            {
                if (cmp(*(ptl + 1), *ptr) <= 0)
                {
                    *dest++ = *ptl++; *dest++ = *ptl++;
                }
                else if (cmp(*ptl, *(ptr + 1)) > 0)
                {
                    *dest++ = *ptr++; *dest++ = *ptr++;
                }
                else 
                {
                    x = cmp(*ptl, *ptr) <= 0; y = !x; dest[x] = *ptr; ptr += 1; dest[y] = *ptl; ptl += 1; dest += 2;
                    x = cmp(*ptl, *ptr) <= 0; y = !x; dest[x] = *ptr; ptr += y; dest[y] = *ptl; ptl += x; dest++;
                }
            }
    
            while (ptl <= m)
            {
                *dest++ = cmp(*ptl, *ptr) <= 0 ? *ptl++ : *ptr++;
            }
    
            do *dest++ = *ptr++; while (ptr <= e);
        }
        else if (cmp(*(m - block / 4), *e) > 0)
        {
            while (ptr < e - 1)
            {
                if (cmp(*ptl, *(ptr + 1)) > 0)
                {
                    *dest++ = *ptr++; *dest++ = *ptr++;
                }
                else if (cmp(*(ptl + 1), *ptr) <= 0)
                {
                    *dest++ = *ptl++; *dest++ = *ptl++;
                }
                else 
                {
                    x = cmp(*ptl, *ptr) <= 0; y = !x; dest[x] = *ptr; ptr += 1; dest[y] = *ptl; ptl += 1; dest += 2;
                    x = cmp(*ptl, *ptr) <= 0; y = !x; dest[x] = *ptr; ptr += y; dest[y] = *ptl; ptl += x; dest++;
                }
            }
    
            while (ptr <= e)
            {
                *dest++ = cmp(*ptl, *ptr) > 0 ? *ptr++ : *ptl++;
            }
    
            do *dest++ = *ptl++; while (ptl <= m);
        }
        else
        {
            parity_merge<_T, _Cmp>(dest, from, block, block * 2, cmp);
        }
    }
    
    // main memory: [A][B][C][D]
    // swap memory: [A  B]       step 1
    // swap memory: [A  B][C  D] step 2
    // main memory: [A  B  C  D] step 3
    template <typename _T, typename _Cmp> 
    void quad_merge_block(_T *array, _T *swap, size_t block, _Cmp cmp)
    {
        _T *pts = nullptr, *c, *c_max;
        size_t block_x_2 = block * 2;
    
        c_max = array + block;
    
        if (cmp(*(c_max - 1), *c_max) <= 0)
        {
            c_max += block_x_2;
    
            if (cmp(*(c_max - 1), *c_max) <= 0)
            {
                c_max -= block;
    
                if (cmp(*(c_max - 1), *c_max) <= 0)
                {
                    return;
                }
                pts = swap;
    
                c = array;
    
                do *pts++ = *c++; while (c < c_max); // step 1
    
                c_max = c + block_x_2;
    
                do *pts++ = *c++; while (c < c_max); // step 2
    
                return forward_merge<_T, _Cmp>(array, swap, block_x_2, cmp); // step 3
            }
            pts = swap;
    
            c = array;
            c_max = array + block_x_2;
    
            do *pts++ = *c++; while (c < c_max); // step 1
        }
        else
        {
            forward_merge<_T, _Cmp>(swap, array, block, cmp); // step 1
        }
        forward_merge<_T, _Cmp>(swap + block_x_2, array + block_x_2, block, cmp); // step 2
    
        forward_merge<_T, _Cmp>(array, swap, block_x_2, cmp); // step 3
    }
    
    template <typename _T, typename _Cmp> 
    size_t quad_merge(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t block, _Cmp cmp)
    {
        _T *pta, *pte;
    
        pte = array + nmemb;
    
        block *= 4;
    
        while (block <= nmemb && block <= swap_size)
        {
            pta = array;
    
            do
            {
                quad_merge_block<_T, _Cmp>(pta, swap, block / 4, cmp);
    
                pta += block;
            }
            while (pta + block <= pte);
    
            tail_merge<_T, _Cmp>(pta, swap, swap_size, pte - pta, block / 4, cmp);
    
            block *= 4;
        }
    
        tail_merge<_T, _Cmp>(array, swap, swap_size, nmemb, block / 4, cmp);
    
        return block / 2;
    }
    
    template <typename _T, typename _Cmp> 
    void partial_forward_merge(_T *array, _T *swap, size_t nmemb, size_t block, _Cmp cmp)
    {
        _T *r, *m, *e, *s; // right, middle, end, swap
        size_t x, y;
    
        r = array + block;
        e = array + nmemb - 1;
    
        memcpy(swap, array, block * sizeof(_T));
    
        s = swap;
        m = swap + block - 1;
    
        while (s < m - 1 && r < e - 1)
        {
            if (cmp(*s, *(r + 1)) > 0)
            {
                *array++ = *r++; *array++ = *r++;
            }
            else if (cmp(*(s + 1), *r) <= 0)
            {
                *array++ = *s++; *array++ = *s++;
            }
            else 
            {
                x = cmp(*s, *r) <= 0; y = !x; array[x] = *r; r += 1; array[y] = *s; s += 1; array += 2;
                x = cmp(*s, *r) <= 0; y = !x; array[x] = *r; r += y; array[y] = *s; s += x; array++;
            }
        }
    
        while (s <= m && r <= e)
        {
            *array++ = cmp(*s, *r) <= 0 ? *s++ : *r++;
        }
    
        while (s <= m)
        {
            *array++ = *s++;
        }
    }
    
    template <typename _T, typename _Cmp> 
    void partial_backward_merge(_T *array, _T *swap, size_t nmemb, size_t block, _Cmp cmp)
    {
        _T *m, *e, *s; // middle, end, swap
        size_t x, y;
    
        m = array + block - 1;
        e = array + nmemb - 1;
    
        if (cmp(*m, *(m + 1)) <= 0)
        {
            return;
        }
    
        memcpy(swap, array + block, (nmemb - block) * sizeof(_T));
    
        s = swap + nmemb - block - 1;
    
        while (s > swap + 1 && m > array + 1)
        {
            if (cmp(*(m - 1), *s) > 0)
            {
                *e-- = *m--;
                *e-- = *m--;
            }
            else if (cmp(*m, *(s - 1)) <= 0)
            {
                *e-- = *s--;
                *e-- = *s--;
            }
            else
            {
                x = cmp(*m, *s) <= 0; y = !x; e--; e[x] = *s; s -= 1; e[y] = *m; m -= 1; e--;
                x = cmp(*m, *s) <= 0; y = !x; e--; e[x] = *s; s -= x; e[y] = *m; m -= y;
            }
        }
    
        while (s >= swap && m >= array)
        {
            *e-- = cmp(*m, *s) > 0 ? *m-- : *s--;
        }
    
        while (s >= swap)
        {
            *e-- = *s--;
        }
    }
    
    template <typename _T, typename _Cmp> 
    void tail_merge(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t block, _Cmp cmp)
    {
        _T *pta, *pte;
    
        pte = array + nmemb;
    
        while (block < nmemb && block <= swap_size)
        {
            for (pta = array ; pta + block < pte ; pta += block * 2)
            {
                if (pta + block * 2 < pte)
                {
                    partial_backward_merge<_T, _Cmp>(pta, swap, block * 2, block, cmp);
    
                    continue;
                }
                partial_backward_merge<_T, _Cmp>(pta, swap, pte - pta, block, cmp);
    
                break;
            }
            block *= 2;
        }
    }
    
    // the next four functions provide in-place rotate merge support
    template <typename _T, typename _Cmp>
    void trinity_rotation(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t left)
    {
        size_t bridge, right = nmemb - left;
    
        if (left < right)
        {
            if (left <= swap_size)
            {
                memcpy(swap, array, left * sizeof(_T));
                memmove(array, array + left, right * sizeof(_T));
                memcpy(array + right, swap, left * sizeof(_T));
            }
            else
            {
                _T *pta, *ptb, *ptc, *ptd;
    
                pta = array;
                ptb = pta + left;
    
                bridge = right - left;
    
                if (bridge <= swap_size && bridge > 3)
                {
                    ptc = pta + right;
                    ptd = ptc + left;
    
                    memcpy(swap, ptb, bridge * sizeof(_T));
    
                    while (left--)
                    {
                        *--ptc = *--ptd; *ptd = *--ptb;
                    }
                    memcpy(pta, swap, bridge * sizeof(_T));
                }
                else
                {
                    ptc = ptb;
                    ptd = ptc + right;
    
                    bridge = left / 2;
    
                    while (bridge--)
                    {
                        *swap = *--ptb; *ptb = *pta; *pta++ = *ptc; *ptc++ = *--ptd; *ptd = *swap;
                    }
    
                    bridge = (ptd - ptc) / 2;
    
                    while (bridge--)
                    {
                        *swap = *ptc; *ptc++ = *--ptd; *ptd = *pta; *pta++ = *swap;
                    }
    
                    bridge = (ptd - pta) / 2;
    
                    while (bridge--)
                    {
                        *swap = *pta; *pta++ = *--ptd; *ptd = *swap;
                    }
                }
            }
        }
        else if (right < left)
        {
            if (right <= swap_size)
            {
                memcpy(swap, array + left, right * sizeof(_T));
                memmove(array + right, array, left * sizeof(_T));
                memcpy(array, swap, right * sizeof(_T));
            }
            else
            {
                _T *pta, *ptb, *ptc, *ptd;
    
                pta = array;
                ptb = pta + left;
    
                bridge = left - right;
    
                if (bridge <= swap_size && bridge > 3)
                {
                    ptc = pta + right;
                    ptd = ptc + left;
    
                    memcpy(swap, ptc, bridge * sizeof(_T));
    
                    while (right--)
                    {
                        *ptc++ = *pta; *pta++ = *ptb++;
                    }
                    memcpy(ptd - bridge, swap, bridge * sizeof(_T));
                }
                else
                {
                    ptc = ptb;
                    ptd = ptc + right;
    
                    bridge = right / 2;
    
                    while (bridge--)
                    {
                        *swap = *--ptb; *ptb = *pta; *pta++ = *ptc; *ptc++ = *--ptd; *ptd = *swap;
                    }
    
                    bridge = (ptb - pta) / 2;
    
                    while (bridge--)
                    {
                        *swap = *--ptb; *ptb = *pta; *pta++ = *--ptd; *ptd = *swap;
                    }
    
                    bridge = (ptd - pta) / 2;
    
                    while (bridge--)
                    {
                        *swap = *pta; *pta++ = *--ptd; *ptd = *swap;
                    }
                }
            }
        }
        else
        {
            _T *pta, *ptb;
    
            pta = array;
            ptb = pta + left;
    
            while (left--)
            {
                *swap = *pta; *pta++ = *ptb; *ptb++ = *swap;
            }
        }
    }
    
    template <typename _T, typename _Cmp> 
    size_t monobound_binary_first(_T *array, _T *value, size_t top, _Cmp cmp)
    {
        _T *end;
        size_t mid;
    
        end = array + top;
    
        while (top > 1)
        {
            mid = top / 2;
    
            if (cmp(*value, *(end - mid)) <= 0)
            {
                end -= mid;
            }
            top -= mid;
        }
    
        if (cmp(*value, *(end - 1)) <= 0)
        {
            end--;
        }
        return (end - array);
    }
    
    template <typename _T, typename _Cmp> 
    void blit_merge_block(_T *array, _T *swap, size_t swap_size, size_t lblock, size_t right, _Cmp cmp)
    {
        size_t left, rblock;
    
        if (cmp(*(array + lblock - 1), *(array + lblock)) <= 0)
        {
            return;
        }
    
        rblock = lblock / 2;
        lblock -= rblock;
    
        left = monobound_binary_first<_T, _Cmp>(array + lblock + rblock, array + lblock, right, cmp);
    
        right -= left;
    
        if (left)
        {
            trinity_rotation<_T, _Cmp>(array + lblock, swap, swap_size, rblock + left, rblock);
    
            if (left <= swap_size)
            {
                partial_backward_merge<_T, _Cmp>(array, swap, lblock + left, lblock, cmp);
            }
            else if (lblock <= swap_size)
            {
                partial_forward_merge<_T, _Cmp>(array, swap, lblock + left, lblock, cmp);
            }
            else
            {
                blit_merge_block<_T, _Cmp>(array, swap, swap_size, lblock, left, cmp);
            }
        }
    
        if (right)
        {
            if (right <= swap_size)
            {
                partial_backward_merge<_T, _Cmp>(array + lblock + left, swap, rblock + right, rblock, cmp);
            }
            else if (rblock <= swap_size)
            {
                partial_forward_merge<_T, _Cmp>(array + lblock + left, swap, rblock + right, rblock, cmp);
            }
            else
            {
                blit_merge_block<_T, _Cmp>(array + lblock + left, swap, swap_size, rblock, right, cmp);
            }
        }
    }
    
    template <typename _T, typename _Cmp> 
    void blit_merge(_T *array, _T *swap, size_t swap_size, size_t nmemb, size_t block, _Cmp cmp)
    {
        _T *pta, *pte;
    
        pte = array + nmemb;
    
        while (block < nmemb)
        {
            for (pta = array ; pta + block < pte ; pta += block * 2)
            {
                if (pta + block * 2 < pte)
                {
                    blit_merge_block<_T, _Cmp>(pta, swap, swap_size, block, block, cmp);
    
                    continue;
                }
                blit_merge_block<_T, _Cmp>(pta, swap, swap_size, block, pte - pta - block, cmp);
    
                break;
            }
            block *= 2;
        }
    }

    template <typename _T, typename _Cmp> 
    void quadsort_swap(_T *array, _T *swap, size_t swap_size, size_t nmemb, _Cmp cmp)
    {
        if (nmemb < 32)
        {
            tail_swap<_T, _Cmp>(array, nmemb, cmp);
        }
        else if (quad_swap<_T, _Cmp>(array, nmemb, cmp) == 0)
        {
            size_t block;

            block = quad_merge<_T, _Cmp>(array, swap, swap_size, nmemb, 32, cmp);

            blit_merge<_T, _Cmp>(array, swap, swap_size, nmemb, block, cmp);
        }
    }

} // _private

template <typename _T, typename _Cmp> 
void BlitSort(_T *array, size_t count, _Cmp cmp)
{
    if (count < 32)
    {
        return _private::tail_swap<_T, _Cmp>(array, count, cmp);
    }
    else
    {
        constexpr size_t swap_size = 512;
        _T swap[swap_size];
    
        _private::blit_analyze<_T, _Cmp>(array, swap, swap_size, count, cmp);
    }
}
