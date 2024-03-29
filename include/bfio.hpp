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
#ifndef BFIO_HPP
#define BFIO_HPP 1

// Include the configuration-specific preprocessor definitions
#include "bfio/config.h"

// One could probably speed up compile time by including everything in a 
// dependency-aware order
#include "bfio/constants.hpp"
#include "bfio/structures.hpp"
#include "bfio/tools.hpp"
#include "bfio/functors.hpp"

#include "bfio/rfio.hpp"
#include "bfio/lagrangian_nuft.hpp"
#include "bfio/interpolative_nuft.hpp"

#endif // BFIO_HPP 
