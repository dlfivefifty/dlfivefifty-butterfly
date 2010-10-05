/*
   ButterflyFIO: a distributed-memory fast algorithm for applying FIOs.
   Copyright (C) 2010 Jack Poulson <jack.poulson@gmail.com>
 
   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.
 
   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.
 
   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/
#ifndef BFIO_GENERAL_FIO_SOURCE_WEIGHT_RECURSION_HPP
#define BFIO_GENERAL_FIO_SOURCE_WEIGHT_RECURSION_HPP 1

#include <cstddef>
#include <cstring>
#include <vector>

#include "bfio/constants.hpp"

#include "bfio/structures/array.hpp"
#include "bfio/structures/weight_grid.hpp"
#include "bfio/structures/weight_grid_list.hpp"

#include "bfio/functors/phase_functor.hpp"

#include "bfio/tools/special_functions.hpp"

#include "bfio/general_fio/context.hpp"

namespace bfio {
namespace general_fio {

template<typename R,std::size_t d,std::size_t q>
void
SourceWeightRecursion
( const general_fio::Context<R,d,q>& context,
  const Plan<d>& plan,
  const PhaseFunctor<R,d>& Phi,
  const std::size_t level,
  const Array<R,d>& x0A,
  const Array<R,d>& p0B,
  const Array<R,d>& wB,
  const std::size_t parentInteractionOffset,
  const WeightGridList<R,d,q>& oldWeightGridList,
        WeightGrid<R,d,q>& weightGrid )
{
    const std::size_t q_to_d = Pow<q,d>::val;
    const std::size_t q_to_2d = Pow<q,2*d>::val;
    
    const std::size_t log2NumMergingProcesses = 
        plan.GetLog2NumMergingProcesses( level );

    // We seek performance by isolating the Lagrangian interpolation as
    // a matrix-vector multiplication
    //
    // To do so, the source weight recursion is broken into 3 steps.
    // 
    // For each child:
    //  1) scale the old weights with the appropriate exponentials
    //  2) accumulate the lagrangian matrix against the scaled weights
    // Finally:
    // 3) scale the accumulated weights
    std::memset( weightGrid.Buffer(), 0, 2*q_to_d*sizeof(R) );

    std::vector<R> phiResults;
    std::vector<R> sinResults;
    std::vector<R> cosResults;
    std::vector< Array<R,d> > xPoint( 1, x0A );
    std::vector< Array<R,d> > pPoints( q_to_d );
    const std::vector<R>& sourceMaps = context.GetSourceMaps();
    const std::vector< Array<R,d> >& sourceChildGrids = 
        context.GetSourceChildGrids();
    for( std::size_t cLocal=0; 
         cLocal<(1u<<(d-log2NumMergingProcesses)); 
         ++cLocal )
    {
        // Step 1
        const std::size_t interactionIndex = parentInteractionOffset + cLocal;
        const std::size_t c = plan.LocalToClusterSourceIndex( level, cLocal );

        // Form the set of p points to evaluate
        {
            R* pPointsBuffer = &(pPoints[0][0]);
            const R* wBBuffer = &wB[0];
            const R* p0BBuffer = &p0B[0];
            const R* sourceChildBuffer = &(sourceChildGrids[c*q_to_d][0]);
            for( std::size_t tPrime=0; tPrime<q_to_d; ++tPrime )
                for( std::size_t j=0; j<d; ++j )
                    pPointsBuffer[tPrime*d+j] = 
                        p0BBuffer[j] + 
                        wBBuffer[j]*sourceChildBuffer[tPrime*d+j];
        }

        // Form the phase factors
        Phi.BatchEvaluate( xPoint, pPoints, phiResults );
        {
            R* phiBuffer = &phiResults[0];
            for( std::size_t tPrime=0; tPrime<q_to_d; ++tPrime )
                phiBuffer[tPrime] *= TwoPi;
        }
        SinCosBatch( phiResults, sinResults, cosResults );

        WeightGrid<R,d,q> scaledWeightGrid;
        {
            R* scaledRealBuffer = scaledWeightGrid.RealBuffer();
            R* scaledImagBuffer = scaledWeightGrid.ImagBuffer();
            const R* cosBuffer = &cosResults[0];
            const R* sinBuffer = &sinResults[0];
            const R* oldRealBuffer = 
                oldWeightGridList[interactionIndex].RealBuffer();
            const R* oldImagBuffer = 
                oldWeightGridList[interactionIndex].ImagBuffer();
            for( std::size_t tPrime=0; tPrime<q_to_d; ++tPrime )
            {
                const R realWeight = oldRealBuffer[tPrime];
                const R imagWeight = oldImagBuffer[tPrime];
                const R realPhase = cosBuffer[tPrime];
                const R imagPhase = sinBuffer[tPrime];
                scaledRealBuffer[tPrime] = 
                    realPhase*realWeight - imagPhase*imagWeight;
                scaledImagBuffer[tPrime] = 
                    imagPhase*realWeight + realPhase*imagWeight;
            }
        }
        
        // Step 2
        Gemm
        ( 'N', 'N', q_to_d, 2, q_to_d, 
          (R)1, &sourceMaps[c*q_to_2d],    q_to_d,
                scaledWeightGrid.Buffer(), q_to_d,
          (R)1,       weightGrid.Buffer(), q_to_d );
    }

    // Step 3
    const std::vector< Array<R,d> >& chebyshevGrid = context.GetChebyshevGrid();
    {
        R* pPointsBuffer = &(pPoints[0][0]);
        const R* wBBuffer = &wB[0];
        const R* p0BBuffer = &p0B[0];
        const R* chebyshevBuffer = &(chebyshevGrid[0][0]);
        for( std::size_t t=0; t<q_to_d; ++t )
            for( std::size_t j=0; j<d; ++j )
                pPointsBuffer[t*d+j] =
                    p0BBuffer[j] + wBBuffer[j]*chebyshevBuffer[t*d+j];
    }
    Phi.BatchEvaluate( xPoint, pPoints, phiResults );
    {
        R* phiBuffer = &phiResults[0];
        for( std::size_t t=0; t<q_to_d; ++t )
            phiBuffer[t] *= -TwoPi;
    }
    SinCosBatch( phiResults, sinResults, cosResults );
    {
        R* realBuffer = weightGrid.RealBuffer();
        R* imagBuffer = weightGrid.ImagBuffer();
        const R* cosBuffer = &cosResults[0];
        const R* sinBuffer = &sinResults[0];
        for( std::size_t t=0; t<q_to_d; ++t )
        {
            const R realPhase = cosBuffer[t];
            const R imagPhase = sinBuffer[t];
            const R realWeight = realBuffer[t];
            const R imagWeight = imagBuffer[t];
            realBuffer[t] = realPhase*realWeight - imagPhase*imagWeight;
            imagBuffer[t] = imagPhase*realWeight + realPhase*imagWeight;
        }
    }
}

} // general_fio
} // bfio

#endif // BFIO_GENERAL_FIO_SOURCE_WEIGHT_RECURSION_HPP

