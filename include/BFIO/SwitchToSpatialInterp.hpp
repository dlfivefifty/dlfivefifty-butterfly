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
#ifndef BFIO_SWITCH_TO_SPATIAL_INTERP_HPP
#define BFIO_SWITCH_TO_SPATIAL_INTERP_HPP 1

#include "BFIO/Lagrange.hpp"

namespace BFIO
{
    using namespace std;

    template<typename Psi,typename R,unsigned d,unsigned q>
    void
    SwitchToSpatialInterp
    ( const unsigned L, 
      const unsigned s,
      const unsigned log2LocalFreqBoxes,
      const unsigned log2LocalSpatialBoxes,
      const Array<unsigned,d>& log2LocalFreqBoxesPerDim,
      const Array<unsigned,d>& log2LocalSpatialBoxesPerDim,
      const Array<R,d>& myFreqBoxOffsets,
      const Array<R,d>& mySpatialBoxOffsets,
      const vector< Array<R,d> >& chebyGrid,
            vector< vector< complex<R> > >& weights         )
    {
        typedef complex<R> C;
        const unsigned N = 1u<<L;

        // Compute the width of the nodes at level l
        const unsigned l = L/2;
        const R wA = static_cast<R>(1) / static_cast<R>(1u<<l);
        const R wB = static_cast<R>(1) / static_cast<R>(1u<<(L-l));
        vector< vector< complex<R> > > oldWeights = weights;
        for( unsigned i=0; i<(1u<<log2LocalSpatialBoxes); ++i )
        {
            // Compute the coordinates and center of this spatial box
            Array<R,d> x0A;
            Array<unsigned,d> A;
            for( unsigned j=0; j<d; ++j )
            {
                static unsigned log2LocalSpatialBoxesUpToDim = 0;
                // A[j] = (i/localSpatialBoxesUpToDim) % 
                //        localSpatialBoxesPerDim[j]
                A[j] = (i>>log2LocalSpatialBoxesUpToDim) &
                       ((1u<<log2LocalSpatialBoxesPerDim[j])-1);
                x0A[j] = myFreqBoxOffsets[j] + A[j]*wA + wA/2;

                log2LocalSpatialBoxesUpToDim += 
                    log2LocalSpatialBoxesPerDim[j];
            }

            static vector< Array<R,d> > xPoints( Power<q,d>::value );
            for( unsigned t=0; t<Power<q,d>::value; ++t )
                for( unsigned j=0; j<d; ++j )
                    xPoints[t][j] = x0A[j] + wA*chebyGrid[t][j];

            for( unsigned k=0; k<(1u<<log2LocalFreqBoxes); ++k )
            {
                // Compute the coordinates and center of this freq box
                Array<R,d> p0B;
                Array<unsigned,d> B;
                for( unsigned j=0; j<d; ++j )
                {
                    static unsigned log2LocalFreqBoxesUpToDim = 0;
                    B[j] = (k>>log2LocalFreqBoxesUpToDim) &
                           ((1u<<log2LocalFreqBoxesPerDim[j])-1);
                    p0B[j] = mySpatialBoxOffsets[j] + B[j]*wB + wB/2;

                    log2LocalFreqBoxesUpToDim += log2LocalFreqBoxesPerDim[j];
                }

                static vector< Array<R,d> > pPoints( Power<q,d>::value );
                for( unsigned t=0; t<Power<q,d>::value; ++t )
                    for( unsigned j=0; j<d; ++j )
                        pPoints[t][j] = p0B[j] + wB*chebyGrid[t][j];

                const unsigned key = k+i*(1u<<log2LocalFreqBoxes);
                for( unsigned t=0; t<Power<q,d>::value; ++t )
                {
                    weights[key][t] = 0;
                    for( unsigned tp=0; tp<Power<q,d>::value; ++tp )
                    {
                        R alpha = TwoPi*N*Psi::Eval(xPoints[t],pPoints[s]);
                        weights[key][t] += C( cos(alpha), sin(alpha) ) *
                                           oldWeights[key][tp];
                    }
                }
            }
        }
    }
}

#endif /* BFIO_SWITCH_TO_SPATIAL_INTERP_HPP */

