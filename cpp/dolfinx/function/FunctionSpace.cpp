// Copyright (C) 2008-2019 Anders Logg and Garth N. Wells
//
// This file is part of DOLFINX (https://www.fenicsproject.org)
//
// SPDX-License-Identifier:    LGPL-3.0-or-later

#include "FunctionSpace.h"
#include <dolfinx/common/IndexMap.h>
#include <dolfinx/common/UniqueIdGenerator.h>
#include <dolfinx/common/types.h>
#include <dolfinx/common/utils.h>
#include <dolfinx/fem/CoordinateElement.h>
#include <dolfinx/fem/DofMap.h>
#include <dolfinx/fem/FiniteElement.h>
#include <dolfinx/graph/AdjacencyList.h>
#include <dolfinx/la/utils.h>
#include <dolfinx/mesh/Geometry.h>
#include <dolfinx/mesh/Mesh.h>
#include <dolfinx/mesh/Topology.h>
#include <vector>

using namespace dolfinx;
using namespace dolfinx::function;

//-----------------------------------------------------------------------------
FunctionSpace::FunctionSpace(std::shared_ptr<const mesh::Mesh> mesh,
                             std::shared_ptr<const fem::FiniteElement> element,
                             std::shared_ptr<const fem::DofMap> dofmap)
    : _mesh(mesh), _element(element), _dofmap(dofmap),
      _id(common::UniqueIdGenerator::id()), _root_space_id(_id)
{
  // Do nothing
}
//-----------------------------------------------------------------------------
bool FunctionSpace::operator==(const FunctionSpace& V) const
{
  // Compare pointers to shared objects
  return _element.get() == V._element.get() and _mesh.get() == V._mesh.get()
         and _dofmap.get() == V._dofmap.get();
}
//-----------------------------------------------------------------------------
bool FunctionSpace::operator!=(const FunctionSpace& V) const
{
  // Compare pointers to shared objects
  return !(*this == V);
}
//-----------------------------------------------------------------------------
std::int64_t FunctionSpace::dim() const
{
  assert(_dofmap);
  assert(_dofmap->element_dof_layout);
  if (_dofmap->element_dof_layout->is_view())
  {
    throw std::runtime_error("FunctionSpace dimension not supported for "
                             "sub-functions");
  }

  assert(_dofmap->index_map);
  return _dofmap->index_map->size_global() * _dofmap->index_map->block_size();
}
//-----------------------------------------------------------------------------
std::shared_ptr<FunctionSpace>
FunctionSpace::sub(const std::vector<int>& component) const
{
  assert(_mesh);
  assert(_element);
  assert(_dofmap);

  // Check if sub space is already in the cache and not expired
  if (auto it = _subspaces.find(component); it != _subspaces.end())
  {
    if (auto s = it->second.lock())
      return s;
  }

  // Extract sub-element
  std::shared_ptr<const fem::FiniteElement> element
      = this->_element->extract_sub_element(component);

  // Extract sub dofmap
  auto dofmap
      = std::make_shared<fem::DofMap>(_dofmap->extract_sub_dofmap(component));

  // Create new sub space
  auto sub_space = std::make_shared<FunctionSpace>(_mesh, element, dofmap);

  // Set root space id and component w.r.t. root
  sub_space->_root_space_id = _root_space_id;
  sub_space->_component = _component;
  sub_space->_component.insert(sub_space->_component.end(), component.begin(),
                               component.end());

  // Insert new subspace into cache
  _subspaces.insert(std::pair<std::vector<int>, std::shared_ptr<FunctionSpace>>(
      sub_space->_component, sub_space));

  return sub_space;
}
//-----------------------------------------------------------------------------
std::pair<std::shared_ptr<FunctionSpace>, std::vector<std::int32_t>>
FunctionSpace::collapse() const
{
  if (_component.empty())
    throw std::runtime_error("Function space is not a subspace");

  // Create collapsed DofMap
  std::shared_ptr<fem::DofMap> collapsed_dofmap;
  std::vector<std::int32_t> collapsed_dofs;
  std::tie(collapsed_dofmap, collapsed_dofs)
      = _dofmap->collapse(_mesh->mpi_comm(), _mesh->topology());

  // Create new FunctionSpace and return
  auto collapsed_sub_space
      = std::make_shared<FunctionSpace>(_mesh, _element, collapsed_dofmap);

  return std::pair(std::move(collapsed_sub_space), std::move(collapsed_dofs));
}
//-----------------------------------------------------------------------------
bool FunctionSpace::has_element(const fem::FiniteElement& element) const
{
  return element.hash() == this->_element->hash();
}
//-----------------------------------------------------------------------------
std::vector<int> FunctionSpace::component() const { return _component; }
//-----------------------------------------------------------------------------
Eigen::Array<double, Eigen::Dynamic, 3, Eigen::RowMajor>
FunctionSpace::tabulate_dof_coordinates() const
{
  // Geometric dimension
  assert(_mesh);
  assert(_element);
  const int gdim = _mesh->geometry().dim();
  const int tdim = _mesh->topology().dim();

  if (!_component.empty())
  {
    throw std::runtime_error(
        "Cannot tabulate coordinates for a FunctionSpace that is a subspace.");
  }

  // Get local size
  assert(_dofmap);
  std::shared_ptr<const common::IndexMap> index_map = _dofmap->index_map;
  assert(index_map);
  int bs = index_map->block_size();
  std::int32_t local_size
      = bs * (index_map->size_local() + index_map->num_ghosts());

  // Dof coordinate on reference element
  const Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>& X
      = _element->dof_reference_coordinates();

  // Get coordinate map
  const fem::CoordinateElement& cmap = _mesh->geometry().cmap();

  // Prepare cell geometry
  const graph::AdjacencyList<std::int32_t>& x_dofmap
      = _mesh->geometry().dofmap();

  // FIXME: Add proper interface for num coordinate dofs
  const int num_dofs_g = x_dofmap.num_links(0);
  const Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>&
      x_g
      = _mesh->geometry().x();

  // Array to hold coordinates to return
  Eigen::Array<double, Eigen::Dynamic, 3, Eigen::RowMajor> x
      = Eigen::Array<double, Eigen::Dynamic, 3, Eigen::RowMajor>::Zero(
          local_size, 3);

  // Loop over cells and tabulate dofs
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      coordinates(_element->space_dimension(), gdim);
  Eigen::Array<double, Eigen::Dynamic, Eigen::Dynamic, Eigen::RowMajor>
      coordinate_dofs(num_dofs_g, gdim);

  auto map = _mesh->topology().index_map(tdim);
  assert(map);
  const int num_cells = map->size_local() + map->num_ghosts();
  for (int c = 0; c < num_cells; ++c)
  {
    // Update cell
    auto x_dofs = x_dofmap.links(c);
    for (int i = 0; i < num_dofs_g; ++i)
      coordinate_dofs.row(i) = x_g.row(x_dofs[i]).head(gdim);

    // Get local-to-global map
    auto dofs = _dofmap->cell_dofs(c);

    // Tabulate dof coordinates on cell
    cmap.push_forward(coordinates, X, coordinate_dofs);

    // Copy dof coordinates into vector
    for (Eigen::Index i = 0; i < dofs.size(); ++i)
    {
      const std::int32_t dof = dofs[i];
      x.row(dof).head(gdim) = coordinates.row(i);
    }
  }

  return x;
}
//-----------------------------------------------------------------------------
std::size_t FunctionSpace::id() const { return _id; }
//-----------------------------------------------------------------------------
std::shared_ptr<const mesh::Mesh> FunctionSpace::mesh() const { return _mesh; }
//-----------------------------------------------------------------------------
std::shared_ptr<const fem::FiniteElement> FunctionSpace::element() const
{
  return _element;
}
//-----------------------------------------------------------------------------
std::shared_ptr<const fem::DofMap> FunctionSpace::dofmap() const
{
  return _dofmap;
}
//-----------------------------------------------------------------------------
bool FunctionSpace::contains(const FunctionSpace& V) const
{
  // Is the root space same?
  if (_root_space_id != V._root_space_id)
    return false;

  // Is V possibly our superspace?
  if (_component.size() > V._component.size())
    return false;

  // Are our components same as leading components of V?
  for (std::size_t i = 0; i < _component.size(); ++i)
  {
    if (_component[i] != V._component[i])
      return false;
  }

  // Ok, V is really our subspace
  return true;
}
//-----------------------------------------------------------------------------
