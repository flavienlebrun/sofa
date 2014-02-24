/******************************************************************************
*       SOFA, Simulation Open-Framework Architecture, version 1.0 RC 1        *
*                (c) 2006-2011 MGH, INRIA, USTL, UJF, CNRS                    *
*                                                                             *
* This library is free software; you can redistribute it and/or modify it     *
* under the terms of the GNU Lesser General Public License as published by    *
* the Free Software Foundation; either version 2.1 of the License, or (at     *
* your option) any later version.                                             *
*                                                                             *
* This library is distributed in the hope that it will be useful, but WITHOUT *
* ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or       *
* FITNESS FOR A PARTICULAR PURPOSE. See the GNU Lesser General Public License *
* for more details.                                                           *
*                                                                             *
* You should have received a copy of the GNU Lesser General Public License    *
* along with this library; if not, write to the Free Software Foundation,     *
* Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA.          *
*******************************************************************************
*                               SOFA :: Plugins                               *
*                                                                             *
* Authors: The SOFA Team and external contributors (see Authors.txt)          *
*                                                                             *
* Contact information: contact@sofa-framework.org                             *
******************************************************************************/
#ifndef SOFA_COMPONENT_MAPPING_BaseDeformationMultiMAPPING_H
#define SOFA_COMPONENT_MAPPING_BaseDeformationMultiMAPPING_H

#include "../initFlexible.h"
#include <sofa/core/Multi2Mapping.h>
#include <sofa/component/component.h>
#include <sofa/helper/vector.h>
#include <sofa/defaulttype/Vec.h>
#include <sofa/defaulttype/Mat.h>
#include "../types/DeformationGradientTypes.h"
#include <sofa/simulation/common/Simulation.h>

#include "../shapeFunction/BaseShapeFunction.h"
#include <sofa/component/topology/TopologyData.inl>
#include <sofa/core/topology/BaseMeshTopology.h>
#include <sofa/component/container/MechanicalObject.h>
#include <sofa/core/visual/VisualParams.h>
#include <sofa/helper/OptionsGroup.h>
#include <sofa/helper/kdTree.inl>

#include <sofa/component/linearsolver/EigenSparseMatrix.h>

#include "BaseDeformationMapping.h"

namespace sofa
{
namespace component
{
namespace mapping
{

using helper::vector;

/// This class can be overridden if needed for additionnal storage within template specializations.
template<class InDataTypes1,class InDataTypes2, class OutDataTypes>
class BaseDeformationMultiMappingInternalData
{
public:
};


// not templated BaseDeformationMultiMapping for identification
class BaseDeformationMultiMapping
{
protected:

    virtual ~BaseDeformationMultiMapping() {}

public:

    /// \returns the from1 model size
    virtual size_t getFromSize1() const = 0;
    /// \returns the from2 model size
    virtual size_t getFromSize2() const = 0;
    /// \returns the to model size
    virtual size_t getToSize() const = 0;
};



/** Abstract mapping (one parent->several children with different influence) using JacobianBlocks or sparse eigen matrix
*/

template <class JacobianBlockType1,class JacobianBlockType2>
class BaseDeformationMultiMappingT : public BaseDeformationMultiMapping, public core::Multi2Mapping<typename JacobianBlockType1::In,typename JacobianBlockType2::In,typename JacobianBlockType1::Out>, public BasePointMapper<JacobianBlockType1::Out::spatial_dimensions,typename JacobianBlockType1::In::Real>
{
public:
    typedef core::Multi2Mapping<typename JacobianBlockType1::In, typename JacobianBlockType2::In, typename JacobianBlockType1::Out> Inherit;
    SOFA_ABSTRACT_CLASS2(SOFA_TEMPLATE2(BaseDeformationMultiMappingT,JacobianBlockType1,JacobianBlockType2), SOFA_TEMPLATE3(core::Multi2Mapping,typename JacobianBlockType1::In,typename JacobianBlockType2::In,typename JacobianBlockType1::Out), SOFA_TEMPLATE2(BasePointMapper,JacobianBlockType1::Out::spatial_dimensions,typename JacobianBlockType1::In::Real) );

    /** @name  Input types    */
    //@{
    typedef typename JacobianBlockType1::In In1;
    typedef typename In1::Coord InCoord1;
    typedef typename In1::Deriv InDeriv1;
    typedef typename In1::VecCoord InVecCoord1;
    typedef typename In1::VecDeriv InVecDeriv1;
    typedef typename In1::MatrixDeriv InMatrixDeriv1;
    typedef typename In1::Real Real;

    typedef typename JacobianBlockType2::In In2;
    typedef typename In2::Coord InCoord2;
    typedef typename In2::Deriv InDeriv2;
    typedef typename In2::VecCoord InVecCoord2;
    typedef typename In2::VecDeriv InVecDeriv2;
    typedef typename In2::MatrixDeriv InMatrixDeriv2;
    //@}

    /** @name  Output types    */
    //@{
    typedef typename JacobianBlockType1::Out Out;
    typedef typename Out::Coord OutCoord;
    typedef typename Out::Deriv OutDeriv;
    typedef typename Out::VecCoord OutVecCoord;
    typedef typename Out::VecDeriv OutVecDeriv;
    typedef typename Out::MatrixDeriv OutMatrixDeriv;
    enum { spatial_dimensions = Out::spatial_dimensions };
    enum { material_dimensions = OutDataTypesInfo<Out>::material_dimensions };
    //@}

    /** @name  Shape Function types    */
    //@{
    typedef core::behavior::ShapeFunctionTypes<spatial_dimensions,Real> ShapeFunctionType;
    typedef core::behavior::BaseShapeFunction<ShapeFunctionType> BaseShapeFunction;
    typedef typename BaseShapeFunction::VReal VReal;
    typedef typename BaseShapeFunction::Gradient Gradient;
    typedef typename BaseShapeFunction::VGradient VGradient;
    typedef typename BaseShapeFunction::Hessian Hessian;
    typedef typename BaseShapeFunction::VHessian VHessian;
    typedef typename BaseShapeFunction::VRef VRef;
    typedef typename BaseShapeFunction::MaterialToSpatial MaterialToSpatial ; ///< MaterialToSpatial transformation = deformation gradient type
    typedef typename BaseShapeFunction::VMaterialToSpatial VMaterialToSpatial;
    typedef typename BaseShapeFunction::Coord mCoord; ///< material coordinates
    //@}

    /** @name  Coord types    */
    //@{
    typedef Vec<spatial_dimensions,Real> Coord ; ///< spatial coordinates
    typedef vector<Coord> VecCoord;
    typedef helper::kdTree<Coord> KDT;      ///< kdTree for fast search of closest mapped points
    typedef typename KDT::distanceSet distanceSet;
    //@}

    /** @name  Jacobian types    */
    //@{
    typedef JacobianBlockType1 BlockType1;
    typedef vector<vector<BlockType1> >  SparseMatrix1;
    typedef JacobianBlockType2 BlockType2;
    typedef vector<vector<BlockType2> >  SparseMatrix2;

    typedef typename BlockType1::MatBlock  MatBlock1;  ///< Jacobian block matrix
    typedef linearsolver::EigenSparseMatrix<In1,Out>    SparseMatrixEigen1;
    typedef typename BlockType2::MatBlock  MatBlock2;  ///< Jacobian block matrix
    typedef linearsolver::EigenSparseMatrix<In2,Out>    SparseMatrixEigen2;

    typedef typename BlockType1::KBlock  KBlock1;  ///< stiffness block matrix
    typedef linearsolver::EigenSparseMatrix<In1,In1>    SparseKMatrixEigen1;
    typedef typename BlockType2::KBlock  KBlock2;  ///< stiffness block matrix
    typedef linearsolver::EigenSparseMatrix<In2,In2>    SparseKMatrixEigen2;
    //@}


    void resizeOut(); /// automatic resizing (of output model and jacobian blocks) when input samples have changed. Recomputes weights from shape function component.
    virtual void resizeOut(const vector<Coord>& position0, vector<vector<unsigned int> > index,vector<vector<Real> > w, vector<vector<Vec<spatial_dimensions,Real> > > dw, vector<vector<Mat<spatial_dimensions,spatial_dimensions,Real> > > ddw, vector<Mat<spatial_dimensions,spatial_dimensions,Real> > F0); /// resizing given custom positions and weights

    /** @name Mapping functions */
    //@{
    virtual void init();
    virtual void reinit();

    void apply(const core::MechanicalParams * /*mparams*/ , Data<OutVecCoord>& dOut, const Data<InVecCoord1>& dIn1, const Data<InVecCoord2>& dIn2);
    virtual void apply(const core::MechanicalParams* mparams,const helper::vector<Data<OutVecCoord>*>& dOut, const helper::vector<const Data<InVecCoord1>*>& dIn1, const helper::vector<const Data<InVecCoord2>*>& dIn2)
    {
        apply(mparams,*dOut[0],*dIn1[0],*dIn2[0]);
    }
    void applyJ(const core::MechanicalParams * /*mparams*/ , Data<OutVecDeriv>& dOut, const Data<InVecDeriv1>& dIn1, const Data<InVecDeriv2>& dIn2);
    virtual void applyJ(const core::MechanicalParams* mparams , const helper::vector<Data<OutVecDeriv>*>& dOut, const helper::vector<const Data<InVecDeriv1>*>& dIn1, const helper::vector<const Data<InVecDeriv2>*>& dIn2)
    {
        if(this->isMechanical())
        applyJ(mparams,*dOut[0],*dIn1[0],*dIn2[0]);
    }
    void applyJT(const core::MechanicalParams * /*mparams*/ , Data<InVecDeriv1>& dIn1, Data<InVecDeriv2>& dIn2, const Data<OutVecDeriv>& dOut);
    virtual void applyJT(const core::MechanicalParams* mparams , const helper::vector<Data<InVecDeriv1>*>& dIn1,  const helper::vector<Data<InVecDeriv2>*>& dIn2, const helper::vector<const Data<OutVecDeriv>*>& dOut)
    {
        if(this->isMechanical())
        applyJT(mparams,*dIn1[0],*dIn2[0],*dOut[0]);
    }
    void applyJT(const core::ConstraintParams * /*cparams*/ , Data<InMatrixDeriv1>& /*out1*/, Data<InMatrixDeriv2>& /*out2*/,  const Data<OutMatrixDeriv>& /*in*/);
    virtual void applyJT(const core::ConstraintParams* cparams ,const helper::vector<Data<InMatrixDeriv1>*>& dIn1,const  helper::vector<Data<InMatrixDeriv2>*>& dIn2, const helper::vector<const Data<OutMatrixDeriv>*>& dOut)
    {
        if(this->isMechanical())
        applyJT(cparams,*dIn1[0],*dIn2[0],*dOut[0]);
    }
    virtual void applyDJT(const core::MechanicalParams* mparams, core::MultiVecDerivId parentDfId, core::ConstMultiVecDerivId );

    // Compliant plugin experimental API
    virtual const vector<sofa::defaulttype::BaseMatrix*>* getJs()
    {
        if(!this->assemble.getValue()) { updateJ1(); updateJ2(); }
        else
        {
            if( this->maskTo && this->maskTo->isInUse() && previousMask!=this->maskTo->getEntries() )
            {
                previousMask = this->maskTo->getEntries();
                updateJ1();
                updateJ2();
            }
            else
            {
                if(!BlockType1::constant) updateJ1();
                if(!BlockType2::constant) updateJ2();
            }
        }

        return &baseMatrices;
    }

    virtual const vector<defaulttype::BaseMatrix*>* getKs()
    {
        updateK1(this->toModel->readForces().ref());
        updateK2(this->toModel->readForces().ref());
        return &stiffnessBaseMatrices;
    }

    void draw(const core::visual::VisualParams* vparams);

    //@}

    virtual size_t getFromSize1() const { return this->fromModels1[0]->getSize(); }
    virtual size_t getFromSize2() const { return this->fromModels2[0]->getSize(); }
    virtual size_t getToSize()  const { return this->toModel->getSize(); }


    /** @name PointMapper functions */
    //@{
    virtual void ForwardMapping(Coord& p,const Coord& p0);
    virtual void BackwardMapping(Coord& p0,const Coord& p,const Real Thresh=1e-5, const size_t NbMaxIt=10);
    virtual unsigned int getClosestMappedPoint(const Coord& p, Coord& x0,Coord& x, bool useKdTree=false);

    virtual void mapPosition(Coord& p,const Coord &p0, const VRef& ref, const VReal& w)=0;
    virtual void mapDeformationGradient(MaterialToSpatial& F, const Coord &p0, const MaterialToSpatial& M, const VRef& ref, const VReal& w, const VGradient& dw)=0;
    //@}

//    SparseMatrix& getJacobianBlocks()
//    {
//        if(!this->assemble.getValue() || !BlockType::constant) updateJ();
//        return jacobian;
//    }

    Data<std::string> f_shapeFunction_name; ///< name of the shape function component (optional: if not specified, will searchup)
    BaseShapeFunction* _shapeFunction;        ///< where the weights are computed
    Data<vector<VRef> > f_index;            ///< The numChildren * numRefs column indices. index[i][j] is the index of the j-th parent influencing child i.
    Data<vector<VRef> > f_index1;            ///< The numChildren * numRefs column indices. index1[i][j] is the index of the j-th parent of type 1 influencing child i.
    Data<vector<VRef> > f_index2;            ///< The numChildren * numRefs column indices. index2[i][j] is the index of the j-th parent of type 2 influencing child i.
    vector<VRef> f_index_parentToChild1;            ///< Constructed at init from f_index1 to parallelize applyJT. index_parentToChild[i][j] is the index of the j-th children influenced by parent i of type 1.
    vector<VRef> f_index_parentToChild2;            ///< Constructed at init from f_index2 to parallelize applyJT. index_parentToChild[i][j] is the index of the j-th children influenced by parent i of type 2.
    Data<vector<VReal> >       f_w;
    Data<vector<VGradient> >   f_dw;
    Data<vector<VHessian> >    f_ddw;
    Data<VMaterialToSpatial>    f_F0;
    Data< vector<int> > f_cell;    ///< indices required by shape function in case of overlapping elements


    Data<bool> assemble;

protected:
    BaseDeformationMultiMappingT ();
    virtual ~BaseDeformationMultiMappingT()     { }

    Data<VecCoord >    f_pos0; ///< initial spatial positions of children

    VecCoord f_pos;
    void mapPositions() ///< map initial spatial positions stored in f_pos0 to f_pos (used for visualization)
    {
        this->f_pos.resize(this->f_pos0.getValue().size());
        for(size_t i=0; i<this->f_pos.size(); i++ ) mapPosition(f_pos[i],this->f_pos0.getValue()[i],this->f_index.getValue()[i],this->f_w.getValue()[i]);
    }
    KDT f_KdTree;

    VMaterialToSpatial f_F;
    void mapDeformationGradients() ///< map initial deform  gradients stored in f_F0 to f_F      (used for visualization)
    {
        this->f_F.resize(this->f_pos0.getValue().size());
        for(size_t i=0; i<this->f_F.size(); i++ ) mapDeformationGradient(f_F[i],this->f_pos0.getValue()[i],this->f_F0.getValue()[i],this->f_index.getValue()[i],this->f_w.getValue()[i],this->f_dw.getValue()[i]);
    }

    bool missingInformationDirty;  ///< tells if pos or F need to be updated (to speed up visualization)
    bool KdTreeDirty;              ///< tells if kdtree need to be updated (to speed up closest point search)

    SparseMatrix1 jacobian1;   ///< Jacobian of the mapping
    SparseMatrix2 jacobian2;   ///< Jacobian of the mapping
    virtual void initJacobianBlocks()=0;

    core::State<In1>* fromModel1;   ///< DOF of the master1
    core::State<In2>* fromModel2;   ///< DOF of the master2
    core::State<Out>* toModel;      ///< DOF of the slave

    helper::ParticleMask* maskFrom1;  ///< Subset of master DOF, to cull out computations involving null forces or displacements
    helper::ParticleMask* maskFrom2;  ///< Subset of master DOF, to cull out computations involving null forces or displacements
    helper::ParticleMask* maskTo;    ///< Subset of slave DOF, to cull out computations involving null forces or displacements



    bool J1Dirty, J2Dirty; ///< Does J needs to be updated?
    SparseMatrixEigen1 eigenJacobian1;  ///< Assembled Jacobian matrix
    void updateJ1();
    SparseMatrixEigen2 eigenJacobian2;  ///< Assembled Jacobian matrix
    void updateJ2();
    vector<defaulttype::BaseMatrix*> baseMatrices;      ///< Vector of jacobian matrices, for the Compliant plugin API

    SparseKMatrixEigen1 K1;  ///< Assembled geometric stiffness matrix
    void updateK1(const OutVecDeriv& childForce);
    SparseKMatrixEigen2 K2;  ///< Assembled geometric stiffness matrix
    void updateK2(const OutVecDeriv& childForce);
    vector<defaulttype::BaseMatrix*> stiffnessBaseMatrices;      ///< Vector of geometric stiffness matrices, for the Compliant plugin API
    helper::ParticleMask::InternalStorage previousMask; ///< storing previous dof maskTo to check if it changed from last time step to updateJ in consequence

    const core::topology::BaseMeshTopology::SeqTriangles *triangles; // Used for visualization
    const defaulttype::ResizableExtVector<core::topology::BaseMeshTopology::Triangle> *extTriangles;
    const defaulttype::ResizableExtVector<int> *extvertPosIdx;
    Data< float > showDeformationGradientScale;
    Data< helper::OptionsGroup > showDeformationGradientStyle;
    Data< helper::OptionsGroup > showColorOnTopology;
    Data< float > showColorScale;
};


} // namespace mapping
} // namespace component
} // namespace sofa

#endif