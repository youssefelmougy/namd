
#ifndef TOPO_DEFS_STRUCT_H
#define TOPO_DEFS_STRUCT_H

#include "hasharray.h"
#include "topo_defs.h"

#define NAMEMAXLEN 8
#define NAMETOOLONG(X) ( strlen(X) >= NAMEMAXLEN )

typedef struct topo_defs_type_t {
  char name[NAMEMAXLEN];
  int id;
  double mass;
} topo_defs_type_t;

typedef struct topo_defs_atom_t {
  struct topo_defs_atom_t *next;
  char name[NAMEMAXLEN];
  char type[NAMEMAXLEN];
  double charge;
  int res, rel;
  int del;
} topo_defs_atom_t;

typedef struct topo_defs_bond_t {
  struct topo_defs_bond_t *next;
  char atom1[NAMEMAXLEN];
  char atom2[NAMEMAXLEN];
  int res1, rel1;
  int res2, rel2;
  int del;
} topo_defs_bond_t;

typedef struct topo_defs_angle_t {
  struct topo_defs_angle_t *next;
  char atom1[NAMEMAXLEN];
  char atom2[NAMEMAXLEN];
  char atom3[NAMEMAXLEN];
  int res1, rel1;
  int res2, rel2;
  int res3, rel3;
  int del;
} topo_defs_angle_t;

typedef struct topo_defs_dihedral_t {
  struct topo_defs_dihedral_t *next;
  char atom1[NAMEMAXLEN];
  char atom2[NAMEMAXLEN];
  char atom3[NAMEMAXLEN];
  char atom4[NAMEMAXLEN];
  int res1, rel1;
  int res2, rel2;
  int res3, rel3;
  int res4, rel4;
  int del;
} topo_defs_dihedral_t;

typedef struct topo_defs_improper_t {
  struct topo_defs_improper_t *next;
  char atom1[NAMEMAXLEN];
  char atom2[NAMEMAXLEN];
  char atom3[NAMEMAXLEN];
  char atom4[NAMEMAXLEN];
  int res1, rel1;
  int res2, rel2;
  int res3, rel3;
  int res4, rel4;
  int del;
} topo_defs_improper_t;

typedef struct topo_defs_conformation_t {
  struct topo_defs_conformation_t *next;
  char atom1[NAMEMAXLEN];
  char atom2[NAMEMAXLEN];
  char atom3[NAMEMAXLEN];
  char atom4[NAMEMAXLEN];
  int res1, rel1;
  int res2, rel2;
  int res3, rel3;
  int res4, rel4;
  int del;
  int improper;
  double dist12, angle123, dihedral, angle234, dist34;
} topo_defs_conformation_t;

typedef struct topo_defs_residue_t {
  char name[NAMEMAXLEN];
  int patch;
  topo_defs_atom_t *atoms;
  topo_defs_bond_t *bonds;
  topo_defs_angle_t *angles;
  topo_defs_dihedral_t *dihedrals;
  topo_defs_improper_t *impropers;
  topo_defs_conformation_t *conformations;
  char pfirst[NAMEMAXLEN];
  char plast[NAMEMAXLEN];
} topo_defs_residue_t;

struct topo_defs {
  char *errors;
  void (*error_handler)(const char *);
  int auto_angles;
  int auto_dihedrals;
  char pfirst[NAMEMAXLEN];
  char plast[NAMEMAXLEN];

  topo_defs_type_t *type_array;
  hasharray *type_hash;

  topo_defs_residue_t *residue_array;
  hasharray *residue_hash;
  topo_defs_residue_t *buildres;
  int buildres_no_errors;
};

#endif

