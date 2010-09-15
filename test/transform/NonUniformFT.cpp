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
using namespace std;
using namespace bfio;

void 
Usage()
{
    cout << "NonUniformFT <N> <M> <testAccuracy?>" << endl;
    cout << "  N: power of 2, the frequency spread in each dimension" << endl;
    cout << "  M: number of random sources to instantiate" << endl;
    cout << "  testAccuracy?: tests accuracy iff 1" << endl;
    cout << endl;
}

// Define the dimension of the problem and the order of interpolation
static const unsigned d = 2;
static const unsigned q = 8;

// If we test the accuracy, define the number of tests to perform per box
static const unsigned numTestsPerBox = 10;

template<typename R>
class Unity : public AmplitudeFunctor<R,d>
{
public:
    complex<R>
    operator() ( const Array<R,d>& x, const Array<R,d>& p ) const
    { return complex<R>(1); }
};

template<typename R>
class Fourier : public PhaseFunctor<R,d>
{
public:
    R
    operator() ( const Array<R,d>& x, const Array<R,d>& p ) const
    {
        return x[0]*p[0]+x[1]*p[1];
    }
};

int
main
( int argc, char* argv[] )
{
    MPI_Init( &argc, &argv );

    int rank, numProcesses;
    MPI_Comm comm = MPI_COMM_WORLD;
    MPI_Comm_rank( comm, &rank );
    MPI_Comm_size( comm, &numProcesses );

    if( !IsPowerOfTwo(numProcesses) )
    {
        if( rank == 0 )
            cout << "Must run with a power of two number of cores." << endl;
        MPI_Finalize();
        return 0;
    }

    if( argc != 4 )
    {
        if( rank == 0 )
            Usage();
        MPI_Finalize();
        return 0;
    }
    const unsigned N = atoi(argv[1]);
    const unsigned M = atoi(argv[2]);
    const bool testAccuracy = atoi(argv[3]);

    const unsigned log2N = Log2( N );
    const unsigned log2NumProcesses = Log2( numProcesses );
    if( log2NumProcesses > d*log2N )
    {
        if( rank == 0 )
            cout << "Cannot run with more than N^d processes." << endl;
        MPI_Finalize();
        return 0;
    }

    // Set our spatial and frequency boxes
    Box<double,d> freqBox, spatialBox;
    for( unsigned j=0; j<d; ++j )
    {
        freqBox.offsets[j] = -0.5*N;
        freqBox.widths[j] = N;
        spatialBox.offsets[j] = 0;
        spatialBox.widths[j] = 1;
    }

    if( rank == 0 )
    {
        ostringstream msg;
        msg << "Will distribute " << M << " random sources over the frequency " 
            << "domain, which will be split into " << N 
            << " boxes in each of the " << d << " dimensions and distributed "
            << "amongst " << numProcesses << " processes." << endl << endl;
        cout << msg.str();
    }

    try 
    {
        // Consistently randomly seed all of the processes' PRNG.
        long seed;
        if( rank == 0 )
            seed = time(0);
        MPI_Bcast( &seed, 1, MPI_LONG, 0, comm );
        srand( seed );

        // Compute the box that our process owns within the frequency box
        Box<double,d> myFreqBox;
        LocalFreqPartitionData( freqBox, myFreqBox, comm );

        // Now generate random sources across the domain and store them in 
        // our local list when appropriate
        double L1Sources = 0;
        vector< Source<double,d> > mySources;
        vector< Source<double,d> > globalSources;
        if( testAccuracy )
        {
            globalSources.resize( M );
            for( unsigned i=0; i<M; ++i )
            {
                for( unsigned j=0; j<d; ++j )
                {
                    globalSources[i].p[j] = freqBox.offsets[j] + 
                        freqBox.widths[j]*Uniform<double>(); 
                }
                globalSources[i].magnitude = 1.*(2*Uniform<double>()-1); 
                L1Sources += abs(globalSources[i].magnitude);

                // Check if we should push this source onto our local list
                bool isMine = true;
                for( unsigned j=0; j<d; ++j )
                {
                    double u = globalSources[i].p[j];
                    double start = myFreqBox.offsets[j];
                    double stop = myFreqBox.offsets[j] + myFreqBox.widths[j];
                    if( u < start || u >= stop )
                        isMine = false;
                }
                if( isMine )
                    mySources.push_back( globalSources[i] );
            }
        }
        else
        {
            unsigned numLocalSources = 
                ( rank<(int)(M%numProcesses) 
                  ? M/numProcesses+1 : M/numProcesses );
            mySources.resize( numLocalSources );
            for( unsigned i=0; i<numLocalSources; ++i )
            {
                for( unsigned j=0; j<d; ++j )
                {
                    mySources[i].p[j] = myFreqBox.offsets[j] + 
                                        Uniform<double>()*myFreqBox.widths[j];
                }
                mySources[i].magnitude = 1.*(2*Uniform<double>()-1);
                L1Sources += abs(mySources[i].magnitude);
            }
        }

        // Set up our amplitude and phase functors
        Unity<double> unity;
        Fourier<double> fourier;

        // Create vectors for storing the results
        unsigned numLocalLRPs = NumLocalBoxes<d>( N, comm );
        vector< LowRankPotential<double,d,q> > myFourierLRPs
        ( numLocalLRPs, LowRankPotential<double,d,q>(unity,fourier) );

        // Run the algorithm
        MPI_Barrier( comm );
        double startTime = MPI_Wtime();
        FreqToSpatial
        ( N, freqBox, spatialBox, mySources, myFourierLRPs, comm );
        MPI_Barrier( comm );
        double stopTime = MPI_Wtime();
        if( rank == 0 )
            cout << "Runtime: " << stopTime-startTime << " seconds." << endl;

        if( testAccuracy )
        {
            // Compute the relative error using 256 random samples on the grid 
            // as in Candes et al.'s ButterflyFIO paper.
            double myL2ErrorSquared256 = 0;
            double myL2TruthSquared256 = 0;
            double myLinfError256 = 0;
            for( unsigned s=0; s<256; ++s )
            {
                unsigned k = rand() % myFourierLRPs.size();

                // Retrieve the spatial center of LRP k
                Array<double,d> x0 = myFourierLRPs[k].GetSpatialCenter();
                
                // Evaluate our LRP at x0 and compare against truth
                complex<double> u = myFourierLRPs[k]( x0 );
                complex<double> uTruth(0,0);
                for( unsigned m=0; m<globalSources.size(); ++m )
                {
                    complex<double> beta = 
                        ImagExp( TwoPi*fourier(x0,globalSources[m].p) );
                    uTruth += beta * globalSources[m].magnitude;
                }
                double absError = abs(u-uTruth);
                double absTruth = abs(uTruth);
                myL2ErrorSquared256 += absError*absError;
                myL2TruthSquared256 += absTruth*absTruth;
                myLinfError256 = max( myLinfError256, absError );
            }

            double L2ErrorSquared256;
            double L2TruthSquared256;
            double LinfError256;
            MPI_Reduce
            ( &myL2ErrorSquared256, &L2ErrorSquared256, 1, MPI_DOUBLE, 
              MPI_SUM, 0, comm );
            MPI_Reduce
            ( &myL2TruthSquared256, &L2TruthSquared256, 1, MPI_DOUBLE, 
              MPI_SUM, 0, comm );
            MPI_Reduce
            ( &myLinfError256, &LinfError256, 1, MPI_DOUBLE, MPI_MAX, 0, comm );
            if( rank == 0 )
            {   
                cout << endl;
                cout << "256 samples " << endl;
                cout << "---------------------------------------------" << endl;
                cout << "Estimate of relative ||e||_2:    "
                     << sqrt(L2ErrorSquared256/L2TruthSquared256) << endl;
                cout << "Estimate of ||e||_inf:           "                     
                     << LinfError256 << endl;
                cout << "||f||_1:                         "
                     << L1Sources << endl;
                cout << "Estimate of ||e||_inf / ||f||_1: "
                     << LinfError256/L1Sources << endl;
                cout << endl;
            }

            // Compute error estimates using a constant number of samples within
            // each box in the resulting approximation of the transform.
            // 
            // We also have each process generate a file with its true solution,
            // approximate solution, and error.
            ofstream realTruthFile, imagTruthFile, 
                     realApproxFile, imagApproxFile, 
                     absErrorFile;
            {
                ostringstream basenameStream;
                basenameStream << "nuft-N=" << N << "-" << "q=" << q 
                    << "-rank=" << rank;
                string basename = basenameStream.str();
                string realTruthName = basename + "-realTruth.dat";
                string imagTruthName = basename + "-imagTruth.dat";
                string realApproxName = basename + "-realApprox.dat";
                string imagApproxName = basename + "-imagApprox.dat";
                string absErrorName = basename + "-absError.dat";

                realTruthFile.open( realTruthName.c_str() );
                imagTruthFile.open( imagTruthName.c_str() );
                realApproxFile.open( realApproxName.c_str() );
                imagApproxFile.open( imagApproxName.c_str() );
                absErrorFile.open( absErrorName.c_str() );
            }
            double myL2ErrorSquared = 0;
            double myL2TruthSquared = 0;
            double myLinfError = 0;
            for( unsigned k=0; k<myFourierLRPs.size(); ++k )
            {
                // Retrieve the spatial center of LRP k
                Array<double,d> x0 = 
                    myFourierLRPs[k].GetSpatialCenter();

                for( unsigned s=0; s<numTestsPerBox; ++s )
                {
                    // Find a random point in that box
                    Array<double,d> x;
                    for( unsigned j=0; j<d; ++j )
                    {
                        x[j] = x0[j] + spatialBox.widths[j] /
                                       (2*N)*(2*Uniform<double>()-1.);
                    }

                    // Evaluate our LRP at x  and compare against truth
                    complex<double> u = myFourierLRPs[k]( x );
                    complex<double> uTruth(0.,0.);
                    for( unsigned m=0; m<globalSources.size(); ++m )
                    {
                        complex<double> beta = 
                            ImagExp( TwoPi*fourier(x,globalSources[m].p) );
                        uTruth += beta * globalSources[m].magnitude;
                    }
                    double absError = abs(u-uTruth);
                    double absTruth = abs(uTruth);
                    myL2ErrorSquared += absError*absError;
                    myL2TruthSquared += absTruth*absTruth;
                    myLinfError = max( myLinfError, absError );

                    // Write to our files in "X Y Z" format
                    for( unsigned j=0; j<d; ++j )
                    {
                        realTruthFile << x[j] << " ";
                        imagTruthFile << x[j] << " ";
                        realApproxFile << x[j] << " ";
                        imagApproxFile << x[j] << " ";
                        absErrorFile << x[j] << " ";
                    }
                    realTruthFile << real(uTruth) << endl;
                    imagTruthFile << imag(uTruth) << endl;
                    realApproxFile << real(u) << endl;
                    imagApproxFile << imag(u) << endl;
                    absErrorFile << absError << endl;
                }
            }
            realTruthFile.close();
            imagTruthFile.close();
            realApproxFile.close();
            imagApproxFile.close();
            absErrorFile.close();

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
                cout << "O(N^d) samples " << endl;
                cout << "---------------------------------------------" << endl;
                cout << "Estimate of relative ||e||_2:    "
                     << sqrt(L2ErrorSquared/L2TruthSquared) << endl;
                cout << "Estimate of ||e||_inf:           "                     
                     << LinfError << endl;
                cout << "||f||_1:                         "
                     << L1Sources << endl;
                cout << "Estimate of ||e||_inf / ||f||_1: "
                     << LinfError/L1Sources << endl;
            }
        }
    }
    catch( const exception& e )
    {
        ostringstream msg;
        msg << "Caught exception on process " << rank << ":" << endl;
        msg << "   " << e.what() << endl;
        cout << msg.str();
    }

    MPI_Finalize();
    return 0;
}

