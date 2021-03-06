// Copyright (C) 2008-today The SG++ project
// This file is part of the SG++ project. For conditions of distribution and
// use, please see the copyright notice provided with SG++ or at
// sgpp.sparsegrids.org

#include <sgpp/base/operation/hash/common/algorithm_sweep/StencilDehierarchisationModLinear.hpp>

#include <sgpp/globaldef.hpp>


namespace sgpp {
namespace base {



StencilDehierarchisationModLinear::StencilDehierarchisationModLinear(
  GridStorage& storage,
  OperationStencilHierarchisation::IndexStencil& surplusStencil,
  OperationStencilHierarchisation::IndexStencil& neighborStencil,
  OperationStencilHierarchisation::WeightStencil& weightStencil) :
  storage(storage), _surplusStencil(surplusStencil),
  _neighborStencil(neighborStencil), _weightStencil(weightStencil) {
}

StencilDehierarchisationModLinear::~StencilDehierarchisationModLinear() {
}

void StencilDehierarchisationModLinear::operator()(DataVector& source,
    DataVector& result, grid_iterator& index, size_t dim) {
  rec(source, result, index, dim, -1, -1);
}

void StencilDehierarchisationModLinear::rec(DataVector& source,
    DataVector& result, grid_iterator& index, size_t dim, int seql, int seqr) {
  // current position on the grid
  int seqm = static_cast<int>(index.seq());

  GridStorage::index_type::level_type l;
  GridStorage::index_type::index_type i;

  index.get(dim, l, i);

  // recursive calls for the right and left side of the current node
  bool isLeaf = index.hint();


  // When we descend the hierarchical basis
  // we have to modify the boundary values
  // in case the index is 1 or (2^l)-1 or we are on the first level
  // level 1, constant function
  if (l == 1) {
    // no dehierarchisation necessary for constant function (root)

    // descend left
    index.leftChild(dim);

    if (!storage.end(index.seq())) {
      // pass down the parent in the parameter for the left neighbor
      rec(source, result, index, dim, seql, seqm);
    }

    // descend right
    index.stepRight(dim);

    if (!storage.end(index.seq())) {
      // pass down the parent in the parameter for the right neighbor
      rec(source, result, index, dim, seqm, seqr);
    }

    // ascend
    index.up(dim);
  } else if (i == 1) {  // left boundary
    // dehierarchisation
    if (l > 2) {
      // seql actually stands for the grandparent here, both
      // values are needed to subtract the extrapolated part!
      // the parent (to the right) 1.5 times
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seqr);
      _weightStencil.push_back(1.5f);
      // the grandparent (2x to the right) -0.5 times
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seql);
      _weightStencil.push_back(-0.5f);
    } else {
      // special case level 2: only one parent function!
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seqr);
      _weightStencil.push_back(1.0f);
    }

    if (!isLeaf) {
      // descend left
      index.leftChild(dim);

      if (!storage.end(index.seq())) {
        // special boundary treatment:
        // pass down the parent in the parameter for the left neighbor
        rec(source, result, index, dim, seqr, seqm);
      }

      // descend right
      index.stepRight(dim);

      if (!storage.end(index.seq())) {
        rec(source, result, index, dim, seqm, seqr);
      }

      // ascend
      index.up(dim);
    }
  } else if (static_cast<int>(i) == static_cast<int>((1 << l) - 1)) {
    // right boundary
    // dehierarchisation
    if (l > 2) {
      // seqr actually stands for the grandparent here, both
      // values are needed to subtract the extrapolated part!
      // the parent (to the left) 1.5 times
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seql);
      _weightStencil.push_back(1.5f);
      // the grandparent (2x to the left) -0.5 times
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seqr);
      _weightStencil.push_back(-0.5f);
    } else {
      // special case level 2: only one parent function!
      _surplusStencil.push_back(seqm);
      _neighborStencil.push_back(seql);
      _weightStencil.push_back(1.0f);
    }

    if (!isLeaf) {
      // descend left
      index.leftChild(dim);

      if (!storage.end(index.seq())) {
        rec(source, result, index, dim, seql, seqm);
      }

      // descend right
      index.stepRight(dim);

      if (!storage.end(index.seq())) {
        // special boundary treatment:
        // pass down the parent in the parameter for the right neighbor
        rec(source, result, index, dim, seqm, seql);
      }

      // ascend
      index.up(dim);
    }
  } else {  // inner functions
    // dehierarchisation

    // left parent contribution
    _surplusStencil.push_back(seqm);
    _neighborStencil.push_back(seql);
    _weightStencil.push_back(0.5f);

    // right parent contribution
    _surplusStencil.push_back(seqm);
    _neighborStencil.push_back(seqr);
    _weightStencil.push_back(0.5f);

    if (!isLeaf) {
      // descend left
      index.leftChild(dim);

      if (!storage.end(index.seq())) {
        rec(source, result, index, dim, seql, seqm);
      }

      // descend right
      index.stepRight(dim);

      if (!storage.end(index.seq())) {
        rec(source, result, index, dim, seqm, seqr);
      }

      // ascend
      index.up(dim);
    }
  }
}

}  // namespace base
}  // namespace sgpp
