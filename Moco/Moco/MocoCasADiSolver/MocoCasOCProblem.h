#ifndef MOCO_MOCOCASOCPROBLEM_H
#define MOCO_MOCOCASOCPROBLEM_H
/* -------------------------------------------------------------------------- *
 * OpenSim Moco: MocoCasOCProblem.h                                           *
 * -------------------------------------------------------------------------- *
 * Copyright (c) 2018 Stanford University and the Authors                     *
 *                                                                            *
 * Author(s): Christopher Dembia                                              *
 *                                                                            *
 * Licensed under the Apache License, Version 2.0 (the "License"); you may    *
 * not use this file except in compliance with the License. You may obtain a  *
 * copy of the License at http://www.apache.org/licenses/LICENSE-2.0          *
 *                                                                            *
 * Unless required by applicable law or agreed to in writing, software        *
 * distributed under the License is distributed on an "AS IS" BASIS,          *
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.   *
 * See the License for the specific language governing permissions and        *
 * limitations under the License.                                             *
 * -------------------------------------------------------------------------- */

#include "../Components/AccelerationMotion.h"
#include "../Components/DiscreteForces.h"
#include "../MocoBounds.h"
#include "../MocoProblemRep.h"
#include "CasOCProblem.h"
#include "MocoCasADiSolver.h"

namespace OpenSim {

using VectorDM = std::vector<casadi::DM>;

inline CasOC::Bounds convertBounds(const MocoBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}
inline CasOC::Bounds convertBounds(const MocoInitialBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}
inline CasOC::Bounds convertBounds(const MocoFinalBounds& mb) {
    return {mb.getLower(), mb.getUpper()};
}

/// This converts a SimTK::Matrix to a casadi::DM matrix, transposing the
/// data in the process.
inline casadi::DM convertToCasADiDMTranspose(const SimTK::Matrix& simtkMatrix) {
    casadi::DM out(simtkMatrix.ncol(), simtkMatrix.nrow());
    for (int irow = 0; irow < simtkMatrix.nrow(); ++irow) {
        for (int icol = 0; icol < simtkMatrix.ncol(); ++icol) {
            out(icol, irow) = simtkMatrix(irow, icol);
        }
    }
    return out;
}

template <typename T> casadi::DM convertToCasADiDMTemplate(const T& simtk) {
    casadi::DM out(casadi::Sparsity::dense(simtk.size(), 1));
    std::copy_n(simtk.getContiguousScalarData(), simtk.size(), out.ptr());
    return out;
}
/// This converts a SimTK::RowVector to a casadi::DM column vector.
inline casadi::DM convertToCasADiDMTranspose(const SimTK::RowVector& simtkRV) {
    return convertToCasADiDMTemplate(simtkRV);
}
/// This converts a SimTK::Vector to a casadi::DM column vector.
inline casadi::DM convertToCasADiDM(const SimTK::Vector& simtkVec) {
    return convertToCasADiDMTemplate(simtkVec);
}

/// This resamples the iterate to obtain values that lie on the mesh.
inline CasOC::Iterate convertToCasOCIterate(const MocoTrajectory& mocoIt) {
    CasOC::Iterate casIt;
    CasOC::VariablesDM& casVars = casIt.variables;
    using CasOC::Var;
    casVars[Var::initial_time] = mocoIt.getInitialTime();
    casVars[Var::final_time] = mocoIt.getFinalTime();
    casVars[Var::states] =
            convertToCasADiDMTranspose(mocoIt.getStatesTrajectory());
    casVars[Var::controls] =
            convertToCasADiDMTranspose(mocoIt.getControlsTrajectory());
    casVars[Var::multipliers] =
            convertToCasADiDMTranspose(mocoIt.getMultipliersTrajectory());
    if (!mocoIt.getSlackNames().empty()) {
        casVars[Var::slacks] =
                convertToCasADiDMTranspose(mocoIt.getSlacksTrajectory());
    }
    if (!mocoIt.getDerivativeNames().empty()) {
        casVars[Var::derivatives] =
                convertToCasADiDMTranspose(mocoIt.getDerivativesTrajectory());
    }
    casVars[Var::parameters] =
            convertToCasADiDMTranspose(mocoIt.getParameters());
    casIt.times = convertToCasADiDMTranspose(mocoIt.getTime());
    casIt.state_names = mocoIt.getStateNames();
    casIt.control_names = mocoIt.getControlNames();
    casIt.multiplier_names = mocoIt.getMultiplierNames();
    casIt.slack_names = mocoIt.getSlackNames();
    casIt.derivative_names = mocoIt.getDerivativeNames();
    casIt.parameter_names = mocoIt.getParameterNames();
    return casIt;
}

template <typename VectorType = SimTK::Vector>
VectorType convertToSimTKVector(const casadi::DM& casVector) {
    OPENSIM_THROW_IF(casVector.columns() != 1 && casVector.rows() != 1,
            Exception,
            format("casVector should be 1-dimensional, but has size %i x "
                   "%i.",
                    casVector.rows(), casVector.columns()));
    VectorType simtkVector((int)casVector.numel());
    for (int i = 0; i < casVector.numel(); ++i) {
        simtkVector[i] = double(casVector(i));
    }
    return simtkVector;
}

/// This converts a casadi::DM matrix to a
/// SimTK::Matrix, transposing the data in the process.
inline SimTK::Matrix convertToSimTKMatrix(const casadi::DM& casMatrix) {
    SimTK::Matrix simtkMatrix((int)casMatrix.columns(), (int)casMatrix.rows());
    for (int irow = 0; irow < casMatrix.rows(); ++irow) {
        for (int icol = 0; icol < casMatrix.columns(); ++icol) {
            simtkMatrix(icol, irow) = double(casMatrix(irow, icol));
        }
    }
    return simtkMatrix;
}

template <typename TOut = MocoTrajectory>
TOut convertToMocoTrajectory(const CasOC::Iterate& casIt) {
    SimTK::Matrix simtkStates;
    const auto& casVars = casIt.variables;
    using CasOC::Var;
    if (!casIt.state_names.empty()) {
        simtkStates = convertToSimTKMatrix(casVars.at(Var::states));
    }
    SimTK::Matrix simtkControls;
    if (!casIt.control_names.empty()) {
        simtkControls = convertToSimTKMatrix(casVars.at(Var::controls));
    }
    SimTK::Matrix simtkMultipliers;
    if (!casIt.multiplier_names.empty()) {
        const auto multsValue = casVars.at(Var::multipliers);
        simtkMultipliers = convertToSimTKMatrix(multsValue);
    }
    SimTK::Matrix simtkSlacks;
    if (!casIt.slack_names.empty()) {
        const auto slacksValue = casVars.at(Var::slacks);
        simtkSlacks = convertToSimTKMatrix(slacksValue);
    }
    SimTK::Matrix simtkDerivatives;
    auto derivativeNames = casIt.derivative_names;
    if (casVars.count(Var::derivatives) &&
            casVars.at(Var::derivatives).numel()) {
        const auto derivsValue = casVars.at(Var::derivatives);
        simtkDerivatives = convertToSimTKMatrix(derivsValue);
    } else {
        derivativeNames.clear();
    }
    SimTK::RowVector simtkParameters;
    if (!casIt.parameter_names.empty()) {
        const auto paramsValue = casVars.at(Var::parameters);
        simtkParameters = convertToSimTKVector<SimTK::RowVector>(paramsValue);
    }
    SimTK::Vector simtkTimes = convertToSimTKVector(casIt.times);

    TOut mocoTraj(simtkTimes, casIt.state_names, casIt.control_names,
            casIt.multiplier_names, derivativeNames, casIt.parameter_names,
            simtkStates, simtkControls, simtkMultipliers, simtkDerivatives,
            simtkParameters);

    // Append slack variables. MocoTrajectory requires the slack variables to be
    // the same length as its time vector, but it will not be if the
    // CasOC::Iterate was generated from a CasOC::Transcription object.
    // Therefore, slack variables are interpolated as necessary.
    if (!casIt.slack_names.empty()) {
        int simtkSlacksLength = simtkSlacks.nrow();
        SimTK::Vector slackTime = createVectorLinspace(simtkSlacksLength,
                simtkTimes[0], simtkTimes[simtkTimes.size() - 1]);
        for (int i = 0; i < (int)casIt.slack_names.size(); ++i) {
            if (simtkSlacksLength != simtkTimes.size()) {
                mocoTraj.appendSlack(casIt.slack_names[i],
                        interpolate(slackTime, simtkSlacks.col(i), simtkTimes));
            } else {
                mocoTraj.appendSlack(casIt.slack_names[i], simtkSlacks.col(i));
            }
        }
    }
    return mocoTraj;
}

class MocoCasOCProblem : public CasOC::Problem {
public:
    MocoCasOCProblem(const MocoCasADiSolver& mocoCasADiSolver,
            const MocoProblemRep& mocoProblemRep,
            std::unique_ptr<ThreadsafeJar<const MocoProblemRep>> jar,
            std::string dynamicsMode);

    int getJarSize() const { return (int)m_jar->size(); }

private:
    void calcMultibodySystemExplicit(const ContinuousInput& input,
            bool calcKCErrors,
            MultibodySystemExplicitOutput& output) const override {
        auto mocoProblemRep = m_jar->take();

        const auto& modelBase = mocoProblemRep->getModelBase();
        auto& simtkStateBase = mocoProblemRep->updStateBase();

        const auto& modelDisabledConstraints =
                mocoProblemRep->getModelDisabledConstraints();
        auto& simtkStateDisabledConstraints =
                mocoProblemRep->updStateDisabledConstraints();

        applyInput(input.time, input.states, input.controls, input.multipliers,
                input.derivatives, input.parameters, mocoProblemRep);

        // Compute the accelerations.
        modelDisabledConstraints.realizeAcceleration(
                simtkStateDisabledConstraints);

        // Compute kinematic constraint errors if they exist.
        if (getNumMultipliers() && calcKCErrors) {
            calcKinematicConstraintErrors(modelBase, simtkStateBase,
                    simtkStateDisabledConstraints,
                    output.kinematic_constraint_errors);
        }

        // Copy state derivative values to output.
        const auto& udot = simtkStateDisabledConstraints.getUDot();
        const auto& zdot = simtkStateDisabledConstraints.getZDot();
        std::copy_n(udot.getContiguousScalarData(), udot.size(),
                output.multibody_derivatives.ptr());
        std::copy_n(zdot.getContiguousScalarData(), zdot.size(),
                output.auxiliary_derivatives.ptr());

        m_jar->leave(std::move(mocoProblemRep));
    }
    void calcMultibodySystemImplicit(const ContinuousInput& input,
            bool calcKCErrors,
            MultibodySystemImplicitOutput& output) const override {
        auto mocoProblemRep = m_jar->take();

        // Original model and its associated state. These are used to calculate
        // kinematic constraint forces and errors.
        const auto& modelBase = mocoProblemRep->getModelBase();
        auto& simtkStateBase = mocoProblemRep->updStateBase();

        // Model with disabled constriants and its associated state. These are
        // used to compute the accelerations.
        const auto& modelDisabledConstraints =
                mocoProblemRep->getModelDisabledConstraints();
        auto& simtkStateDisabledConstraints =
                mocoProblemRep->updStateDisabledConstraints();

        applyInput(input.time, input.states, input.controls, input.multipliers,
                input.derivatives, input.parameters, mocoProblemRep);

        modelDisabledConstraints.realizeAcceleration(
                simtkStateDisabledConstraints);

        // Compute kinematic constraint errors if they exist.
        // TODO: Do not enforce kinematic constraints if prescribedKinematics,
        // but must make sure the prescribedKinematics already obey the
        // constraints. This is simple at the q and u level (using assemble()),
        // but what do we do for the acceleration level?
        if (getNumMultipliers() && calcKCErrors) {
            calcKinematicConstraintErrors(modelBase, simtkStateBase,
                    simtkStateDisabledConstraints,
                    output.kinematic_constraint_errors);
        }

        const SimTK::SimbodyMatterSubsystem& matterDisabledConstraints =
                modelDisabledConstraints.getMatterSubsystem();
        SimTK::Vector simtkResidual((int)output.multibody_residuals.rows(),
                output.multibody_residuals.ptr(), true);
        matterDisabledConstraints.findMotionForces(
                simtkStateDisabledConstraints, simtkResidual);

        // Copy auxiliary dynamics to output.
        const auto& zdot = simtkStateDisabledConstraints.getZDot();
        std::copy_n(zdot.getContiguousScalarData(), zdot.size(),
                output.auxiliary_derivatives.ptr());

        m_jar->leave(std::move(mocoProblemRep));
    }
    void calcVelocityCorrection(const double& time,
            const casadi::DM& multibody_states, const casadi::DM& slacks,
            const casadi::DM& parameters,
            casadi::DM& velocity_correction) const override {
        auto mocoProblemRep = m_jar->take();

        const auto& modelBase = mocoProblemRep->getModelBase();
        auto& simtkStateBase = mocoProblemRep->updStateBase();

        // Update the model and state.
        applyParametersToModelProperties(parameters, *mocoProblemRep);
        convertToSimTKState(
                time, multibody_states, modelBase, simtkStateBase, false);
        modelBase.realizeVelocity(simtkStateBase);

        // Apply velocity correction to qdot if at a mesh interval midpoint.
        // This correction modifies the dynamics to enable a projection of
        // the model coordinates back onto the constraint manifold whenever
        // they deviate.
        // Posa, Kuindersma, Tedrake, 2016. "Optimization and stabilization
        // of trajectories for constrained dynamical systems"
        // Note: Only supported for the Hermite-Simpson transcription
        // scheme.
        const SimTK::SimbodyMatterSubsystem& matterBase =
                modelBase.getMatterSubsystem();

        SimTK::Vector gamma(getNumSlacks(), slacks.ptr(), true);
        SimTK::Vector qdotCorr((int)velocity_correction.rows(),
                velocity_correction.ptr(), true);
        matterBase.multiplyByGTranspose(simtkStateBase, gamma, qdotCorr);

        m_jar->leave(std::move(mocoProblemRep));
    }
    void calcIntegrand(int integralIndex, const ContinuousInput& input,
            double& integrand) const override {
        auto mocoProblemRep = m_jar->take();
        applyInput(input.time, input.states, input.controls, input.multipliers,
                input.derivatives, input.parameters, mocoProblemRep);

        auto& simtkStateDisabledConstraints =
                mocoProblemRep->updStateDisabledConstraints();

        const auto& mocoCost = mocoProblemRep->getCostByIndex(integralIndex);
        integrand = mocoCost.calcIntegrand(simtkStateDisabledConstraints);

        m_jar->leave(std::move(mocoProblemRep));
    }
    void calcCost(int index, const CostInput& input,
            casadi::DM& cost) const override {
        // TODO:index is incorrect; could be for either cost or endpoint.
        auto mocoProblemRep = m_jar->take();

        applyInput(input.initial_time, input.initial_states,
                input.initial_controls, input.initial_multipliers,
                input.initial_derivatives, input.parameters, mocoProblemRep, 0);

        auto& simtkStateDisabledConstraintsInitial =
                mocoProblemRep->updStateDisabledConstraints(0);

        applyInput(input.final_time, input.final_states, input.final_controls,
                input.final_multipliers, input.final_derivatives,
                input.parameters, mocoProblemRep, 1);

        auto& simtkStateDisabledConstraintsFinal =
                mocoProblemRep->updStateDisabledConstraints(1);

        // Compute the cost for this cost term.
        const auto& mocoCost = mocoProblemRep->getCostByIndex(index);
        SimTK::Vector simtkCost =
                mocoCost.calcGoal({simtkStateDisabledConstraintsInitial,
                        simtkStateDisabledConstraintsFinal, input.integral});
        cost = convertToCasADiDM(simtkCost);

        m_jar->leave(std::move(mocoProblemRep));
    }

    void calcEndpointConstraint(int index, const CostInput& input,
            casadi::DM& values) const override {
        // TODO:index is incorrect; could be for either cost or endpoint.
        auto mocoProblemRep = m_jar->take();

        applyInput(input.initial_time, input.initial_states,
                input.initial_controls, input.initial_multipliers,
                input.initial_derivatives, input.parameters, mocoProblemRep, 0);

        auto& simtkStateDisabledConstraintsInitial =
                mocoProblemRep->updStateDisabledConstraints(0);

        applyInput(input.final_time, input.final_states, input.final_controls,
                input.final_multipliers, input.final_derivatives,
                input.parameters, mocoProblemRep, 1);

        auto& simtkStateDisabledConstraintsFinal =
                mocoProblemRep->updStateDisabledConstraints(1);

        // Compute the cost for this cost term.
        const auto& mocoEC = mocoProblemRep->getEndpointConstraintByIndex(index);
        // TODO pass in the argument.
        SimTK::Vector simtkValues =
                mocoEC.calcGoal({simtkStateDisabledConstraintsInitial,
                        simtkStateDisabledConstraintsFinal, input.integral});
        values = convertToCasADiDM(simtkValues);

        m_jar->leave(std::move(mocoProblemRep));
    }

    void calcPathConstraint(int constraintIndex, const ContinuousInput& input,
            casadi::DM& path_constraint) const override {
        auto mocoProblemRep = m_jar->take();
        applyInput(input.time, input.states, input.controls, input.multipliers,
                input.derivatives, input.parameters, mocoProblemRep);
        auto& simtkStateDisabledConstraints =
                mocoProblemRep->updStateDisabledConstraints();

        // Compute path constraint errors.
        const auto& mocoPathCon =
                mocoProblemRep->getPathConstraintByIndex(constraintIndex);
        SimTK::Vector errors(
                (int)path_constraint.rows(), path_constraint.ptr(), true);
        mocoPathCon.calcPathConstraintErrors(
                simtkStateDisabledConstraints, errors);

        m_jar->leave(std::move(mocoProblemRep));
    }
    std::vector<std::string>
    createKinematicConstraintEquationNamesImpl() const override {
        auto mocoProblemRep = m_jar->take();
        const auto names = mocoProblemRep->getKinematicConstraintEquationNames(
                getEnforceConstraintDerivatives());
        m_jar->leave(std::move(mocoProblemRep));
        return names;
    }
    void intermediateCallbackImpl() const override {
        m_fileDeletionThrower->throwIfDeleted();
    }
    void intermediateCallbackWithIterateImpl(
            const CasOC::Iterate& iterate) const override {
        std::string filename = format("MocoCasADiSolver_%s_iterate%06i.sto",
                m_formattedTimeString, iterate.iteration);
        convertToMocoTrajectory(iterate).write(filename);
    }

private:
    /// Apply parameters to properties in the models returned by
    /// `mocoProblemRep.getModelBase()` and
    /// `mocoProblemRep.getModelDisabledConstraints()`.
    void applyParametersToModelProperties(const casadi::DM& parameters,
            const MocoProblemRep& mocoProblemRep) const {
        if (parameters.numel()) {
            SimTK::Vector simtkParams(
                    (int)parameters.size1(), parameters.ptr(), true);
            mocoProblemRep.applyParametersToModelProperties(
                    simtkParams, m_paramsRequireInitSystem);
        }
    }
    /// Copy values from `states` into `simtkState.updY()`, accounting for empty
    /// slots in Simbody's Y vector.
    /// It's fine for the size of `states` to be less than the size of Y; only
    /// the first states.size1() values are copied.
    inline void convertToSimTKState(const double& time,
            const casadi::DM& states, const Model& model,
            SimTK::State& simtkState, bool copyAuxStates) const {
        simtkState.setTime(time);
        // Assign the generalized coordinates. We know we have NU generalized
        // speeds because we do not yet support quaternions.
        for (int isv = 0; isv < getNumCoordinates(); ++isv) {
            simtkState.updQ()[m_yIndexMap.at(isv)] = *(states.ptr() + isv);
        }
        std::copy_n(states.ptr() + getNumCoordinates(), getNumSpeeds(),
                simtkState.updY().updContiguousScalarData() +
                        simtkState.getNQ());
        if (copyAuxStates) {
            std::copy_n(states.ptr() + getNumCoordinates() + getNumSpeeds(),
                    getNumAuxiliaryStates(),
                    simtkState.updY().updContiguousScalarData() +
                            simtkState.getNQ() + simtkState.getNU());
        }
        model.getSystem().prescribe(simtkState);
    }

    void convertToSimTKState(const double& time, const casadi::DM& states,
            const casadi::DM& controls, const Model& model,
            SimTK::State& simtkState, bool copyAuxStates) const {
        convertToSimTKState(time, states, model, simtkState, copyAuxStates);
        auto& simtkControls = model.updControls(simtkState);
        for (int ic = 0; ic < getNumControls(); ++ic) {
            simtkControls[m_modelControlIndices[ic]] = *(controls.ptr() + ic);
        }
        model.realizeVelocity(simtkState);
        model.setControls(simtkState, simtkControls);
    }
    void applyInput(const double& time, const casadi::DM& states,
            const casadi::DM& controls, const casadi::DM& multipliers,
            const casadi::DM& derivatives, const casadi::DM& parameters,
            const std::unique_ptr<const MocoProblemRep>& mocoProblemRep,
            int stateDisConIndex = 0) const {
        // Original model and its associated state. These are used to calculate
        // kinematic constraint forces and errors.
        const auto& modelBase = mocoProblemRep->getModelBase();
        auto& simtkStateBase = mocoProblemRep->updStateBase();

        // Model with disabled constraints and its associated state. These are
        // used to compute the accelerations.
        const auto& modelDisabledConstraints =
                mocoProblemRep->getModelDisabledConstraints();
        auto& simtkStateDisabledConstraints =
                mocoProblemRep->updStateDisabledConstraints(stateDisConIndex);

        // Update the model and state.
        applyParametersToModelProperties(parameters, *mocoProblemRep);
        modelBase.getSystem().prescribe(simtkStateBase);
        modelDisabledConstraints.getSystem().prescribe(
                simtkStateDisabledConstraints);

        if (getNumDerivatives()) {
            auto& accel = mocoProblemRep->getAccelerationMotion();
            accel.setEnabled(simtkStateDisabledConstraints, true);
            SimTK::Vector udot(
                    (int)derivatives.rows(), derivatives.ptr(), true);
            accel.setUDot(simtkStateDisabledConstraints, udot);
        }

        convertToSimTKState(
                time, states, controls, modelBase, simtkStateBase, true);
        convertToSimTKState(time, states, controls, modelDisabledConstraints,
                simtkStateDisabledConstraints, true);
        // If enabled constraints exist in the model, compute constraint forces
        // based on Lagrange multipliers. This also updates the associated
        // discrete variables in the state.
        if (getNumMultipliers()) {
            calcKinematicConstraintForces(multipliers, simtkStateBase,
                    modelBase, mocoProblemRep->getConstraintForces(),
                    simtkStateDisabledConstraints);
        }
    }

    void calcKinematicConstraintForces(const casadi::DM& multipliers,
            const SimTK::State& stateBase, const Model& modelBase,
            const DiscreteForces& constraintForces,
            SimTK::State& stateDisabledConstraints) const {
        // Calculate the constraint forces using the original model and the
        // solver-provided Lagrange multipliers.
        modelBase.realizeVelocity(stateBase);
        const auto& matterBase = modelBase.getMatterSubsystem();
        SimTK::Vector simtkMultipliers(
                (int)multipliers.size1(), multipliers.ptr(), true);
        // Multipliers are negated so constraint forces can be used like
        // applied forces.
        matterBase.calcConstraintForcesFromMultipliers(stateBase,
                -simtkMultipliers, m_constraintBodyForces,
                m_constraintMobilityForces);

        // Apply the constraint forces on the model with disabled constraints.
        constraintForces.setAllForces(stateDisabledConstraints,
                m_constraintMobilityForces, m_constraintBodyForces);
    }

    void calcKinematicConstraintErrors(const Model& modelBase,
            const SimTK::State& stateBase,
            const SimTK::State& simtkStateDisabledConstraints,
            casadi::DM& kinematic_constraint_errors) const {

        // If all kinematics are prescribed, we assume that the prescribed
        // kinematics obey any kinematic constraints. Therefore, the kinematic
        // constraints would be redundant, and we need not enforce them.
        if (isPrescribedKinematics()) return;

        // The total number of scalar holonomic, non-holonomic, and acceleration
        // constraint equations enabled in the model. This does not count
        // equations for derivatives of holonomic and non-holonomic constraints.
        const int total_mp = getNumHolonomicConstraintEquations();
        const int total_mv = getNumNonHolonomicConstraintEquations();
        const int total_ma = getNumAccelerationConstraintEquations();

        // Position-level errors.
        const auto& qerr = stateBase.getQErr();

        if (getEnforceConstraintDerivatives() || total_ma) {
            // Calculuate udoterr. We cannot use State::getUDotErr()
            // because that uses Simbody's multiplilers and UDot,
            // whereas we have our own multipliers and UDot. Here, we use
            // the udot computed from the model with disabled constraints
            // since we cannot use (nor do we have available) udot computed
            // from the original model.
            const auto& matter = modelBase.getMatterSubsystem();
            matter.calcConstraintAccelerationErrors(stateBase,
                    simtkStateDisabledConstraints.getUDot(), m_pvaerr);
        } else {
            m_pvaerr = SimTK::NaN;
        }

        const auto& uerr = stateBase.getUErr();
        int uerrOffset;
        int uerrSize;
        const auto& udoterr = m_pvaerr;
        int udoterrOffset;
        int udoterrSize;
        // TODO These offsets and sizes could be computed once.
        if (getEnforceConstraintDerivatives()) {
            // Velocity-level errors.
            uerrOffset = 0;
            uerrSize = uerr.size();
            // Acceleration-level errors.
            udoterrOffset = 0;
            udoterrSize = udoterr.size();
        } else {
            // Velocity-level errors. Skip derivatives of position-level
            // constraint equations.
            uerrOffset = total_mp;
            uerrSize = total_mv;
            // Acceleration-level errors. Skip derivatives of velocity-
            // and position-level constraint equations.
            udoterrOffset = total_mp + total_mv;
            udoterrSize = total_ma;
        }

        // This way of copying the data avoids a threadsafety issue in
        // CasADi related to cached Sparsity objects.
        std::copy_n(qerr.getContiguousScalarData(), qerr.size(),
                kinematic_constraint_errors.ptr());
        std::copy_n(uerr.getContiguousScalarData() + uerrOffset, uerrSize,
                kinematic_constraint_errors.ptr() + qerr.size());
        std::copy_n(udoterr.getContiguousScalarData() + udoterrOffset,
                udoterrSize,
                kinematic_constraint_errors.ptr() + qerr.size() + uerrSize);
    }

    std::unique_ptr<ThreadsafeJar<const MocoProblemRep>> m_jar;
    bool m_paramsRequireInitSystem = true;
    std::string m_formattedTimeString;
    std::unordered_map<int, int> m_yIndexMap;
    std::vector<int> m_modelControlIndices;
    std::unique_ptr<FileDeletionThrower> m_fileDeletionThrower;
    // Local memory to hold constraint forces.
    static thread_local SimTK::Vector_<SimTK::SpatialVec>
            m_constraintBodyForces;
    static thread_local SimTK::Vector m_constraintMobilityForces;
    // This is the output argument of
    // SimbodyMatterSubsystem::calcConstraintAccelerationErrors(), and includes
    // the acceleration-level holonomic, non-holonomic constraint errors and the
    // acceleration-only constraint errors.
    static thread_local SimTK::Vector m_pvaerr;
};

} // namespace OpenSim

#endif // MOCO_MOCOCASOCPROBLEM_H
