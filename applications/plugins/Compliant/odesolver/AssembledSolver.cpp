#include "AssembledSolver.h"

#include <sofa/component/linearsolver/EigenSparseMatrix.h>
#include <sofa/component/linearsolver/EigenVector.h>
#include <sofa/core/ObjectFactory.h>
#include <sofa/simulation/common/MechanicalOperations.h>
#include <sofa/simulation/common/VectorOperations.h>

#include "assembly/AssemblyVisitor.h"
#include "misc/Stabilization.h"

#include "utils/minres.h"
#include "utils/scoped.h"



#ifdef GR_BENCHMARK
#include <sofa/helper/system/thread/CTime.h>
#endif


namespace sofa {
namespace component {
namespace odesolver {

SOFA_DECL_CLASS(AssembledSolver);
int AssembledSolverClass = core::RegisterObject("Example compliance solver using assembly").add< AssembledSolver >();

using namespace sofa::defaulttype;
using namespace sofa::helper;
using namespace core::behavior;


AssembledSolver::AssembledSolver()
	: warm_start(initData(&warm_start, 
	                      true,
	                      "warm_start",
	                      "warm start iterative solvers: avoids biasing solution towards zero (and speeds-up resolution)")),
	  propagate_lambdas(initData(&propagate_lambdas, 
	                             false,
	                             "propagate_lambdas",
	                             "propagate Lagrange multipliers in force vector at the end of time step")),
	  stabilization(initData(&stabilization, 
	                         false,
	                         "stabilization",
	                         "apply a stabilization pass on kinematic constraints requesting it")),
	  debug(initData(&debug,
	                 false,
	                 "debug",
	                 "print debug stuff")),
	alpha( initData(&alpha, 
	                SReal(1),
	                "implicitVelocity",
	                "Weight of the next forces in the average forces used to update the velocities. 1 is implicit, 0 is explicit.")),
	beta( initData(&beta,
	               SReal(1),
	               "implicitPosition",
	               "Weight of the next velocities in the average velocities used to update the positions. 1 is implicit, 0 is explicit."))
	
{
	
}

void AssembledSolver::send(simulation::Visitor& vis) {
	scoped::timer step("visitor execution");
				
	this->getContext()->executeVisitor( &vis );
}
			
		
void AssembledSolver::integrate( const core::MechanicalParams* params,
                                 core::MultiVecCoordId posId,
                                 core::MultiVecDerivId velId ) {
	scoped::timer step("position integration");
	SReal dt = params->dt();
				
	// integrate positions
	sofa::simulation::common::VectorOperations vop( params, this->getContext() );
				
	typedef core::behavior::BaseMechanicalState::VMultiOp VMultiOp;
	VMultiOp multi;
				
	multi.resize(1);
	
	multi[0].first = posId;
	multi[0].second.push_back( std::make_pair(posId, 1.0) );
	multi[0].second.push_back( std::make_pair(velId, beta.getValue() * dt) );

	vop.v_multiop( multi );
}




void AssembledSolver::alloc(const core::ExecParams& params) {
	scoped::timer step("lambdas alloc");
	sofa::simulation::common::VectorOperations vop( &params, this->getContext() );
	lagrange.realloc( &vop, false, true );
}

AssembledSolver::~AssembledSolver() {
}

void AssembledSolver::cleanup() {
	sofa::simulation::common::VectorOperations vop( core::ExecParams::defaultInstance(), this->getContext() );
	vop.v_free( lagrange.id(), false, true );
}


// this is c_k computation (see compliant-theory.pdf, section 3)
void AssembledSolver::compute_forces(const core::MechanicalParams& params) {
	scoped::timer step("forces computation");
				
	sofa::simulation::common::MechanicalOperations mop( &params, this->getContext() );
	sofa::simulation::common::VectorOperations vop( &params, this->getContext() );
				
	MultiVecDeriv c(&vop, core::VecDerivId::force() );
	
	SReal h = params.dt();
			
	// note: only for stiffness dofs
	mop.computeForceNeglectingCompliance(c); // c = f
	
	// c = h f
	c *= h;
	
	// M v_k
	SReal mfactor = 1;

	// h (1-alpha) B v_k
	SReal bfactor = h * (1 - alpha.getValue());

	// h^2 alpha (1 - beta ) K v_k
	SReal kfactor = h * h * alpha.getValue() * (1 - beta.getValue());
	
	// note: K v_k factor only for stiffness dofs
	mop.addMBKvNeglectingCompliance( c, 
	                                 mfactor,
	                                 bfactor, 
	                                 kfactor);
	
	// TODO project response ?
	
}


void AssembledSolver::propagate(const core::MechanicalParams* params) {
	simulation::MechanicalPropagatePositionAndVelocityVisitor bob( params );
	send( bob );
}			



void AssembledSolver::rhs_dynamics(vec& res, const system_type& sys, const vec& v) const {
	assert( res.size() == sys.size() );
	
	unsigned off = 0;
	
	// master dofs
	for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.master[i];
	
		unsigned dim = dofs->getMatrixSize();
		
		dofs->copyToBuffer(&res(off), core::VecDerivId::force(), dim);
		off += dim;
	}
	
	// TODO in compute_forces instead ?
	res.head( sys.m ) = sys.P * res.head( sys.m );
	
	// compliant dofs
	for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.compliant[i];
		
		unsigned dim = dofs->getMatrixSize();

		// fetch constraint value if any
		ConstraintValue::SPtr value = 
			dofs->getContext()->get<ConstraintValue>( core::objectmodel::BaseContext::Local );
		
		// fallback TODO optimize ?
		if(!value ) {
			value = new ConstraintValue;
			dofs->getContext()->addObject( value );
			value->init();
		}
		
		value->dynamics(&res(off), dim);
		off += dim;
	}
	assert( off == sys.size() );
	
	// adjust compliant value based on alpha/beta
	if( sys.n ) {

		if(alpha.getValue() != 1 ) res.tail( sys.n ) /= alpha.getValue();
		
		if( beta.getValue() != 1 ) {
            // TODO dofs->copyToBuffer(v_compliant, core::VecDerivId::vel(), dim); rather than sys.J * v, v_compliant is already mapped
            // TODO use v_compliant to implement constraint damping
			res.tail( sys.n ).noalias() = res.tail( sys.n ) - (1 - beta.getValue()) * (sys.J * v);
			res.tail( sys.n ) /= beta.getValue();
		}
		
	}
	
}

void AssembledSolver::rhs_correction(vec& res, const system_type& sys) const {
	assert( res.size() == sys.size() );
	
	// master dofs
	res.head( sys.m ).setZero();
	unsigned off = sys.m;
	
	// compliant dofs
	
	for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.compliant[i];
		
		unsigned dim = dofs->getMatrixSize();
	
		// fetch constraint value if any
		ConstraintValue::SPtr value = 
			dofs->getContext()->get<ConstraintValue>( core::objectmodel::BaseContext::Local );
	
		// fallback TODO optimize ?
		if(!value ) {
			value = new ConstraintValue;
			dofs->getContext()->addObject( value );
			value->init();
		}
		
		value->correction(&res(off), dim);
		
		off += dim;
	}
	
}


void AssembledSolver::buildMparams(core::MechanicalParams& mparams,
                                   core::MechanicalParams& mparamsWithoutStiffness,
                                   const core::ExecParams& params,
                                   double dt) const
{

	SReal mfactor = 1.0;
	SReal bfactor = -dt * alpha.getValue();
	SReal kfactor = -dt * dt * alpha.getValue() * beta.getValue();


	mparams.setExecParams( &params );
	mparams.setMFactor( mfactor );
	mparams.setBFactor( bfactor );
	mparams.setKFactor( kfactor );
	mparams.setDt( dt );
    
	mparams.setImplicitVelocity( alpha.getValue() );
	mparams.setImplicitPosition( beta.getValue() );

	// to treat compliant components (ie stiffness components treated
	// as compliance)
    
	mparamsWithoutStiffness.setExecParams( &params );
	mparamsWithoutStiffness.setMFactor( mfactor );
	mparamsWithoutStiffness.setBFactor( bfactor );
	mparamsWithoutStiffness.setKFactor( 0 ); 
	mparamsWithoutStiffness.setDt( dt );

	mparamsWithoutStiffness.setImplicitVelocity( alpha.getValue() );
	mparamsWithoutStiffness.setImplicitPosition( beta.getValue() );
}
			


void AssembledSolver::get_state(vec& res, const system_type& sys) const {
	
	assert( res.size() == sys.size() );
	
	unsigned off = 0;
	
	for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.master[i];
	
		unsigned dim = dofs->getMatrixSize();
		
		dofs->copyToBuffer(&res(off), core::VecDerivId::velocity(), dim);
		off += dim;
	}

	for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.compliant[i];
		
		unsigned dim = dofs->getMatrixSize();
		
		dofs->copyToBuffer(&res(off), lagrange.id().getId( dofs ), dim);
		off += dim;
	}
	
	assert( off == sys.size() );
	// TODO multiply lambda by dt ?
	
}


void AssembledSolver::set_state(const system_type& sys, const vec& data) const {
	
	assert( data.size() == sys.size() );
	
	unsigned off = 0;
	
	// TODO project v ?
	for(unsigned i = 0, end = sys.master.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.master[i];
	
		unsigned dim = dofs->getMatrixSize();
		
		dofs->copyFromBuffer(core::VecDerivId::velocity(), &data(off), dim);
		off += dim;
	}

	
	// TODO divide lambda by dt ?
	for(unsigned i = 0, end = sys.compliant.size(); i < end; ++i) {
		system_type::dofs_type* dofs = sys.compliant[i];
		
		unsigned dim = dofs->getMatrixSize();
		
		dofs->copyFromBuffer(lagrange.id().getId( dofs ), &data(off) , dim);
		off += dim;
	}
	
	
}

void AssembledSolver::solve(const core::ExecParams* /*params*/,
                                    double /*dt*/,
                                    core::MultiVecCoordId /*posId*/,
                                    core::MultiVecDerivId /*velId*/,
                                    bool /*computeForce*/, // should the right part of the implicit system be computed?
                                    bool /*integratePosition*/, // should the position be updated?
                                    simulation::AssemblyVisitor */*vis*/) {

	throw std::logic_error("broken lol ! subclass AssembledSolver if you need customization");

} 

void AssembledSolver::solve(const core::ExecParams* /*params*/,
                                    double /*dt*/,
                                    core::MultiVecCoordId /*posId*/,
                                    core::MultiVecDerivId /*velId*/,
                                    bool /*computeForce*/, // should the right part of the implicit system be computed?
                                    bool /*integratePosition*/ // should the position be updated?
	) {

	throw std::logic_error("broken lol ! subclass AssembledSolver if you need customization");
	
}

void AssembledSolver::solve(const core::ExecParams* params,
                            double dt,
                            core::MultiVecCoordId posId,
                            core::MultiVecDerivId velId) {
	assert(kkt);
	
	// mechanical parameters
	core::MechanicalParams mparams_stiffness, mparams_compliance;
	this->buildMparams( mparams_stiffness, mparams_compliance, *params, dt );

	compute_forces( mparams_stiffness );
	
	// assembly visitor 
	simulation::AssemblyVisitor vis(&mparams_stiffness,
	                                &mparams_compliance);
	// fetch data
	send( vis );
	
	// assemble system
	system_type sys = vis.assemble();
	
	// debugging
	if( debug.getValue() ) sys.debug();
	
	// system factor
	{
		scoped::timer step("system factor");
		kkt->factor( sys );
	}

	// backup current state as correction might erase it, if any
	vec current( sys.size() );
	get_state( current, sys );

	// system solution / rhs 
	vec x(sys.size());
	vec rhs(sys.size());
	
	// ready to solve yo
	{
		scoped::timer step("system solve");
		
		// constraint stabilization
		if( stabilization.getValue() ) {
			scoped::timer step("correction");
			
			x = vec::Zero( sys.size() );
			rhs_correction(rhs, sys);

			kkt->solve(x, sys, rhs);

			if( debug.getValue() ) {
				std::cerr << "correction rhs:" << std::endl 
				          << rhs.transpose() << std::endl
				          << "solution:" << std::endl
				          << x.transpose() << std::endl;
			}

			set_state(sys, x);
			integrate( &mparams_compliance, posId, velId );
		}

		// actual dynamics
		{
			scoped::timer step("dynamics");
			x = vec::Zero( sys.size() );
			
			if( warm_start.getValue() ) x = current;
			rhs_dynamics(rhs, sys, current.head(sys.m) );
			
			kkt->solve(x, sys, rhs);

			if( debug.getValue() ) {
				std::cerr << "dynamics rhs:" << std::endl 
				          << rhs.transpose() << std::endl
				          << "solution:" << std::endl
				          << x.transpose() << std::endl;
			}

			set_state(sys, x);
			integrate( &mparams_stiffness, posId, velId );
		}
		
	}
	
	// propagate lambdas if asked to
	if( propagate_lambdas.getValue() ) {
		scoped::timer step("lambda propagation");
		propagate_visitor prop( &mparams_stiffness );
		prop.out = core::VecId::force();
		prop.in = lagrange.id();
			
		send( prop );
	}

}

			
			
void AssembledSolver::init() {
				
	// do want KKTSolver !
	kkt = this->getContext()->get<kkt_type>(core::objectmodel::BaseContext::Local);
	
	// TODO slightly less dramatic error, maybe ?
	if( !kkt ) throw std::logic_error("AssembledSolver needs a KKTSolver lol");

}



}
}
}