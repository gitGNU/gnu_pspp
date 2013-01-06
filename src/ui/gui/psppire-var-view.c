/* PSPPIRE - a graphical user interface for PSPP.
   Copyright (C) 2009, 2010, 2011  Free Software Foundation

   This program is free software: you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation, either version 3 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program.  If not, see <http://www.gnu.org/licenses/>. */

#include <config.h>

#include <gtk/gtk.h>
#include "psppire-var-view.h"
#include "psppire-var-ptr.h"
#include "psppire-select-dest.h"

#include <libpspp/str.h>
#include <data/variable.h>

#include <gettext.h>
#define _(msgid) gettext (msgid)
#define N_(msgid) msgid

static void psppire_var_view_base_finalize (PsppireVarViewClass *, gpointer);
static void psppire_var_view_base_init     (PsppireVarViewClass *class);
static void psppire_var_view_class_init    (PsppireVarViewClass *class);
static void psppire_var_view_init          (PsppireVarView      *var_view);

/* Returns TRUE iff VV contains the item V.
   V must be an initialised value containing a
   PSPPIRE_VAR_PTR_TYPE.
*/
static gboolean
var_view_contains_var (PsppireSelectDestWidget *sdm, const GValue *v)
{
  gboolean ok;
  GtkTreeIter iter;
  PsppireVarView *vv = PSPPIRE_VAR_VIEW (sdm);
  g_return_val_if_fail (G_VALUE_HOLDS (v, PSPPIRE_VAR_PTR_TYPE), FALSE);

  for (ok = psppire_var_view_get_iter_first (vv, &iter);
       ok;
       ok = psppire_var_view_get_iter_next (vv, &iter))
    {
      const struct variable *var = psppire_var_view_get_variable (vv, 0, &iter);
      if (var == g_value_get_boxed (v))
	return TRUE;
    }

  return FALSE;
}

static void
model_init (PsppireSelectDestWidgetIface *iface)
{
  iface->contains_var = var_view_contains_var;
}

GType
psppire_var_view_get_type (void)
{
  static GType psppire_var_view_type = 0;

  if (!psppire_var_view_type)
    {
      static const GTypeInfo psppire_var_view_info =
      {
	sizeof (PsppireVarViewClass),
	(GBaseInitFunc) psppire_var_view_base_init,
        (GBaseFinalizeFunc) psppire_var_view_base_finalize,
	(GClassInitFunc)psppire_var_view_class_init,
	(GClassFinalizeFunc) NULL,
	NULL,
        sizeof (PsppireVarView),
	0,
	(GInstanceInitFunc) psppire_var_view_init,
      };

      static const GInterfaceInfo var_view_model_info = {
	(GInterfaceInitFunc) model_init, /* Fill this in */
	NULL,
	NULL
      };

      psppire_var_view_type =
	g_type_register_static (GTK_TYPE_TREE_VIEW, "PsppireVarView",
				&psppire_var_view_info, 0);

      g_type_add_interface_static (psppire_var_view_type,
				   PSPPIRE_TYPE_SELECT_DEST_WIDGET,
				   &var_view_model_info);
    }

  return psppire_var_view_type;
}

void 
psppire_var_view_clear (PsppireVarView *vv)
{
  gint i;
  for (i = 0; i < vv->n_lists; ++i)
    g_object_unref (vv->list[i]);

  g_free (vv->list);

  vv->n_lists = 0;
  vv->l_idx = -1;
  vv->list = NULL;

  psppire_var_view_push_model (vv);
}


static void
psppire_var_view_finalize (GObject *object)
{
  gint i;
  PsppireVarView *var_view = PSPPIRE_VAR_VIEW (object);
  g_free (var_view->nums);
  for (i = 0; i < var_view->n_lists; ++i)
    g_object_unref (var_view->list[i]);

  g_free (var_view->list);
  g_free (var_view->cols);
}



/* Properties */
enum
{
  PROP_0,
  PROP_N_COLS
};

/* A (*GtkTreeCellDataFunc) function.
   This function expects TREEMODEL to hold PSPPIRE_VAR_PTR_TYPE.
   It renders the name of the variable into CELL.
*/
static void
display_cell_var_name (GtkTreeViewColumn *tree_column,
		       GtkCellRenderer *cell,
		       GtkTreeModel *treemodel,
		       GtkTreeIter *iter,
		       gpointer data)
{
  struct variable *var;
  GValue value = {0};
  gint *col = data;

  GtkTreePath *path = gtk_tree_model_get_path (treemodel, iter);

  gtk_tree_model_get_value (treemodel, iter, *col, &value);

  gtk_tree_path_free (path);

  var = g_value_get_boxed (&value);

  g_value_unset (&value);

  g_object_set (cell, "text", var ? var_get_name (var) : "", NULL);
}


static void
psppire_var_view_get_property (GObject         *object,
			       guint            prop_id,
			       GValue          *value,
			       GParamSpec      *pspec)
{
  PsppireVarView *var_view = PSPPIRE_VAR_VIEW (object);

  switch (prop_id)
    {
    case PROP_N_COLS:
      g_value_set_int (value,  var_view->n_cols);
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
set_renderers (PsppireVarView *var_view)
{
  gint c;
  var_view->nums = g_malloc (sizeof *var_view->nums * var_view->n_cols);
  
  for (c = 0 ; c < var_view->n_cols; ++c)
    {
      GtkCellRenderer *renderer = gtk_cell_renderer_text_new ();
      GtkTreeViewColumn *col = gtk_tree_view_column_new ();
      
      gchar *label = g_strdup_printf (_("Var%d"), c + 1);
      
      gtk_tree_view_column_set_min_width (col, 100);
      gtk_tree_view_column_set_sizing (col, GTK_TREE_VIEW_COLUMN_FIXED);
      gtk_tree_view_column_set_resizable (col, TRUE);
      gtk_tree_view_column_set_title (col, label);
      
      g_free (label);
      
      var_view->nums[c] = c;
      
      gtk_tree_view_column_pack_start (col, renderer, TRUE);
      gtk_tree_view_column_set_cell_data_func (col, renderer,
					       display_cell_var_name,
					       &var_view->nums[c], 0);
      
      gtk_tree_view_append_column (GTK_TREE_VIEW (var_view), col);
    }
}




/* Set a model, which is an GtkListStore of gpointers which point to a variable */
void
psppire_var_view_push_model (PsppireVarView *vv)
{
  vv->n_lists++;
  vv->l_idx++;
  vv->list = xrealloc (vv->list, sizeof (*vv->list) * vv->n_lists);
  vv->list[vv->l_idx] = gtk_list_store_newv  (vv->n_cols, vv->cols);
  g_object_ref (vv->list[vv->l_idx]);
  gtk_tree_view_set_model (GTK_TREE_VIEW (vv), GTK_TREE_MODEL (vv->list[vv->l_idx]));
}

gboolean
psppire_var_view_set_current_model (PsppireVarView *vv, gint n)
{
  if (n < 0 || n >= vv->n_lists)
    return FALSE;

  vv->l_idx = n;

  gtk_tree_view_set_model (GTK_TREE_VIEW (vv), GTK_TREE_MODEL (vv->list[vv->l_idx]));

  return TRUE;
}

static void
psppire_var_view_set_property (GObject         *object,
			       guint            prop_id,
			       const GValue    *value,
			       GParamSpec      *pspec)
{
  PsppireVarView *var_view = PSPPIRE_VAR_VIEW (object);

  switch (prop_id)
    {
    case PROP_N_COLS:
      {
	gint c;
	var_view->n_cols = g_value_get_int (value);

	var_view->cols = xrealloc (var_view->cols, sizeof (GType) *  var_view->n_cols);

	for (c = 0 ; c < var_view->n_cols; ++c)
	  var_view->cols[c] = PSPPIRE_VAR_PTR_TYPE;

	set_renderers (var_view);

	psppire_var_view_clear (var_view);
      }
      break;
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
      break;
    };
}

static void
psppire_var_view_class_init (PsppireVarViewClass *class)
{
  GObjectClass *object_class = G_OBJECT_CLASS (class);

  GParamSpec *n_cols_spec =
    g_param_spec_int ("n-cols",
		      "Number of columns",
		      "The Number of Columns in the Variable View",
		      1, 20,
		      1,
		      G_PARAM_CONSTRUCT_ONLY | G_PARAM_READABLE | G_PARAM_WRITABLE);


  object_class->set_property = psppire_var_view_set_property;
  object_class->get_property = psppire_var_view_get_property;

  g_object_class_install_property (object_class,
                                   PROP_N_COLS,
                                   n_cols_spec);
}


static void
psppire_var_view_base_init (PsppireVarViewClass *class)
{

  GObjectClass *object_class = G_OBJECT_CLASS (class);



  object_class->finalize = psppire_var_view_finalize;
}



static void
psppire_var_view_base_finalize (PsppireVarViewClass *class,
				 gpointer class_data)
{
}



static void
psppire_var_view_init (PsppireVarView *vv)
{
  vv->cols = 0;
  vv->n_lists = 0;
  vv->l_idx = -1;
  vv->list = NULL;
}


GtkWidget*
psppire_var_view_new (void)
{
  return GTK_WIDGET (g_object_new (psppire_var_view_get_type (), NULL));
}


gboolean
psppire_var_view_get_iter_first (PsppireVarView *vv, GtkTreeIter *iter)
{
  GtkTreeIter dummy;
  if ( vv->l_idx < 0)
    return FALSE;

  return gtk_tree_model_get_iter_first (GTK_TREE_MODEL (vv->list[vv->l_idx]), iter ? iter : &dummy);
}

gboolean
psppire_var_view_get_iter_next (PsppireVarView *vv, GtkTreeIter *iter)
{
  if ( vv->l_idx < 0)
    return FALSE;

  return gtk_tree_model_iter_next (GTK_TREE_MODEL (vv->list[vv->l_idx]), iter);
}

const struct variable *
psppire_var_view_get_variable (PsppireVarView *vv, gint column, GtkTreeIter *iter)
{
  const struct variable *var = NULL;
  GValue value = {0};
  gtk_tree_model_get_value (GTK_TREE_MODEL (vv->list[vv->l_idx]), iter, column, &value);

  if ( G_VALUE_TYPE (&value) == PSPPIRE_VAR_PTR_TYPE)
    var = g_value_get_boxed (&value);
  else
    g_critical ("Unsupported type `%s', in variable name treeview.",
		G_VALUE_TYPE_NAME (&value));

  g_value_unset (&value);

  return var;
}

/*
  Append the names of selected variables to STRING.
  Returns the number of variables appended.
*/
gint
psppire_var_view_append_names (PsppireVarView *vv, gint column, GString *string)
{
  gint n_vars = 0;
  GtkTreeIter iter;

  if ( psppire_var_view_get_iter_first (vv, &iter) )
    {
      do
	{
	  const struct variable *var = psppire_var_view_get_variable (vv, column, &iter);
	  g_string_append (string, " ");
	  g_string_append (string, var_get_name (var));

	  n_vars++;
	}
      while (psppire_var_view_get_iter_next (vv, &iter));
    }

  return n_vars;
}


/*
  Append the names of selected variables to STR
  Returns the number of variables appended.
*/
gint 
psppire_var_view_append_names_str (PsppireVarView *vv, gint column, struct string *str)
{
  gint n_vars = 0;
  GtkTreeIter iter;

  if ( psppire_var_view_get_iter_first (vv, &iter) )
    {
      do
	{
	  const struct variable *var = psppire_var_view_get_variable (vv, column, &iter);
	  ds_put_cstr (str, " ");
	  ds_put_cstr (str, var_get_name (var));

	  n_vars++;
	}
      while (psppire_var_view_get_iter_next (vv, &iter));
    }

  return n_vars;
}



