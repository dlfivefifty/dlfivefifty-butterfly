/*
   Copyright 2010 Jack Poulson

   This file is part of ButterflyFIO.

   This program is free software: you can redistribute it and/or modify it under
   the terms of the GNU Lesser General Public License as published by the
   Free Software Foundation; either version 3 of the License, or 
   (at your option) any later version.

   This program is distributed in the hope that it will be useful, but 
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU Lesser General Public License for more details.

   You should have received a copy of the GNU Lesser General Public License
   along with this program. If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef BFIO_UTIL_HPP
#define BFIO_UTIL_HPP 1

// C standard headers
#include <cmath>
#include <cstdlib>

// C++ standard headers
#include <complex>
#include <iostream>
#include <vector>

// Additional library headers
#include "mpi.h"

#include "BFIO/Data.hpp"
#include "BFIO/MPI.hpp"

namespace BFIO
{
    inline bool
    IsPowerOfTwo( unsigned N )
    { return N && !(N & (N-1)); }

    inline unsigned
    Log2( unsigned N )
    { 
        unsigned j = 0;
        while( (N>>j) > 1 )
            ++j;
        return j;
    }

    inline unsigned
    TwiddleBit
    ( unsigned N, unsigned l )
    { return N ^ (1<<l); }

    template<unsigned d>
    inline void
    UnpackIndex
    ( unsigned i, 
      const Array<unsigned,d>& log2BoxesPerDim, Array<unsigned,d>& x )
    {
        unsigned log2BoxesUpToDim = 0;
        for( unsigned j=0; j<d; ++j )
        {
            x[j] = (i>>log2BoxesUpToDim)&((1<<log2BoxesPerDim[j])-1);
            log2BoxesUpToDim += log2BoxesPerDim[j];
        }
    }
}

#endif /* BFIO_UTIL_HPP */

