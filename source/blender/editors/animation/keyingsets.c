/* SPDX-License-Identifier: GPL-2.0-or-later
 * Copyright 2009 Blender Foundation, Joshua Leung. All rights reserved. */

/** \file
 * \ingroup edanimation
 */

#include <float.h>
#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include "MEM_guardedalloc.h"

#include "BLI_blenlib.h"
#include "BLI_utildefines.h"

#include "DNA_anim_types.h"
#include "DNA_object_types.h"
#include "DNA_scene_types.h"

#include "BKE_animsys.h"
#include "BKE_context.h"
#include "BKE_main.h"
#include "BKE_report.h"

#include "DEG_depsgraph.h"

#include "ED_keyframing.h"
#include "ED_screen.h"

#include "UI_interface.h"
#include "UI_resources.h"

#include "WM_api.h"
#include "WM_types.h"

#include "RNA_access.h"
#include "RNA_define.h"
#include "RNA_enum_types.h"
#include "RNA_path.h"

#include "anim_intern.h"

/* ************************************************** */
/* KEYING SETS - OPERATORS (for use in UI panels) */
/* These operators are really duplication of existing functionality, but just for completeness,
 * they're here too, and will give the basic data needed...
 */

/* poll callback for adding default KeyingSet */
static bool keyingset_poll_default_add(bContext *C)
{
  /* as long as there's an active Scene, it's fine */
  return (CTX_data_scene(C) != NULL);
}

/* poll callback for editing active KeyingSet */
static bool keyingset_poll_active_edit(bContext *C)
{
  Scene *scene = CTX_data_scene(C);

  if (scene == NULL) {
    return 0;
  }

  /* there must be an active KeyingSet (and KeyingSets) */
  return ((scene->active_keyingset > 0) && (scene->keyingsets.first));
}

/* poll callback for editing active KeyingSet Path */
static bool keyingset_poll_activePath_edit(bContext *C)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks;

  if (scene == NULL) {
    return 0;
  }
  if (scene->active_keyingset <= 0) {
    return 0;
  }

  ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);

  /* there must be an active KeyingSet and an active path */
  return ((ks) && (ks->paths.first) && (ks->active_path > 0));
}

/* Add a Default (Empty) Keying Set ------------------------- */

static int add_default_keyingset_exec(bContext *C, wmOperator *UNUSED(op))
{
  Scene *scene = CTX_data_scene(C);
  eKS_Settings flag = 0;
  eInsertKeyFlags keyingflag = 0;

  /* validate flags
   * - absolute KeyingSets should be created by default
   */
  flag |= KEYINGSET_ABSOLUTE;

  /* 2nd arg is 0 to indicate that we don't want to include autokeying mode related settings */
  keyingflag = ANIM_get_keyframing_flags(scene, false);

  /* call the API func, and set the active keyingset index */
  BKE_keyingset_add(&scene->keyingsets, NULL, NULL, flag, keyingflag);

  scene->active_keyingset = BLI_listbase_count(&scene->keyingsets);

  /* send notifiers */
  WM_event_add_notifier(C, NC_SCENE | ND_KEYINGSET, NULL);

  return OPERATOR_FINISHED;
}

void ANIM_OT_keying_set_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Empty Keying Set";
  ot->idname = "ANIM_OT_keying_set_add";
  ot->description = "Add a new (empty) keying set to the active Scene";

  /* callbacks */
  ot->exec = add_default_keyingset_exec;
  ot->poll = keyingset_poll_default_add;
}

/* Remove 'Active' Keying Set ------------------------- */

static int remove_active_keyingset_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks;

  /* verify the Keying Set to use:
   * - use the active one
   * - return error if it doesn't exist
   */
  if (scene->active_keyingset == 0) {
    BKE_report(op->reports, RPT_ERROR, "No active Keying Set to remove");
    return OPERATOR_CANCELLED;
  }

  if (scene->active_keyingset < 0) {
    BKE_report(op->reports, RPT_ERROR, "Cannot remove built in keying set");
    return OPERATOR_CANCELLED;
  }

  ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);

  /* free KeyingSet's data, then remove it from the scene */
  BKE_keyingset_free(ks);
  BLI_freelinkN(&scene->keyingsets, ks);

  /* the active one should now be the previously second-to-last one */
  scene->active_keyingset--;

  /* send notifiers */
  WM_event_add_notifier(C, NC_SCENE | ND_KEYINGSET, NULL);

  return OPERATOR_FINISHED;
}

void ANIM_OT_keying_set_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Active Keying Set";
  ot->idname = "ANIM_OT_keying_set_remove";
  ot->description = "Remove the active keying set";

  /* callbacks */
  ot->exec = remove_active_keyingset_exec;
  ot->poll = keyingset_poll_active_edit;
}

/* Add Empty Keying Set Path ------------------------- */

static int add_empty_ks_path_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks;
  KS_Path *ksp;

  /* verify the Keying Set to use:
   * - use the active one
   * - return error if it doesn't exist
   */
  if (scene->active_keyingset == 0) {
    BKE_report(op->reports, RPT_ERROR, "No active Keying Set to add empty path to");
    return OPERATOR_CANCELLED;
  }

  ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);

  /* don't use the API method for this, since that checks on values... */
  ksp = MEM_callocN(sizeof(KS_Path), "KeyingSetPath Empty");
  BLI_addtail(&ks->paths, ksp);
  ks->active_path = BLI_listbase_count(&ks->paths);

  ksp->groupmode = KSP_GROUP_KSNAME; /* XXX? */
  ksp->idtype = ID_OB;
  ksp->flag = KSP_FLAG_WHOLE_ARRAY;

  return OPERATOR_FINISHED;
}

void ANIM_OT_keying_set_path_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add Empty Keying Set Path";
  ot->idname = "ANIM_OT_keying_set_path_add";
  ot->description = "Add empty path to active keying set";

  /* callbacks */
  ot->exec = add_empty_ks_path_exec;
  ot->poll = keyingset_poll_active_edit;
}

/* Remove Active Keying Set Path ------------------------- */

static int remove_active_ks_path_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);

  /* if there is a KeyingSet, find the nominated path to remove */
  if (ks) {
    KS_Path *ksp = BLI_findlink(&ks->paths, ks->active_path - 1);

    if (ksp) {
      /* remove the active path from the KeyingSet */
      BKE_keyingset_free_path(ks, ksp);

      /* the active path should now be the previously second-to-last active one */
      ks->active_path--;
    }
    else {
      BKE_report(op->reports, RPT_ERROR, "No active Keying Set path to remove");
      return OPERATOR_CANCELLED;
    }
  }
  else {
    BKE_report(op->reports, RPT_ERROR, "No active Keying Set to remove a path from");
    return OPERATOR_CANCELLED;
  }

  return OPERATOR_FINISHED;
}

void ANIM_OT_keying_set_path_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove Active Keying Set Path";
  ot->idname = "ANIM_OT_keying_set_path_remove";
  ot->description = "Remove active Path from active keying set";

  /* callbacks */
  ot->exec = remove_active_ks_path_exec;
  ot->poll = keyingset_poll_activePath_edit;
}

/* ************************************************** */
/* KEYING SETS - OPERATORS (for use in UI menus) */

/* Add to KeyingSet Button Operator ------------------------ */

static int add_keyingset_button_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = NULL;
  PropertyRNA *prop = NULL;
  PointerRNA ptr = {NULL};
  char *path = NULL;
  bool changed = false;
  int index = 0, pflag = 0;
  const bool all = RNA_boolean_get(op->ptr, "all");

  /* try to add to keyingset using property retrieved from UI */
  if (!UI_context_active_but_prop_get(C, &ptr, &prop, &index)) {
    /* pass event on if no active button found */
    return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
  }

  /* verify the Keying Set to use:
   * - use the active one for now (more control over this can be added later)
   * - add a new one if it doesn't exist
   */
  if (scene->active_keyingset == 0) {
    eKS_Settings flag = 0;
    eInsertKeyFlags keyingflag = 0;

    /* validate flags
     * - absolute KeyingSets should be created by default
     */
    flag |= KEYINGSET_ABSOLUTE;

    keyingflag |= ANIM_get_keyframing_flags(scene, false);

    if (IS_AUTOKEY_FLAG(scene, XYZ2RGB)) {
      keyingflag |= INSERTKEY_XYZ2RGB;
    }

    /* call the API func, and set the active keyingset index */
    ks = BKE_keyingset_add(
        &scene->keyingsets, "ButtonKeyingSet", "Button Keying Set", flag, keyingflag);

    scene->active_keyingset = BLI_listbase_count(&scene->keyingsets);
  }
  else if (scene->active_keyingset < 0) {
    BKE_report(op->reports, RPT_ERROR, "Cannot add property to built in keying set");
    return OPERATOR_CANCELLED;
  }
  else {
    ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);
  }

  /* check if property is able to be added */
  if (ptr.owner_id && ptr.data && prop && RNA_property_animateable(&ptr, prop)) {
    path = RNA_path_from_ID_to_property(&ptr, prop);

    if (path) {
      /* set flags */
      if (all) {
        pflag |= KSP_FLAG_WHOLE_ARRAY;

        /* we need to set the index for this to 0, even though it may break in some cases, this is
         * necessary if we want the entire array for most cases to get included without the user
         * having to worry about where they clicked
         */
        index = 0;
      }

      /* add path to this setting */
      BKE_keyingset_add_path(ks, ptr.owner_id, NULL, path, index, pflag, KSP_GROUP_KSNAME);
      ks->active_path = BLI_listbase_count(&ks->paths);
      changed = true;

      /* free the temp path created */
      MEM_freeN(path);
    }
  }

  if (changed) {
    /* send updates */
    WM_event_add_notifier(C, NC_SCENE | ND_KEYINGSET, NULL);

    /* show notification/report header, so that users notice that something changed */
    BKE_reportf(op->reports, RPT_INFO, "Property added to Keying Set: '%s'", ks->name);
  }

  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_keyingset_button_add(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Add to Keying Set";
  ot->idname = "ANIM_OT_keyingset_button_add";
  ot->description = "Add current UI-active property to current keying set";

  /* callbacks */
  ot->exec = add_keyingset_button_exec;
  // op->poll = ???

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* properties */
  RNA_def_boolean(ot->srna, "all", 1, "All", "Add all elements of the array to a Keying Set");
}

/* Remove from KeyingSet Button Operator ------------------------ */

static int remove_keyingset_button_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks = NULL;
  PropertyRNA *prop = NULL;
  PointerRNA ptr = {NULL};
  char *path = NULL;
  bool changed = false;
  int index = 0;

  /* try to add to keyingset using property retrieved from UI */
  if (UI_context_active_but_prop_get(C, &ptr, &prop, &index)) {
    /* pass event on if no active button found */
    return (OPERATOR_CANCELLED | OPERATOR_PASS_THROUGH);
  }

  /* verify the Keying Set to use:
   * - use the active one for now (more control over this can be added later)
   * - return error if it doesn't exist
   */
  if (scene->active_keyingset == 0) {
    BKE_report(op->reports, RPT_ERROR, "No active Keying Set to remove property from");
    return OPERATOR_CANCELLED;
  }

  if (scene->active_keyingset < 0) {
    BKE_report(op->reports, RPT_ERROR, "Cannot remove property from built in keying set");
    return OPERATOR_CANCELLED;
  }

  ks = BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);

  if (ptr.owner_id && ptr.data && prop) {
    path = RNA_path_from_ID_to_property(&ptr, prop);

    if (path) {
      KS_Path *ksp;

      /* try to find a path matching this description */
      ksp = BKE_keyingset_find_path(ks, ptr.owner_id, ks->name, path, index, KSP_GROUP_KSNAME);

      if (ksp) {
        BKE_keyingset_free_path(ks, ksp);
        changed = true;
      }

      /* free temp path used */
      MEM_freeN(path);
    }
  }

  if (changed) {
    /* send updates */
    WM_event_add_notifier(C, NC_SCENE | ND_KEYINGSET, NULL);

    /* show warning */
    BKE_report(op->reports, RPT_INFO, "Property removed from keying set");
  }

  return (changed) ? OPERATOR_FINISHED : OPERATOR_CANCELLED;
}

void ANIM_OT_keyingset_button_remove(wmOperatorType *ot)
{
  /* identifiers */
  ot->name = "Remove from Keying Set";
  ot->idname = "ANIM_OT_keyingset_button_remove";
  ot->description = "Remove current UI-active property from current keying set";

  /* callbacks */
  ot->exec = remove_keyingset_button_exec;
  // op->poll = ???

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;
}

/* ******************************************* */

/* Change Active KeyingSet Operator ------------------------ */
/* This operator checks if a menu should be shown
 * for choosing the KeyingSet to make the active one. */

static int keyingset_active_menu_invoke(bContext *C, wmOperator *op, const wmEvent *UNUSED(event))
{
  uiPopupMenu *pup;
  uiLayout *layout;

  /* call the menu, which will call this operator again, hence the canceled */
  pup = UI_popup_menu_begin(C, op->type->name, ICON_NONE);
  layout = UI_popup_menu_layout(pup);
  uiItemsEnumO(layout, "ANIM_OT_keying_set_active_set", "type");
  UI_popup_menu_end(C, pup);

  return OPERATOR_INTERFACE;
}

static int keyingset_active_menu_exec(bContext *C, wmOperator *op)
{
  Scene *scene = CTX_data_scene(C);
  int type = RNA_enum_get(op->ptr, "type");

  /* If type == 0, it will deselect any active keying set. */
  scene->active_keyingset = type;

  /* send notifiers */
  WM_event_add_notifier(C, NC_SCENE | ND_KEYINGSET, NULL);

  return OPERATOR_FINISHED;
}

void ANIM_OT_keying_set_active_set(wmOperatorType *ot)
{
  PropertyRNA *prop;

  /* identifiers */
  ot->name = "Set Active Keying Set";
  ot->idname = "ANIM_OT_keying_set_active_set";
  ot->description = "Set a new active keying set";

  /* callbacks */
  ot->invoke = keyingset_active_menu_invoke;
  ot->exec = keyingset_active_menu_exec;
  ot->poll = ED_operator_areaactive;

  /* flags */
  ot->flag = OPTYPE_REGISTER | OPTYPE_UNDO;

  /* keyingset to use (dynamic enum) */
  prop = RNA_def_enum(
      ot->srna, "type", DummyRNA_DEFAULT_items, 0, "Keying Set", "The Keying Set to use");
  RNA_def_enum_funcs(prop, ANIM_keying_sets_enum_itemf);
  // RNA_def_property_flag(prop, PROP_HIDDEN);
}

/* ******************************************* */
/* REGISTERED KEYING SETS */

/* Keying Set Type Info declarations */
static ListBase keyingset_type_infos = {NULL, NULL};

ListBase builtin_keyingsets = {NULL, NULL};

/* --------------- */

KeyingSetInfo *ANIM_keyingset_info_find_name(const char name[])
{
  /* sanity checks */
  if ((name == NULL) || (name[0] == 0)) {
    return NULL;
  }

  /* search by comparing names */
  return BLI_findstring(&keyingset_type_infos, name, offsetof(KeyingSetInfo, idname));
}

KeyingSet *ANIM_builtin_keyingset_get_named(KeyingSet *prevKS, const char name[])
{
  KeyingSet *ks, *first = NULL;

  /* sanity checks  any name to check? */
  if (name[0] == 0) {
    return NULL;
  }

  /* get first KeyingSet to use */
  if (prevKS && prevKS->next) {
    first = prevKS->next;
  }
  else {
    first = builtin_keyingsets.first;
  }

  /* loop over KeyingSets checking names */
  for (ks = first; ks; ks = ks->next) {
    if (STREQ(name, ks->idname)) {
      return ks;
    }
  }

  /* complain about missing keying sets on debug builds */
#ifndef NDEBUG
  printf("%s: '%s' not found\n", __func__, name);
#endif

  /* no matches found */
  return NULL;
}

/* --------------- */

void ANIM_keyingset_info_register(KeyingSetInfo *ksi)
{
  KeyingSet *ks;

  /* create a new KeyingSet
   * - inherit name and keyframing settings from the typeinfo
   */
  ks = BKE_keyingset_add(&builtin_keyingsets, ksi->idname, ksi->name, 1, ksi->keyingflag);

  /* link this KeyingSet with its typeinfo */
  memcpy(&ks->typeinfo, ksi->idname, sizeof(ks->typeinfo));

  /* Copy description... */
  BLI_strncpy(ks->description, ksi->description, sizeof(ks->description));

  /* add type-info to the list */
  BLI_addtail(&keyingset_type_infos, ksi);
}

void ANIM_keyingset_info_unregister(Main *bmain, KeyingSetInfo *ksi)
{
  KeyingSet *ks, *ksn;

  /* find relevant builtin KeyingSets which use this, and remove them */
  /* TODO: this isn't done now, since unregister is really only used at the moment when we
   * reload the scripts, which kind of defeats the purpose of "builtin"? */
  for (ks = builtin_keyingsets.first; ks; ks = ksn) {
    ksn = ks->next;

    /* remove if matching typeinfo name */
    if (STREQ(ks->typeinfo, ksi->idname)) {
      Scene *scene;
      BKE_keyingset_free(ks);
      BLI_remlink(&builtin_keyingsets, ks);

      for (scene = bmain->scenes.first; scene; scene = scene->id.next) {
        BLI_remlink_safe(&scene->keyingsets, ks);
      }

      MEM_freeN(ks);
    }
  }

  /* free the type info */
  BLI_freelinkN(&keyingset_type_infos, ksi);
}

void ANIM_keyingset_infos_exit(void)
{
  KeyingSetInfo *ksi, *next;

  /* free type infos */
  for (ksi = keyingset_type_infos.first; ksi; ksi = next) {
    next = ksi->next;

    /* free extra RNA data, and remove from list */
    if (ksi->rna_ext.free) {
      ksi->rna_ext.free(ksi->rna_ext.data);
    }
    BLI_freelinkN(&keyingset_type_infos, ksi);
  }

  /* free builtin sets */
  BKE_keyingsets_free(&builtin_keyingsets);
}

bool ANIM_keyingset_find_id(KeyingSet *ks, ID *id)
{
  /* sanity checks */
  if (ELEM(NULL, ks, id)) {
    return false;
  }

  return BLI_findptr(&ks->paths, id, offsetof(KS_Path, id)) != NULL;
}

/* ******************************************* */
/* KEYING SETS API (for UI) */

/* Getters for Active/Indices ----------------------------- */

KeyingSet *ANIM_scene_get_active_keyingset(const Scene *scene)
{
  /* if no scene, we've got no hope of finding the Keying Set */
  if (scene == NULL) {
    return NULL;
  }

  /* currently, there are several possibilities here:
   * -   0: no active keying set
   * - > 0: one of the user-defined Keying Sets, but indices start from 0 (hence the -1)
   * - < 0: a builtin keying set
   */
  if (scene->active_keyingset > 0) {
    return BLI_findlink(&scene->keyingsets, scene->active_keyingset - 1);
  }
  return BLI_findlink(&builtin_keyingsets, (-scene->active_keyingset) - 1);
}

int ANIM_scene_get_keyingset_index(Scene *scene, KeyingSet *ks)
{
  int index;

  /* if no KeyingSet provided, have none */
  if (ks == NULL) {
    return 0;
  }

  /* check if the KeyingSet exists in scene list */
  if (scene) {
    /* get index and if valid, return
     * - (absolute) Scene KeyingSets are from (>= 1)
     */
    index = BLI_findindex(&scene->keyingsets, ks);
    if (index != -1) {
      return (index + 1);
    }
  }

  /* Still here, so try built-ins list too:
   * - Built-ins are from (<= -1).
   * - None/Invalid is (= 0).
   */
  index = BLI_findindex(&builtin_keyingsets, ks);
  if (index != -1) {
    return -(index + 1);
  }
  return 0;
}

KeyingSet *ANIM_get_keyingset_for_autokeying(const Scene *scene, const char *transformKSName)
{
  /* get KeyingSet to use
   * - use the active KeyingSet if defined (and user wants to use it for all autokeying),
   *   or otherwise key transforms only
   */
  if (IS_AUTOKEY_FLAG(scene, ONLYKEYINGSET) && (scene->active_keyingset)) {
    return ANIM_scene_get_active_keyingset(scene);
  }
  if (IS_AUTOKEY_FLAG(scene, INSERTAVAIL)) {
    return ANIM_builtin_keyingset_get_named(NULL, ANIM_KS_AVAILABLE_ID);
  }
  return ANIM_builtin_keyingset_get_named(NULL, transformKSName);
}

static void anim_keyingset_visit_for_search_impl(const bContext *C,
                                                 StringPropertySearchVisitFunc visit_fn,
                                                 void *visit_user_data,
                                                 const bool use_poll)
{
  /* Poll requires context. */
  if (use_poll && (C == NULL)) {
    return;
  }

  Scene *scene = C ? CTX_data_scene(C) : NULL;
  KeyingSet *ks;

  /* Active Keying Set. */
  if (!use_poll || (scene && scene->active_keyingset)) {
    StringPropertySearchVisitParams visit_params = {NULL};
    visit_params.text = "__ACTIVE__";
    visit_params.info = "Active Keying Set";
    visit_fn(visit_user_data, &visit_params);
  }

  /* User-defined Keying Sets. */
  if (scene && scene->keyingsets.first) {
    for (ks = scene->keyingsets.first; ks; ks = ks->next) {
      if (use_poll && !ANIM_keyingset_context_ok_poll((bContext *)C, ks)) {
        continue;
      }
      StringPropertySearchVisitParams visit_params = {NULL};
      visit_params.text = ks->idname;
      visit_params.info = ks->name;
      visit_fn(visit_user_data, &visit_params);
    }
  }

  /* Builtin Keying Sets. */
  for (ks = builtin_keyingsets.first; ks; ks = ks->next) {
    if (use_poll && !ANIM_keyingset_context_ok_poll((bContext *)C, ks)) {
      continue;
    }
    StringPropertySearchVisitParams visit_params = {NULL};
    visit_params.text = ks->idname;
    visit_params.info = ks->name;
    visit_fn(visit_user_data, &visit_params);
  }
}

void ANIM_keyingset_visit_for_search(const bContext *C,
                                     PointerRNA *UNUSED(ptr),
                                     PropertyRNA *UNUSED(prop),
                                     const char *UNUSED(edit_text),
                                     StringPropertySearchVisitFunc visit_fn,
                                     void *visit_user_data)
{
  anim_keyingset_visit_for_search_impl(C, visit_fn, visit_user_data, false);
}

void ANIM_keyingset_visit_for_search_no_poll(const bContext *C,
                                             PointerRNA *UNUSED(ptr),
                                             PropertyRNA *UNUSED(prop),
                                             const char *UNUSED(edit_text),
                                             StringPropertySearchVisitFunc visit_fn,
                                             void *visit_user_data)
{
  anim_keyingset_visit_for_search_impl(C, visit_fn, visit_user_data, true);
}

/* Menu of All Keying Sets ----------------------------- */

const EnumPropertyItem *ANIM_keying_sets_enum_itemf(bContext *C,
                                                    PointerRNA *UNUSED(ptr),
                                                    PropertyRNA *UNUSED(prop),
                                                    bool *r_free)
{
  Scene *scene = CTX_data_scene(C);
  KeyingSet *ks;
  EnumPropertyItem *item = NULL, item_tmp = {0};
  int totitem = 0;
  int i = 0;

  if (C == NULL) {
    return DummyRNA_DEFAULT_items;
  }

  /* active Keying Set
   * - only include entry if it exists
   */
  if (scene->active_keyingset) {
    /* active Keying Set */
    item_tmp.identifier = "__ACTIVE__";
    item_tmp.name = "Active Keying Set";
    item_tmp.value = i;
    RNA_enum_item_add(&item, &totitem, &item_tmp);

    /* separator */
    RNA_enum_item_add_separator(&item, &totitem);
  }

  i++;

  /* user-defined Keying Sets
   * - these are listed in the order in which they were defined for the active scene
   */
  if (scene->keyingsets.first) {
    for (ks = scene->keyingsets.first; ks; ks = ks->next, i++) {
      if (ANIM_keyingset_context_ok_poll(C, ks)) {
        item_tmp.identifier = ks->idname;
        item_tmp.name = ks->name;
        item_tmp.description = ks->description;
        item_tmp.value = i;
        RNA_enum_item_add(&item, &totitem, &item_tmp);
      }
    }

    /* separator */
    RNA_enum_item_add_separator(&item, &totitem);
  }

  /* builtin Keying Sets */
  i = -1;
  for (ks = builtin_keyingsets.first; ks; ks = ks->next, i--) {
    /* only show KeyingSet if context is suitable */
    if (ANIM_keyingset_context_ok_poll(C, ks)) {
      item_tmp.identifier = ks->idname;
      item_tmp.name = ks->name;
      item_tmp.description = ks->description;
      item_tmp.value = i;
      RNA_enum_item_add(&item, &totitem, &item_tmp);
    }
  }

  RNA_enum_item_end(&item, &totitem);
  *r_free = true;

  return item;
}

KeyingSet *ANIM_keyingset_get_from_enum_type(Scene *scene, int type)
{
  KeyingSet *ks = NULL;

  if (type == 0) {
    type = scene->active_keyingset;
  }

  if (type > 0) {
    ks = BLI_findlink(&scene->keyingsets, type - 1);
  }
  else {
    ks = BLI_findlink(&builtin_keyingsets, -type - 1);
  }
  return ks;
}

KeyingSet *ANIM_keyingset_get_from_idname(Scene *scene, const char *idname)
{
  KeyingSet *ks = BLI_findstring(&scene->keyingsets, idname, offsetof(KeyingSet, idname));
  if (ks == NULL) {
    ks = BLI_findstring(&builtin_keyingsets, idname, offsetof(KeyingSet, idname));
  }
  return ks;
}

/* ******************************************* */
/* KEYFRAME MODIFICATION */

/* Polling API ----------------------------------------------- */

bool ANIM_keyingset_context_ok_poll(bContext *C, KeyingSet *ks)
{
  if ((ks->flag & KEYINGSET_ABSOLUTE) == 0) {
    KeyingSetInfo *ksi = ANIM_keyingset_info_find_name(ks->typeinfo);

    /* get the associated 'type info' for this KeyingSet */
    if (ksi == NULL) {
      return 0;
    }
    /* TODO: check for missing callbacks! */

    /* check if it can be used in the current context */
    return (ksi->poll(ksi, C));
  }

  return true;
}

/* Special 'Overrides' Iterator for Relative KeyingSets ------ */

/* 'Data Sources' for relative Keying Set 'overrides'
 * - this is basically a wrapper for PointerRNA's in a linked list
 * - do not allow this to be accessed from outside for now
 */
typedef struct tRKS_DSource {
  struct tRKS_DSource *next, *prev;
  PointerRNA ptr; /* the whole point of this exercise! */
} tRKS_DSource;

/* Iterator used for overriding the behavior of iterators defined for
 * relative Keying Sets, with the main usage of this being operators
 * requiring Auto Keyframing. Internal Use Only!
 */
static void RKS_ITER_overrides_list(KeyingSetInfo *ksi,
                                    bContext *C,
                                    KeyingSet *ks,
                                    ListBase *dsources)
{
  tRKS_DSource *ds;

  for (ds = dsources->first; ds; ds = ds->next) {
    /* run generate callback on this data */
    ksi->generate(ksi, C, ks, &ds->ptr);
  }
}

void ANIM_relative_keyingset_add_source(ListBase *dsources, ID *id, StructRNA *srna, void *data)
{
  tRKS_DSource *ds;

  /* sanity checks
   * - we must have somewhere to output the data
   * - we must have both srna+data (and with id too optionally), or id by itself only
   */
  if (dsources == NULL) {
    return;
  }
  if (ELEM(NULL, srna, data) && (id == NULL)) {
    return;
  }

  /* allocate new elem, and add to the list */
  ds = MEM_callocN(sizeof(tRKS_DSource), "tRKS_DSource");
  BLI_addtail(dsources, ds);

  /* depending on what data we have, create using ID or full pointer call */
  if (srna && data) {
    RNA_pointer_create(id, srna, data, &ds->ptr);
  }
  else {
    RNA_id_pointer_create(id, &ds->ptr);
  }
}

/* KeyingSet Operations (Insert/Delete Keyframes) ------------ */

eModifyKey_Returns ANIM_validate_keyingset(bContext *C, ListBase *dsources, KeyingSet *ks)
{
  /* sanity check */
  if (ks == NULL) {
    return 0;
  }

  /* if relative Keying Sets, poll and build up the paths */
  if ((ks->flag & KEYINGSET_ABSOLUTE) == 0) {
    KeyingSetInfo *ksi = ANIM_keyingset_info_find_name(ks->typeinfo);

    /* clear all existing paths
     * NOTE: BKE_keyingset_free() frees all of the paths for the KeyingSet, but not the set itself
     */
    BKE_keyingset_free(ks);

    /* get the associated 'type info' for this KeyingSet */
    if (ksi == NULL) {
      return MODIFYKEY_MISSING_TYPEINFO;
    }
    /* TODO: check for missing callbacks! */

    /* check if it can be used in the current context */
    if (ksi->poll(ksi, C)) {
      /* if a list of data sources are provided, run a special iterator over them,
       * otherwise, just continue per normal
       */
      if (dsources) {
        RKS_ITER_overrides_list(ksi, C, ks, dsources);
      }
      else {
        ksi->iter(ksi, C, ks);
      }

      /* if we don't have any paths now, then this still qualifies as invalid context */
      /* FIXME: we need some error conditions (to be retrieved from the iterator why this failed!)
       */
      if (BLI_listbase_is_empty(&ks->paths)) {
        return MODIFYKEY_INVALID_CONTEXT;
      }
    }
    else {
      /* poll callback tells us that KeyingSet is useless in current context */
      /* FIXME: the poll callback needs to give us more info why */
      return MODIFYKEY_INVALID_CONTEXT;
    }
  }

  /* succeeded; return 0 to tag error free */
  return 0;
}

/* Determine which keying flags apply based on the override flags */
static eInsertKeyFlags keyingset_apply_keying_flags(const eInsertKeyFlags base_flags,
                                                    const eInsertKeyFlags overrides,
                                                    const eInsertKeyFlags own_flags)
{
  /* Pass through all flags by default (i.e. even not explicitly listed ones). */
  eInsertKeyFlags result = base_flags;

  /* The logic for whether a keying flag applies is as follows:
   * - If the flag in question is set in "overrides", that means that the
   *   status of that flag in "own_flags" is used
   * - If however the flag isn't set, then its value in "base_flags" is used
   *   instead (i.e. no override)
   */
#define APPLY_KEYINGFLAG_OVERRIDE(kflag) \
  if (overrides & kflag) { \
    result &= ~kflag; \
    result |= (own_flags & kflag); \
  }

  /* Apply the flags one by one...
   * (See rna_def_common_keying_flags() for the supported flags)
   */
  APPLY_KEYINGFLAG_OVERRIDE(INSERTKEY_NEEDED)
  APPLY_KEYINGFLAG_OVERRIDE(INSERTKEY_MATRIX)
  APPLY_KEYINGFLAG_OVERRIDE(INSERTKEY_XYZ2RGB)

#undef APPLY_KEYINGFLAG_OVERRIDE

  return result;
}

int ANIM_apply_keyingset(
    bContext *C, ListBase *dsources, bAction *act, KeyingSet *ks, short mode, float cfra)
{
  Main *bmain = CTX_data_main(C);
  Scene *scene = CTX_data_scene(C);
  ReportList *reports = CTX_wm_reports(C);
  KS_Path *ksp;
  ListBase nla_cache = {NULL, NULL};
  const eInsertKeyFlags base_kflags = ANIM_get_keyframing_flags(scene, true);
  const char *groupname = NULL;
  eInsertKeyFlags kflag = 0;
  int num_channels = 0;
  char keytype = scene->toolsettings->keyframe_type;

  /* sanity checks */
  if (ks == NULL) {
    return 0;
  }

  /* get flags to use */
  if (mode == MODIFYKEY_MODE_INSERT) {
    /* use context settings as base */
    kflag = keyingset_apply_keying_flags(base_kflags, ks->keyingoverride, ks->keyingflag);
  }
  else if (mode == MODIFYKEY_MODE_DELETE) {
    kflag = 0;
  }

  /* if relative Keying Sets, poll and build up the paths */
  {
    const eModifyKey_Returns error = ANIM_validate_keyingset(C, dsources, ks);
    if (error != 0) {
      BLI_assert(error < 0);
      /* return error code if failed */
      return error;
    }
  }

  /* apply the paths as specified in the KeyingSet now */
  for (ksp = ks->paths.first; ksp; ksp = ksp->next) {
    int arraylen, i;
    eInsertKeyFlags kflag2;

    /* skip path if no ID pointer is specified */
    if (ksp->id == NULL) {
      BKE_reportf(reports,
                  RPT_WARNING,
                  "Skipping path in keying set, as it has no ID (KS = '%s', path = '%s[%d]')",
                  ks->name,
                  ksp->rna_path,
                  ksp->array_index);
      continue;
    }

    /* Since keying settings can be defined on the paths too,
     * apply the settings for this path first. */
    kflag2 = keyingset_apply_keying_flags(kflag, ksp->keyingoverride, ksp->keyingflag);

    /* get pointer to name of group to add channels to */
    if (ksp->groupmode == KSP_GROUP_NONE) {
      groupname = NULL;
    }
    else if (ksp->groupmode == KSP_GROUP_KSNAME) {
      groupname = ks->name;
    }
    else {
      groupname = ksp->group;
    }

    /* init arraylen and i - arraylen should be greater than i so that
     * normal non-array entries get keyframed correctly
     */
    i = ksp->array_index;
    arraylen = i;

    /* get length of array if whole array option is enabled */
    if (ksp->flag & KSP_FLAG_WHOLE_ARRAY) {
      PointerRNA id_ptr, ptr;
      PropertyRNA *prop;

      RNA_id_pointer_create(ksp->id, &id_ptr);
      if (RNA_path_resolve_property(&id_ptr, ksp->rna_path, &ptr, &prop)) {
        arraylen = RNA_property_array_length(&ptr, prop);
        /* start from start of array, instead of the previously specified index - T48020 */
        i = 0;
      }
    }

    /* we should do at least one step */
    if (arraylen == i) {
      arraylen++;
    }

    /* for each possible index, perform operation
     * - assume that arraylen is greater than index
     */
    Depsgraph *depsgraph = CTX_data_depsgraph_pointer(C);
    const AnimationEvalContext anim_eval_context = BKE_animsys_eval_context_construct(depsgraph,
                                                                                      cfra);
    for (; i < arraylen; i++) {
      /* action to take depends on mode */
      if (mode == MODIFYKEY_MODE_INSERT) {
        num_channels += insert_keyframe(bmain,
                                        reports,
                                        ksp->id,
                                        act,
                                        groupname,
                                        ksp->rna_path,
                                        i,
                                        &anim_eval_context,
                                        keytype,
                                        &nla_cache,
                                        kflag2);
      }
      else if (mode == MODIFYKEY_MODE_DELETE) {
        num_channels += delete_keyframe(bmain, reports, ksp->id, act, ksp->rna_path, i, cfra);
      }
    }

    /* set recalc-flags */
    switch (GS(ksp->id->name)) {
      case ID_OB: /* Object (or Object-Related) Keyframes */
      {
        Object *ob = (Object *)ksp->id;

        /* XXX: only object transforms? */
        DEG_id_tag_update(&ob->id, ID_RECALC_TRANSFORM | ID_RECALC_GEOMETRY);
        break;
      }
      default:
        DEG_id_tag_update(ksp->id, ID_RECALC_ANIMATION_NO_FLUSH);
        break;
    }

    /* send notifiers for updates (this doesn't require context to work!) */
    WM_main_add_notifier(NC_ANIMATION | ND_KEYFRAME | NA_ADDED, NULL);
  }

  BKE_animsys_free_nla_keyframing_context_cache(&nla_cache);

  /* return the number of channels successfully affected */
  BLI_assert(num_channels >= 0);
  return num_channels;
}

/* ************************************************** */
