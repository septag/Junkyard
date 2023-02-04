#pragma once

//
// Scalar and Vector math functions
// Contains vector primitives and vector/fpu math functions,
// Easings:
//      Reference: https://easings.net/
//                 https://github.com/r-lyeh-archived/tween
// Conventions:
//      - The lib prefers Right-Handed system (default API), although there are functions for
//        both LH or RH system for calulating view/projection matrices 
//      - Rotations are CCW 
//      - Matrices are Column-Major, but the representation is row-major.
//          which means:
//              mat->m[r][c] -> which R is the row, and C is column index
//              transform vector (v) by matrix (M) = M.v
//              matrix transforms are multiplied in reverse:
//              Scale->Rotation->Translate = TxRxS
//
// C++ Programmers:
//     Some useful operators for basic vector and matrix arithmatic are
//     included. See the end of MathVector.h
//

#include "MathTypes.h"
#include "MathScalar.h"
#include "MathVector.h"

