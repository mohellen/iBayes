// eBayes - Elastic Bayesian Inference Framework with iMPI
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

#include <tools/ErrorAnalysis.hpp>

using namespace std;


void ErrorAnalysis::add_test_points(std::size_t n)
{
	std::random_device rd;
	std::mt19937 eng (rd());
	pair<double,double> range;
	std::size_t input_size = cfg.get_input_size();
	test_points.resize(n);
	test_points_data.resize(n);

	for (std::size_t k=0; k < n; ++k) {
		test_points[k].resize(input_size);
		for (std::size_t i=0; i < input_size; ++i) {
			range = fullmodel.get_input_space(i);
			uniform_real_distribution<double> udist (range.first, range.second);
			test_points[k][i] = udist(eng);
		}
		test_points_data[k] = fullmodel.run(test_points[k]);
	}
}

void ErrorAnalysis::add_test_point_at(vector<double> const& m)
{
	test_points.push_back( vector<double>(m.cbegin(), m.cend()) );
	test_points_data.push_back( fullmodel.run(m) );
	return;
}

void ErrorAnalysis::copy_test_points(ErrorAnalysis const& that)
{
	this->test_points = that.test_points;
	this->test_points_data = that.test_points_data;
	return;
}

double ErrorAnalysis::compute_surrogate_error()
{
	if (test_points.size() < 1) {
		cout << tools::red << "Error: no test points added. Program abort." << tools::reset << endl;
		exit(EXIT_FAILURE);
	}
	if (test_points.size() != test_points_data.size()) {
		cout << tools::red << "Error: test points and data mismatch. Program abort." << tools::reset << endl;
		exit(EXIT_FAILURE);
	}
	std::size_t n = test_points.size();
	double sum = 0.0;
	for (int i=0; i < n; ++i) {
		sum += tools::compute_normalizedl2norm(test_points_data[i], surrogate.run(test_points[i]));
	}
	return sum/double(n);
}

double ErrorAnalysis::compute_surrogate_error_at(std::vector<double> const& m)
{
	vector<double> fd = fullmodel.run(m);
	vector<double> sd = surrogate.run(m);
	return tools::compute_normalizedl2norm(fd, sd);
}

bool ErrorAnalysis::mpi_is_model_accurate(double tol)
{
	double err = compute_surrogate_error();
	double mean;
	MPI_Allreduce(&err, &mean, 1, MPI_DOUBLE, MPI_SUM, MPI_COMM_WORLD);
	mean = mean / double(par.size);
	cout << "Rank " << par.rank << ": local error = " << err << ", global err = " << mean
			<< ", MPI size = " << par.size << endl;
	if (par.is_master())
		cout << "Average surrogate model error = " << mean << ", tol = " << tol << endl;
	return (mean <= tol) ? true : false;
}

