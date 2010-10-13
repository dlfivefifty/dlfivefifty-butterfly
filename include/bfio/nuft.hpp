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
#ifndef BFIO_NUFT_HPP
#define BFIO_NUFT_HPP 1

#include <iostream>
#include <memory>
#include <stdexcept>

#include "bfio/structures.hpp"
#include "bfio/tools.hpp"

#include "bfio/nuft/context.hpp"
#include "bfio/nuft/dot_product.hpp"
#include "bfio/nuft/potential_field.hpp"

#include "bfio/general_fio/initialize_weights.hpp"
#include "bfio/general_fio/source_weight_recursion.hpp"
#include "bfio/general_fio/target_weight_recursion.hpp"

#include "bfio/nuft/switch_to_target_interp.hpp"

namespace bfio {

template<typename R,std::size_t d,std::size_t q>
std::auto_ptr< const nuft::PotentialField<R,d,q> >
NUFT
( const nuft::Context<R,d,q>& nuftContext,
  const Plan<d>& plan,
  const Box<R,d>& sourceBox,
  const Box<R,d>& targetBox,
  const std::vector< Source<R,d> >& mySources )
{
    typedef std::complex<R> C;
    const std::size_t q_to_d = Pow<q,d>::val;
    const nuft::DotProduct<R,d> dotProduct;
    const general_fio::Context<R,d,q>& generalContext = 
        nuftContext.GetGeneralContext();

    // Extract our communicator and its size
    MPI_Comm comm = plan.GetComm();
    int rank, numProcesses;
    MPI_Comm_rank( comm, &rank );
    MPI_Comm_size( comm, &numProcesses ); 

    // Get the problem-specific parameters
    const std::size_t N = plan.GetN();
    const std::size_t log2N = Log2( N );
    const Array<std::size_t,d>& myInitialSourceBoxCoords = 
        plan.GetMyInitialSourceBoxCoords();
    const Array<std::size_t,d>& log2InitialSourceBoxesPerDim = 
        plan.GetLog2InitialSourceBoxesPerDim();
    Array<std::size_t,d> mySourceBoxCoords = myInitialSourceBoxCoords;
    Array<std::size_t,d> log2SourceBoxesPerDim = log2InitialSourceBoxesPerDim;
    Box<R,d> mySourceBox;
    for( std::size_t j=0; j<d; ++j )
    {
        mySourceBox.widths[j] = 
            sourceBox.widths[j] / (1u<<log2SourceBoxesPerDim[j]);
        mySourceBox.offsets[j] = 
            sourceBox.offsets[j] + mySourceBox.widths[j]*mySourceBoxCoords[j];
    }

    Array<std::size_t,d> myTargetBoxCoords(0);
    Array<std::size_t,d> log2TargetBoxesPerDim(0);
    Box<R,d> myTargetBox;
    myTargetBox = targetBox;

    // Compute the number of leaf-level boxes in the source domain that 
    // our process is responsible for initializing the weights in. 
    std::size_t log2LocalSourceBoxes = 0;
    std::size_t log2LocalTargetBoxes = 0;
    Array<std::size_t,d> log2LocalSourceBoxesPerDim;
    Array<std::size_t,d> log2LocalTargetBoxesPerDim(0);
    for( std::size_t j=0; j<d; ++j )
    {
        log2LocalSourceBoxesPerDim[j] = log2N-log2SourceBoxesPerDim[j];
        log2LocalSourceBoxes += log2LocalSourceBoxesPerDim[j];
    }

    // Initialize the weights using Lagrangian interpolation on the 
    // smooth component of the kernel.
    WeightGridList<R,d,q> weightGridList( 1<<log2LocalSourceBoxes );
    general_fio::InitializeWeights
    ( generalContext, plan, dotProduct, sourceBox, targetBox, mySourceBox, 
      log2LocalSourceBoxes, log2LocalSourceBoxesPerDim, mySources, 
      weightGridList );

    // Start the main recursion loop
    if( log2N == 0 || log2N == 1 )
    {
        nuft::SwitchToTargetInterp
        ( nuftContext, plan, sourceBox, targetBox, mySourceBox, 
          myTargetBox, log2LocalSourceBoxes, log2LocalTargetBoxes,
          log2LocalSourceBoxesPerDim, log2LocalTargetBoxesPerDim,
          weightGridList );
    }
    for( std::size_t level=1; level<=log2N; ++level )
    {
        // Compute the width of the nodes at this level
        Array<R,d> wA;
        Array<R,d> wB;
        for( std::size_t j=0; j<d; ++j )
        {
            wA[j] = targetBox.widths[j] / (1<<level);
            wB[j] = sourceBox.widths[j] / (1<<(log2N-level));
        }

        if( log2LocalSourceBoxes >= d )
        {
            // Refine target domain and coursen the source domain
            for( std::size_t j=0; j<d; ++j )
            {
                --log2LocalSourceBoxesPerDim[j];
                ++log2LocalTargetBoxesPerDim[j];
            }
            log2LocalSourceBoxes -= d;
            log2LocalTargetBoxes += d;

            // Loop over boxes in target domain. 
            ConstrainedHTreeWalker<d> AWalker( log2LocalTargetBoxesPerDim );
            WeightGridList<R,d,q> oldWeightGridList( weightGridList );
            for( std::size_t targetIndex=0; 
                 targetIndex<(1u<<log2LocalTargetBoxes); 
                 ++targetIndex, AWalker.Walk() )
            {
                const Array<std::size_t,d> A = AWalker.State();

                // Compute coordinates and center of this target box
                Array<R,d> x0A;
                for( std::size_t j=0; j<d; ++j )
                    x0A[j] = myTargetBox.offsets[j] + (A[j]+0.5)*wA[j];

                // Loop over the B boxes in source domain
                ConstrainedHTreeWalker<d> BWalker( log2LocalSourceBoxesPerDim );
                for( std::size_t sourceIndex=0; 
                     sourceIndex<(1u<<log2LocalSourceBoxes); 
                     ++sourceIndex, BWalker.Walk() )
                {
                    const Array<std::size_t,d> B = BWalker.State();

                    // Compute coordinates and center of this source box
                    Array<R,d> p0B;
                    for( std::size_t j=0; j<d; ++j )
                        p0B[j] = mySourceBox.offsets[j] + (B[j]+0.5)*wB[j];

                    // We are storing the interaction pairs source-major
                    const std::size_t interactionIndex = 
                        sourceIndex + (targetIndex<<log2LocalSourceBoxes);

                    // Grab the interaction offset for the parent of target box 
                    // i interacting with the children of source box k
                    const std::size_t parentInteractionOffset = 
                        ((targetIndex>>d)<<(log2LocalSourceBoxes+d)) + 
                        (sourceIndex<<d);

                    if( level <= log2N/2 )
                    {
                        general_fio::SourceWeightRecursion
                        ( generalContext, plan, dotProduct, level, x0A, p0B, wB,
                          parentInteractionOffset, oldWeightGridList,
                          weightGridList[interactionIndex] );
                    }
                    else
                    {
                        Array<R,d> x0Ap;
                        Array<std::size_t,d> globalA;
                        std::size_t ARelativeToAp = 0;
                        for( std::size_t j=0; j<d; ++j )
                        {
                            globalA[j] = 
                                (myTargetBoxCoords[j]<<
                                 log2LocalTargetBoxesPerDim[j])+A[j];
                            x0Ap[j] = targetBox.offsets[j] + 
                                      (globalA[j]|1)*wA[j];
                            ARelativeToAp |= (globalA[j]&1)<<j;
                        }
                        general_fio::TargetWeightRecursion
                        ( generalContext, plan, dotProduct, level,
                          ARelativeToAp, x0A, x0Ap, p0B, wA, wB,
                          parentInteractionOffset, oldWeightGridList, 
                          weightGridList[interactionIndex] );
                    }
                }
            }
        }
        else 
        {
            const std::size_t log2NumMergingProcesses = d-log2LocalSourceBoxes;
            const std::size_t numMergingProcesses = 1u<<log2NumMergingProcesses;

            log2LocalSourceBoxes = 0; 
            for( std::size_t j=0; j<d; ++j )
                log2LocalSourceBoxesPerDim[j] = 0;

            // Fully refine target domain and coarsen source domain.
            // We partition the target domain after the SumScatter.
            const std::vector<std::size_t>& sourceDimsToMerge = 
                plan.GetSourceDimsToMerge( level );
            for( std::size_t i=0; i<log2NumMergingProcesses; ++i )
            {
                const std::size_t j = sourceDimsToMerge[i];
                if( mySourceBoxCoords[j] & 1 )
                    mySourceBox.offsets[j] -= mySourceBox.widths[j];
                mySourceBoxCoords[j] >>= 1;
                mySourceBox.widths[j] *= 2;
            }
            for( std::size_t j=0; j<d; ++j )
            {
                ++log2LocalTargetBoxesPerDim[j];
                ++log2LocalTargetBoxes;
            }

            // Compute the coordinates and center of this source box
            Array<R,d> p0B;
            for( std::size_t j=0; j<d; ++j )
                p0B[j] = mySourceBox.offsets[j] + 0.5*wB[j];

            // Form the partial weights by looping over the boxes in the  
            // target domain.
            ConstrainedHTreeWalker<d> AWalker( log2LocalTargetBoxesPerDim );
            WeightGridList<R,d,q> partialWeightGridList
            ( 1<<log2LocalTargetBoxes );
            for( std::size_t targetIndex=0; 
                 targetIndex<(1u<<log2LocalTargetBoxes); 
                 ++targetIndex, AWalker.Walk() )
            {
                const Array<std::size_t,d> A = AWalker.State();

                // Compute coordinates and center of this target box
                Array<R,d> x0A;
                for( std::size_t j=0; j<d; ++j )
                    x0A[j] = myTargetBox.offsets[j] + (A[j]+0.5)*wA[j];

                // Compute the interaction offset of A's parent interacting 
                // with the remaining local source boxes
                const std::size_t parentInteractionOffset = 
                    ((targetIndex>>d)<<(d-log2NumMergingProcesses));
                if( level <= log2N/2 )
                {
                    general_fio::SourceWeightRecursion
                    ( generalContext, plan, dotProduct, level, x0A, p0B, wB,
                      parentInteractionOffset, weightGridList,
                      partialWeightGridList[targetIndex] );
                }
                else
                {
                    Array<R,d> x0Ap;
                    Array<std::size_t,d> globalA;
                    std::size_t ARelativeToAp = 0;
                    for( std::size_t j=0; j<d; ++j )
                    {
                        globalA[j] = 
                            (myTargetBoxCoords[j]<<
                             log2LocalTargetBoxesPerDim[j])+A[j];
                        x0Ap[j] = targetBox.offsets[j] + (globalA[j]|1)*wA[j];
                        ARelativeToAp |= (globalA[j]&1)<<j;
                    }
                    general_fio::TargetWeightRecursion
                    ( generalContext, plan, dotProduct, level,
                      ARelativeToAp, x0A, x0Ap, p0B, wA, wB,
                      parentInteractionOffset, weightGridList, 
                      partialWeightGridList[targetIndex] );
                }
            }

            // Scatter the summation of the weights
            std::vector<int> recvCounts( numMergingProcesses );
            for( std::size_t j=0; j<numMergingProcesses; ++j )
                recvCounts[j] = 2*weightGridList.Length()*q_to_d;
            // Currently two types of planned communication are supported, as 
            // they are the only required types for transforming and inverting 
            // the transform:
            //  1) partitions of dimensions 0 -> c
            //  2) partitions of dimensions c -> d-1
            // Both 1 and 2 include partitioning 0 -> d-1, but, in general, 
            // the second category never requires packing.
            const std::size_t log2SubclusterSize = 
                plan.GetLog2SubclusterSize( level );
            if( log2SubclusterSize == 0 )
            {
                MPI_Comm clusterComm = plan.GetClusterComm( level );
                SumScatter    
                ( partialWeightGridList.Buffer(), weightGridList.Buffer(),
                  &recvCounts[0], clusterComm );
            }
            else
            {
                const std::size_t log2NumSubclusters = 
                    log2NumMergingProcesses-log2SubclusterSize;
                const std::size_t numSubclusters = 1u<<log2NumSubclusters;
                const std::size_t subclusterSize = 1u<<log2SubclusterSize;

                const std::size_t recvSize = recvCounts[0];
                const std::size_t sendSize = recvSize*numMergingProcesses;
                const std::size_t numChunksPerProcess = subclusterSize;
                const std::size_t chunkSize = recvSize / numChunksPerProcess;
                const R* partialBuffer = partialWeightGridList.Buffer();
                std::vector<R> sendBuffer( sendSize );
                for( std::size_t sc=0; sc<numSubclusters; ++sc )
                {
                    R* subclusterSendBuffer = 
                        &sendBuffer[sc*subclusterSize*recvSize];
                    const R* subclusterPartialBuffer = 
                        &partialBuffer[sc*subclusterSize*recvSize];
                    for( std::size_t p=0; p<subclusterSize; ++p )
                    {
                        R* processSend = &subclusterSendBuffer[p*recvSize];
                        for( std::size_t c=0; c<numChunksPerProcess; ++c )
                        {
                            std::memcpy 
                            ( &processSend[c*chunkSize],
                              &subclusterPartialBuffer
                              [(p+c*subclusterSize)*chunkSize],
                              chunkSize*sizeof(R) );
                        }
                    }
                }
                MPI_Comm clusterComm = plan.GetClusterComm( level );
                SumScatter
                ( &sendBuffer[0], weightGridList.Buffer(), 
                  &recvCounts[0], clusterComm );
            }

            const std::vector<std::size_t>& targetDimsToCut = 
                plan.GetTargetDimsToCut( level );
            const std::vector<bool>& rightSideOfCut = 
                plan.GetRightSideOfCut( level );
            for( std::size_t i=0; i<log2NumMergingProcesses; ++i )
            {
                const std::size_t j = targetDimsToCut[i];
                myTargetBox.widths[j] *= 0.5;
                myTargetBoxCoords[j] *= 2;
                if( rightSideOfCut[i] )
                {
                    myTargetBoxCoords[j] |= 1;
                    myTargetBox.offsets[j] += myTargetBox.widths[j];
                }
                --log2LocalTargetBoxesPerDim[j];
                --log2LocalTargetBoxes;
            }
        }
        if( level==log2N/2 )
        {
            nuft::SwitchToTargetInterp
            ( nuftContext, plan, sourceBox, targetBox, mySourceBox, 
              myTargetBox, log2LocalSourceBoxes, log2LocalTargetBoxes,
              log2LocalSourceBoxesPerDim, log2LocalTargetBoxesPerDim,
              weightGridList );
        }
    }

    // Construct the general FIO PotentialField
    std::auto_ptr< const nuft::PotentialField<R,d,q> > potentialField( 
        new nuft::PotentialField<R,d,q>
            ( nuftContext, sourceBox, myTargetBox, log2LocalTargetBoxesPerDim,
              weightGridList )
    );

    return potentialField;
}

} // bfio

#endif // BFIO_GENERAL_FIO_HPP

