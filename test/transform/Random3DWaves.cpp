/*
   ButterflyFIO: a distributed-memory fast algorithm for applying FIOs.
   Copyright (C) 2010-2011 Jack Poulson <jack.poulson@gmail.com>
 
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
#include <ctime>
#include <memory>
#include "bfio.hpp"

void 
Usage()
{
    std::cout << "Random3DWaves <N> <M> <bootstrap> <T> <nT>\n" 
              << "  N: power of 2, the source spread in each dimension\n" 
              << "  M: number of random sources to instantiate\n" 
              << "  bootstrapSkip: level to bootstrap to\n"
              << "  T: time to simulate to\n" 
              << "  nT: number of timesteps\n" 
              << std::endl;
}

// Define the dimension of the problem and the order of interpolation
static const std::size_t d = 3;
static const std::size_t q = 5;

template<typename R>
class UpWave : public bfio::Phase<R,d>
{
    R _t;
public:
    UpWave();

    virtual UpWave<R>* Clone() const;

    void SetTime( const R t );
    R GetTime() const;

    virtual R
    operator() 
    ( const bfio::Array<R,d>& x, const bfio::Array<R,d>& p ) const;

    // We can optionally override the batched application for better efficiency
    virtual void
    BatchEvaluate
    ( const std::vector< bfio::Array<R,d> >& xPoints,
      const std::vector< bfio::Array<R,d> >& pPoints,
            std::vector< R                >& results ) const;
};

template<typename R>
class DownWave : public bfio::Phase<R,d>
{
    R _t;
public:
    DownWave();

    virtual DownWave<R>* Clone() const;

    void SetTime( const R t );
    R GetTime() const;

    virtual R
    operator() 
    ( const bfio::Array<R,d>& x, const bfio::Array<R,d>& p ) const;
    
    // We can optionally override the batched application for better efficiency
    virtual void
    BatchEvaluate
    ( const std::vector< bfio::Array<R,d> >& xPoints,
      const std::vector< bfio::Array<R,d> >& pPoints,
            std::vector< R                >& results ) const;
};

template<typename R>
inline
UpWave<R>::UpWave() 
: _t(0) 
{ }

template<typename R>
inline
DownWave<R>::DownWave() 
: _t(0) 
{ }

template<typename R>
inline UpWave<R>*
UpWave<R>::Clone() const
{ return new UpWave<R>(*this); }

template<typename R>
inline DownWave<R>*
DownWave<R>::Clone() const
{ return new DownWave<R>(*this); }

template<typename R>
inline void 
UpWave<R>::SetTime( const R t ) 
{ _t = t; }

template<typename R>
inline void 
DownWave<R>::SetTime( const R t ) 
{ _t = t; }

template<typename R>
inline R 
UpWave<R>::GetTime() const 
{ return _t; }

template<typename R>
inline R 
DownWave<R>::GetTime() const 
{ return _t; }

template<typename R>
inline R
UpWave<R>::operator() 
( const bfio::Array<R,d>& x, const bfio::Array<R,d>& p ) const
{ 
    return bfio::TwoPi*( 
             x[0]*p[0]+x[1]*p[1]+x[2]*p[2] + 
             _t * sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2])
           );
}

template<typename R>
inline R
DownWave<R>::operator() 
( const bfio::Array<R,d>& x, const bfio::Array<R,d>& p ) const
{ 
    return bfio::TwoPi*(
             x[0]*p[0]+x[1]*p[1]+x[2]*p[2] -
             _t * sqrt(p[0]*p[0]+p[1]*p[1]+p[2]*p[2])
           );
}

template<typename R>
void
UpWave<R>::BatchEvaluate
( const std::vector< bfio::Array<R,d> >& xPoints,
  const std::vector< bfio::Array<R,d> >& pPoints,
        std::vector< R                >& results ) const
{
    const std::size_t xSize = xPoints.size();
    const std::size_t pSize = pPoints.size();

    // Set up the square root arguments
    std::vector<R> sqrtArguments( pSize );
    {
        R* RESTRICT sqrtArgBuffer = &sqrtArguments[0];
        const R* RESTRICT pPointsBuffer = &(pPoints[0][0]);
        for( std::size_t j=0; j<pSize; ++j )
            sqrtArgBuffer[j] = pPointsBuffer[j*d+0]*pPointsBuffer[j*d+0] +
                               pPointsBuffer[j*d+1]*pPointsBuffer[j*d+1] +
                               pPointsBuffer[j*d+2]*pPointsBuffer[j*d+2];
    }

    // Perform the batched square roots
    std::vector<R> sqrtResults;
    bfio::SqrtBatch( sqrtArguments, sqrtResults );

    // Scale the square roots by _t
    {
        R* sqrtBuffer = &sqrtResults[0];
        for( std::size_t j=0; j<pSize; ++j )
            sqrtBuffer[j] *= _t;
    }

    // Form the final results
    results.resize( xSize*pSize );
    {
        R* RESTRICT resultsBuffer = &results[0];
        const R* RESTRICT sqrtBuffer = &sqrtResults[0];
        const R* RESTRICT xPointsBuffer = &(xPoints[0][0]);
        const R* RESTRICT pPointsBuffer = &(pPoints[0][0]);
        for( std::size_t i=0; i<xSize; ++i )
        {
            for( std::size_t j=0; j<pSize; ++j )
            {
                resultsBuffer[i*pSize+j] = 
                    xPointsBuffer[i*d+0]*pPointsBuffer[j*d+0] + 
                    xPointsBuffer[i*d+1]*pPointsBuffer[j*d+1] +
                    xPointsBuffer[i*d+2]*pPointsBuffer[j*d+2] +
                    sqrtBuffer[j];
                resultsBuffer[i*pSize+j] *= bfio::TwoPi;
            }
        }
    }
}

template<typename R>
void
DownWave<R>::BatchEvaluate
( const std::vector< bfio::Array<R,d> >& xPoints,
  const std::vector< bfio::Array<R,d> >& pPoints,
        std::vector< R                >& results ) const
{
    const std::size_t xSize = xPoints.size();
    const std::size_t pSize = pPoints.size();

    // Set up the square root arguments
    std::vector<R> sqrtArguments( pSize );
    {
        R* sqrtArgBuffer = &sqrtArguments[0];
        const R* pPointsBuffer = &(pPoints[0][0]);
        for( std::size_t j=0; j<pSize; ++j )
            sqrtArgBuffer[j] = pPointsBuffer[j*d+0]*pPointsBuffer[j*d+0] +
                               pPointsBuffer[j*d+1]*pPointsBuffer[j*d+1] +
                               pPointsBuffer[j*d+2]*pPointsBuffer[j*d+2];
    }

    // Perform the batched square roots
    std::vector<R> sqrtResults;
    bfio::SqrtBatch( sqrtArguments, sqrtResults );

    // Scale the square roots by _t
    {
        R* sqrtBuffer = &sqrtResults[0];
        for( std::size_t j=0; j<pSize; ++j )
            sqrtBuffer[j] *= _t;
    }

    // Form the final results
    results.resize( xSize*pSize );
    {
        R* resultsBuffer = &results[0];
        const R* sqrtBuffer = &sqrtResults[0];
        const R* xPointsBuffer = &(xPoints[0][0]);
        const R* pPointsBuffer = &(pPoints[0][0]);
        for( std::size_t i=0; i<xSize; ++i )
        {
            for( std::size_t j=0; j<pSize; ++j )
            {
                resultsBuffer[i*pSize+j] = 
                    xPointsBuffer[i*d+0]*pPointsBuffer[j*d+0] + 
                    xPointsBuffer[i*d+1]*pPointsBuffer[j*d+1] +
                    xPointsBuffer[i*d+2]*pPointsBuffer[j*d+2] -
                    sqrtBuffer[j];
                resultsBuffer[i*pSize+j] *= bfio::TwoPi;
            }
        }
    }
}

int
main
( int argc, char* argv[] )
{
    MPI_Init( &argc, &argv );

    int rank, numProcesses;
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_rank( comm, &rank );
    MPI_Comm_size( comm, &numProcesses );

    if( argc != 6 )
    {
        if( rank == 0 )
            Usage();
        MPI_Finalize();
        return 0;
    }
    const std::size_t N = atoi(argv[1]);
    const std::size_t M = atoi(argv[2]);
    const std::size_t bootstrapSkip = atoi(argv[3]);
    const double T = atof(argv[4]);
    const std::size_t nT = atoi(argv[5]);

    try 
    {
        // Define the source and target boxes
        bfio::Box<double,d> sourceBox, targetBox; 
        for( std::size_t j=0; j<d; ++j )
        {
            sourceBox.offsets[j] = -0.5*N;
            sourceBox.widths[j] = N;
            targetBox.offsets[j] = 0;
            targetBox.widths[j] = 1;
        }

        // Set up the general strategy for the forward transform
        bfio::Plan<d> plan( comm, bfio::FORWARD, N, bootstrapSkip );
        bfio::Box<double,d> mySourceBox = 
            plan.GetMyInitialSourceBox( sourceBox );

        if( rank == 0 )
        {
            std::ostringstream msg;
            msg << "Will distribute " << M << " random sources over the source "
                << "domain, which will be split into " << N
                << " boxes in each of the " << d << " dimensions and "
                << "distributed amongst " << numProcesses << " processes. "
                << "The simulation will be over " << T << " units of time with "
                << nT << " timesteps.\n";
            std::cout << msg.str() << std::endl;
        }

        // Consistently seed all of the processes' PRNGs
        long seed;
        if( rank == 0 )
            seed = time(0);
        MPI_Bcast( &seed, 1, MPI_LONG, 0, comm );
        srand( seed );

        // Now generate random sources in our frequency box
        std::size_t numLocalSources = 
            ( rank<(int)(M%numProcesses) 
              ? M/numProcesses+1 : M/numProcesses );
        std::vector< bfio::Source<double,d> > mySources( numLocalSources );
        for( std::size_t i=0; i<numLocalSources; ++i )
        {
            for( std::size_t j=0; j<d; ++j )
            {
                mySources[i].p[j] = 
                    mySourceBox.offsets[j] +
                    bfio::Uniform<double>()*mySourceBox.widths[j];
            }
            mySources[i].magnitude = 200*bfio::Uniform<double>()-100;
        }

        // Set up our phase functors
        UpWave<double> upWave;
        DownWave<double> downWave;

        // Create the context 
        if( rank == 0 )
            std::cout << "Creating context..." << std::endl;
        bfio::rfio::Context<double,d,q> context;

        // Loop over each timestep, computing in parallel, gathering the 
        // results, and then dumping to file
        double deltaT = T/(nT-1);
        for( std::size_t i=0; i<nT; ++i )
        {
            const double t = i*deltaT;
            upWave.SetTime( t );
            downWave.SetTime( t );

            std::auto_ptr
            < const bfio::rfio::PotentialField<double,d,q> > u;
            if( rank == 0 )
            {
                std::cout << "t=" << t << "\n"
                          << "  Starting upWave transform...";
                std::cout.flush();
            }
            u = bfio::ReducedFIO
            ( context, plan, upWave, sourceBox, targetBox, mySources );
            if( rank == 0 )
                std::cout << "done" << std::endl;
#ifdef TIMING
            if( rank == 0 )
                bfio::rfio::PrintTimings();
#endif

            std::auto_ptr
            < const bfio::rfio::PotentialField<double,d,q> > v;
            if( rank == 0 )
            {
                std::cout << "  Starting downWave transform...";
                std::cout.flush();
            }
            v = bfio::ReducedFIO
            ( context, plan, downWave, sourceBox, targetBox, mySources );
            if( rank == 0 )
                std::cout << "done" << std::endl;
#ifdef TIMING
            if( rank == 0 )
                bfio::rfio::PrintTimings();
#endif

            // Store this timeslice
            std::ostringstream fileStream;
            fileStream << "randomWaves-" << i;
            bfio::rfio::WriteVtkXmlPImageData
            ( comm, N, targetBox, *u, fileStream.str() );
        }
    }
    catch( const std::exception& e )
    {
        std::ostringstream msg;
        msg << "Caught exception on process " << rank << ":\n"
            << "   " << e.what();
        std::cout << msg.str() << std::endl;
    }

    MPI_Finalize();
    return 0;
}

