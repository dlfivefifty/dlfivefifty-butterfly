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
#ifndef BFIO_LOW_RANK_SOURCE_HPP
#define BFIO_LOW_RANK_SOURCE_HPP 1

#include "BFIO/Structures/Data.hpp"
#include "BFIO/Structures/PhaseFunctor.hpp"
#include "BFIO/Tools/Lagrange.hpp"

namespace BFIO
{
    // Low-rank source
    template<typename R,unsigned d,unsigned q>
    class LowRankSource
    {
        const PhaseFunctor<R,d>& _Phi;
        unsigned _N;
        Array<R,d> _x0;
        Array<R,d> _p0;
        PointSet<R,d,q> _pointSet;
        WeightSet<R,d,q> _weightSet;

    public:
        LowRankSource
        ( PhaseFunctor<R,d>& Phi, const unsigned N )
        : _Phi(Phi), _N(N)
        { }

        const Array<R,d>&
        GetSpatialCenter() const
        { return _x0; }

        void 
        SetSpatialCenter( const Array<R,d>& x0 )
        { _x0 = x0; }

        const Array<R,d>&
        GetFreqCenter() const
        { return _p0; }

        void 
        SetFreqCenter( const Array<R,d>& p0 )
        { _p0 = p0; }

        const PointSet<R,d,q>&
        GetPointSet() const
        { return _pointSet; }

        void 
        SetPointSet( const PointSet<R,d,q>& pointSet )
        { _pointSet = pointSet; }

        const WeightSet<R,d,q>&
        GetWeightSet() const
        { return _weightSet; }

        void 
        SetWeightSet( const WeightSet<R,d,q>& weightSet )
        { _weightSet = weightSet; }
        
        std::complex<R> operator()( const Array<R,d>& p );
    };
}

// Implementations
namespace BFIO
{
    template<typename R,unsigned d,unsigned q>
    inline std::complex<R>
    LowRankSource<R,d,q>::operator()( const Array<R,d>& p )
    {
        typedef std::complex<R> C;

        // Convert p to the reference domain of [-1/2,+1/2]^d
        Array<R,d> pRef;
        for( unsigned j=0; j<d; ++j )
            pRef[j] = (p[j]-_p0[j])*_N;

        C value(0.,0.);
        for( unsigned t=0; t<Pow<q,d>::val; ++t )
        {
            R alpha = -TwoPi * _Phi( _x0, _pointSet[t] );
            value += Lagrange<R,d,q>( t, pRef ) * 
                     C( cos(alpha), sin(alpha) ) * _weightSet[t];
        }
        R alpha = TwoPi * _Phi( _x0, p );
        value *= C( cos(alpha), sin(alpha) );
        return value;
    }
}

#endif /* BFIO_LOW_RANK_SOURCE_HPP */

