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
#ifndef BFIO_PHASE_FUNCTOR_HPP
#define BFIO_PHASE_FUNCTOR_HPP 1

#include "BFIO/Structures/Data.hpp"

namespace BFIO
{
    // You will need to derive from this class and override the operator()
    template<typename R,unsigned d>
    class PhaseFunctor
    {
    public:
        virtual inline ~PhaseFunctor() {}
        virtual R operator() 
        ( const Array<R,d>& x, const Array<R,d>& p ) const = 0;
    };
}

#endif /* BFIO_PHASE_FUNCTOR_HPP */
