#pragma once

//
// Scalar and Vector math functions
// Contains vector primitives and vector/fpu math functions,
// Individual files:
//      MathTypes: Basic declarations for math primitives. Include this mainly in other headers
//      MathScalar: Scalar math functions: sqrt/sin/cos/Lerp/etc.
//      MathVector: Functions and operators for math primitives: Vector/Matrix/Quaternion/Rect
//
// Easings:
//      Reference: https://easings.net/
//                 https://github.com/r-lyeh-archived/tween
// Conventions:
//      - The lib prefers Right-Handed system (default API), although there are functions for
//        both LH or RH system for calulating view/projection matrices 
//      - Rotations are CCW (right thumb for the rotation axis, then fold your fingers around your thumb)
//      - Matrices are Column-Major, but the representation is row-major.
//          which means:
//              mat->mRC -> which R is the row, and C is column index
//              transform vector (v) by matrix (M) = M.v
//              matrix transforms are multiplied in reverse:
//              Vector transform: Vector x Scale->Rotation->Translate = TxRxSxv
//
// 3D coordinate system: Prefered is the Right-handed - Z UP
// Example: pass kFloat3UnitZ to mat4ViewLookAt's up vector
//      
//            +z
//            ^   ^ +y
//            |  /
//            | /
//            |/      
//            ■-----> +x
//
// 2D coordinate system: Prefered is the Y UP
//
//            +y
//            ^ 
//            |
//            |      
//            ■-----> +x
//
// Vulkan NDC vs D3D NDC (also referenced in mat4Perspective/mat4Ortho functions): 
// +Z goes into the screen for both. Normalized between [0, 1]
// 
// Vulkan:                          
//  (-1, -1)                   
//         +-----+-----+       
//         |     |     |       
//         |     |     |       
//         +-----+-----> +x    
//         |     |     |       
//         |     |     |       
//         +-----v-----+       
//              +y      (1, 1) 
//  
// D3D:
//  (-1, 1)     +y             
//         +-----^-----+       
//         |     |     |       
//         |     |     |       
//         +-----+-----> +x    
//         |     |     |       
//         |     |     |       
//         +-----+-----+       
//                     (1, -1) 
//                    
// C++ operators:
//     Some useful operators for basic vector and matrix arithmatic are
//     included. See the end of MathVector.h
//

#include "MathTypes.h"
#include "MathScalar.h"
#include "MathVector.h"

