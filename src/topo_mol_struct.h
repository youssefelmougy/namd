
#ifndef TOPO_DEFS_MOL_H
#define TOPO_DEFS_MOL_H

#include "hasharray.h"
#include "memarena.h"
#include "topo_defs_struct.h"
#include "topo_mol.h"

#define NAMEMAXLEN 8
#define NAMETOOLONG(X) ( strlen(X) >= NAMEMAXLEN )

struct topo_mol_atom_t;

typedef struct topo_mol_bond_t {
  struct topo_mol_bond_t *next[2];
  struct topo_mol_atom_t *atom[2];
  int del;
} topo_mol_bond_t;

typedef struct topo_mol_angle_t {
  struct topo_mol_angle_t *next[3];
  struct topo_mol_atom_t *atom[3];
  int del;
} topo_mol_angle_t;

typedef struct topo_mol_dihedral_t {
  struct topo_mol_dihedral_t *next[4];
  struct topo_mol_atom_t *atom[4];
  int del;
} topo_mol_dihedral_t;

typedef struct topo_mol_improper_t {
  struct topo_mol_improper_t *next[4];
  struct topo_mol_atom_t *atom[4];
  int del;
} topo_mol_improper_t;

typedef struct topo_mol_conformation_t {
  struct topo_mol_conformation_t *next[4];
  struct topo_mol_atom_t *atom[4];
  int del;
  int improper;
  double dist12, angle123, dihedral, angle234, dist34;
} topo_mol_conformation_t;

#define TOPO_MOL_XYZ_VOID 0
#define TOPO_MOL_XYZ_SET 1
#define TOPO_MOL_XYZ_GUESS 2

typedef struct topo_mol_atom_t {
  struct topo_mol_atom_t *next;
  topo_mol_bond_t *bonds;
  topo_mol_angle_t *angles;
  topo_mol_dihedral_t *dihedrals;
  topo_mol_improper_t *impropers;
  topo_mol_conformation_t *conformations;
  char name[NAMEMAXLEN];
  char type[NAMEMAXLEN];
  double mass;
  double charge;
  double x,y,z;
  int xyz_state;
  int atomid;
} topo_mol_atom_t;

typedef struct topo_mol_residue_t {
  char resid[NAMEMAXLEN];
  char name[NAMEMAXLEN];
  topo_mol_atom_t *atoms;
} topo_mol_residue_t;

typedef struct topo_mol_segment_t {
  char segid[NAMEMAXLEN];
  topo_mol_residue_t *residue_array;
  hasharray *residue_hash;

  int auto_angles;
  int auto_dihedrals;
  char pfirst[NAMEMAXLEN];
  char plast[NAMEMAXLEN];
} topo_mol_segment_t;

struct topo_mol {
  char *errors;
  void (*error_handler)(const char *);

  topo_defs *defs;

  topo_mol_segment_t **segment_array;
  hasharray *segment_hash;
  topo_mol_segment_t *buildseg;

  memarena *arena;
};

topo_mol_bond_t * topo_mol_bond_next(
                topo_mol_bond_t *tuple, topo_mol_atom_t *atom);

topo_mol_angle_t * topo_mol_angle_next(
                topo_mol_angle_t *tuple, topo_mol_atom_t *atom);

topo_mol_dihedral_t * topo_mol_dihedral_next(
                topo_mol_dihedral_t *tuple, topo_mol_atom_t *atom);

topo_mol_improper_t * topo_mol_improper_next(
                topo_mol_improper_t *tuple, topo_mol_atom_t *atom);

topo_mol_conformation_t * topo_mol_conformation_next(
                topo_mol_conformation_t *tuple, topo_mol_atom_t *atom);

#endif

