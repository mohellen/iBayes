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

#include <mcmc/MCMC.hpp>

using namespace std;


MCMC::MCMC(	
		Config const& c,
		Parallel & p,
		ForwardModel & m)
		: cfg(c), par(p), model(m)
{
	num_chains = (par.mpisize < stoi(cfg.get_param("mcmc_max_chains"))) ?
			par.mpisize : stoi(cfg.get_param("mcmc_max_chains"));
}

void MCMC::one_step_single_dim(
		std::size_t dim,	// IN: the dimension to be update in this step
		vector<double> & samplepos) // IN/OUT: sample+posterior vector from last step (to be updated if proposal get accepted)
{
	/// NOTE: samplepos has length (input_size + 1), 
	///       with last element being the posterior of the sample
	/// Initialize proposal with the last sample
	vector<double> proposal (samplepos);
	proposal.pop_back(); /// sample only, remove posterior

	// Compute random walk step = domain size * N  (N in [0.0, 0.1])
	pair<double,double> range = model.get_input_space(dim);
	double randwalk_size = (range.second - range.first) * stod(cfg.get_param("mcmc_randwalk_step"));

	// Initialize random generators
	mt19937 gen(chrono::system_clock::now().time_since_epoch().count());
	normal_distribution<double> ndist(proposal[dim], randwalk_size);
	uniform_real_distribution<double> udist(0.0,1.0);

	// 1. Draw a proposal (we only update proposal[dim])
	proposal[dim] = ndist(gen);
	while ((proposal[dim] < range.first) || (proposal[dim] > range.second)) /// ensure p[dim] is in range
		proposal[dim] = ndist(gen);

	// 2. Compute acceptance rate
	vector<double> d = model.run(proposal);
	double pos = cfg.compute_posterior(d);
	double acc = fmin(1.0, pos/samplepos.back());

	// 3. Accept or reject proposal
	if (udist(gen) <= acc) {
		// Case accept: update sample on dim and posterior
		samplepos[dim] = proposal[dim];
		samplepos.back() = pos;
	}// Case reject: do nothing
	return;
}

// The read from file the sample+posterior vector with the maximum posterior
// This is a line (usually the last line) in format: "MAXPOS 0.1 0.2 0.3 0.4 ..."
vector<double> MCMC::read_max_samplepos(fstream& fin)
{
	/// Note: samplepos is a vector of length (input_size + 1),
	///		with the first (input_size) elements being the sample,
	///		and the last element being the posterior
	vector<double> samplepos (cfg.get_input_size() + 1);

	/// NOTE: Considering the mcmc output file would be large and the MAXPOS line being the last line,
	/// 	we loop from the bottom of file backwards.
	fin.seekg(0, fin.end);
	std::size_t len = fin.tellg(); // Get length of file
	// Loop backwards
	char c;
	string line;
	for (long int i=len-2; i > 0; i--) {
		fin.seekg(i);
		c = fin.get();
		if (c == '\r' || c == '\n') {
			std::getline(fin, line);
			istringstream iss(line);
			vector<string> tokens {istream_iterator<string>{iss}, istream_iterator<string>{}};
			if ((tokens.size() > samplepos.size()) && (tokens[0] == "MAXPOS")) { //This is the line we are looking for
				// Read in data
				for (int k=0; k < samplepos.size(); k++) {
					samplepos[k] = stod(tokens[k+1]);
				}
				fin.seekg(0, fin.end);
				return samplepos; // If found, return immediately
			}
		}
	} //end for
	fin.seekg(0, fin.end);
	// If not found, return samplepos as an empty vector
	samplepos.clear();
	return samplepos; 
}

void MCMC::write_samplepos(
		fstream& fout,
		vector<double> const& samplepos)
{
	for (size_t i=0; i < samplepos.size()-1; ++i) {
		fout << samplepos[i] << " ";
	}
	fout << samplepos.back() << endl;
	return;
}

vector<double> MCMC::initialize_samplepos(
		vector<double> const& init_samplepos)
{	
	// Samplepos is a vector of sample + posterior (length = input_size + 1)
	std::size_t input_size = cfg.get_input_size();
	vector<double> samplepos (input_size + 1);

	// Use the init_samplepos if provided
	if (init_samplepos.size() == input_size + 1) {
		samplepos = init_samplepos;
	// Or generate a random one (posterior is 0.0)
	} else {
		mt19937 gen (chrono::system_clock::now().time_since_epoch().count());
		for (size_t i=0; i < input_size; i++) {
			pair<double,double> range = model.get_input_space(i);
			uniform_real_distribution<double> udist(range.first, range.second);
			samplepos[i] = udist(gen);
		}
	}
	return samplepos;
}


