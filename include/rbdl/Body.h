/*
 * RBDL - Rigid Body Dynamics Library
 * Copyright (c) 2011-2018 Martin Felis <martin@fysx.org>
 *
 * Licensed under the zlib license. See LICENSE for more details.
 */

#ifndef RBDL_BODY_H
#define RBDL_BODY_H

#include "rbdl/rbdl_math.h"
#include "rbdl/rbdl_mathutils.h"
#include <assert.h>
#include <iostream>
#include "rbdl/Logging.h"
#include "rbdl/rbdl_errors.h"

namespace RigidBodyDynamics
{

/** \brief Describes all properties of a single body
 *
 * A Body contains information about mass, the location of its center of
 * mass, and the ineria tensor in the center of mass. This class is
 * designed to use the given information and transform it such that it can
 * directly be used by the spatial algebra.
 */
struct RBDL_DLLAPI Body {
  Body() :
    mMass (0.),
    mCenterOfMass (0., 0., 0.),
    mInertia (Math::Matrix3d::Zero()),
    mIsVirtual (false),
    mInitialMass(mMass),
    mInitialCenterOfMass(mCenterOfMass),
    mInitialInertia(mInertia)
  { };
  Body(const Body &body) :
    mMass (body.mMass),
    mCenterOfMass (body.mCenterOfMass),
    mInertia (body.mInertia),
    mIsVirtual (body.mIsVirtual),
    mInitialMass(mMass),
    mInitialCenterOfMass(mCenterOfMass),
    mInitialInertia(mInertia)
  {};
  Body& operator= (const Body &body)
  {
    if (this != &body) {
      mMass = body.mMass;
      mInertia = body.mInertia;
      mCenterOfMass = body.mCenterOfMass;
      mIsVirtual = body.mIsVirtual;
      mInitialMass = body.mInitialMass;
      mInitialCenterOfMass = body.mInitialCenterOfMass;
      mInitialInertia = body.mInitialInertia;
    }

    return *this;
  }

  /** \brief Constructs a body from mass, center of mass and radii of gyration
   *
   * This constructor eases the construction of a new body as all the
   * required parameters can be specified as parameters to the
   * constructor. These are then used to generate the spatial inertia
   * matrix which is expressed at the origin.
   *
   * \param mass the mass of the body
   * \param com  the position of the center of mass in the bodies coordinates
   * \param gyration_radii the radii of gyration at the center of mass of the body
   */
  Body(const Math::Scalar &mass,
      const Math::Vector3d &com,
      const Math::Vector3d &gyration_radii) :
    mMass (mass),
    mCenterOfMass(com),
    mIsVirtual (false),
    mInitialMass(mMass),
    mInitialCenterOfMass(mCenterOfMass)
  {
    mInertia = Math::Matrix3d (
                 gyration_radii[0], 0., 0.,
                 0., gyration_radii[1], 0.,
                 0., 0., gyration_radii[2]
               );
    mInitialInertia = mInertia;
  }

  /** \brief Constructs a body from mass, center of mass, and a 3x3 inertia matrix
   *
   * This constructor eases the construction of a new body as all the
   * required parameters can simply be specified as parameters to the
   * constructor. These are then used to generate the spatial inertia
   * matrix which is expressed at the origin.
   *
   * \param mass the mass of the body
   * \param com  the position of the center of mass in the bodies coordinates
   * \param inertia_C the inertia at the center of mass
   */
  Body(const Math::Scalar &mass,
      const Math::Vector3d &com,
      const Math::Matrix3d &inertia_C) :
    mMass (mass),
    mCenterOfMass(com),
    mInertia (inertia_C),
    mIsVirtual (false),
    mInitialMass(mMass),
    mInitialCenterOfMass(mCenterOfMass),
    mInitialInertia(mInertia) { }

  /** \brief Joins inertial parameters of two bodies to create a composite
   * body.
   *
   * This function can be used to joint inertial parameters of two bodies
   * to create a composite body that has the inertial properties as if the
   * two bodies were joined by a fixed joint.
   *
   * \param transform The frame transformation from the current body to the
   * other body.
   * \param other_body The other body that will be merged with *this.
   */
  void Join (const Math::SpatialTransform &transform, const Body &other_body)
  {
#ifndef RBDL_USE_CASADI_MATH
    // nothing to do if we join a massles body to the current.
    if (other_body.mMass == 0. && other_body.mInertia == Math::Matrix3d::Zero()) {
      return;
    }
#endif

    Math::Scalar other_mass = other_body.mMass;
    Math::Scalar new_mass = mMass + other_mass;

#ifndef RBDL_USE_CASADI_MATH
    if (new_mass == 0.) {
      throw Errors::RBDLError("Error: cannot join bodies as both have zero mass!\n");
    }
#endif

    Math::Vector3d other_com = transform.E.transpose() * other_body.mCenterOfMass +
                               transform.r;
    Math::Vector3d new_com = (1 / new_mass ) * (mMass * mCenterOfMass + other_mass *
                             other_com);

    LOG << "other_com = " << std::endl << other_com.transpose() << std::endl;
    LOG << "rotation = " << std::endl << transform.E << std::endl;

    // We have to transform the inertia of other_body to the new COM. This
    // is done in 4 steps:
    //
    // 1. Transform the inertia from other origin to other COM
    // 2. Rotate the inertia that it is aligned to the frame of this body
    // 3. Transform inertia of other_body to the origin of the frame of
    // this body
    // 4. Sum the two inertias
    // 5. Transform the summed inertia to the new COM

    Math::SpatialRigidBodyInertia other_rbi =
      Math::SpatialRigidBodyInertia::createFromMassComInertiaC (other_body.mMass,
          other_body.mCenterOfMass, other_body.mInertia);
    Math::SpatialRigidBodyInertia this_rbi =
      Math::SpatialRigidBodyInertia::createFromMassComInertiaC (mMass, mCenterOfMass,
          mInertia);

    Math::Matrix3d inertia_other = other_rbi.toMatrix().block<3,3>(0,0);
    LOG << "inertia_other = " << std::endl << inertia_other << std::endl;

    // 1. Transform the inertia from other origin to other COM
    Math::Matrix3d other_com_cross = Math::VectorCrossMatrix(
                                       other_body.mCenterOfMass);
    Math::Matrix3d inertia_other_com = inertia_other - other_mass * other_com_cross
                                       * other_com_cross.transpose();
    LOG << "inertia_other_com = " << std::endl << inertia_other_com << std::endl;

    // 2. Rotate the inertia that it is aligned to the frame of this body
    Math::Matrix3d inertia_other_com_rotated = transform.E.transpose() *
        inertia_other_com * transform.E;
    LOG << "inertia_other_com_rotated = " << std::endl << inertia_other_com_rotated
        << std::endl;

    // 3. Transform inertia of other_body to the origin of the frame of this body
    Math::Matrix3d inertia_other_com_rotated_this_origin = Math::parallel_axis (
          inertia_other_com_rotated, other_mass, other_com);
    LOG << "inertia_other_com_rotated_this_origin = " << std::endl <<
        inertia_other_com_rotated_this_origin << std::endl;

    // 4. Sum the two inertias
    Math::Matrix3d inertia_summed = Math::Matrix3d (this_rbi.toMatrix().block<3,3>
                                    (0,0)) + inertia_other_com_rotated_this_origin;
    LOG << "inertia_summed  = " << std::endl << inertia_summed << std::endl;

    // 5. Transform the summed inertia to the new COM
    Math::Matrix3d new_inertia = inertia_summed - new_mass *
                                 Math::VectorCrossMatrix (new_com) * Math::VectorCrossMatrix(
                                   new_com).transpose();

    LOG << "new_mass = " << new_mass << std::endl;
    LOG << "new_com  = " << new_com.transpose() << std::endl;
    LOG << "new_inertia  = " << std::endl << new_inertia << std::endl;

    mMass = new_mass;
    mCenterOfMass = new_com;
    mInertia = new_inertia;
  }

  ~Body() {};

  /// \brief The mass of the body
  Math::Scalar mMass;
  /// \brief The position of the center of mass in body coordinates
  Math::Vector3d mCenterOfMass;
  /// \brief Inertia matrix at the center of mass
  Math::Matrix3d mInertia;

  bool mIsVirtual;

  /// The following members are used to save the original values of the Body and
  /// are not updated when a fixed body is attached to this body
  /// \brief The mass of the body not taking in account any fixed body attached to this
  /// body
  double mInitialMass;
  /// \brief The position of the center of mass in body coordinates not taking in account
  /// any fixed body attached to this body
  Math::Vector3d mInitialCenterOfMass;
  /// \brief Inertia matrix at the center of mass not taking in account any fixed body
  /// attached to this body
  Math::Matrix3d mInitialInertia;
};

/** \brief Keeps the information of a body and how it is attached to another body.
 *
 * When using fixed bodies, i.e. a body that is attached to anothe via a
 * fixed joint, the attached body is merged onto its parent. By doing so
 * adding fixed joints do not have an impact on runtime.
 */
struct RBDL_DLLAPI FixedBody {
  /// \brief The mass of the body
  Math::Scalar mMass;
  /// \brief The position of the center of mass in body coordinates
  Math::Vector3d mCenterOfMass;
  /// \brief The spatial inertia that contains both mass and inertia information
  Math::Matrix3d mInertia;

  /// \brief Id of the movable body that this fixed body is attached to.
  unsigned int mMovableParent;
  /// \brief Transforms spatial quantities expressed for the parent to the
  // fixed body.
  Math::SpatialTransform mParentTransform;
  Math::SpatialTransform mBaseTransform;

  static FixedBody CreateFromBody (const Body& body)
  {
    FixedBody fbody;

    fbody.mMass = body.mMass;
    fbody.mCenterOfMass = body.mCenterOfMass;
    fbody.mInertia = body.mInertia;

    return fbody;
  }
};

}

/* RBDL_BODY_H */
#endif
