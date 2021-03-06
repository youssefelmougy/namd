// -*- c++ -*-

// This file is part of the Collective Variables module (Colvars).
// The original version of Colvars and its updates are located at:
// https://github.com/colvars/colvars
// Please update all Colvars source files before making any changes.
// If you wish to distribute your changes, please submit them to the
// Colvars repository at GitHub.

#include <errno.h>

#include "common.h"
#include "fstream_namd.h"
#include "BackEnd.h"
#include "InfoStream.h"
#include "Node.h"
#include "Molecule.h"
#include "PDB.h"
#include "PDBData.h"
#include "ReductionMgr.h"
#include "ScriptTcl.h"

#ifdef NAMD_TCL
#include <tcl.h>
#endif

#include "colvarmodule.h"
#include "colvaratoms.h"
#include "colvarproxy.h"
#include "colvarproxy_namd.h"
#include "colvarscript.h"


colvarproxy_namd::colvarproxy_namd()
{
  first_timestep = true;
  total_force_requested = false;
  requestTotalForce(total_force_requested);

  // initialize pointers to NAMD configuration data
  simparams = Node::Object()->simParameters;

  if (cvm::debug())
    iout << "Info: initializing the colvars proxy object.\n" << endi;

  // find the configuration file, if provided
  StringList *config = Node::Object()->configList->find("colvarsConfig");

  // find the input state file
  StringList *input_restart = Node::Object()->configList->find("colvarsInput");
  input_prefix_str = std::string(input_restart ? input_restart->data : "");
  if (input_prefix_str.rfind(".colvars.state") != std::string::npos) {
    // strip the extension, if present
    input_prefix_str.erase(input_prefix_str.rfind(".colvars.state"),
                           std::string(".colvars.state").size());
  }

  // get the thermostat temperature
  if (simparams->rescaleFreq > 0)
    thermostat_temperature = simparams->rescaleTemp;
  else if (simparams->reassignFreq > 0)
    thermostat_temperature = simparams->reassignTemp;
  else if (simparams->langevinOn)
    thermostat_temperature = simparams->langevinTemp;
  else if (simparams->tCoupleOn)
    thermostat_temperature = simparams->tCoupleTemp;
  //else if (simparams->loweAndersenOn)
  //  thermostat_temperature = simparams->loweAndersenTemp;
  else
    thermostat_temperature = 0.0;

  random = Random(simparams->randomSeed);

  // take the output prefixes from the namd input
  output_prefix_str = std::string(simparams->outputFilename);
  restart_output_prefix_str = std::string(simparams->restartFilename);
  restart_frequency_s = simparams->restartFrequency;

  // check if it is possible to save output configuration
  if ((!output_prefix_str.size()) && (!restart_output_prefix_str.size())) {
    fatal_error("Error: neither the final output state file or "
                "the output restart file could be defined, exiting.\n");
  }


#ifdef NAMD_TCL
  have_scripts = true;
  // Store pointer to NAMD's Tcl interpreter
  interp = Node::Object()->getScript()->interp;

  // See is user-scripted forces are defined
  if (Tcl_FindCommand(interp, "calc_colvar_forces", NULL, 0) == NULL) {
    force_script_defined = false;
  } else {
    force_script_defined = true;
  }
#else
  force_script_defined = false;
  have_scripts = false;
#endif


  // initiate module: this object will be the communication proxy
  colvars = new colvarmodule(this);
  log("Using NAMD interface, version "+
      cvm::to_str(COLVARPROXY_VERSION)+".\n");

  if (config) {
    colvars->read_config_file(config->data);
  }

  setup();
  colvars->setup();
  colvars->setup_input();
  colvars->setup_output();

  // save to Node for Tcl script access
  Node::Object()->colvars = colvars;

#ifdef NAMD_TCL
  // Construct instance of colvars scripting interface
  script = new colvarscript(this);
#endif

  if (simparams->firstTimestep != 0) {
    log("Initializing step number as firstTimestep.\n");
    colvars->it = colvars->it_restart = simparams->firstTimestep;
  }

  reduction = ReductionMgr::Object()->willSubmit(REDUCTIONS_BASIC);

  if (cvm::debug())
    iout << "Info: done initializing the colvars proxy object.\n" << endi;
}


colvarproxy_namd::~colvarproxy_namd()
{
  delete reduction;
  if (script != NULL) {
    delete script;
    script = NULL;
  }
  if (colvars != NULL) {
    delete colvars;
    colvars = NULL;
  }
}


int colvarproxy_namd::update_atoms_map(AtomIDList::const_iterator begin,
                                       AtomIDList::const_iterator end)
{
  for (AtomIDList::const_iterator a_i = begin; a_i != end; a_i++) {

    if (cvm::debug()) {
      cvm::log("Updating atoms_map for atom ID "+cvm::to_str(*a_i)+"\n");
    }

    if (atoms_map[*a_i] >= 0) continue;

    for (size_t i = 0; i < atoms_ids.size(); i++) {
      if (atoms_ids[i] == *a_i) {
        atoms_map[*a_i] = i;
        break;
      }
    }

    if (atoms_map[*a_i] < 0) {
      // this atom is probably managed by another GlobalMaster:
      // add it here anyway to avoid having to test for array boundaries at each step
      int const index = add_atom_slot(*a_i);
      atoms_map[*a_i] = index;
      update_atom_properties(index);
    }
  }

  if (cvm::debug()) {
    log("atoms_map = "+cvm::to_str(atoms_map)+".\n");
  }

  return COLVARS_OK;
}


int colvarproxy_namd::setup()
{
  if (colvars->size() == 0) return COLVARS_OK;

  log("Updating NAMD interface:\n");

  if (simparams->wrapAll) {
    cvm::log("Warning: enabling wrapAll can lead to inconsistent results "
             "for Colvars calculations: please disable wrapAll, "
             "as is the default option in NAMD.\n");
  }

  log("updating atomic data ("+cvm::to_str(atoms_ids.size())+" atoms).\n");

  size_t i;
  for (i = 0; i < atoms_ids.size(); i++) {
    update_atom_properties(i);

    // zero out mutable arrays
    atoms_positions[i] = cvm::rvector(0.0, 0.0, 0.0);
    atoms_total_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
    atoms_new_colvar_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
  }

  size_t n_group_atoms = 0;
  for (int ig = 0; ig < modifyRequestedGroups().size(); ig++) {
    n_group_atoms += modifyRequestedGroups()[ig].size();
  }

  log("updating group data ("+cvm::to_str(atom_groups_ids.size())+" scalable groups, "+
      cvm::to_str(n_group_atoms)+" atoms in total).\n");

  // Note: groupMassBegin, groupMassEnd may be used here, but they won't work for charges
  for (int ig = 0; ig < modifyRequestedGroups().size(); ig++) {

    // update mass and charge
    update_group_properties(ig);

    atom_groups_coms[ig] = cvm::rvector(0.0, 0.0, 0.0);
    atom_groups_total_forces[ig] = cvm::rvector(0.0, 0.0, 0.0);
    atom_groups_new_colvar_forces[ig] = cvm::rvector(0.0, 0.0, 0.0);
  }

  return COLVARS_OK;
}


int colvarproxy_namd::reset()
{
  int error_code = COLVARS_OK;

  // Unrequest all atoms and group from NAMD
  modifyRequestedAtoms().clear();
  modifyRequestedGroups().clear();

  atoms_map.clear();

  // Clear internal Proxy records
  error_code |= colvarproxy::reset();

  return error_code;
}


void colvarproxy_namd::calculate()
{
  if (first_timestep) {

    this->setup();
    colvars->setup();
    colvars->setup_input();
    colvars->setup_output();

    first_timestep = false;

  } else {
    // Use the time step number inherited from GlobalMaster
    if ( step - previous_NAMD_step == 1 ) {
      colvars->it++;
    }
    // Other cases could mean:
    // - run 0
    // - beginning of a new run statement
    // then the internal counter should not be incremented
  }

  previous_NAMD_step = step;

  if (cvm::debug()) {
    log(std::string(cvm::line_marker)+
        "colvarproxy_namd, step no. "+cvm::to_str(colvars->it)+"\n"+
        "Updating atomic data arrays.\n");
  }

  // must delete the forces applied at the previous step: we can do
  // that because they have already been used and copied to other
  // memory locations
  modifyForcedAtoms().clear();
  modifyAppliedForces().clear();

  // prepare local arrays
  for (size_t i = 0; i < atoms_ids.size(); i++) {
    atoms_positions[i] = cvm::rvector(0.0, 0.0, 0.0);
    atoms_total_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
    atoms_new_colvar_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
  }

  for (size_t i = 0; i < atom_groups_ids.size(); i++) {
    atom_groups_total_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
    atom_groups_new_colvar_forces[i] = cvm::rvector(0.0, 0.0, 0.0);
  }

  // create the atom map if needed
  size_t const n_all_atoms = Node::Object()->molecule->numAtoms;
  if (atoms_map.size() != n_all_atoms) {
    atoms_map.resize(n_all_atoms);
    atoms_map.assign(n_all_atoms, -1);
    update_atoms_map(getAtomIdBegin(), getAtomIdEnd());
  }

  // if new atomic positions or forces have been communicated by other GlobalMasters, add them to the atom map
  if ((int(atoms_ids.size()) < (getAtomIdEnd() - getAtomIdBegin())) ||
      (int(atoms_ids.size()) < (getForceIdEnd() - getForceIdBegin()))) {
    update_atoms_map(getAtomIdBegin(), getAtomIdEnd());
    update_atoms_map(getForceIdBegin(), getForceIdEnd());
  }

  {
    if (cvm::debug()) {
      log("Updating positions arrays.\n");
    }
    size_t n_positions = 0;
    AtomIDList::const_iterator a_i = getAtomIdBegin();
    AtomIDList::const_iterator a_e = getAtomIdEnd();
    PositionList::const_iterator p_i = getAtomPositionBegin();

    for ( ; a_i != a_e; ++a_i, ++p_i ) {
      atoms_positions[atoms_map[*a_i]] = cvm::rvector((*p_i).x, (*p_i).y, (*p_i).z);
      n_positions++;
    }

    // The following had to be relaxed because some atoms may be forced without their position being requested
    // if (n_positions < atoms_ids.size()) {
    //   cvm::error("Error: did not receive the positions of all atoms.\n", BUG_ERROR);
    // }
  }

  if (total_force_requested && cvm::step_relative() > 0) {

    // sort the force arrays the previous step
    // (but only do so if there *is* a previous step!)

    {
      if (cvm::debug()) {
        log("Updating total forces arrays.\n");
      }
      size_t n_total_forces = 0;
      AtomIDList::const_iterator a_i = getForceIdBegin();
      AtomIDList::const_iterator a_e = getForceIdEnd();
      ForceList::const_iterator f_i = getTotalForce();

      for ( ; a_i != a_e; ++a_i, ++f_i ) {
        atoms_total_forces[atoms_map[*a_i]] = cvm::rvector((*f_i).x, (*f_i).y, (*f_i).z);
        n_total_forces++;
      }

      if (n_total_forces < atoms_ids.size()) {
        cvm::error("Error: total forces were requested, but total forces "
                   "were not received for all atoms.\n"
                   "The most probable cause is combination of energy "
                   "minimization with a biasing method that requires MD (e.g. ABF).\n"
                   "Always run minimization and ABF separately.", INPUT_ERROR);
      }
    }

    {
      if (cvm::debug()) {
        log("Updating group total forces arrays.\n");
      }
      ForceList::const_iterator f_i = getGroupTotalForceBegin();
      ForceList::const_iterator f_e = getGroupTotalForceEnd();
      size_t i = 0;
      if ((f_e - f_i) != ((int) atom_groups_ids.size())) {
        cvm::error("Error: total forces were requested for scalable groups, "
                   "but they are not in the same number from the number of groups.\n"
                   "The most probable cause is combination of energy "
                   "minimization with a biasing method that requires MD (e.g. ABF).\n"
                   "Always run minimization and ABF separately.", INPUT_ERROR);
      }
      for ( ; f_i != f_e; f_i++, i++) {
        atom_groups_total_forces[i] = cvm::rvector((*f_i).x, (*f_i).y, (*f_i).z);
      }
    }
  }

  {
    if (cvm::debug()) {
      log("Updating group positions arrays.\n");
    }
    // update group data (only coms available so far)
    size_t ig;
    // note: getGroupMassBegin() could be used here, but masses and charges
    // have already been calculated from the last call to setup()
    PositionList::const_iterator gp_i = getGroupPositionBegin();
    for (ig = 0; gp_i != getGroupPositionEnd(); gp_i++, ig++) {
      atom_groups_coms[ig] = cvm::rvector(gp_i->x, gp_i->y, gp_i->z);
    }
  }

  if (cvm::debug()) {
    log("atoms_ids = "+cvm::to_str(atoms_ids)+"\n");
    log("atoms_ncopies = "+cvm::to_str(atoms_ncopies)+"\n");
    log("atoms_masses = "+cvm::to_str(atoms_masses)+"\n");
    log("atoms_charges = "+cvm::to_str(atoms_charges)+"\n");
    log("atoms_positions = "+cvm::to_str(atoms_positions)+"\n");
    log("atoms_total_forces = "+cvm::to_str(atoms_total_forces)+"\n");
    log(cvm::line_marker);

    log("atom_groups_ids = "+cvm::to_str(atom_groups_ids)+"\n");
    log("atom_groups_ncopies = "+cvm::to_str(atom_groups_ncopies)+"\n");
    log("atom_groups_masses = "+cvm::to_str(atom_groups_masses)+"\n");
    log("atom_groups_charges = "+cvm::to_str(atom_groups_charges)+"\n");
    log("atom_groups_coms = "+cvm::to_str(atom_groups_coms)+"\n");
    log("atom_groups_total_forces = "+cvm::to_str(atom_groups_total_forces)+"\n");
    log(cvm::line_marker);
  }

  // call the collective variable module
  if (colvars->calc() != COLVARS_OK) {
    cvm::error("Error in the collective variables module.\n", COLVARS_ERROR);
  }

  if (cvm::debug()) {
    log(cvm::line_marker);
    log("atoms_new_colvar_forces = "+cvm::to_str(atoms_new_colvar_forces)+"\n");
    log(cvm::line_marker);
    log("atom_groups_new_colvar_forces = "+cvm::to_str(atom_groups_new_colvar_forces)+"\n");
    log(cvm::line_marker);
  }

  // communicate all forces to the MD integrator
  for (size_t i = 0; i < atoms_ids.size(); i++) {
    cvm::rvector const &f = atoms_new_colvar_forces[i];
    modifyForcedAtoms().add(atoms_ids[i]);
    modifyAppliedForces().add(Vector(f.x, f.y, f.z));
  }

  {
    // zero out the applied forces on each group
    modifyGroupForces().resize(modifyRequestedGroups().size());
    ForceList::iterator gf_i = modifyGroupForces().begin();
    for (int ig = 0; gf_i != modifyGroupForces().end(); gf_i++, ig++) {
      cvm::rvector const &f = atom_groups_new_colvar_forces[ig];
      *gf_i = Vector(f.x, f.y, f.z);
    }
  }

  // send MISC energy
  reduction->submit();

  // NAMD does not destruct GlobalMaster objects, so we must remember
  // to write all output files at the end of a run
  if (step == simparams->N) {
    colvars->write_output_files();
  }
}


// Callback functions

int colvarproxy_namd::run_force_callback() {
#ifdef NAMD_TCL
  std::string cmd = std::string("calc_colvar_forces ")
    + cvm::to_str(cvm::step_absolute());
  int err = Tcl_Eval(interp, cmd.c_str());
  if (err != TCL_OK) {
    log(std::string("Error while executing calc_colvar_forces:\n"));
    cvm::error(Tcl_GetStringResult(interp));
    return COLVARS_ERROR;
  }
  return COLVARS_OK;
#else
  return COLVARS_NOT_IMPLEMENTED;
#endif
}

int colvarproxy_namd::run_colvar_callback(std::string const &name,
                                          std::vector<const colvarvalue *> const &cvc_values,
                                          colvarvalue &value)
{
#ifdef NAMD_TCL
  size_t i;
  std::string cmd = std::string("calc_") + name;
  for (i = 0; i < cvc_values.size(); i++) {
    cmd += std::string(" {") +  (*(cvc_values[i])).to_simple_string() + std::string("}");
  }
  int err = Tcl_Eval(interp, cmd.c_str());
  const char *result = Tcl_GetStringResult(interp);
  if (err != TCL_OK) {
    log(std::string("Error while executing ")
        + cmd + std::string(":\n"));
    cvm::error(result);
    return COLVARS_ERROR;
  }
  std::istringstream is(result);
  if (value.from_simple_string(is.str()) != COLVARS_OK) {
    log("Error parsing colvar value from script:");
    cvm::error(result);
    return COLVARS_ERROR;
  }
  return COLVARS_OK;
#else
  return COLVARS_NOT_IMPLEMENTED;
#endif
}

int colvarproxy_namd::run_colvar_gradient_callback(std::string const &name,
                                                   std::vector<const colvarvalue *> const &cvc_values,
                                                   std::vector<cvm::matrix2d<cvm::real> > &gradient)
{
#ifdef NAMD_TCL
  size_t i;
  std::string cmd = std::string("calc_") + name + "_gradient";
  for (i = 0; i < cvc_values.size(); i++) {
    cmd += std::string(" {") +  (*(cvc_values[i])).to_simple_string() + std::string("}");
  }
  int err = Tcl_Eval(interp, cmd.c_str());
  if (err != TCL_OK) {
    log(std::string("Error while executing ")
        + cmd + std::string(":\n"));
    cvm::error(Tcl_GetStringResult(interp));
    return COLVARS_ERROR;
  }
  Tcl_Obj **list;
  int n;
  Tcl_ListObjGetElements(interp, Tcl_GetObjResult(interp),
                         &n, &list);
  if (n != int(gradient.size())) {
    cvm::error("Error parsing list of gradient values from script: found "
               + cvm::to_str(n) + " values instead of " + cvm::to_str(gradient.size()));
    return COLVARS_ERROR;
  }
  for (i = 0; i < gradient.size(); i++) {
    std::istringstream is(Tcl_GetString(list[i]));
    if (gradient[i].from_simple_string(is.str()) != COLVARS_OK) {
      log("Gradient matrix size: " + cvm::to_str(gradient[i].size()));
      log("Gradient string: " + cvm::to_str(Tcl_GetString(list[i])));
      cvm::error("Error parsing gradient value from script");
      return COLVARS_ERROR;
    }
  }
  return (err == TCL_OK) ? COLVARS_OK : COLVARS_ERROR;
#else
  return COLVARS_NOT_IMPLEMENTED;
#endif
}


void colvarproxy_namd::add_energy(cvm::real energy)
{
  reduction->item(REDUCTION_MISC_ENERGY) += energy;
}

void colvarproxy_namd::request_total_force(bool yesno)
{
  if (cvm::debug()) {
    cvm::log("colvarproxy_namd::request_total_force()\n");
  }
  total_force_requested = yesno;
  requestTotalForce(total_force_requested);
  if (cvm::debug()) {
    cvm::log("colvarproxy_namd::request_total_force() end\n");
  }
}

void colvarproxy_namd::log(std::string const &message)
{
  std::istringstream is(message);
  std::string line;
  while (std::getline(is, line))
    iout << "colvars: " << line << "\n";
  iout << endi;
}

void colvarproxy_namd::error(std::string const &message)
{
  // In NAMD, all errors are fatal
  fatal_error(message);
}


void colvarproxy_namd::fatal_error(std::string const &message)
{
  log(message);
  if (errno) log(strerror(errno));
  // if (!cvm::debug())
  //   log("If this error message is unclear, "
  //       "try recompiling with -DCOLVARS_DEBUG.\n");
  if (errno) {
    NAMD_err("Error in the collective variables module");
  } else {
    NAMD_die("Error in the collective variables module: exiting.\n");
  }
}


void colvarproxy_namd::exit(std::string const &message)
{
  log(message);
  BackEnd::exit();
}


int colvarproxy_namd::check_atom_id(int atom_number)
{
  // NAMD's internal numbering starts from zero
  int const aid = (atom_number-1);

  if (cvm::debug())
    log("Adding atom "+cvm::to_str(atom_number)+
        " for collective variables calculation.\n");

  if ( (aid < 0) || (aid >= Node::Object()->molecule->numAtoms) ) {
    cvm::error("Error: invalid atom number specified, "+
               cvm::to_str(atom_number)+"\n", INPUT_ERROR);
    return INPUT_ERROR;
  }

  return aid;
}


int colvarproxy_namd::init_atom(int atom_number)
{
  // save time by checking first whether this atom has been requested before
  // (this is more common than a non-valid atom number)
  int aid = (atom_number-1);

  for (size_t i = 0; i < atoms_ids.size(); i++) {
    if (atoms_ids[i] == aid) {
      // this atom id was already recorded
      atoms_ncopies[i] += 1;
      return i;
    }
  }

  aid = check_atom_id(atom_number);

  if (aid < 0) {
    return INPUT_ERROR;
  }

  int const index = add_atom_slot(aid);
  modifyRequestedAtoms().add(aid);
  update_atom_properties(index);
  return index;
}


int colvarproxy_namd::check_atom_id(cvm::residue_id const &residue,
                                    std::string const     &atom_name,
                                    std::string const     &segment_id)
{
  int const aid =
    (segment_id.size() ?
     Node::Object()->molecule->get_atom_from_name(segment_id.c_str(),
                                                  residue,
                                                  atom_name.c_str()) :
     Node::Object()->molecule->get_atom_from_name("MAIN",
                                                  residue,
                                                  atom_name.c_str()));

  if (aid < 0) {
    // get_atom_from_name() has returned an error value
    cvm::error("Error: could not find atom \""+
               atom_name+"\" in residue "+
               cvm::to_str(residue)+
               ( (segment_id != "MAIN") ?
                 (", segment \""+segment_id+"\"") :
                 ("") )+
               "\n", INPUT_ERROR);
    return INPUT_ERROR;
  }

  return aid;
}



/// For AMBER topologies, the segment id is automatically set to
/// "MAIN" (the segment id assigned by NAMD's AMBER topology parser),
/// and is therefore optional when an AMBER topology is used
int colvarproxy_namd::init_atom(cvm::residue_id const &residue,
                                std::string const     &atom_name,
                                std::string const     &segment_id)
{
  int const aid = check_atom_id(residue, atom_name, segment_id);

  for (size_t i = 0; i < atoms_ids.size(); i++) {
    if (atoms_ids[i] == aid) {
      // this atom id was already recorded
      atoms_ncopies[i] += 1;
      return i;
    }
  }

  if (cvm::debug())
    log("Adding atom \""+
        atom_name+"\" in residue "+
        cvm::to_str(residue)+
        " (index "+cvm::to_str(aid)+
        ") for collective variables calculation.\n");

  int const index = add_atom_slot(aid);
  modifyRequestedAtoms().add(aid);
  update_atom_properties(index);
  return index;
}


void colvarproxy_namd::clear_atom(int index)
{
  colvarproxy::clear_atom(index);
  // TODO remove it from GlobalMaster arrays?
}


enum e_pdb_field {
  e_pdb_none,
  e_pdb_occ,
  e_pdb_beta,
  e_pdb_x,
  e_pdb_y,
  e_pdb_z,
  e_pdb_ntot
};


e_pdb_field pdb_field_str2enum(std::string const &pdb_field_str)
{
  e_pdb_field pdb_field = e_pdb_none;

  if (colvarparse::to_lower_cppstr(pdb_field_str) ==
      colvarparse::to_lower_cppstr("O")) {
    pdb_field = e_pdb_occ;
  }

  if (colvarparse::to_lower_cppstr(pdb_field_str) ==
      colvarparse::to_lower_cppstr("B")) {
    pdb_field = e_pdb_beta;
  }

  if (colvarparse::to_lower_cppstr(pdb_field_str) ==
      colvarparse::to_lower_cppstr("X")) {
    pdb_field = e_pdb_x;
  }

  if (colvarparse::to_lower_cppstr(pdb_field_str) ==
      colvarparse::to_lower_cppstr("Y")) {
    pdb_field = e_pdb_y;
  }

  if (colvarparse::to_lower_cppstr(pdb_field_str) ==
      colvarparse::to_lower_cppstr("Z")) {
    pdb_field = e_pdb_z;
  }

  if (pdb_field == e_pdb_none) {
    cvm::error("Error: unsupported PDB field, \""+
               pdb_field_str+"\".\n", INPUT_ERROR);
  }

  return pdb_field;
}


int colvarproxy_namd::load_coords(char const *pdb_filename,
                                  std::vector<cvm::atom_pos> &pos,
                                  const std::vector<int> &indices,
                                  std::string const &pdb_field_str,
                                  double const pdb_field_value)
{
  if (pdb_field_str.size() == 0 && indices.size() == 0) {
    cvm::error("Bug alert: either PDB field should be defined or list of "
               "atom IDs should be available when loading atom coordinates!\n", BUG_ERROR);
  }

  e_pdb_field pdb_field_index;
  bool const use_pdb_field = (pdb_field_str.size() > 0);
  if (use_pdb_field) {
    pdb_field_index = pdb_field_str2enum(pdb_field_str);
  }

  // next index to be looked up in PDB file (if list is supplied)
  std::vector<int>::const_iterator current_index = indices.begin();

  PDB *pdb = new PDB(pdb_filename);
  size_t const pdb_natoms = pdb->num_atoms();

  if (pos.size() != pdb_natoms) {

    bool const pos_allocated = (pos.size() > 0);

    size_t ipos = 0, ipdb = 0;
    for ( ; ipdb < pdb_natoms; ipdb++) {

      if (use_pdb_field) {
        // PDB field mode: skip atoms with wrong value in PDB field
        double atom_pdb_field_value = 0.0;

        switch (pdb_field_index) {
        case e_pdb_occ:
          atom_pdb_field_value = (pdb->atom(ipdb))->occupancy();
          break;
        case e_pdb_beta:
          atom_pdb_field_value = (pdb->atom(ipdb))->temperaturefactor();
          break;
        case e_pdb_x:
          atom_pdb_field_value = (pdb->atom(ipdb))->xcoor();
          break;
        case e_pdb_y:
          atom_pdb_field_value = (pdb->atom(ipdb))->ycoor();
          break;
        case e_pdb_z:
          atom_pdb_field_value = (pdb->atom(ipdb))->zcoor();
          break;
        default:
          break;
        }

        if ( (pdb_field_value) &&
             (atom_pdb_field_value != pdb_field_value) ) {
          continue;
        } else if (atom_pdb_field_value == 0.0) {
          continue;
        }

      } else {
        // Atom ID mode: use predefined atom IDs from the atom group
        if (((int) ipdb) != *current_index) {
          // Skip atoms not in the list
          continue;
        } else {
          current_index++;
        }
      }

      if (!pos_allocated) {
        pos.push_back(cvm::atom_pos(0.0, 0.0, 0.0));
      } else if (ipos >= pos.size()) {
        cvm::error("Error: the PDB file \""+
                   std::string(pdb_filename)+
                   "\" contains coordinates for "
                   "more atoms than needed.\n", BUG_ERROR);
      }

      pos[ipos] = cvm::atom_pos((pdb->atom(ipdb))->xcoor(),
                                (pdb->atom(ipdb))->ycoor(),
                                (pdb->atom(ipdb))->zcoor());
      ipos++;
      if (!use_pdb_field && current_index == indices.end())
        break;
    }

    if ((ipos < pos.size()) || (current_index != indices.end()))
      cvm::error("Error: the number of records in the PDB file \""+
                 std::string(pdb_filename)+
                 "\" does not appear to match either the total number of atoms,"+
                 " or the number of coordinates requested at this point("+
                 cvm::to_str(pos.size())+").\n", BUG_ERROR);

  } else {

    // when the PDB contains exactly the number of atoms of the array,
    // ignore the fields and just read coordinates
    for (size_t ia = 0; ia < pos.size(); ia++) {
      pos[ia] = cvm::atom_pos((pdb->atom(ia))->xcoor(),
                              (pdb->atom(ia))->ycoor(),
                              (pdb->atom(ia))->zcoor());
    }
  }

  delete pdb;
  return COLVARS_OK;
}


int colvarproxy_namd::load_atoms(char const *pdb_filename,
                                 cvm::atom_group &atoms,
                                 std::string const &pdb_field_str,
                                 double const pdb_field_value)
{
  if (pdb_field_str.size() == 0)
    cvm::error("Error: must define which PDB field to use "
               "in order to define atoms from a PDB file.\n", INPUT_ERROR);

  PDB *pdb = new PDB(pdb_filename);
  size_t const pdb_natoms = pdb->num_atoms();

  e_pdb_field pdb_field_index = pdb_field_str2enum(pdb_field_str);

  for (size_t ipdb = 0; ipdb < pdb_natoms; ipdb++) {

    double atom_pdb_field_value = 0.0;

    switch (pdb_field_index) {
    case e_pdb_occ:
      atom_pdb_field_value = (pdb->atom(ipdb))->occupancy();
      break;
    case e_pdb_beta:
      atom_pdb_field_value = (pdb->atom(ipdb))->temperaturefactor();
      break;
    case e_pdb_x:
      atom_pdb_field_value = (pdb->atom(ipdb))->xcoor();
      break;
    case e_pdb_y:
      atom_pdb_field_value = (pdb->atom(ipdb))->ycoor();
      break;
    case e_pdb_z:
      atom_pdb_field_value = (pdb->atom(ipdb))->zcoor();
      break;
    default:
      break;
    }

    if ( (pdb_field_value) &&
         (atom_pdb_field_value != pdb_field_value) ) {
      continue;
    } else if (atom_pdb_field_value == 0.0) {
      continue;
    }

    if (atoms.is_enabled(colvardeps::f_ag_scalable)) {
      atoms.add_atom_id(ipdb);
    } else {
      atoms.add_atom(cvm::atom(ipdb+1));
    }
  }

  delete pdb;
  return (cvm::get_error() ? COLVARS_ERROR : COLVARS_OK);
}


std::ostream * colvarproxy_namd::output_stream(std::string const &output_name)
{
  std::list<std::ostream *>::iterator osi  = output_files.begin();
  std::list<std::string>::iterator    osni = output_stream_names.begin();
  for ( ; osi != output_files.end(); osi++, osni++) {
    if (*osni == output_name) {
      return *osi;
    }
  }
  output_stream_names.push_back(output_name);
  backup_file(output_name.c_str());
  ofstream_namd * os = new ofstream_namd(output_name.c_str());
  if (!os->is_open()) {
    cvm::error("Error: cannot write to file \""+output_name+"\".\n",
               FILE_ERROR);
  }
  output_files.push_back(os);
  return os;
}


int colvarproxy_namd::close_output_stream(std::string const &output_name)
{
  std::list<std::ostream *>::iterator osi  = output_files.begin();
  std::list<std::string>::iterator    osni = output_stream_names.begin();
  for ( ; osi != output_files.end(); osi++, osni++) {
    if (*osni == output_name) {
      if (((ofstream_namd *) *osi)->is_open()) {
        ((ofstream_namd *) *osi)->close();
      }
      output_files.erase(osi);
      output_stream_names.erase(osni);
      return COLVARS_OK;
    }
  }
  return COLVARS_ERROR;
}


int colvarproxy_namd::backup_file(char const *filename)
{
  if (std::string(filename).rfind(std::string(".colvars.state")) != std::string::npos) {
    NAMD_backup_file(filename, ".old");
  } else {
    NAMD_backup_file(filename, ".BAK");
  }
  return COLVARS_OK;
}


char *colvarproxy_namd::script_obj_to_str(unsigned char *obj)
{
#ifdef NAMD_TCL
  return Tcl_GetString(reinterpret_cast<Tcl_Obj *>(obj));
#else
  // This is most likely not going to be executed
  return colvarproxy::script_obj_to_str(obj);
#endif
}

int colvarproxy_namd::init_atom_group(std::vector<int> const &atoms_ids)
{
  if (cvm::debug())
    log("Reguesting from NAMD a group of size "+cvm::to_str(atoms_ids.size())+
        " for collective variables calculation.\n");

  // Note: modifyRequestedGroups is supposed to be in sync with the colvarproxy arrays,
  // and to stay that way during a simulation

  // compare this new group to those already allocated inside GlobalMaster
  int ig;
  for (ig = 0; ig < modifyRequestedGroups().size(); ig++) {
    AtomIDList const &namd_group = modifyRequestedGroups()[ig];
    bool b_match = true;

    if (namd_group.size() != ((int) atoms_ids.size())) {
      b_match = false;
    } else {
      int ia;
      for (ia = 0; ia < namd_group.size(); ia++) {
        int const aid = atoms_ids[ia];
        if (namd_group[ia] != aid) {
          b_match = false;
          break;
        }
      }
    }

    if (b_match) {
      if (cvm::debug())
        log("Group was already added.\n");
      // this group already exists
      atom_groups_ncopies[ig] += 1;
      return ig;
    }
  }

  // add this group (note: the argument of add_atom_group_slot() is redundant for NAMD, and provided only for consistency)
  size_t const index = add_atom_group_slot(atom_groups_ids.size());
  modifyRequestedGroups().resize(atom_groups_ids.size());
  // the following is done in calculate()
  // modifyGroupForces().resize(atom_groups_ids.size());
  AtomIDList &namd_group = modifyRequestedGroups()[index];
  namd_group.resize(atoms_ids.size());
  int const n_all_atoms = Node::Object()->molecule->numAtoms;
  for (size_t ia = 0; ia < atoms_ids.size(); ia++) {
    int const aid = atoms_ids[ia];
    if (cvm::debug())
      log("Adding atom "+cvm::to_str(aid+1)+
          " for collective variables calculation.\n");
    if ( (aid < 0) || (aid >= n_all_atoms) ) {
      cvm::error("Error: invalid atom number specified, "+
                 cvm::to_str(aid+1)+"\n", INPUT_ERROR);
      return -1;
    }
    namd_group[ia] = aid;
  }

  update_group_properties(index);

  if (cvm::debug()) {
    log("Group has index "+cvm::to_str(index)+"\n");
    log("modifyRequestedGroups length = "+cvm::to_str(modifyRequestedGroups().size())+
        ", modifyGroupForces length = "+cvm::to_str(modifyGroupForces().size())+"\n");
  }

  return index;
}


void colvarproxy_namd::clear_atom_group(int index)
{
  // do nothing, keep the NAMD arrays in sync with the colvarproxy ones
  colvarproxy::clear_atom_group(index);
}


int colvarproxy_namd::update_group_properties(int index)
{
  AtomIDList const &namd_group = modifyRequestedGroups()[index];
  if (cvm::debug()) {
    log("Re-calculating total mass and charge for scalable group no. "+cvm::to_str(index+1)+" ("+
        cvm::to_str(namd_group.size())+" atoms).\n");
  }

  cvm::real total_mass = 0.0;
  cvm::real total_charge = 0.0;
  for (int i = 0; i < namd_group.size(); i++) {
    total_mass += Node::Object()->molecule->atommass(namd_group[i]);
    total_charge += Node::Object()->molecule->atomcharge(namd_group[i]);
  }
  atom_groups_masses[index] = total_mass;
  atom_groups_charges[index] = total_charge;

  if (cvm::debug()) {
    log("total mass = "+cvm::to_str(total_mass)+
        ", total charge = "+cvm::to_str(total_charge)+"\n");
  }

  return COLVARS_OK;
}

#if CMK_SMP && USE_CKLOOP // SMP only

void calc_colvars_items_smp(int first, int last, void *result, int paramNum, void *param)
{
  colvarproxy_namd *proxy = (colvarproxy_namd *) param;
  colvarmodule *cv = proxy->colvars;

  cvm::increase_depth();
  for (int i = first; i <= last; i++) {
    colvar *x = (*(cv->variables_active_smp()))[i];
    int x_item = (*(cv->variables_active_smp_items()))[i];
    if (cvm::debug()) {
      cvm::log("["+cvm::to_str(proxy->smp_thread_id())+"/"+cvm::to_str(proxy->smp_num_threads())+
               "]: calc_colvars_items_smp(), first = "+cvm::to_str(first)+
               ", last = "+cvm::to_str(last)+", cv = "+
               x->name+", cvc = "+cvm::to_str(x_item)+"\n");
    }
    x->calc_cvcs(x_item, 1);
  }
  cvm::decrease_depth();
}


int colvarproxy_namd::smp_colvars_loop()
{
  colvarmodule *cv = this->colvars;
  CkLoop_Parallelize(calc_colvars_items_smp, 1, this,
                     cv->variables_active_smp()->size(),
                     0, cv->variables_active_smp()->size()-1);
  return cvm::get_error();
}


void calc_cv_biases_smp(int first, int last, void *result, int paramNum, void *param)
{
  colvarproxy_namd *proxy = (colvarproxy_namd *) param;
  colvarmodule *cv = proxy->colvars;

  cvm::increase_depth();
  for (int i = first; i <= last; i++) {
    colvarbias *b = (*(cv->biases_active()))[i];
    if (cvm::debug()) {
      cvm::log("["+cvm::to_str(proxy->smp_thread_id())+"/"+cvm::to_str(proxy->smp_num_threads())+
               "]: calc_cv_biases_smp(), first = "+cvm::to_str(first)+
               ", last = "+cvm::to_str(last)+", bias = "+
               b->name+"\n");
    }
    b->update();
  }
  cvm::decrease_depth();
}


int colvarproxy_namd::smp_biases_loop()
{
  colvarmodule *cv = this->colvars;
  CkLoop_Parallelize(calc_cv_biases_smp, 1, this,
                     cv->biases_active()->size(), 0, cv->biases_active()->size()-1);
  return cvm::get_error();
}


void calc_cv_scripted_forces(int paramNum, void *param)
{
  colvarproxy_namd *proxy = (colvarproxy_namd *) param;
  colvarmodule *cv = proxy->colvars;
  if (cvm::debug()) {
    cvm::log("["+cvm::to_str(proxy->smp_thread_id())+"/"+cvm::to_str(proxy->smp_num_threads())+
             "]: calc_cv_scripted_forces()\n");
  }
  cv->calc_scripted_forces();
}


int colvarproxy_namd::smp_biases_script_loop()
{
  colvarmodule *cv = this->colvars;
  CkLoop_Parallelize(calc_cv_biases_smp, 1, this,
                     cv->biases_active()->size(), 0, cv->biases_active()->size()-1,
                     1, NULL, CKLOOP_NONE,
                     calc_cv_scripted_forces, 1, this);
  return cvm::get_error();
}

#endif  // SMP section
