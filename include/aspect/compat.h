/*
  Copyright (C) 2015 - 2023 by the authors of the ASPECT code.

  This file is part of ASPECT.

  ASPECT is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published by
  the Free Software Foundation; either version 2, or (at your option)
  any later version.

  ASPECT is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with ASPECT; see the file LICENSE.  If not see
  <http://www.gnu.org/licenses/>.
*/

#ifndef _aspect_compat_h
#define _aspect_compat_h

#include <aspect/global.h>
// C++11 related includes.
#include <array>
#include <functional>
#include <memory>

namespace big_mpi
{

  using dealii::Utilities::MPI::broadcast;

}

// deal.II 9.6 introduces the new MGTransferMF class as a replacement
// for MGTransferMatrixFree. Instead of putting an ifdef in every place,
// do this in one central location:
#if !DEAL_II_VERSION_GTE(9,6,0)
#include <deal.II/multigrid/mg_transfer_matrix_free.h>
namespace dealii
{
  template<int dim, class NumberType>
  using MGTransferMF = MGTransferMatrixFree<dim,NumberType>;
}
#endif


// Implement VectorTools::compute_no_normal_flux_constraints_on_level
#if !DEAL_II_VERSION_GTE(9,5,0)
#include <deal.II/numerics/vector_tools.h>
#include <deal.II/hp/fe_values.h>
#include <deal.II/grid/manifold.h>
namespace dealii
{
  namespace VectorTools
  {
    namespace internal
    {
      /**
       * A structure that stores the dim DoF indices that correspond to a
       * vector-valued quantity at a single support point.
       */
      template <int dim>
      struct VectorDoFTuple
      {
        std::array<types::global_dof_index,dim> dof_indices;

        VectorDoFTuple()
        {
          for (unsigned int i = 0; i < dim; ++i)
            dof_indices[i] = numbers::invalid_dof_index;
        }


        bool
        operator<(const VectorDoFTuple<dim> &other) const
        {
          return (dof_indices < other.dof_indices);
        }

        bool
        operator==(const VectorDoFTuple<dim> &other) const
        {
          return (dof_indices == other.dof_indices);
        }

        bool
        operator!=(const VectorDoFTuple<dim> &other) const
        {
          return (dof_indices != other.dof_indices);
        }
      };


      template <int dim>
      std::ostream &
      operator<<(std::ostream &out, const VectorDoFTuple<dim> &vdt)
      {
        for (unsigned int d = 0; d < dim; ++d)
          out << vdt.dof_indices[d] << (d < dim - 1 ? " " : "");
        return out;
      }



      /**
       * Add the constraint $\vec n \cdot \vec u = inhom$ to the list of
       * constraints.
       *
       * Here, $\vec u$ is represented by the set of given DoF indices, and
       * $\vec n$ by the vector specified as the second argument.
       *
       * The function does not add constraints if a degree of freedom is already
       * constrained in the constraints object.
       */
      template <int dim>
      void
      add_constraint(const VectorDoFTuple<dim> &dof_indices,
                     const Tensor<1, dim>      &constraining_vector,
                     AffineConstraints<double> &constraints,
                     const double               inhomogeneity = 0)
      {
        // choose the DoF that has the largest component in the
        // constraining_vector as the one to be constrained as this makes the
        // process stable in cases where the constraining_vector has the form
        // n=(1,0) or n=(0,1)
        //
        // we get constraints of the form x0 = a_1*x1 + a2*x2 + ... if one of
        // the weights is essentially zero then skip this part. the
        // AffineConstraints can also deal with cases like x0 = 0 if
        // necessary
        //
        // there is a problem if we have a normal vector of the form
        // (a,a,small) or (a,a,a). Depending on round-off we may choose the
        // first or second component (or third, in the latter case) as the
        // largest one, and depending on our choice one or another degree of
        // freedom will be constrained. On a single processor this is not
        // much of a problem, but it's a nightmare when we run in parallel
        // and two processors disagree on which DoF should be constrained.
        // This led to an incredibly difficult to find bug in step-32 when
        // running in parallel with 9 or more processors.
        //
        // in practice, such normal vectors of the form (a,a,small) or
        // (a,a,a) happen not infrequently since they lie on the diagonals
        // where vertices frequently happen to land upon mesh refinement if
        // one starts from a symmetric and regular body. we work around this
        // problem in the following way: if we have a normal vector of the
        // form (a,b) (similarly algorithm in 3d), we choose 'a' as the
        // largest coefficient not if a>b but if a>b+1e-10. this shifts the
        // problem away from the frequently visited diagonal to a line that
        // is off the diagonal. there will of course be problems where the
        // exact values of a and b differ by exactly 1e-10 and we get into
        // the same instability, but from a practical viewpoint such problems
        // should be much rarer. in particular, meshes have to be very fine
        // for a vertex to land on this line if the original body had a
        // vertex on the diagonal as well
        switch (dim)
          {
            case 2:
            {
              if (std::fabs(constraining_vector[0]) >
                  std::fabs(constraining_vector[1]) + 1e-10)
                {
                  if (!constraints.is_constrained(dof_indices.dof_indices[0]) &&
                      constraints.can_store_line(dof_indices.dof_indices[0]))
                    {
                      constraints.add_line(dof_indices.dof_indices[0]);

                      if (std::fabs(constraining_vector[1] /
                                    constraining_vector[0]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[0],
                                              dof_indices.dof_indices[1],
                                              -constraining_vector[1] /
                                              constraining_vector[0]);

                      if (std::fabs(inhomogeneity / constraining_vector[0]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.set_inhomogeneity(
                          dof_indices.dof_indices[0],
                          inhomogeneity / constraining_vector[0]);
                    }
                }
              else
                {
                  if (!constraints.is_constrained(dof_indices.dof_indices[1]) &&
                      constraints.can_store_line(dof_indices.dof_indices[1]))
                    {
                      constraints.add_line(dof_indices.dof_indices[1]);

                      if (std::fabs(constraining_vector[0] /
                                    constraining_vector[1]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[1],
                                              dof_indices.dof_indices[0],
                                              -constraining_vector[0] /
                                              constraining_vector[1]);

                      if (std::fabs(inhomogeneity / constraining_vector[1]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.set_inhomogeneity(
                          dof_indices.dof_indices[1],
                          inhomogeneity / constraining_vector[1]);
                    }
                }
              break;
            }

            case 3:
            {
              if ((std::fabs(constraining_vector[0]) >=
                   std::fabs(constraining_vector[1]) + 1e-10) &&
                  (std::fabs(constraining_vector[0]) >=
                   std::fabs(constraining_vector[2]) + 2e-10))
                {
                  if (!constraints.is_constrained(dof_indices.dof_indices[0]) &&
                      constraints.can_store_line(dof_indices.dof_indices[0]))
                    {
                      constraints.add_line(dof_indices.dof_indices[0]);

                      if (std::fabs(constraining_vector[1] /
                                    constraining_vector[0]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[0],
                                              dof_indices.dof_indices[1],
                                              -constraining_vector[1] /
                                              constraining_vector[0]);

                      if (std::fabs(constraining_vector[2] /
                                    constraining_vector[0]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[0],
                                              dof_indices.dof_indices[2],
                                              -constraining_vector[2] /
                                              constraining_vector[0]);

                      if (std::fabs(inhomogeneity / constraining_vector[0]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.set_inhomogeneity(
                          dof_indices.dof_indices[0],
                          inhomogeneity / constraining_vector[0]);
                    }
                }
              else if ((std::fabs(constraining_vector[1]) + 1e-10 >=
                        std::fabs(constraining_vector[0])) &&
                       (std::fabs(constraining_vector[1]) >=
                        std::fabs(constraining_vector[2]) + 1e-10))
                {
                  if (!constraints.is_constrained(dof_indices.dof_indices[1]) &&
                      constraints.can_store_line(dof_indices.dof_indices[1]))
                    {
                      constraints.add_line(dof_indices.dof_indices[1]);

                      if (std::fabs(constraining_vector[0] /
                                    constraining_vector[1]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[1],
                                              dof_indices.dof_indices[0],
                                              -constraining_vector[0] /
                                              constraining_vector[1]);

                      if (std::fabs(constraining_vector[2] /
                                    constraining_vector[1]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[1],
                                              dof_indices.dof_indices[2],
                                              -constraining_vector[2] /
                                              constraining_vector[1]);

                      if (std::fabs(inhomogeneity / constraining_vector[1]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.set_inhomogeneity(
                          dof_indices.dof_indices[1],
                          inhomogeneity / constraining_vector[1]);
                    }
                }
              else
                {
                  if (!constraints.is_constrained(dof_indices.dof_indices[2]) &&
                      constraints.can_store_line(dof_indices.dof_indices[2]))
                    {
                      constraints.add_line(dof_indices.dof_indices[2]);

                      if (std::fabs(constraining_vector[0] /
                                    constraining_vector[2]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[2],
                                              dof_indices.dof_indices[0],
                                              -constraining_vector[0] /
                                              constraining_vector[2]);

                      if (std::fabs(constraining_vector[1] /
                                    constraining_vector[2]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.add_entry(dof_indices.dof_indices[2],
                                              dof_indices.dof_indices[1],
                                              -constraining_vector[1] /
                                              constraining_vector[2]);

                      if (std::fabs(inhomogeneity / constraining_vector[2]) >
                          std::numeric_limits<double>::epsilon())
                        constraints.set_inhomogeneity(
                          dof_indices.dof_indices[2],
                          inhomogeneity / constraining_vector[2]);
                    }
                }

              break;
            }

            default:
              Assert(false, ExcNotImplemented());
          }
      }



      /**
       * Add the constraint $(\vec u-\vec u_\Gamma) \| \vec t$ to the list of
       * constraints. In 2d, this is a single constraint, in 3d these are two
       * constraints.
       *
       * Here, $\vec u$ is represented by the set of given DoF indices, and
       * $\vec t$ by the vector specified as the second argument.
       *
       * The function does not add constraints if a degree of freedom is already
       * constrained in the constraints object.
       */
      template <int dim>
      void
      add_tangentiality_constraints(
        const VectorDoFTuple<dim> &dof_indices,
        const Tensor<1, dim>      &tangent_vector,
        AffineConstraints<double> &constraints,
        const Vector<double>      &b_values = Vector<double>(dim))
      {
        // choose the DoF that has the
        // largest component in the
        // tangent_vector as the
        // independent component, and
        // then constrain the others to
        // it. specifically, if, say,
        // component 0 of the tangent
        // vector t is largest by
        // magnitude, then
        // x1=(b[1]*t[0]-b[0]*t[1])/t[0]+t[1]/t[0]*x_0, etc.
        unsigned int largest_component = 0;
        for (unsigned int d = 1; d < dim; ++d)
          if (std::fabs(tangent_vector[d]) >
              std::fabs(tangent_vector[largest_component]) + 1e-10)
            largest_component = d;

        // then constrain all of the
        // other degrees of freedom in
        // terms of the one just found
        for (unsigned int d = 0; d < dim; ++d)
          if (d != largest_component)
            if (!constraints.is_constrained(dof_indices.dof_indices[d]) &&
                constraints.can_store_line(dof_indices.dof_indices[d]))
              {
                constraints.add_line(dof_indices.dof_indices[d]);

                if (std::fabs(tangent_vector[d] /
                              tangent_vector[largest_component]) >
                    std::numeric_limits<double>::epsilon())
                  constraints.add_entry(
                    dof_indices.dof_indices[d],
                    dof_indices.dof_indices[largest_component],
                    tangent_vector[d] / tangent_vector[largest_component]);

                const double inhomogeneity =
                  (b_values(d) * tangent_vector[largest_component] -
                   b_values(largest_component) * tangent_vector[d]) /
                  tangent_vector[largest_component];

                if (std::fabs(inhomogeneity) >
                    std::numeric_limits<double>::epsilon())
                  constraints.set_inhomogeneity(dof_indices.dof_indices[d],
                                                inhomogeneity);
              }
      }



      /**
       * Compute the mappings from vector degrees of freedom to normal vectors @p dof_to_normals_map
       * and vector degrees of freedom to prescribed normal fluxes @p dof_vector_to_b_values.
       */
      template <int dim, int spacedim>
      void
      map_dofs_to_normal_vectors_and_normal_fluxes(
        const typename DoFHandler<dim, spacedim>::cell_iterator &cell,
        const unsigned int                  first_vector_component,
        const std::set<types::boundary_id> &boundary_ids,
        const std::map<types::boundary_id, const Function<spacedim> *>
        &function_map,
        hp::FEFaceValues<dim, spacedim> &x_fe_face_values,
        const unsigned int               n_dofs,
        const IndexSet                  &refinement_edge_indices,
        const unsigned int               level,
        std::multimap<
        internal::VectorDoFTuple<dim>,
        std::pair<Tensor<1, dim>,
        typename DoFHandler<dim, spacedim>::cell_iterator>>
        &dof_to_normals_map,
        std::map<internal::VectorDoFTuple<dim>, Vector<double>>
        &dof_vector_to_b_values)
      {
        std::set<types::boundary_id>::iterator b_id;
        for (const unsigned int face_no : cell->face_indices())
          if ((b_id = boundary_ids.find(cell->face(face_no)->boundary_id())) !=
              boundary_ids.end())
            {
              const FiniteElement<dim> &fe = cell->get_fe();
              typename DoFHandler<dim, spacedim>::level_face_iterator face =
                cell->face(face_no);

              std::vector<types::global_dof_index> face_dofs;
              // get the indices of the dofs on this cell...
              face_dofs.resize(fe.n_dofs_per_face(face_no));

              if (level != numbers::invalid_unsigned_int)
                face->get_mg_dof_indices(level,
                                         face_dofs,
                                         cell->active_fe_index());
              else
                face->get_dof_indices(face_dofs, cell->active_fe_index());

              x_fe_face_values.reinit(cell, face_no);
              const FEFaceValues<dim> &fe_values =
                x_fe_face_values.get_present_fe_values();

              const auto &face_quadrature_collection =
                x_fe_face_values.get_quadrature_collection();

              // then identify which of them correspond to the selected set of
              // vector components
              for (unsigned int i = 0; i < face_dofs.size(); ++i)
                if (fe.face_system_to_component_index(i, face_no).first ==
                    first_vector_component)
                  // Refinement edge indices are going to be constrained to 0
                  // during a multigrid cycle and do not need no-normal-flux
                  // constraints, so skip them:
                  if (level == numbers::invalid_unsigned_int ||
                      !refinement_edge_indices.is_element(face_dofs[i]))
                    {
                      // find corresponding other components of vector
                      internal::VectorDoFTuple<dim> vector_dofs;
                      vector_dofs.dof_indices[0] = face_dofs[i];

                      Assert(
                        first_vector_component + dim <= fe.n_components(),
                        ExcMessage(
                          "Error: the finite element does not have enough components "
                          "to define a normal direction."));

                      for (unsigned int k = 0; k < fe.n_dofs_per_face(face_no);
                           ++k)
                        if ((k != i) &&
                            (face_quadrature_collection[cell->active_fe_index()]
                             .point(k) ==
                             face_quadrature_collection[cell->active_fe_index()]
                             .point(i)) &&
                            (fe.face_system_to_component_index(k, face_no)
                             .first >= first_vector_component) &&
                            (fe.face_system_to_component_index(k, face_no).first <
                             first_vector_component + dim))
                          vector_dofs.dof_indices
                          [fe.face_system_to_component_index(k, face_no).first -
                           first_vector_component] = face_dofs[k];

                      for (unsigned int d = 0; d < dim; ++d)
                        Assert(vector_dofs.dof_indices[d] < n_dofs,
                               ExcInternalError());
                      (void)n_dofs;

                      // we need the normal vector on this face. we know that
                      // it is a vector of length 1 but at least with higher
                      // order mappings it isn't always possible to guarantee
                      // that each component is exact up to zero tolerance. in
                      // particular, as shown in the deal.II/no_flux_06 test,
                      // if we just take the normal vector as given by the
                      // fe_values object, we can get entries in the normal
                      // vectors of the unit cube that have entries up to
                      // several times 1e-14.
                      //
                      // the problem with this is that this later yields
                      // constraints that are circular (e.g., in the testcase,
                      // we get constraints of the form
                      //
                      // x22 =  2.93099e-14*x21 + 2.93099e-14*x23
                      // x21 = -2.93099e-14*x22 + 2.93099e-14*x21
                      //
                      // in both of these constraints, the small numbers
                      // should be zero and the constraints should simply be
                      // x22 = x21 = 0
                      //
                      // to achieve this, we utilize that we know that the
                      // normal vector has (or should have) length 1 and that
                      // we can simply set small elements to zero (without
                      // having to check that they are small *relative to
                      // something else*). we do this and then normalize the
                      // length of the vector back to one, just to be on the
                      // safe side
                      //
                      // one more point: we would like to use the "real"
                      // normal vector here, as provided by the boundary
                      // description and as opposed to what we get from the
                      // FEValues object. we do this in the immediately next
                      // line, but as is obvious, the boundary only has a
                      // vague idea which side of a cell it is on -- indicated
                      // by the face number. in other words, it may provide
                      // the inner or outer normal. by and large, there is no
                      // harm from this, since the tangential vector we
                      // compute is still the same. however, we do average
                      // over normal vectors from adjacent cells and if they
                      // have recorded normal vectors from the inside once and
                      // from the outside the other time, then this averaging
                      // is going to run into trouble. as a consequence we ask
                      // the mapping after all for its normal vector, but we
                      // only ask it so that we can possibly correct the sign
                      // of the normal vector provided by the boundary if they
                      // should point in different directions. this is the
                      // case in tests/deal.II/no_flux_11.
                      Tensor<1, dim> normal_vector =
                        (cell->face(face_no)->get_manifold().normal_vector(
                           cell->face(face_no), fe_values.quadrature_point(i)));
                      if (normal_vector * fe_values.normal_vector(i) < 0)
                        normal_vector *= -1;
                      Assert(std::fabs(normal_vector.norm() - 1) < 1e-14,
                             ExcInternalError());
                      for (unsigned int d = 0; d < dim; ++d)
                        if (std::fabs(normal_vector[d]) < 1e-13)
                          normal_vector[d] = 0;
                      normal_vector /= normal_vector.norm();

                      const Point<dim> &point = fe_values.quadrature_point(i);
                      Vector<double>    b_values(dim);
                      function_map.at(*b_id)->vector_value(point, b_values);

                      // now enter the (dofs,(normal_vector,cell)) entry into
                      // the map
                      dof_to_normals_map.insert(
                        std::make_pair(vector_dofs,
                                       std::make_pair(normal_vector, cell)));
                      dof_vector_to_b_values.insert(
                        std::make_pair(vector_dofs, b_values));

#ifdef DEBUG_NO_NORMAL_FLUX
                      std::cout << "Adding normal vector:" << std::endl
                                << "   dofs=" << vector_dofs << std::endl
                                << "   cell=" << cell << " at " << cell->center()
                                << std::endl
                                << "   normal=" << normal_vector << std::endl;
#endif
                    }
            }
      }



      /**
       * This is the internal function that computes the nonzero normal
       * flux constraints on active cells
       * if @p level is an invalid unsigned integer or level cells if the cell level is provided.
       * It's called by compute_nonzero_normal_flux_constraints() and
       * compute_nonzero_normal_flux_constraints_on_level() so as to have
       * separate interfaces for the active and level cells.
       */
      template <int dim, int spacedim>
      void
      compute_nonzero_normal_flux_constraints_active_or_level(
        const DoFHandler<dim, spacedim>    &dof_handler,
        const unsigned int                  first_vector_component,
        const std::set<types::boundary_id> &boundary_ids,
        const std::map<types::boundary_id, const Function<spacedim> *>
        &function_map,
        AffineConstraints<double>    &constraints,
        const Mapping<dim, spacedim> &mapping,
        const IndexSet               &refinement_edge_indices = IndexSet(),
        const unsigned int            level = numbers::invalid_unsigned_int)
      {
        Assert(dim > 1,
               ExcMessage("This function is not useful in 1d because it amounts "
                          "to imposing Dirichlet values on the vector-valued "
                          "quantity."));

        // create FE and mapping collections for all elements in use by this
        // DoFHandler
        const hp::FECollection<dim, spacedim> &fe_collection =
          dof_handler.get_fe_collection();
        hp::MappingCollection<dim, spacedim> mapping_collection;
        for (unsigned int i = 0; i < fe_collection.size(); ++i)
          mapping_collection.push_back(mapping);

        // TODO: the implementation makes the assumption that all faces have the
        // same number of dofs
        AssertDimension(dof_handler.get_fe().n_unique_faces(), 1);
        const unsigned int face_no = 0;

        // now also create a quadrature collection for the faces of a cell. fill
        // it with a quadrature formula with the support points on faces for each
        // FE
        hp::QCollection<dim - 1> face_quadrature_collection;
        for (unsigned int i = 0; i < fe_collection.size(); ++i)
          {
            const std::vector<Point<dim - 1>> &unit_support_points =
              fe_collection[i].get_unit_face_support_points(face_no);

            Assert(unit_support_points.size() ==
                   fe_collection[i].n_dofs_per_face(face_no),
                   ExcInternalError());

            face_quadrature_collection.push_back(
              Quadrature<dim - 1>(unit_support_points));
          }

        // now create the object with which we will generate the normal vectors
        hp::FEFaceValues<dim, spacedim> x_fe_face_values(
          mapping_collection,
          fe_collection,
          face_quadrature_collection,
          update_quadrature_points | update_normal_vectors);

        // have a map that stores normal vectors for each vector-dof tuple we want
        // to constrain. since we can get at the same vector dof tuple more than
        // once (for example if it is located at a vertex that we visit from all
        // adjacent cells), we will want to average later on the normal vectors
        // computed on different cells as described in the documentation of this
        // function. however, we can only average if the contributions came from
        // different cells, whereas we want to constrain twice or more in case the
        // contributions came from different faces of the same cell
        // (i.e. constrain not just the *average normal direction* but *all normal
        // directions* we find). consequently, we also have to store which cell a
        // normal vector was computed on
        using DoFToNormalsMap = std::multimap<
                                internal::VectorDoFTuple<dim>,
                                std::pair<Tensor<1, dim>,
                                typename DoFHandler<dim, spacedim>::cell_iterator>>;
        std::map<internal::VectorDoFTuple<dim>, Vector<double>>
        dof_vector_to_b_values;

        DoFToNormalsMap dof_to_normals_map;

        const unsigned int n_dof = dof_handler.n_dofs();

        if (level == numbers::invalid_unsigned_int)
          {
            // active cells
            for (const auto &cell : dof_handler.active_cell_iterators())
              if (!cell->is_artificial())
                {
                  internal::map_dofs_to_normal_vectors_and_normal_fluxes(
                    cell,
                    first_vector_component,
                    boundary_ids,
                    function_map,
                    x_fe_face_values,
                    n_dof,
                    refinement_edge_indices,
                    level,
                    dof_to_normals_map,
                    dof_vector_to_b_values);
                }
          }
        else
          {
            // level cells
            for (const auto &cell : dof_handler.cell_iterators_on_level(level))
              if (cell->level_subdomain_id() !=
                  numbers::artificial_subdomain_id &&
                  cell->level_subdomain_id() != numbers::invalid_subdomain_id)
                {
                  internal::map_dofs_to_normal_vectors_and_normal_fluxes(
                    cell,
                    first_vector_component,
                    boundary_ids,
                    function_map,
                    x_fe_face_values,
                    n_dof,
                    refinement_edge_indices,
                    level,
                    dof_to_normals_map,
                    dof_vector_to_b_values);
                }
          }

        // Now do something with the collected information. To this end, loop
        // through all sets of pairs (dofs,normal_vector) and identify which
        // entries belong to the same set of dofs and then do as described in the
        // documentation, i.e. either average the normal vector or don't for this
        // particular set of dofs
        typename DoFToNormalsMap::const_iterator p = dof_to_normals_map.begin();

        while (p != dof_to_normals_map.end())
          {
            // first find the range of entries in the multimap that corresponds to
            // the same vector-dof tuple. as usual, we define the range
            // half-open. the first entry of course is 'p'
            typename DoFToNormalsMap::const_iterator same_dof_range[2] = {p};
            for (++p; p != dof_to_normals_map.end(); ++p)
              if (p->first != same_dof_range[0]->first)
                {
                  same_dof_range[1] = p;
                  break;
                }
            if (p == dof_to_normals_map.end())
              same_dof_range[1] = dof_to_normals_map.end();

#ifdef DEBUG_NO_NORMAL_FLUX
            std::cout << "For dof indices <" << p->first
                      << ">, found the following normals" << std::endl;
            for (typename DoFToNormalsMap::const_iterator q = same_dof_range[0];
                 q != same_dof_range[1];
                 ++q)
              std::cout << "   " << q->second.first << " from cell "
                        << q->second.second << std::endl;
#endif


            // now compute the reverse mapping: for each of the cells that
            // contributed to the current set of vector dofs, add up the normal
            // vectors. the values of the map are pairs of normal vectors and
            // number of cells that have contributed
            using CellToNormalsMap =
              std::map<typename DoFHandler<dim, spacedim>::cell_iterator,
              std::pair<Tensor<1, dim>, unsigned int>>;

            CellToNormalsMap cell_to_normals_map;
            for (typename DoFToNormalsMap::const_iterator q = same_dof_range[0];
                 q != same_dof_range[1];
                 ++q)
              if (cell_to_normals_map.find(q->second.second) ==
                  cell_to_normals_map.end())
                cell_to_normals_map[q->second.second] =
                  std::make_pair(q->second.first, 1U);
              else
                {
                  const Tensor<1, dim> old_normal =
                    cell_to_normals_map[q->second.second].first;
                  const unsigned int old_count =
                    cell_to_normals_map[q->second.second].second;

                  Assert(old_count > 0, ExcInternalError());

                  // in the same entry, store again the now averaged normal vector
                  // and the new count
                  cell_to_normals_map[q->second.second] =
                    std::make_pair((old_normal * old_count + q->second.first) /
                                   (old_count + 1),
                                   old_count + 1);
                }
            Assert(cell_to_normals_map.size() >= 1, ExcInternalError());

#ifdef DEBUG_NO_NORMAL_FLUX
            std::cout << "   cell_to_normals_map:" << std::endl;
            for (typename CellToNormalsMap::const_iterator x =
                   cell_to_normals_map.begin();
                 x != cell_to_normals_map.end();
                 ++x)
              std::cout << "      " << x->first << " -> (" << x->second.first
                        << ',' << x->second.second << ')' << std::endl;
#endif

            // count the maximum number of contributions from each cell
            unsigned int max_n_contributions_per_cell = 1;
            for (typename CellToNormalsMap::const_iterator x =
                   cell_to_normals_map.begin();
                 x != cell_to_normals_map.end();
                 ++x)
              max_n_contributions_per_cell =
                std::max(max_n_contributions_per_cell, x->second.second);

            // verify that each cell can have only contributed at most dim times,
            // since that is the maximum number of faces that come together at a
            // single place
            Assert(max_n_contributions_per_cell <= dim, ExcInternalError());

            switch (max_n_contributions_per_cell)
              {
                // first deal with the case that a number of cells all have
                // registered that they have a normal vector defined at the
                // location of a given vector dof, and that each of them have
                // encountered this vector dof exactly once while looping over all
                // their faces. as stated in the documentation, this is the case
                // where we want to simply average over all normal vectors
                //
                // the typical case is in 2d where multiple cells meet at one
                // vertex sitting on the boundary. same in 3d for a vertex that
                // is associated with only one of the boundary indicators passed
                // to this function
                case 1:
                {
                  // compute the average normal vector from all the ones that
                  // have the same set of dofs. we could add them up and divide
                  // them by the number of additions, or simply normalize them
                  // right away since we want them to have unit length anyway
                  Tensor<1, dim> normal;
                  for (typename CellToNormalsMap::const_iterator x =
                         cell_to_normals_map.begin();
                       x != cell_to_normals_map.end();
                       ++x)
                    normal += x->second.first;
                  normal /= normal.norm();

                  // normalize again
                  for (unsigned int d = 0; d < dim; ++d)
                    if (std::fabs(normal[d]) < 1e-13)
                      normal[d] = 0;
                  normal /= normal.norm();

                  // then construct constraints from this:
                  const internal::VectorDoFTuple<dim> &dof_indices =
                    same_dof_range[0]->first;
                  double               normal_value = 0.;
                  const Vector<double> b_values =
                    dof_vector_to_b_values[dof_indices];
                  for (unsigned int i = 0; i < dim; ++i)
                    normal_value += b_values[i] * normal[i];
                  internal::add_constraint(dof_indices,
                                           normal,
                                           constraints,
                                           normal_value);

                  break;
                }

                // this is the slightly more complicated case that a single cell
                // has contributed with exactly DIM normal vectors to the same set
                // of vector dofs. this is what happens in a corner in 2d and 3d
                // (but not on an edge in 3d, where we have only 2, i.e. <DIM,
                // contributions. Here we do not want to average the normal
                // vectors. Since we have DIM contributions, let's assume (and
                // verify) that they are in fact all linearly independent; in that
                // case, all vector components are constrained and we need to set
                // all of them to the corresponding boundary values
                case dim:
                {
                  // assert that indeed only a single cell has contributed
                  Assert(cell_to_normals_map.size() == 1, ExcInternalError());

                  // check linear independence by computing the determinant of
                  // the matrix created from all the normal vectors. if they are
                  // linearly independent, then the determinant is nonzero. if
                  // they are orthogonal, then the matrix is in fact equal to 1
                  // (since they are all unit vectors); make sure the
                  // determinant is larger than 1e-3 to avoid cases where cells
                  // are degenerate
                  {
                    Tensor<2, dim> t;

                    typename DoFToNormalsMap::const_iterator x =
                      same_dof_range[0];
                    for (unsigned int i = 0; i < dim; ++i, ++x)
                      for (unsigned int j = 0; j < dim; ++j)
                        t[i][j] = x->second.first[j];

                    Assert(
                      std::fabs(determinant(t)) > 1e-3,
                      ExcMessage(
                        "Found a set of normal vectors that are nearly collinear."));
                  }

                  // so all components of this vector dof are constrained. enter
                  // this into the AffineConstraints object
                  //
                  // ignore dofs already constrained
                  const internal::VectorDoFTuple<dim> &dof_indices =
                    same_dof_range[0]->first;
                  const Vector<double> b_values =
                    dof_vector_to_b_values[dof_indices];
                  for (unsigned int i = 0; i < dim; ++i)
                    if (!constraints.is_constrained(
                          same_dof_range[0]->first.dof_indices[i]) &&
                        constraints.can_store_line(
                          same_dof_range[0]->first.dof_indices[i]))
                      {
                        const types::global_dof_index line =
                          dof_indices.dof_indices[i];
                        constraints.add_line(line);
                        if (std::fabs(b_values[i]) >
                            std::numeric_limits<double>::epsilon())
                          constraints.set_inhomogeneity(line, b_values[i]);
                        // no add_entries here
                      }

                  break;
                }

                // this is the case of an edge contribution in 3d, i.e. the vector
                // is constrained in two directions but not the third.
                default:
                {
                  Assert(dim >= 3, ExcNotImplemented());
                  Assert(max_n_contributions_per_cell == 2, ExcInternalError());

                  // as described in the documentation, let us first collect
                  // what each of the cells contributed at the current point. we
                  // use a std::list instead of a std::set (which would be more
                  // natural) because std::set requires that the stored elements
                  // are comparable with operator<
                  using CellContributions =
                    std::map<typename DoFHandler<dim, spacedim>::cell_iterator,
                    std::list<Tensor<1, dim>>>;
                  CellContributions cell_contributions;

                  for (typename DoFToNormalsMap::const_iterator q =
                         same_dof_range[0];
                       q != same_dof_range[1];
                       ++q)
                    cell_contributions[q->second.second].push_back(
                      q->second.first);
                  Assert(cell_contributions.size() >= 1, ExcInternalError());

                  // now for each cell that has contributed determine the number
                  // of normal vectors it has contributed. we currently only
                  // implement if this is dim-1 for all cells (if a single cell
                  // has contributed dim, or if all adjacent cells have
                  // contributed 1 normal vector, this is already handled
                  // above).
                  //
                  // we only implement the case that all cells contribute
                  // dim-1 because we assume that we are following an edge
                  // of the domain (think: we are looking at a vertex
                  // located on one of the edges of a refined cube where the
                  // boundary indicators of the two adjacent faces of the
                  // cube are both listed in the set of boundary indicators
                  // passed to this function). in that case, all cells along
                  // that edge of the domain are assumed to have contributed
                  // dim-1 normal vectors. however, there are cases where
                  // this assumption is not justified (see the lengthy
                  // explanation in test no_flux_12.cc) and in those cases
                  // we simply ignore the cell that contributes only
                  // once. this is also discussed at length in the
                  // documentation of this function.
                  //
                  // for each contributing cell compute the tangential vector
                  // that remains unconstrained
                  std::list<Tensor<1, dim>> tangential_vectors;
                  for (typename CellContributions::const_iterator contribution =
                         cell_contributions.begin();
                       contribution != cell_contributions.end();
                       ++contribution)
                    {
#ifdef DEBUG_NO_NORMAL_FLUX
                      std::cout
                          << "   Treating edge case with dim-1 contributions."
                          << std::endl
                          << "   Looking at cell " << contribution->first
                          << " which has contributed these normal vectors:"
                          << std::endl;
                      for (typename std::list<Tensor<1, dim>>::const_iterator
                           t = contribution->second.begin();
                           t != contribution->second.end();
                           ++t)
                        std::cout << "      " << *t << std::endl;
#endif

                      // as mentioned above, simply ignore cells that only
                      // contribute once
                      if (contribution->second.size() < dim - 1)
                        continue;

                      Tensor<1, dim> normals[dim - 1];
                      {
                        unsigned int index = 0;
                        for (typename std::list<Tensor<1, dim>>::const_iterator
                             t = contribution->second.begin();
                             t != contribution->second.end();
                             ++t, ++index)
                          normals[index] = *t;
                        Assert(index == dim - 1, ExcInternalError());
                      }

                      // calculate the tangent as the outer product of the
                      // normal vectors. since these vectors do not need to be
                      // orthogonal (think, for example, the case of the
                      // deal.II/no_flux_07 test: a sheared cube in 3d, with Q2
                      // elements, where we have constraints from the two normal
                      // vectors of two faces of the sheared cube that are not
                      // perpendicular to each other), we have to normalize the
                      // outer product
                      Tensor<1, dim> tangent;
                      switch (dim)
                        {
                          case 3:
                            // take cross product between normals[0] and
                            // normals[1]. write it in the current form (with
                            // [dim-2]) to make sure that compilers don't warn
                            // about out-of-bounds accesses -- the warnings are
                            // bogus since we get here only for dim==3, but at
                            // least one isn't quite smart enough to notice this
                            // and warns when compiling the function in 2d
                            tangent =
                              cross_product_3d(normals[0], normals[dim - 2]);
                            break;
                          default:
                            Assert(false, ExcNotImplemented());
                        }

                      Assert(
                        std::fabs(tangent.norm()) > 1e-12,
                        ExcMessage(
                          "Two normal vectors from adjacent faces are almost "
                          "parallel."));
                      tangent /= tangent.norm();

                      tangential_vectors.push_back(tangent);
                    }

                  // go through the list of tangents and make sure that they all
                  // roughly point in the same direction as the first one (i.e.
                  // have an angle less than 90 degrees); if they don't then
                  // flip their sign
                  {
                    const Tensor<1, dim> first_tangent =
                      tangential_vectors.front();
                    typename std::list<Tensor<1, dim>>::iterator t =
                      tangential_vectors.begin();
                    ++t;
                    for (; t != tangential_vectors.end(); ++t)
                      if (*t * first_tangent < 0)
                        *t *= -1;
                  }

                  // now compute the average tangent and normalize it
                  Tensor<1, dim> average_tangent;
                  for (typename std::list<Tensor<1, dim>>::const_iterator t =
                         tangential_vectors.begin();
                       t != tangential_vectors.end();
                       ++t)
                    average_tangent += *t;
                  average_tangent /= average_tangent.norm();

                  // now all that is left is that we add the constraints that
                  // the vector is parallel to the tangent
                  const internal::VectorDoFTuple<dim> &dof_indices =
                    same_dof_range[0]->first;
                  const Vector<double> b_values =
                    dof_vector_to_b_values[dof_indices];
                  internal::add_tangentiality_constraints(dof_indices,
                                                          average_tangent,
                                                          constraints,
                                                          b_values);
                }
              }
          }
      }

    } // namespece internal

    template <int dim, int spacedim>
    void
    compute_no_normal_flux_constraints_on_level(
      const DoFHandler<dim, spacedim>    &dof_handler,
      const unsigned int                  first_vector_component,
      const std::set<types::boundary_id> &boundary_ids,
      AffineConstraints<double>          &constraints,
      const Mapping<dim, spacedim>       &mapping,
      const IndexSet                     &refinement_edge_indices,
      const unsigned int                  level)
    {
      Functions::ZeroFunction<dim>                             zero_function(dim);
      std::map<types::boundary_id, const Function<spacedim> *> function_map;
      for (const types::boundary_id boundary_id : boundary_ids)
        function_map[boundary_id] = &zero_function;
      internal::compute_nonzero_normal_flux_constraints_active_or_level(
        dof_handler,
        first_vector_component,
        boundary_ids,
        function_map,
        constraints,
        mapping,
        refinement_edge_indices,
        level);
    }

  } // namespace VectorTools
} // namespace dealii

#endif


// deal.II versions up to 9.5 had a poorly designed interface of the
// SphericalManifold class that made it impossible for us to use.
// This file thus contains a copy of it.
#if !DEAL_II_VERSION_GTE(9,6,0)

#include <deal.II/grid/manifold.h>
#include <deal.II/grid/manifold_lib.h>

namespace aspect
{
  using namespace dealii;

  /**
   * The deal.II class SphericalManifold has a design flaw that made it
   * impossible to derive from the class. This is fixed post-9.5,
   * see https://github.com/dealii/dealii/pull/16242 and
   * https://github.com/dealii/dealii/pull/16248, but we can't
   * use deal.II 9.5 and earlier for this class. The current class
   * here is therefore a copy of the fixed class.
   */
  template <int dim, int spacedim = dim>
  class SphericalManifold : public Manifold<dim, spacedim>
  {
    public:
      /**
       * Constructor.
       *
       * @param[in] center The center of the coordinate system. Defaults to the
       * origin.
       */
      SphericalManifold(const Point<spacedim> center = Point<spacedim>());

      /**
       * Make a clone of this Manifold object.
       */
      virtual std::unique_ptr<Manifold<dim, spacedim>>
      clone() const override;

      /**
       * Given any two points in space, first project them on the surface
       * of a sphere with unit radius, then connect them with a geodesic
       * and find the intermediate point, and finally rescale the final
       * radius so that the resulting one is the convex combination of the
       * starting radii.
       */
      virtual Point<spacedim>
      get_intermediate_point(const Point<spacedim> &p1,
                             const Point<spacedim> &p2,
                             const double           w) const override;

      /**
       * Compute the derivative of the get_intermediate_point() function
       * with parameter w equal to zero.
       */
      virtual Tensor<1, spacedim>
      get_tangent_vector(const Point<spacedim> &x1,
                         const Point<spacedim> &x2) const override;

      /**
       * @copydoc Manifold::normal_vector()
       */
      virtual Tensor<1, spacedim>
      normal_vector(
        const typename Triangulation<dim, spacedim>::face_iterator &face,
        const Point<spacedim> &p) const override;

      /**
       * Compute the normal vectors to the boundary at each vertex.
       */
      virtual void
      get_normals_at_vertices(
        const typename Triangulation<dim, spacedim>::face_iterator &face,
        typename Manifold<dim, spacedim>::FaceVertexNormals &face_vertex_normals)
      const override;

      /**
       * Compute a new set of points that interpolate between the given points @p
       * surrounding_points. @p weights is a table with as many columns as @p
       * surrounding_points.size(). The number of rows in @p weights must match
       * the length of @p new_points.
       *
       * This function is optimized to perform on a collection
       * of new points, by collecting operations that are not dependent on the
       * weights outside of the loop over all new points.
       *
       * The implementation does not allow for @p surrounding_points and
       * @p new_points to point to the same array, so make sure to pass different
       * objects into the function.
       */
      virtual void
      get_new_points(const ArrayView<const Point<spacedim>> &surrounding_points,
                     const Table<2, double>                 &weights,
                     ArrayView<Point<spacedim>> new_points) const override;

      /**
       * Return a point on the spherical manifold which is intermediate
       * with respect to the surrounding points.
       */
      virtual Point<spacedim>
      get_new_point(const ArrayView<const Point<spacedim>> &vertices,
                    const ArrayView<const double>          &weights) const override;

      /**
       * The center of the spherical coordinate system.
       */
      const Point<spacedim> center;

    private:
      /**
       * Return a point on the spherical manifold which is intermediate
       * with respect to the surrounding points. This function uses a linear
       * average of the directions to find an estimated point. It returns a pair
       * of radius and direction from the center point to the candidate point.
       */
      std::pair<double, Tensor<1, spacedim>>
      guess_new_point(const ArrayView<const Tensor<1, spacedim>> &directions,
                      const ArrayView<const double>              &distances,
                      const ArrayView<const double>              &weights) const;

      /**
       * This function provides an internal implementation of the get_new_points()
       * interface.
       *
       * It computes a new set of points that interpolate between the given points
       * @p
       * surrounding_points. @p weights is an array view with as many entries as @p
       * surrounding_points.size() times @p new_points.size().
       *
       * This function is optimized to perform on a collection
       * of new points, by collecting operations that are not dependent on the
       * weights outside of the loop over all new points.
       *
       * The implementation does not allow for @p surrounding_points and
       * @p new_points to point to the same array, so make sure to pass different
       * objects into the function.
       */
      void
      do_get_new_points(const ArrayView<const Point<spacedim>> &surrounding_points,
                        const ArrayView<const double>          &weights,
                        ArrayView<Point<spacedim>>              new_points) const;

      /**
       * A manifold description to be used for get_new_point in 2d.
       */
      const PolarManifold<spacedim> polar_manifold;
  };
}

#else

// For sufficiently new deal.II versions, we can use the deal.II class, but to
// avoid name clashes, we have to import the class into namespace aspect. Once
// we rely on these sufficiently new versions of deal.II, we can not only remove
// the code above, but also the following lines, and in all places where we
// reference 'aspect::SphericalManifold' simply use 'SphericalManifold' instead
// (which then refers to the deal.II class).

#include <deal.II/grid/manifold_lib.h>
namespace aspect
{
  using dealii::SphericalManifold;
}

#endif

#endif
