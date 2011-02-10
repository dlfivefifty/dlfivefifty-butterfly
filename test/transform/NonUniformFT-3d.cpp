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
#include <ctime>
#include <fstream>
#include <memory>
#include "bfio.hpp"

void 
Usage()
{
    std::cout << "NonUniformFT-3d <N> <M> <testAccuracy?> <store?>\n" 
              << "  N: power of 2, the source spread in each dimension\n" 
              << "  M: number of random sources to instantiate\n" 
              << "  bootstrap: level to bootstrap to\n"
              << "  testAccuracy?: tests accuracy iff 1\n" 
              << "  store?: creates data files iff 1\n" 
              << std::endl;
}

// Define the dimension of the problem and the order of interpolation
static const std::size_t d = 3;
static const std::size_t q = 5;

// If we test the accuracy, define the number of tests to perform per box
static const std::size_t numAccuracyTestsPerBox = 10;

// If we store the results, define the number of samples per box per dim.
static const std::size_t numSamplesPerBoxDim = 5;
static const std::size_t numSamplesPerBox = 
    bfio::Pow<numSamplesPerBoxDim,d>::val;

template<typename R>
class Fourier : public bfio::PhaseFunctor<R,d>
{
public:
    // This is the only routines that must be implemented
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

// This is the only routines that must be implemented
template<typename R>
inline R
Fourier<R>::operator() 
( const bfio::Array<R,d>& x, const bfio::Array<R,d>& p ) const
{ return x[0]*p[0]+x[1]*p[1]+x[2]*p[2]; }

// We can optionally override the batched application for better efficiency
template<typename R>
void
Fourier<R>::BatchEvaluate
( const std::vector< bfio::Array<R,d> >& xPoints,
  const std::vector< bfio::Array<R,d> >& pPoints,
        std::vector< R                >& results ) const
{
    const std::size_t nxPoints = xPoints.size();
    const std::size_t npPoints = pPoints.size();
    results.resize( nxPoints*npPoints );

    R* resultsBuffer = &results[0];
    const R* xPointsBuffer = &(xPoints[0][0]);
    const R* pPointsBuffer = &(pPoints[0][0]);
    for( std::size_t i=0; i<nxPoints; ++i )
        for( std::size_t j=0; j<npPoints; ++j )
            resultsBuffer[i*npPoints+j] = 
                xPointsBuffer[i*d+0]*pPointsBuffer[j*d+0] + 
                xPointsBuffer[i*d+1]*pPointsBuffer[j*d+1] + 
                xPointsBuffer[i*d+2]*pPointsBuffer[j*d+2];
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
    const bool testAccuracy = atoi(argv[4]);
    const bool store = atoi(argv[5]);

    try 
    {
        // Set our source and target boxes
        bfio::Box<double,d> sourceBox, targetBox;
        for( std::size_t j=0; j<d; ++j )
        {
            sourceBox.offsets[j] = -0.5*N;
            sourceBox.widths[j] = N;
            targetBox.offsets[j] = 0;
            targetBox.widths[j] = 1;
        }

        // Set up the general strategy for the forward transform
        bfio::ForwardPlan<d> plan( comm, N, bootstrapSkip );
        //bfio::AdjointPlan<d> plan( comm, N, bootstrapSkip );
        bfio::Box<double,d> mySourceBox = 
            plan.GetMyInitialSourceBox( sourceBox );

        if( rank == 0 )
        {
            std::ostringstream msg;
            msg << "Will distribute " << M << " random sources over the source "
                << "domain, which will be split into " << N 
                << " boxes in each of the " << d << " dimensions and "
                << "distributed amongst " << numProcesses << " processes.\n";
            std::cout << msg.str() << std::endl;
        }

        // Consistently randomly seed all of the processes' PRNG.
        long seed;
        if( rank == 0 )
            seed = time(0);
        MPI_Bcast( &seed, 1, MPI_LONG, 0, comm );
        srand( seed );

        // Now generate random sources across the domain and store them in 
        // our local list when appropriate
        double L1Sources = 0;
        std::vector< bfio::Source<double,d> > mySources;
        std::vector< bfio::Source<double,d> > globalSources;
        if( testAccuracy || store )
        {
            globalSources.resize( M );
            for( std::size_t i=0; i<M; ++i )
            {
                for( std::size_t j=0; j<d; ++j )
                {
                    globalSources[i].p[j] = sourceBox.offsets[j] + 
                        sourceBox.widths[j]*bfio::Uniform<double>(); 
                }
                globalSources[i].magnitude = 1.*(2*bfio::Uniform<double>()-1); 
                L1Sources += std::abs(globalSources[i].magnitude);

                // Check if we should push this source onto our local list
                bool isMine = true;
                for( std::size_t j=0; j<d; ++j )
                {
                    double u = globalSources[i].p[j];
                    double start = mySourceBox.offsets[j];
                    double stop = 
                        mySourceBox.offsets[j] + mySourceBox.widths[j];
                    if( u < start || u >= stop )
                        isMine = false;
                }
                if( isMine )
                    mySources.push_back( globalSources[i] );
            }
        }
        else
        {
            std::size_t numLocalSources = 
                ( rank<(int)(M%numProcesses) 
                  ? M/numProcesses+1 : M/numProcesses );
            mySources.resize( numLocalSources );
            for( std::size_t i=0; i<numLocalSources; ++i )
            {
                for( std::size_t j=0; j<d; ++j )
                {
                    mySources[i].p[j] = 
                        mySourceBox.offsets[j] + 
                        bfio::Uniform<double>()*mySourceBox.widths[j];
                }
                mySources[i].magnitude = 1.*(2*bfio::Uniform<double>()-1);
                L1Sources += std::abs(mySources[i].magnitude);
            }
        }

        // Create a context for Interpolative NUFTs
        if( rank == 0 )
            std::cout << "Creating InterpolativeNUFT context..." << std::endl;
        bfio::interpolative_nuft::Context<double,d,q> 
            interpolativeNuftContext( N, sourceBox, targetBox );

        // Run with the interpolative NUFT
        std::auto_ptr< 
            const bfio::interpolative_nuft::PotentialField<double,d,q> > u;
        if( rank == 0 )
            std::cout << "Starting InterpolativeNUFT..." << std::endl;
        MPI_Barrier( comm );
        double startTime = MPI_Wtime();
        u = bfio::InterpolativeNUFT
        ( interpolativeNuftContext, plan, sourceBox, targetBox, mySources );
        MPI_Barrier( comm );
        double stopTime = MPI_Wtime();
        if( rank == 0 )
        {
            std::cout << "Runtime: " << stopTime-startTime << " seconds.\n"
                      << std::endl;
        }
#ifdef TIMING
	if( rank == 0 )
	    bfio::interpolative_nuft::PrintTimings();
#endif

        // Create a context for NUFTs with Lagrangian interpolation
        /*
        if( rank == 0 )
            std::cout << "Creating LagrangianNUFT context..." << std::endl;
        bfio::lagrangian_nuft::Context<double,d,q> 
            lagrangianNuftContext( N, sourceBox, targetBox );

        // Run with the Lagrangian NUFT
        std::auto_ptr< const bfio::lagrangian_nuft::PotentialField<double,d,q> >
            v;
        if( rank == 0 )
            std::cout << "Starting LagrangianNUFT..." << std::endl;
        MPI_Barrier( comm );
        startTime = MPI_Wtime();
        v = bfio::LagrangianNUFT
        ( lagrangianNuftContext, plan, sourceBox, targetBox, mySources );
        MPI_Barrier( comm );
        stopTime = MPI_Wtime();
        if( rank == 0 )
        {
            std::cout << "Runtime: " << stopTime-startTime << " seconds.\n"
                      << std::endl;
        }
#ifdef TIMING
	if( rank == 0 )
	    bfio::lagrangian_nuft::PrintTimings();
#endif
        */

        // Set up our phase functor
        Fourier<double> fourier;

        // Create a general context 
        if( rank == 0 )
            std::cout << "Creating GeneralFIO context..." << std::endl;
        bfio::general_fio::Context<double,d,q> generalContext;

        // Run the general algorithm
        std::auto_ptr< const bfio::general_fio::PotentialField<double,d,q> > w;
        if( rank == 0 )
            std::cout << "Starting GeneralFIO transform..." << std::endl;
        MPI_Barrier( comm );
        startTime = MPI_Wtime();
        w = bfio::GeneralFIO
        ( generalContext, plan, fourier, sourceBox, targetBox, mySources );
        MPI_Barrier( comm );
        stopTime = MPI_Wtime();
        if( rank == 0 )
        {
            std::cout << "Runtime: " << stopTime-startTime << " seconds.\n" 
                      << std::endl;
        }
#ifdef TIMING
	if( rank == 0 )
	    bfio::general_fio::PrintTimings();
#endif

        if( testAccuracy )
        {
            const bfio::Box<double,d>& myBox = u->GetBox();
            const std::size_t numSubboxes = u->GetNumSubboxes();
            const std::size_t numTests = numSubboxes*numAccuracyTestsPerBox;

            // Compute error estimates using a constant number of samples within
            // each box in the resulting approximation of the transform.
            if( rank == 0 )
            {
                std::cout << "Testing accuracy with O(N^d) samples..." 
                          << std::endl;
            }
            double myL2ErrorSquared = 0;
            double myL2TruthSquared = 0;
            double myLinfError = 0;
            for( std::size_t k=0; k<numTests; ++k )
            {
                // Compute a random point in our process's target box
                bfio::Array<double,d> x;
                for( std::size_t j=0; j<d; ++j )
                    x[j] = myBox.offsets[j] + 
                           bfio::Uniform<double>()*myBox.widths[j];

                // Evaluate our potential field at x and compare against truth
                std::complex<double> approx = u->Evaluate( x );
                std::complex<double> truth(0.,0.);
                for( std::size_t m=0; m<globalSources.size(); ++m )
                {
                    std::complex<double> beta =
                        bfio::ImagExp
                        ( bfio::TwoPi*fourier(x,globalSources[m].p) );
                    truth += beta * globalSources[m].magnitude;
                }
                double absError = std::abs(approx-truth);
                double absTruth = std::abs(truth);

                myL2ErrorSquared += absError*absError;
                myL2TruthSquared += absTruth*absTruth;
                myLinfError = std::max( myLinfError, absError );
            }

            double L2ErrorSquared;
            double L2TruthSquared;
            double LinfError;
            MPI_Reduce
            ( &myL2ErrorSquared, &L2ErrorSquared, 1, MPI_DOUBLE, MPI_SUM, 0,
              comm ); 
            MPI_Reduce
            ( &myL2TruthSquared, &L2TruthSquared, 1, MPI_DOUBLE, MPI_SUM, 0,
              comm );
            MPI_Reduce
            ( &myLinfError, &LinfError, 1, MPI_DOUBLE, MPI_MAX, 0, comm );
            if( rank == 0 )
            {   
                std::cout << "---------------------------------------------\n" 
                          << "Estimate of relative ||e||_2:    "
                          << sqrt(L2ErrorSquared/L2TruthSquared) << "\n"
                          << "Estimate of ||e||_inf:           "  
                          << LinfError << "\n"
                          << "||f||_1:                         "
                          << L1Sources << "\n"
                          << "Estimate of ||e||_inf / ||f||_1: "
                          << LinfError/L1Sources << "\n" << std::endl;
            }
        }

        if( store )
        {
            std::ostringstream basenameStream;
            basenameStream << "nuft3d-N=" << N << "-" << "q=" << q
                           << "-rank=" << rank;
            std::string basename = basenameStream.str();

            // Columns 0-(d-1) contain the coordinates of the sources, 
            // and columns d and d+1 contain the real and complex components of
            // the magnitudes of the sources.
            if( rank == 0 )
                std::cout << "Creating sources file..." << std::endl;
            std::ofstream file;
            file.open( (basename+"-sources.dat").c_str() );
            for( std::size_t i=0; i<globalSources.size(); ++i )
            {
                for( std::size_t j=0; j<d; ++j )
                    file << globalSources[i].p[j] << " ";
                file << std::real(globalSources[i].magnitude) << " "
                     << std::imag(globalSources[i].magnitude) << std::endl;
            }
            file.close();

            // Columns 0-(d-1) contain the coordinates of the samples, 
            // columns d and d+1 contain the real and complex components of 
            // the true solution, d+2 and d+3 contain the real and complex 
            // components of the approximate solution, and columns d+4 and d+5
            // contain the real and complex parts of the error, truth-approx.
            if( rank == 0 )
                std::cout << "Creating results file..." << std::endl;
            file.open( (basename+"-results.dat").c_str() );
            const bfio::Box<double,d>& myBox = u->GetBox();
            const bfio::Array<double,d>& wA = u->GetSubboxWidths();
            const bfio::Array<std::size_t,d>& log2SubboxesPerDim =
                u->GetLog2SubboxesPerDim();
            const std::size_t numSubboxes = u->GetNumSubboxes();
            const std::size_t numSamples = numSamplesPerBox*numSubboxes;

            bfio::Array<std::size_t,d> numSamplesUpToDim;
            for( std::size_t j=0; j<d; ++j )
            {
                numSamplesUpToDim[j] = 1;
                for( std::size_t i=0; i<j; ++i )
                {
                    numSamplesUpToDim[j] *=
                        numSamplesPerBoxDim << log2SubboxesPerDim[i];
                }
            }

            for( std::size_t k=0; k<numSamples; ++k )
            {
                // Extract our indices in each dimension
                bfio::Array<std::size_t,d> coords;
                for( std::size_t j=0; j<d; ++j )
                    coords[j] = (k/numSamplesUpToDim[j]) %
                                (numSamplesPerBoxDim<<log2SubboxesPerDim[j]);

                // Compute the location of our sample
                bfio::Array<double,d> x;
                for( std::size_t j=0; j<d; ++j )
                {
                    x[j] = myBox.offsets[j] +
                           coords[j]*wA[j]/numSamplesPerBoxDim;
                }

                std::complex<double> truth(0,0);
                for( std::size_t m=0; m<globalSources.size(); ++m )
                {
                    std::complex<double> beta =
                        bfio::ImagExp
                        ( bfio::TwoPi*fourier(x,globalSources[m].p) );
                    truth += beta * globalSources[m].magnitude;
                }
                std::complex<double> approx = u->Evaluate( x );
                std::complex<double> error = truth - approx;

                // Write out this sample
                for( std::size_t j=0; j<d; ++j )
                    file << x[j] << " ";
                file << std::real(truth)  << " " << std::imag(truth)  << " "
                     << std::real(approx) << " " << std::imag(approx) << " "
                     << std::real(error)  << " " << std::imag(error)  
                     << std::endl;
            }
            file.close();
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

