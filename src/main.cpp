// iBayes - Elastic Bayesian Inference Framework with Sparse Grid
// Copyright (C) 2015-today Ao Mo-Hellenbrand
//
// All copyrights remain with the respective authors.
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program. If not, see <http://www.gnu.org/licenses/>.

#include "config.h"
#include "model/ForwardModel.hpp"
#include "model/NS.hpp"

#include <mpi.h>
#include <iostream>
#include <cmath>
#include <sys/time.h>

using namespace std;


int main(int argc, char* argv[]) {

	NS* fm = new NS("./input/obstacles_in_flow.dat", 1, 1);

	fm->print_info();

	std::size_t input_size = fm->get_input_size();
	std::size_t output_size = fm->get_output_size();



	fm->sim();


	return 0;
}
